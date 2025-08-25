/*
 *      Copyright (C) 1994-1998 Claus-Justus Heine.

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License as
 published by the Free Software Foundation; either version 2, or (at
 your option) any later version.
 
 This program is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139,
 USA.
 
 *
 *     This file implements a "generic" interface between the *
 *     zftape-driver and a compression-algorithm. The *
 *     compression-algorithm currently used is a LZ77. I use the *
 *     implementation lzrw3 by Ross N. Williams (Renaissance *
 *     Software). The compression program itself is in the file
 *     lzrw3.c * and lzrw3.h.  To adopt another compression algorithm
 *     the functions * zft_compress() and zft_uncompress() must be
 *     changed * appropriately. See below.
 */

 char zftc_src[] ="$RCSfile: zftape-compress.c,v $";
 char zftc_rev[] = "$Revision: 1.10 $";
 char zftc_dat[] = "$Date: 2000/06/23 12:07:22 $";

#define ZFTC_TRACING


#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <linux/zftape.h>

#include <asm/uaccess.h>

#include "../zftape/zftape-init.h"
#include "../zftape/zftape-eof.h"
#include "../zftape/zftape-ctl.h"
#include "../zftape/zftape-write.h"
#include "../zftape/zftape-read.h"
#include "../zftape/zftape-rw.h"
#include "../zftape/zftape-vtbl.h"
#include "zftape-compress.h"
#include "lzrw3.h"

/* compressed segment. This conforms to QIC-80-MC, Revision K.
 * 
 * Rev. K applies to tapes with `fixed length format' which is
 * indicated by format code 2,3 and 5. See below for format code 4 and 6
 *
 * 2 bytes: offset of compression segment structure
 *          29k > offset >= 29k-18: data from previous segment ens in this
 *                                  segment and no compressed block starts
 *                                  in this segment
 *                     offset == 0: data from previous segment occupies entire
 *                                  segment and continues in next segment
 * n bytes: remainder from previous segment
 * 
 * Rev. K:  
 * 4 bytes: 4 bytes: files set byte offset
 * Post Rev. K and QIC-3020/3020:
 * 8 bytes: 8 bytes: files set byte offset
 * 2 bytes: byte count N (amount of data following)
 *          bit 15 is set if data is compressed, bit 15 is not
 *          set if data is uncompressed
 * N bytes: data (as much as specified in the byte count)
 * 2 bytes: byte count N_1 of next cluster
 * N_1 bytes: data of next cluset
 * 2 bytes: byte count N_2 of next cluster
 * N_2 bytes: ...  
 *
 * Note that the `N' byte count accounts only for the bytes that in the
 * current segment if the cluster spans to the next segment.
 */

typedef struct
{
	int cmpr_pos;             /* actual position in compression buffer */
	int cmpr_sz;              /* what is left in the compression buffer
				   * when copying the compressed data to the
				   * deblock buffer
				   */
	unsigned int first_block; /* location of header information in
				   * this segment
				   */
	unsigned int count;       /* amount of data of current block
				   * contained in current segment 
				   */
	unsigned int offset;      /* offset in current segment */
	unsigned int spans:1;     /* might continue in next segment */
	unsigned int uncmpr;      /* 0x8000 if this block contains
				   * uncompressed data 
				   */
	__s64 foffs;              /* file set byte offset, same as in 
				   * compression map segment
				   */
} cmpr_info;

#define DUMP_CMPR_INFO(level, msg, info)				\
	TRACE(level, msg "\n"						\
	      KERN_INFO "cmpr_pos   : %d\n"				\
	      KERN_INFO "cmpr_sz    : %d\n"				\
	      KERN_INFO "first_block: %d\n"				\
	      KERN_INFO "count      : %d\n"				\
	      KERN_INFO "offset     : %d\n"				\
	      KERN_INFO "spans      : %d\n"				\
	      KERN_INFO "uncmpr     : 0x%04x\n"				\
	      KERN_INFO "foffs      : " LL_X,				\
	      (info)->cmpr_pos, (info)->cmpr_sz, (info)->first_block,	\
	      (info)->count, (info)->offset, (info)->spans == 1,	\
	      (info)->uncmpr, LL((info)->foffs))

struct zftc_struct {
	zftape_info_t *zftape;
	void *wrk_mem;
	__u8 *buf;
	void *scratch_buf;
	/*
	 * compression statistics 
	 */
	unsigned int rd_uncompressed;
	unsigned int rd_compressed;

	int cmpr_mem_initialized;
	unsigned int alloc_blksz;

	cmpr_info cseg; /* static data. Must be kept uptodate and shared by 
			* read, write and seek functions
			*/
	unsigned int locked:1;
};

/*
 *   global variables
 */

/* forward */
static int  zftc_read(void *handle,
		      int *read_cnt,
		      __u8  *dst_buf, const int to_do,
		      const __u8 *src_buf, const int seg_sz,
		      const zft_position *pos, const zft_volinfo *volume);
