#ifndef _ZFTAPE_VTBL_H
#define _ZFTAPE_VTBL_H

/*
 *      Copyright (c) 1995-1998  Claus-Justus Heine

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
 * $RCSfile: zftape-vtbl.h,v $
 * $Revision: 1.16 $
 * $Date: 2000/07/21 09:32:50 $
 *
 *      This file defines a volume table as defined in the QIC-80
 *      development standards.
 */

#include <linux/list.h>

#include "../zftape/zftape-eof.h"

#define VTBL_SIZE 128 /* bytes */

/* The following are offsets in the vtbl.  */
#define VTBL_SIG   0
#define VTBL_START 4
#define VTBL_END   6
#define VTBL_DESC  8
#define VTBL_DATE  52
#define VTBL_DESC_LEN (VTBL_DATE - VTBL_DESC)
#define VTBL_FLAGS 56
#define VTBL_FL_VENDOR_SPECIFIC (1<<0)
#define VTBL_FL_MUTLI_CARTRIDGE (1<<1)
#define VTBL_FL_NOT_VERIFIED    (1<<2)
#define VTBL_FL_REDIR_INHIBIT   (1<<3)
#define VTBL_FL_SEG_SPANNING    (1<<4)
#define VTBL_FL_DIRECTORY_LAST  (1<<5)
#define VTBL_FL_RESERVED_6      (1<<6)
#define VTBL_FL_RESERVED_7      (1<<7)
#define VTBL_M_NO  57
#define VTBL_EXT   58
#define EXT_ZFT_SIG     0
#define EXT_ZFT_BLKSZ  10
#define EXT_ZFT_CMAP   12
#define EXT_ZFT_QIC113 13
#define VTBL_PWD   84
#define VTBL_DIR_SIZE 92
#define VTBL_DATA_SIZE 96
#define VTBL_OS_VERSION 104
#define VTBL_SRC_DRIVE  106
#define VTBL_DEV        122
#define VTBL_RESERVED_1 123
#define VTBL_CMPR       124
#define VTBL_CMPR_UNREG 0x3f
#define VTBL_CMPR_USED  0x80
#define VTBL_FMT        125
#define VTBL_RESERVED_2 126
#define VTBL_RESERVED_3 127
/* compatability with pre revision K */
#define VTBL_K_CMPR     120 

/*  the next is used by QIC-3020 tapes with format code 6 (>2^16
 *  segments) It is specified in QIC-113, Rev. G, Section 5 (SCSI
 *  volume table). The difference is simply, that we only store the
 *  number of segments used, not the starting segment. The starting
 *  and ending segment is stored at offset 76 and 80. The vendor
 *  extension data starts at 62.
 *
 */
#define VTBL_6_SEGS      4  /* is a 4 byte value */
#define VTBL_6_MAJOR     58 /* set to 113 */
#define VTBL_6_MINOR     60 /* Revision G, i.e. 7 */
#define VTBL_6_EXT       62 /* 14 bytes */
#define EXT_6_ZFT_SIG       0 /* signature, 3 bytes */
#define EXT_6_ZFT_SIGV      3 /* version, 1 byte */
#define EXT_6_ZFT_BLKSZ     4 /* block size, 2 bytes */
#define EXT_6_ZFT_RES       6 /* reserved, 8 bytes */
#define VTBL_6_START     76
#define VTBL_6_END       80

/*  one vtbl is 128 bytes, that results in a maximum number of
 *  29*1024/128 = 232 volumes.
 */
#define ZFT_MAX_VOLUMES (zftape->vtbl_size/VTBL_SIZE)
#define VTBL_ID  "VTBL"
#define VTBL_IDS { VTBL_ID, "XTBL", "UTID", "EXVT" } /* other valid ids */
#define ZFT_VOL_NAME "zftape volume" /* volume label used by me */
#define ZFT_SIG     "LINUX ZFT"  /* QIC-80/3010/3020 vendor extension */
#define ZFT_SIGLEN  10
#define ZFT_SIG2    "ZFT"        /* QIC-113 vendor extension */
#define ZFT_SIGLEN2 3


/*  global variables
 */