static int  zftc_seek(void *handle,
		      unsigned int new_block_pos, 
		      zft_position *pos, const zft_volinfo *volume,
		      __u8 **buffer);
static void *zftc_open(zftape_info_t *zftape);
static void zftc_close  (void *handle);
static void zftc_cleanup(void *handle);
static void zftc_stats  (struct zftc_struct *zftc);

/* local variables 
 */

static int keep_module_locked = 0;
static struct zftc_struct *zftcs[4] = { NULL, };

/*   dispatch compression segment info, return error code
 *  
 *   afterwards, cseg->offset points to start of data of the NEXT
 *   compressed block, and cseg->count contains the amount of data
 *   left in the actual compressed block. cseg->spans is set to 1 if
 *   the block is continued in the following segment. Otherwise it is
 *   set to 0. 
 */
static int get_cseg (struct zftc_struct *zftc,
		     cmpr_info *cinfo, const __u8 *buff, 
		     const unsigned int seg_sz,
		     const zft_volinfo *volume)
{
	TRACE_FUN(ft_t_flow);

 	cinfo->first_block = GET2(buff, 0);
	if (cinfo->first_block == 0) { /* data spans to next segment */
		cinfo->count  = seg_sz - sizeof(__u16);
		cinfo->offset = seg_sz;
		cinfo->spans = 1;
	} else { /* cluster definetely ends in this segment */
		if (cinfo->first_block > seg_sz) {
			/* data corrupted */
			TRACE_ABORT(-EIO, ft_t_err, "corrupted data:\n"
				    KERN_INFO "segment size: %d\n"
				    KERN_INFO "first block : %d",
				    seg_sz, cinfo->first_block);
		}
	        cinfo->count  = cinfo->first_block - sizeof(__u16);
		cinfo->offset = cinfo->first_block;
		cinfo->spans = 0;
	}
	/* now get the offset the first block should have in the
	 * uncompressed data stream.
	 *
	 * For this magic `18' refer to CRF-3 standard or QIC-80MC,
	 * Rev. K.  
	 */
	if ((seg_sz - cinfo->offset) > 18) {
		if (volume->qic113) { /* > revision K */
			TRACE(ft_t_data_flow, "New QIC-113 compliance");
			cinfo->foffs = GET8(buff, cinfo->offset);
			cinfo->offset += sizeof(__s64); 
		} else {
			TRACE(ft_t_data_flow, "pre QIC-113 version");
			cinfo->foffs   = (__s64)GET4(buff, cinfo->offset);
			cinfo->offset += sizeof(__u32); 
		}
	}
	if (cinfo->foffs > volume->size) {
		TRACE_ABORT(-EIO, ft_t_err, "Inconsistency:\n"
			    KERN_INFO "offset in current volume: %d\n"
			    KERN_INFO "size of current volume  : %d",
			    (int)(cinfo->foffs>>10), (int)(volume->size>>10));
	}
	if (cinfo->cmpr_pos + cinfo->count > volume->blk_sz) {
		TRACE_ABORT(-EIO, ft_t_err, "Inconsistency:\n"
			    KERN_INFO "block size : %d\n"
			    KERN_INFO "data record: %d",
			    volume->blk_sz, cinfo->cmpr_pos + cinfo->count);
	}
	DUMP_CMPR_INFO(ft_t_noise /* ft_t_any */, "", cinfo);
	TRACE_EXIT 0;
}

/*  This one is called, when a new cluster starts in same segment.
 *  
 *  Note: if this is the first cluster in the current segment, we must
 *  not check whether there are more than 18 bytes available because
 *  this have already been done in get_cseg() and there may be less
 *  than 18 bytes available due to header information.
 * 
 */
static void get_next_cluster(struct zftc_struct *zftc,
			     cmpr_info *cluster, const __u8 *buff, 
			     const int seg_sz, const int finish)
{
	TRACE_FUN(ft_t_flow);

	if (seg_sz - cluster->offset > 18 || cluster->foffs != 0) {
		cluster->count   = GET2(buff, cluster->offset);
		cluster->uncmpr  = cluster->count & 0x8000;
		cluster->count  -= cluster->uncmpr;
		cluster->offset += sizeof(__u16);
		cluster->foffs   = 0;
		if ((cluster->offset + cluster->count) < seg_sz) {
			cluster->spans = 0;
		} else if (cluster->offset + cluster->count == seg_sz) {
			cluster->spans = !finish;
		} else {
			/* either an error or a volume written by an 
			 * old version. If this is a data error, then we'll
			 * catch it later.
			 */
			TRACE(ft_t_data_flow, "Either error or old volume");
			cluster->spans = 1;
			cluster->count = seg_sz - cluster->offset;
		}
	} else {
		cluster->count = 0;
		cluster->spans = 0;
		cluster->foffs = 0;
	}
	DUMP_CMPR_INFO(ft_t_noise /* ft_t_any */ , "", cluster);
	TRACE_EXIT;
}

/*
 *  This function also allocates the "handle" and if handle == NULL
 */
#define ZFTAPE_TRACING
#include "../lowlevel/ftape-real-tracing.h"

static void *zftc_open(zftape_info_t *zftape)
{
	struct zftc_struct *zftc = zftcs[FTAPE_SEL(zftape->unit)];
	TRACE_FUN(ft_t_flow);

	if (zftc && zftc->locked) {
		TRACE_EXIT (void *)zftc;
	}

	keep_module_locked ++;

	if (zftc == NULL) {
		zftc = ftape_kmalloc(FTAPE_SEL(zftape->unit),
				     sizeof(struct zftc_struct), 1);
	}
	if (zftc == NULL) {
		keep_module_locked --;
		return NULL;
	}
	memset(zftc, 0, sizeof(*zftc));
	zftc->locked = 1;
	zftc->zftape = zftape;
	zftcs[FTAPE_SEL(zftape->unit)] = zftc;
	TRACE_EXIT (void *)zftc;
}

#define ZFTC_TRACING
#include "../lowlevel/ftape-real-tracing.h"

/*  this function is needed for zftape_reset_position in zftape-io.c 
 */
static void zftc_close(void *handle)
{
	struct zftc_struct *zftc = handle;
	TRACE_FUN(ft_t_flow);

	if (!zftc || !zftc->locked) {
		/* this is not a bug */
		TRACE_EXIT;
	}
	zftc->locked = 0;
	zftc_stats(zftc);
	memset((void *)&zftc->cseg, 0, sizeof(zftc->cseg));
	keep_module_locked --;
	TRACE_EXIT;
}

static int zft_allocate_cmpr_mem(struct zftc_struct *zftc, unsigned int blksz)
{
	TRACE_FUN(ft_t_flow);

	if (zftc->cmpr_mem_initialized && blksz == zftc->alloc_blksz) {
		TRACE_EXIT 0;
	}
	TRACE_CATCH(ftape_vmalloc(FTAPE_SEL(zftc->zftape->unit), &zftc->wrk_mem, CMPR_WRK_MEM_SIZE),
		    zftc_cleanup(zftc));
	TRACE_CATCH(zft_vmalloc_always(zftc->zftape, &zftc->buf, blksz + CMPR_OVERRUN),
		    zftc_cleanup(zftc));
	zftc->alloc_blksz = blksz;
	TRACE_CATCH(zft_vmalloc_always(zftc->zftape, &zftc->scratch_buf, blksz+CMPR_OVERRUN),
		    zftc_cleanup(zftc));
	zftc->cmpr_mem_initialized = 1;
	TRACE_EXIT 0;
}

static void zftc_cleanup(void *handle)
{
	struct zftc_struct *zftc = handle;
	int sel = FTAPE_SEL(zftc->zftape->unit);

	ftape_vfree(sel, &zftc->wrk_mem, CMPR_WRK_MEM_SIZE);
	ftape_vfree(sel, &zftc->buf, zftc->alloc_blksz + CMPR_OVERRUN);
	ftape_vfree(sel, &zftc->scratch_buf, zftc->alloc_blksz + CMPR_OVERRUN);
	zftc->cmpr_mem_initialized = zftc->alloc_blksz = 0;
}

/* called by zft_compress_read() to decompress the data. Must
 * return the size of the decompressed data for sanity checks
 * (compared with zft_blk_sz)
 *
 * NOTE: Read the note for zft_compress() above!  If bit 15 of the
 *       parameter in_sz is set, then the data in in_buffer isn't
 *       compressed, which must be handled by the un-compression
 *       algorithm. (I changed lzrw3 to handle this.)
 *
 *  The parameter max_out_sz is needed to prevent buffer overruns when 
 *  uncompressing corrupt data.
 */
static unsigned int zft_uncompress(struct zftc_struct *zftc,
				   __u8 *in_buffer, 
				   int in_sz, 
				   __u8 *out_buffer,
				   unsigned int max_out_sz)
{ 
	TRACE_FUN(ft_t_flow);
	
	lzrw3_compress(COMPRESS_ACTION_DECOMPRESS, zftc->wrk_mem,
		       in_buffer, (__s32)in_sz,
		       out_buffer, (__u32 *)&max_out_sz);
	
	if (TRACE_LEVEL >= ft_t_info) {
		TRACE(ft_t_data_flow, "\n"
		      KERN_INFO "before decompression: %d bytes\n"
		      KERN_INFO "after decompression : %d bytes", 
		      in_sz < 0 ? -in_sz : in_sz,(int)max_out_sz);
		/*  for statistical purposes
		 */
		zftc->rd_compressed   += in_sz < 0 ? -in_sz : in_sz;
		zftc->rd_uncompressed += max_out_sz;
	}
	TRACE_EXIT (unsigned int)max_out_sz;
}

/* print some statistics about the efficiency of the compression to
 * the kernel log 
 */