typedef struct zft_internal_vtbl
{
	struct list_head node;
	int          count;
	unsigned int start_seg;         /* 32 bits are enough for now */
	unsigned int end_seg;           /* 32 bits are enough for now */
	__s64        size;              /* size after compression */
	__s64        rawsize;           /* tape space used */
        unsigned int blk_sz;            /* block size for this volume */
	unsigned int version;           /* corresponds to ZFT_SIG/_SIG2 */
	unsigned int offset;            /* offset in vtbl segment */
	unsigned int zft_volume     :1; /* zftape created this volume */
	unsigned int use_compression:1; /* compressed volume (obsolete) */
	unsigned int qic113         :1; /* layout of compressed block
					 * info and vtbl conforms to
					 * QIC-113, Rev. G 
					 */
	unsigned int new_volume     :1; /* it was created by us, this
					 * run.  this allows the
					 * fields that aren't really
					 * used by zftape to be filled
					 * in by some user level
					 * program.
					 */
	unsigned int open           :1; /* just in progress of being 
					 * written
					 */
} zft_volinfo;

#define zft_head_vtbl  list_entry((zftape)->vtbl.next, zft_volinfo, node)
#define zft_eom_vtbl   list_entry((zftape)->vtbl.prev, zft_volinfo, node)
#define zft_last_vtbl  list_entry(zft_eom_vtbl->node.prev, zft_volinfo, node)
#define zft_first_vtbl list_entry(zft_head_vtbl->node.next, zft_volinfo, node)
#define zft_vtbl_empty (zft_eom_vtbl->node.prev == &zft_head_vtbl->node)

#define DUMP_VOLINFO(level, desc, info)					\
{									\
	char tmp[21];							\
	strncpy(tmp, desc, 20);						\
	tmp[20] = '\0';							\
	TRACE(level, "Volume %d:\n"					\
	      KERN_INFO "description  : %s\n"				\
	      KERN_INFO "first segment: %d\n"				\
	      KERN_INFO "last  segment: %d\n"				\
	      KERN_INFO "size         : " LL_X "\n"			\
	      KERN_INFO "rawsize      : " LL_X "\n"			\
	      KERN_INFO "block size   : %d\n"				\
	      KERN_INFO "version      : %d\n"				\
	      KERN_INFO "offset       : %d\n"				\
	      KERN_INFO "compression  : %d\n"				\
	      KERN_INFO "zftape volume: %d\n"				\
	      KERN_INFO "QIC-113 conf.: %d",				\
	      (info)->count, tmp, (info)->start_seg, (info)->end_seg,	\
	      LL((info)->size), LL((info)->rawsize), (info)->blk_sz,	\
	      (info)->version, (info)->offset,				\
	      (info)->use_compression != 0, (info)->zft_volume != 0,	\
	      (info)->qic113 != 0);					\
}

/* exported functions */
extern void  zft_init_vtbl(struct zftape_info *zftape);
extern void  zft_free_vtbl(struct zftape_info *zftape);
extern void  zft_new_vtbl_entry(struct zftape_info *zftape);
extern void  zft_extract_volume_headers(struct zftape_info *zftape,
					__u8 *buffer);
extern int   zft_update_volume_table(struct zftape_info *zftape,
				     unsigned int segment);
extern int   zft_open_volume(struct zftape_info *zftape,
			     zft_position *pos,
			     int blk_sz);
extern int   zft_close_volume(struct zftape_info *zftape, zft_position *pos);
extern const zft_volinfo *zft_volume_by_segpos(struct zftape_info *zftape,
					       unsigned int seg_pos);
extern const zft_volinfo *zft_volume_by_blkpos(struct zftape_info *zftape,
					       __s64 blk_pos,
					       __s64 *blk_offs);
extern __s64 zft_blk_offs(const struct zftape_info *zftape,
			  const zft_volinfo *vtbl);
extern int   zft_skip_volumes(struct zftape_info *zftape,
			      int count,
			      zft_position *pos);
extern __s64 zft_get_eom_pos(struct zftape_info *zftape);
extern void  zft_skip_to_eom(struct zftape_info *zftape, zft_position *pos);
extern int   zft_fake_volume_headers(struct zftape_info *zftape,
				     eof_mark_union *eof_map, 
				     int num_failed_sectors);
extern int   zft_weof(struct zftape_info *zftape,
		      unsigned int count,
		      zft_position *pos);
extern void  zft_move_past_eof(struct zftape_info *zftape, zft_position *pos);

#endif /* _ZFTAPE_VTBL_H */