static void zftc_stats(struct zftc_struct *zftc)
{
	TRACE_FUN(ft_t_flow);

	if (TRACE_LEVEL < ft_t_info) {
		TRACE_EXIT;
	}
	if (zftc->rd_uncompressed != 0) {
		if (zftc->rd_compressed > (1<<14)) {
			TRACE(ft_t_info, "compression statistics (reading):\n"
			      KERN_INFO " compr./uncmpr.   : %3d %%",
			      (((zftc->rd_compressed>>10) * 100)
			       / (zftc->rd_uncompressed>>10)));
		} else {
			TRACE(ft_t_info, "compression statistics (reading):\n"
			      KERN_INFO " compr./uncmpr.   : %3d %%",
			      ((zftc->rd_compressed * 100)
			       / zftc->rd_uncompressed));
		}
	}
	/* only print it once: */
	zftc->rd_uncompressed =	zftc->rd_compressed   = 0;
	TRACE_EXIT;
}

/* out:
 *
 * int *read_cnt: the number of bytes we removed from the zft_deblock_buf
 *                (result)
 * int *to_do   : the remaining size of the read-request.
 *
 * in:
 *
 * char *buff          : buff is the address of the upper part of the user
 *                       buffer, that hasn't been filled with data yet.

 * int buf_pos_read    : copy of from _ftape_read()
 * int buf_len_read    : copy of buf_len_rd from _ftape_read()
 * char *zft_deblock_buf: zft_deblock_buf
 * unsigned short blk_sz: the block size valid for this volume, may differ
 *                            from zft_blk_sz.
 * int finish: if != 0 means that this is the last segment belonging
 *  to this volume
 * returns the amount of data actually copied to the user-buffer
 *
 * to_do MUST NOT SHRINK except to indicate an EOF. In this case *to_do has to
 * be set to 0 
 */
static int zftc_read (void *handle,
		      int *read_cnt, 
		      __u8  *dst_buf, const int to_do, 
		      const __u8 *src_buf, const int seg_sz, 
		      const zft_position *pos, const zft_volinfo *volume)
{          
	struct zftc_struct *zftc = handle;
	int uncompressed_sz;         
	int result = 0;
	int remaining = to_do;
	TRACE_FUN(ft_t_flow);

	TRACE_CATCH(zft_allocate_cmpr_mem(zftc, volume->blk_sz),);
	if (pos->seg_byte_pos == 0) {
		/* new segment just read
		 */
		TRACE_CATCH(get_cseg(zftc, &zftc->cseg, src_buf, seg_sz, volume),
			    *read_cnt = 0);
		memcpy(zftc->buf + zftc->cseg.cmpr_pos, src_buf + sizeof(__u16), 
		       zftc->cseg.count);
		zftc->cseg.cmpr_pos += zftc->cseg.count;
		*read_cnt      = zftc->cseg.offset;
		DUMP_CMPR_INFO(ft_t_noise /* ft_t_any */, "", &zftc->cseg);
	} else {
		*read_cnt = 0;
	}
	/* loop and uncompress until user buffer full or
	 * deblock-buffer empty 
	 */
	TRACE(ft_t_data_flow, "compressed_sz: %d, compos : %d, *read_cnt: %d",
	      zftc->cseg.cmpr_sz, zftc->cseg.cmpr_pos, *read_cnt);
	while ((zftc->cseg.spans == 0) && (remaining > 0)) {
		if (zftc->cseg.cmpr_pos  != 0) { /* cmpr buf is not empty */
			uncompressed_sz = 
				zft_uncompress(zftc, zftc->buf,
					       zftc->cseg.uncmpr == 0x8000 ?
					       -zftc->cseg.cmpr_pos : zftc->cseg.cmpr_pos,
					       zftc->scratch_buf,
					       volume->blk_sz);
			if (uncompressed_sz != volume->blk_sz) {
				*read_cnt = 0;
				TRACE_ABORT(-EIO, ft_t_warn,
				      "Uncompressed blk (%d) != blk size (%d)",
				      uncompressed_sz, volume->blk_sz);
			}
			if (copy_to_user(dst_buf + result, 
					 zftc->scratch_buf, 
					 uncompressed_sz) != 0 ) {
				TRACE_EXIT -EFAULT;
			}
			remaining      -= uncompressed_sz;
			result     += uncompressed_sz;
			zftc->cseg.cmpr_pos  = 0;
		}                                              
		if (remaining > 0) {
			get_next_cluster(zftc, &zftc->cseg, src_buf, seg_sz, 
					 volume->end_seg == pos->seg_pos);
			if (zftc->cseg.count != 0) {
				memcpy(zftc->buf, src_buf + zftc->cseg.offset,
				       zftc->cseg.count);
				zftc->cseg.cmpr_pos = zftc->cseg.count;
				zftc->cseg.offset  += zftc->cseg.count;
				*read_cnt += zftc->cseg.count + sizeof(__u16);
			} else {
				remaining = 0;
			}
		}
		TRACE(ft_t_data_flow, "\n" 
		      KERN_INFO "compressed_sz: %d\n"
		      KERN_INFO "compos       : %d\n"
		      KERN_INFO "*read_cnt    : %d",
		      zftc->cseg.cmpr_sz, zftc->cseg.cmpr_pos, *read_cnt);
	}
	if (seg_sz - zftc->cseg.offset <= 18) {
		*read_cnt += seg_sz - zftc->cseg.offset;
		TRACE(ft_t_data_flow, "expanding read cnt to: %d", *read_cnt);
	}
	TRACE(ft_t_data_flow, "\n"
	      KERN_INFO "segment size   : %d\n"
	      KERN_INFO "read count     : %d\n"
	      KERN_INFO "buf_pos_read   : %d\n"
	      KERN_INFO "remaining      : %d",
		seg_sz, *read_cnt, pos->seg_byte_pos, 
		seg_sz - *read_cnt - pos->seg_byte_pos);
	TRACE(ft_t_data_flow, "returning: %d", result);
	TRACE_EXIT result;
}                

/* seeks to the new data-position. Reads sometimes a segment.
 *  
 * start_seg and end_seg give the boundaries of the current volume
 * blk_sz is the blk_sz of the current volume as stored in the
 * volume label
 *
 * We don't allow blocksizes less than 1024 bytes, therefore we don't need
 * a 64 bit argument for new_block_pos.
 */

static int seek_in_segment(struct zftc_struct *zftc,
			   const unsigned int to_do, cmpr_info  *c_info,
			   const char *src_buf, const int seg_sz, 
			   const int seg_pos, const zft_volinfo *volume);
static int slow_seek_forward_until_error(struct zftc_struct *zftc,
					 const unsigned int distance,
					 cmpr_info *c_info, zft_position *pos, 
					 const zft_volinfo *volume, __u8 **buf);
static int search_valid_segment(struct zftc_struct *zftc,
				unsigned int segment,
				const unsigned int end_seg,
				const unsigned int max_foffs,
				zft_position *pos, cmpr_info *c_info,
				const zft_volinfo *volume, __u8 **buf);
static int slow_seek_forward(struct zftc_struct *zftc,
			     unsigned int dest, cmpr_info *c_info,
			     zft_position *pos, const zft_volinfo *volume,
			     __u8 **buf);
static int compute_seg_pos(unsigned int dest, zft_position *pos,
			   const zft_volinfo *volume);

#define ZFT_SLOW_SEEK_THRESHOLD  10 /* segments */
#define ZFT_FAST_SEEK_MAX_TRIALS 10 /* times */
#define ZFT_FAST_SEEK_BACKUP     10 /* segments */

static int zftc_seek(void *handle, unsigned int new_block_pos,
		     zft_position *pos, const zft_volinfo *volume, __u8 **buf)
{
	struct zftc_struct *zftc = handle;
	unsigned int dest;
	int limit;
	int distance;
	int result = 0;
	int seg_dist;
	int new_seg;
	int old_seg = 0;
	int fast_seek_trials = 0;
	TRACE_FUN(ft_t_flow);

	if (new_block_pos == 0) {
		pos->seg_pos      = volume->start_seg;
		pos->seg_byte_pos = 0;
		pos->volume_pos   = 0;
		memset(&zftc->cseg, 0, sizeof(zftc->cseg));
		TRACE_EXIT 0;
	}
	dest = new_block_pos * (volume->blk_sz >> 10);
	distance = dest - (pos->volume_pos >> 10);
	while (distance != 0) {
		seg_dist = compute_seg_pos(dest, pos, volume);
		TRACE(ft_t_noise, "\n"
		      KERN_INFO "seg_dist: %d\n"
		      KERN_INFO "distance: %d\n"
		      KERN_INFO "dest    : %d\n"
		      KERN_INFO "vpos    : %d\n"
		      KERN_INFO "seg_pos : %d\n"
		      KERN_INFO "trials  : %d",
		      seg_dist, distance, dest,
		      (unsigned int)(pos->volume_pos>>10), pos->seg_pos,
		      fast_seek_trials);
		if (distance > 0) {
			if (seg_dist < 0) {
				TRACE(ft_t_bug, "BUG: distance %d > 0, "
				      "segment difference %d < 0",
				      distance, seg_dist);
				result = -EIO;
				break;
			}
			new_seg = pos->seg_pos + seg_dist;
			if (new_seg > volume->end_seg) {
				new_seg = volume->end_seg;
			}
			if (old_seg == new_seg || /* loop */
			    seg_dist <= ZFT_SLOW_SEEK_THRESHOLD ||
			    fast_seek_trials >= ZFT_FAST_SEEK_MAX_TRIALS) {
				TRACE(ft_t_noise, "starting slow seek:\n"
				   KERN_INFO "fast seek failed too often: %s\n"
				   KERN_INFO "near target position      : %s\n"
				   KERN_INFO "looping between two segs  : %s",
				      (fast_seek_trials >= 
				       ZFT_FAST_SEEK_MAX_TRIALS)
				      ? "yes" : "no",
				      (seg_dist <= ZFT_SLOW_SEEK_THRESHOLD) 
				      ? "yes" : "no",
				      (old_seg == new_seg)
				      ? "yes" : "no");
				result = slow_seek_forward(zftc, dest, &zftc->cseg, 
							   pos, volume, buf);
				break;
			}
			old_seg = new_seg;
			limit = volume->end_seg;
			fast_seek_trials ++;
			for (;;) {
				result = search_valid_segment(zftc, new_seg, limit,
							      volume->size,
							      pos, &zftc->cseg,
							      volume, buf);
				if (result == 0 || result == -EINTR) {
					break;
				}
				if (new_seg == volume->start_seg) {
					result = -EIO; /* set errror 
							* condition
							*/
					break;
				}
				limit    = new_seg;
				new_seg -= ZFT_FAST_SEEK_BACKUP;
				if (new_seg < volume->start_seg) {
					new_seg = volume->start_seg;
				}
			}
			if (result < 0) {
				TRACE(ft_t_warn,
				      "Couldn't find a readable segment");
				break;
			}
		} else /* if (distance < 0) */ {
			if (seg_dist > 0) {
				TRACE(ft_t_bug, "BUG: distance %d < 0, "
				      "segment difference %d >0",
				      distance, seg_dist);
				result = -EIO;
				break;
			}
			new_seg = pos->seg_pos + seg_dist;
			if (fast_seek_trials > 0 && seg_dist == 0) {
				/* this avoids sticking to the same
				 * segment all the time. On the other hand:
				 * if we got here for the first time, and the
				 * deblock_buffer still contains a valid
				 * segment, then there is no need to skip to 
				 * the previous segment if the desired position
				 * is inside this segment.
				 */
				new_seg --;
			}
			if (new_seg < volume->start_seg) {
				new_seg = volume->start_seg;
			}
			limit   = pos->seg_pos;
			fast_seek_trials ++;
			for (;;) {
				result = search_valid_segment(zftc, new_seg, limit,
							      pos->volume_pos,
							      pos, &zftc->cseg,
							      volume, buf);
				if (result == 0 || result == -EINTR) {
					break;
				}
				if (new_seg == volume->start_seg) {
					result = -EIO; /* set errror 
							* condition
							*/
					break;
				}
				limit    = new_seg;
				new_seg -= ZFT_FAST_SEEK_BACKUP;
				if (new_seg < volume->start_seg) {
					new_seg = volume->start_seg;
				}
			}
			if (result < 0) {
				TRACE(ft_t_warn,
				      "Couldn't find a readable segment");
				break;
			}
		}
		distance = dest - (pos->volume_pos >> 10);
	}
	TRACE_EXIT result;
}


/*  advance inside the given segment at most to_do bytes.
 *  of kilobytes moved
 */

static int seek_in_segment(struct zftc_struct *zftc,
			   const unsigned int to_do,
			   cmpr_info  *c_info,
			   const char *src_buf, 
			   const int seg_sz, 
			   const int seg_pos,
			   const zft_volinfo *volume)
{
	int result = 0;
	int blk_sz = volume->blk_sz >> 10;
	int remaining = to_do;
	TRACE_FUN(ft_t_flow);

	if (c_info->offset == 0) {
		/* new segment just read
		 */
		TRACE_CATCH(get_cseg(zftc, c_info, src_buf, seg_sz, volume),);
		c_info->cmpr_pos += c_info->count;
		DUMP_CMPR_INFO(ft_t_noise, "", c_info);
	}
	/* loop and uncompress until user buffer full or
	 * deblock-buffer empty 
	 */
	TRACE(ft_t_noise, "compressed_sz: %d, compos : %d",
	      c_info->cmpr_sz, c_info->cmpr_pos);
	while (c_info->spans == 0 && remaining > 0) {
		if (c_info->cmpr_pos  != 0) { /* cmpr buf is not empty */
			result       += blk_sz;
			remaining    -= blk_sz;
			c_info->cmpr_pos = 0;
		}
		if (remaining > 0) {
			get_next_cluster(zftc, c_info, src_buf, seg_sz, 
					 volume->end_seg == seg_pos);
			if (c_info->count != 0) {
				c_info->cmpr_pos = c_info->count;
				c_info->offset  += c_info->count;
			} else {
				break;
			}
		}
		/*  Allow escape from this loop on signal!
		 */
		FT_SIGNAL_EXIT(_DONT_BLOCK);
		DUMP_CMPR_INFO(ft_t_noise, "", c_info);
		TRACE(ft_t_noise, "to_do: %d", remaining);
	}
	if (seg_sz - c_info->offset <= 18) {
		c_info->offset = seg_sz;
	}
	TRACE(ft_t_noise, "\n"
	      KERN_INFO "segment size   : %d\n"
	      KERN_INFO "buf_pos_read   : %d\n"
	      KERN_INFO "remaining      : %d",
	      seg_sz, c_info->offset,
	      seg_sz - c_info->offset);
	TRACE_EXIT result;
}                

static int slow_seek_forward_until_error(struct zftc_struct *zftc,
					 const unsigned int distance,
					 cmpr_info *c_info,
					 zft_position *pos, 
					 const zft_volinfo *volume,
					 __u8 **buf)
{
	unsigned int remaining = distance;
	int seg_sz;
	int seg_pos;
	int result;
	TRACE_FUN(ft_t_flow);
	
	seg_pos = pos->seg_pos;
	do {
		TRACE_CATCH(seg_sz = zft_fetch_segment(zftc->zftape,
						       seg_pos, buf, 
						       FT_RD_AHEAD),);
		/* now we have the contents of the actual segment in
		 * the deblock buffer
		 */
		TRACE_CATCH(result = seek_in_segment(zftc, remaining, c_info, *buf,
						     seg_sz, seg_pos,volume),);
		remaining        -= result;
		pos->volume_pos  += result<<10;
		pos->seg_pos      = seg_pos;
		pos->seg_byte_pos = c_info->offset;
		seg_pos ++;
		if (seg_pos <= volume->end_seg && c_info->offset == seg_sz) {
			pos->seg_pos ++;
			pos->seg_byte_pos = 0;
			c_info->offset = 0;
		}
		/*  Allow escape from this loop on signal!
		 */
		FT_SIGNAL_EXIT(_DONT_BLOCK);
		TRACE(ft_t_noise, "\n"
		      KERN_INFO "remaining:  %d\n"
		      KERN_INFO "seg_pos:    %d\n"
		      KERN_INFO "end_seg:    %d\n"
		      KERN_INFO "result:     %d",
		      remaining, seg_pos, volume->end_seg, result);  
	} while (remaining > 0 && seg_pos <= volume->end_seg);
	TRACE_EXIT 0;
}

/* return segment id of next segment containing valid data, -EIO otherwise
 */
static int search_valid_segment(struct zftc_struct *zftc,
				unsigned int segment,
				const unsigned int end_seg,
				const unsigned int max_foffs,
				zft_position *pos,
				cmpr_info *c_info,
				const zft_volinfo *volume,
				__u8 **buf)
{
	cmpr_info tmp_info;
	int seg_sz;
	TRACE_FUN(ft_t_flow);
	
	memset(&tmp_info, 0, sizeof(cmpr_info));
	while (segment <= end_seg) {
		FT_SIGNAL_EXIT(_DONT_BLOCK);
		TRACE(ft_t_noise,
		      "Searching readable segment between %d and %d",
		      segment, end_seg);
		seg_sz = zft_fetch_segment(zftc->zftape,
					   segment, buf, FT_RD_AHEAD);
		if ((seg_sz > 0) &&
		    (get_cseg (zftc, &tmp_info, *buf, seg_sz, volume) >= 0) &&
		    (tmp_info.foffs != 0 || segment == volume->start_seg)) {
			if ((tmp_info.foffs>>10) > max_foffs) {
				TRACE_ABORT(-EIO, ft_t_noise, "\n"
					    KERN_INFO "zftc->cseg.foff: %d\n"
					    KERN_INFO "dest     : %d",
					    (int)(tmp_info.foffs >> 10),
					    max_foffs);
			}
			DUMP_CMPR_INFO(ft_t_noise, "", &tmp_info);
			*c_info           = tmp_info;
			pos->seg_pos      = segment;
			pos->volume_pos   = c_info->foffs;
			pos->seg_byte_pos = c_info->offset;
			TRACE(ft_t_noise, "found segment at %d", segment);
			TRACE_EXIT 0;
		}
		segment++;
	}
	TRACE_EXIT -EIO;
}

static int slow_seek_forward(struct zftc_struct *zftc,
			     unsigned int dest,
			     cmpr_info *c_info,
			     zft_position *pos,
			     const zft_volinfo *volume,
			     __u8 **buf)
{
	unsigned int distance;
	int result = 0;
	TRACE_FUN(ft_t_flow);
		
	distance = dest - (pos->volume_pos >> 10);
	while ((distance > 0) &&
	       (result = slow_seek_forward_until_error(zftc, distance,
						       c_info,
						       pos,
						       volume,
						       buf)) < 0) {
		if (result == -EINTR) {
			break;
		}
		TRACE(ft_t_noise, "seg_pos: %d", pos->seg_pos);
		/* the failing segment is either pos->seg_pos or
		 * pos->seg_pos + 1. There is no need to further try
		 * that segment, because ftape_read_segment() already
		 * has tried very much to read it. So we start with
		 * following segment, which is pos->seg_pos + 1
		 */
		if(search_valid_segment(zftc, pos->seg_pos+1, volume->end_seg, dest,
					pos, c_info,
					volume, buf) < 0) {
			TRACE(ft_t_noise, "search_valid_segment() failed");
			result = -EIO;
			break;
		}
		distance = dest - (pos->volume_pos >> 10);
		result = 0;
		TRACE(ft_t_noise, "segment: %d", pos->seg_pos);
		/* found valid segment, retry the seek */
	}
	TRACE_EXIT result;
}

static int compute_seg_pos(const unsigned int dest,
			   zft_position *pos,
			   const zft_volinfo *volume)
{
	int segment;
	int distance = dest - (pos->volume_pos >> 10);
	unsigned int raw_size;
	unsigned int virt_size;
	unsigned int factor;

	if (distance >= 0) {
		raw_size  = volume->end_seg - pos->seg_pos + 1;
		virt_size = ((unsigned int)(volume->size>>10) 
			     - (unsigned int)(pos->volume_pos>>10)
			     + FT_SECTORS_PER_SEGMENT - FT_ECC_SECTORS - 1);
		virt_size /= FT_SECTORS_PER_SEGMENT - FT_ECC_SECTORS;
		if (virt_size == 0 || raw_size == 0) {
			return 0;
		}
		if (raw_size >= (1<<25)) {
			factor = raw_size/(virt_size>>7);
		} else {
			factor = (raw_size<<7)/virt_size;
		}
		segment = distance/(FT_SECTORS_PER_SEGMENT-FT_ECC_SECTORS);
		segment = (segment * factor)>>7;
	} else {
		raw_size  = pos->seg_pos - volume->start_seg + 1;
		virt_size = ((unsigned int)(pos->volume_pos>>10)
			     + FT_SECTORS_PER_SEGMENT - FT_ECC_SECTORS - 1);
		virt_size /= FT_SECTORS_PER_SEGMENT - FT_ECC_SECTORS;
		if (virt_size == 0 || raw_size == 0) {
			return 0;
		}
		if (raw_size >= (1<<25)) {
			factor = raw_size/(virt_size>>7);
		} else {
			factor = (raw_size<<7)/virt_size;
		}
		segment = distance/(FT_SECTORS_PER_SEGMENT-FT_ECC_SECTORS);
	}
	return segment;
}

static struct zft_cmpr_ops cmpr_ops = {
	zftc_read,
	zftc_seek,
	zftc_open,
	zftc_close,
	zftc_cleanup
};

#define GLOBAL_TRACING
#include "../lowlevel/ftape-real-tracing.h"

#ifdef FT_TRACE_ATTR
# undef FT_TRACE_ATTR
#endif
#define FT_TRACE_ATTR

int zft_compressor_init(void)
{
	TRACE_FUN(ft_t_flow);
	
#ifdef MODULE
	printk(KERN_INFO "zftape de-compressor for "FTAPE_VERSION"\n");
        if (TRACE_LEVEL >= ft_t_info) {
		printk(
KERN_INFO "(c) 1994-1998 Claus-Justus Heine <heine@instmath.rwth-aachen.de>\n"
KERN_INFO "De-compressor for zftape (lzrw3 algorithm)\n"
KERN_INFO "Compiled for kernel version %s"
#ifdef MODVERSIONS
		" with versioned symbols"
#endif
		"\n", UTS_RELEASE);
        }
#else /* !MODULE */
	/* print a short no-nonsense boot message */
	printk("zftape de-compressor for "FTAPE_VERSION
	       " for Linux " UTS_RELEASE "\n");
#endif /* MODULE */
	TRACE(ft_t_info, "zft_compressor_init @ 0x%p", zft_compressor_init);
	TRACE(ft_t_info, "installing compressor for zftape ...");
	TRACE_CATCH(zft_cmpr_register(&cmpr_ops),);
	TRACE_EXIT 0;
}


#ifdef MODULE
MODULE_LICENSE("GPL");

MODULE_AUTHOR(
	"(c) 1996-1998 Claus-Justus Heine <heine@instmath.rwth-aachen.de>");
MODULE_DESCRIPTION(
"Compression routines for zftape. Uses the lzrw3 algorithm by Ross Williams");

static int can_unload(void)
{
	return keep_module_locked ? -EBUSY : 0;
}

/* Called by modules package when installing the driver
 */
int init_module(void)
{
	int result;

	if (!mod_member_present(&__this_module, can_unload))
		return -EBUSY;
	__this_module.can_unload = can_unload;
	EXPORT_NO_SYMBOLS;
	result = zft_compressor_init();
	keep_module_locked = 0;
	return result;
}

/* Called by modules package when removing the driver 
 */
void cleanup_module(void)
{
	int sel;
	TRACE_FUN(ft_t_flow);

	if (zft_cmpr_unregister() != &cmpr_ops) {
		TRACE(ft_t_info, "failed");
	} else {
		TRACE(ft_t_info, "successful");
	}
	for (sel = 0; sel < 4; sel++) {
		if (zftcs[sel]) {
			zftc_cleanup(zftcs[sel]);
			kfree(zftcs[sel]);
			zftcs[sel] = NULL;
		}
	}
        printk(KERN_INFO "zft-compressor successfully unloaded.\n");
	TRACE_EXIT;
}
#endif /* MODULE */
