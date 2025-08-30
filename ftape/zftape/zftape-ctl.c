/* 
 *      Copyright (C) 1996-2000 Claus-Justus Heine

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *
 * $RCSfile: zftape-ctl.c,v $
 * $Revision: 1.45 $
 * $Date: 2000/07/23 20:31:00 $
 *
 *      This file contains the non-read/write zftape functions
 *      for the QIC-40/80/3010/3020 floppy-tape driver for Linux.
 */


#include <linux/errno.h>
#define __NO_VERSION__
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fcntl.h>

#include <linux/zftape.h>

#include <asm/uaccess.h>

#include "zftape-init.h"
#include "zftape-eof.h"
#include "zftape-ctl.h"
#include "zftape-write.h"
#include "zftape-read.h"
#include "zftape-rw.h"
#include "zftape-vtbl.h"
#include "zftape-inline.h"

/*      Global vars.
 */
zftape_info_t *zftapes[4] = { NULL, };

/*      Local vars.
 */

typedef int (mt_fun)(zftape_info_t *zftape, int *argptr);
typedef int (*mt_funp)(zftape_info_t *zftape, int *argptr);
typedef struct
{
	mt_funp function;
	unsigned offline         : 1; /* op permitted if offline or no_tape */
	unsigned write_protected : 1; /* op permitted if write-protected    */
	unsigned not_formatted   : 1; /* op permitted if tape not formatted */
	unsigned raw_mode        : 1; /* op permitted if zft_mode == 0    */
	unsigned need_idle_state : 1; /* need to call def_idle_state        */
	char     *name;
} fun_entry;

static mt_fun mt_dummy, mt_reset, mt_fsr, mt_bsr, mt_rew, mt_offl, mt_nop,
	mt_weof, mt_erase, mt_setblk, mt_setdensity,
	mt_seek, mt_tell, mt_reten, mt_eom, mt_fsf, mt_bsf,
	mt_fsfm, mt_bsfm, mt_setdrvbuffer,
	mt_lock, mt_unlock, mt_setpart, mt_load, mt_unload;

static const fun_entry mt_funs[]=
{ 
	{mt_reset       , 1, 1, 1, 1, 0, "MT_RESET" }, /*  0 */
	{mt_fsf         , 0, 1, 0, 0, 1, "MT_FSF"   },
	{mt_bsf         , 0, 1, 0, 0, 1, "MT_BSF"   },
	{mt_fsr         , 0, 1, 0, 1, 1, "MT_FSR"   },
	{mt_bsr         , 0, 1, 0, 1, 1, "MT_BSR"   },
	{mt_weof        , 0, 0, 0, 0, 0, "MT_WEOF"  }, /*  5 */
	{mt_rew         , 0, 1, 1, 1, 0, "MT_REW"   },
	{mt_offl        , 1, 1, 1, 1, 0, "MT_OFFL"  },
	{mt_nop         , 1, 1, 1, 1, 0, "MT_NOP"   },
	{mt_reten       , 0, 1, 1, 1, 0, "MT_RETEN" },
	{mt_bsfm        , 0, 1, 0, 0, 1, "MT_BSFM"  }, /* 10 */
	{mt_fsfm        , 0, 1, 0, 0, 1, "MT_FSFM"  },
	{mt_eom         , 0, 1, 0, 0, 1, "MT_EOM"   },
	{mt_erase       , 0, 0, 0, 1, 0, "MT_ERASE" },
	{mt_dummy       , 1, 1, 1, 1, 0, "MT_RAS1"  },
	{mt_dummy       , 1, 1, 1, 1, 0, "MT_RAS2"  },
	{mt_dummy       , 1, 1, 1, 1, 0, "MT_RAS3"  },
	{mt_dummy       , 1, 1, 1, 1, 0, "UNKNOWN"  },
	{mt_dummy       , 1, 1, 1, 1, 0, "UNKNOWN"  },
	{mt_dummy       , 1, 1, 1, 1, 0, "UNKNOWN"  },
	{mt_setblk      , 1, 1, 1, 1, 1, "MT_SETBLK"}, /* 20 */
	{mt_setdensity  , 1, 1, 1, 1, 0, "MT_SETDENSITY"},
	{mt_seek        , 0, 1, 0, 1, 1, "MT_SEEK"  },
	{mt_dummy       , 0, 1, 0, 1, 1, "MT_TELL"  }, /* wr-only ?! */
	{mt_setdrvbuffer, 1, 1, 1, 1, 0, "MT_SETDRVBUFFER" },
	{mt_dummy       , 1, 1, 1, 1, 0, "MT_FSS"   }, /* 25 */
	{mt_dummy       , 1, 1, 1, 1, 0, "MT_BSS"   },
	{mt_dummy       , 1, 1, 1, 1, 0, "MT_WSM"   },
	{mt_lock        , 1, 1, 1, 1, 0, "MT_LOCK"  },
	{mt_unlock      , 1, 1, 1, 1, 0, "MT_UNLOCK"},
	{mt_load        , 1, 1, 1, 1, 0, "MT_LOAD"  }, /* 30 */
	{mt_unload      , 1, 1, 1, 1, 0, "MT_UNLOAD"},
	{mt_dummy       , 1, 1, 1, 0, 1, "MT_COMPRESSION"},
	{mt_setpart     , 1, 1, 1, 1, 0, "MT_SETPART"},
	{mt_dummy       , 1, 1, 1, 1, 0, "MT_MKPART"}
};  

#define NR_MT_CMDS NR_ITEMS(mt_funs)

/* for mt_fsr/mt_bsr and mt_seek
 */
typedef enum {
	zft_seek_backwards = -1,
	zft_seek_absolute  = 0,
	zft_seek_forward   = 1
} zft_seek_direction_t;

void zft_reset_position(zftape_info_t *zftape, zft_position *pos)
{
	TRACE_FUN(ft_t_flow);

	pos->seg_byte_pos =
		pos->volume_pos = 0;
	if (zftape->vtbl_read ||
	    (zftape->header_read && !zftape->qic_mode)) {
		/* need to keep track of the volume table We therefor
		 * simply position at the beginning of the first
		 * volume. This covers old ftape archives as well has
		 * various flavours of the compression map
		 * segments. The worst case is that the compression
		 * map shows up as an additional volume in front of
		 * all others.
		 */
		pos->seg_pos  = zft_volume_by_segpos(zftape, 0)->start_seg;
		pos->tape_pos = zft_calc_tape_pos(zftape, pos->seg_pos);
	} else {
		pos->tape_pos =  0;
		pos->seg_pos  = -1;
	}
	zftape->just_before_eof =  0;
	zftape->io_state        = zft_idle;
	zft_release_deblock_buffer(zftape, &zftape->deblock_buf);
	zft_zap_read_buffers(zftape);
	zft_zap_write_buffers(zftape);
	zft_prevent_flush(zftape);
#if defined(CONFIG_ZFT_COMPRESSOR) || defined(CONFIG_ZFT_COMPRESSOR_MODULE)
	/* read-only compatibility with pre-ftape-4.x compressed volumes */
	/*  unlock the compresison module if it is loaded.
	 *  The zero arg means not to try to load the module.
	 */
	if (zft_cmpr_lock(zftape, 0) == 0) {
		(*zft_cmpr_ops->reset)(zftape->cmpr_handle); /* unlock */
	}
#endif
	TRACE_EXIT;
}

static void zft_init_driver(zftape_info_t *zftape)
{
	zftape->resid =
		zftape->header_read          =
		zftape->vtbl_read            =
		zftape->old_ftape            =
		zftape->exvt_extension       =
		zftape->vtbl_broken          =
		zftape->offline              =
		zftape->write_protected      =
		zftape->going_offline        =
		zftape->header_changed       =
		zftape->volume_table_changed =
		zftape->force_lock           =
		zftape->written_segments     = 0;
	zft_reset_position(zftape, &zftape->pos); /* does most of the stuff */
	if (zftape->ftape) {
		ftape_set_state(zftape->ftape, waiting);
		if (zftape->ftape->fdc) {
			ftape_zap_read_buffers(zftape->ftape);
		}
	}
}

int zft_def_idle_state(zftape_info_t *zftape)
{ 
	int result = 0;
	int flush  = 1;
	TRACE_FUN(ft_t_flow);
	
	if (!zftape->header_read) {
		result = zft_read_header_segments(zftape);
		flush = 0;
	}
	if (result >= 0 && zftape->qic_mode && !zftape->vtbl_read) {
		result = zft_read_volume_table(zftape);
		flush = 0;
	}
	if (!flush) {
		zft_reset_position(zftape, &zftape->pos);
	} else if ((result = zft_flush_buffers(zftape)) >= 0 &&
		   zftape->qic_mode) {
		/*  don't move past eof
		 */
		(void)zft_close_volume(zftape, &zftape->pos);
	}
	/* just to be sure */
	ftape_abort_operation(zftape->ftape);
	/* clear remaining read buffers */
	zft_zap_read_buffers(zftape);
	zftape->io_state = zft_idle;
	TRACE_EXIT result;
}

/*****************************************************************************
 *                                                                           *
 *  functions for the MTIOCTOP commands                                      *
 *                                                                           *
 *****************************************************************************/

static int mt_dummy(zftape_info_t *zftape, int *dummy)
{
	return -ENOSYS;
}

static int mt_reset(zftape_info_t *zftape, int *dummy)
{        
	ftape_info_t *ftape = zftape->ftape;
	TRACE_FUN(ft_t_flow);

	/* Reset the drive before closing the door. Otherwise we would
	 * have execute two "seek load point" sequences, the explicit
	 * in ftape_reset_drive() and the implied if there is a new
	 * cartridge
	 */	
	zftape->force_lock = 0;
	zftape->blk_sz = CONFIG_ZFT_DFLT_BLK_SZ;
	(void)ftape_seek_to_bot(ftape);
	TRACE_CATCH(ftape_reset_drive(ftape),
		    zft_init_driver(zftape);
		    zft_uninit_mem(zftape);
		    zftape->offline = 1);
	if (ftape->no_tape) {
		TRACE_EXIT mt_load(zftape, dummy);
	}
	/*  fake a re-open of the device. This will set all flage and
	 *  allocate buffers as appropriate. The new tape condition
	 *  will force the open routine to do anything we
	 *  need. Especially, it will reset the offline flag
	 */
	TRACE_EXIT _zft_open(zftape->unit /* dummy */, -1 /* fake reopen */);
}

static int mt_fsf(zftape_info_t *zftape, int *arg)
{
	int result;
	TRACE_FUN(ft_t_flow);

	result = zft_skip_volumes(zftape, *arg, &zftape->pos);
	zftape->just_before_eof = 0;
	TRACE_EXIT result;
}

static int mt_bsf(zftape_info_t *zftape, int *arg)
{
	int result = 0;
	TRACE_FUN(ft_t_flow);
	
	if (*arg != 0) {
		result = zft_skip_volumes(zftape, -*arg + 1, &zftape->pos);
	}
	TRACE_EXIT result;
}

static int seek_block_in_volume(zftape_info_t *zftape,
				__s64 data_offset,
				__s64 block_increment,
				zft_position *pos);

static int seek_block(zftape_info_t *zftape, 
		      zft_seek_direction_t direction,
		      __s64 blocks)
{
	int result = 0;
	__s64 old_pos, new_pos, offset, vol_blocks;
	zft_position *pos = &zftape->pos;
	const zft_volinfo *volume = zft_volume_by_segpos(zftape, pos->seg_pos);
	TRACE_FUN(ft_t_noise);

	/* compute the new block position
	 */
	switch (direction) {
	case zft_seek_backwards:
		old_pos = (zft_div_blksz(pos->volume_pos,
					 zft_block_size(zftape, volume))
			   +
			   zft_blk_offs(zftape, volume));
		new_pos = old_pos - blocks;
		break;
	case zft_seek_forward:
		old_pos = (zft_div_blksz(pos->volume_pos,
					 zft_block_size(zftape, volume))
			   +
			   zft_blk_offs(zftape, volume));
		new_pos = old_pos + blocks;
		break;
	case zft_seek_absolute:
		old_pos = 0;
		new_pos = blocks;
		break;
	default:
		TRACE(ft_t_bug, "BUG: Unknown seek direction");
		zftape->resid = blocks;
		TRACE_EXIT -EINVAL;
	}

	TRACE(ft_t_noise, "old pos: "LL_X", new pos: "LL_X,
	      LL(old_pos), LL(new_pos));

	/* handle the simplest case first: */
	if (new_pos <= 0) {
		result = ftape_seek_to_bot(zftape->ftape);
		zft_reset_position(zftape, pos);
		zftape->resid = -new_pos;	       
		TRACE_EXIT result < 0 ? result : (new_pos != 0 ? -EINVAL : 0);
	}

	volume = zft_volume_by_blkpos(zftape, new_pos, &offset);

	pos->seg_pos        = volume->start_seg;
	pos->seg_byte_pos   = 0;
	pos->volume_pos     = 0;
	pos->tape_pos       = zft_calc_tape_pos(zftape, pos->seg_pos);

	vol_blocks = zft_div_blksz(zft_volume_size(zftape, volume),
				   zft_block_size(zftape, volume));
	
	if (offset + vol_blocks >= new_pos) { /* inside the right volume */
		result = seek_block_in_volume(zftape,
					      0 /* data offset */,
					      new_pos - offset,
					      pos);
	}

	zftape->resid = (new_pos -
			 offset  -
			 zft_div_blksz(pos->volume_pos,
				       zft_block_size(zftape, volume)));
	if (zftape->resid < 0) {
		zftape->resid = -zftape->resid;
	}

	TRACE(ft_t_noise, "result: %d", result);
	TRACE(ft_t_noise, "resid:  %d", zftape->resid);

	TRACE_EXIT result < 0 ? result : (zftape->resid != 0 ? -EINVAL : 0);
}

static int seek_block_in_volume(zftape_info_t *zftape,
				__s64 data_offset,
				__s64 block_increment,
				zft_position *pos)
{ 
	int result      = 0;
	__s64 new_block_pos;
	__s64 vol_block_count;
	const zft_volinfo *volume;
	int exceed;
	TRACE_FUN(ft_t_flow);
	
	volume = zft_volume_by_segpos(zftape, pos->seg_pos);
	if (volume->start_seg == 0 || volume->end_seg == 0) {
		TRACE_EXIT -EIO;
	}
	new_block_pos   = (zft_div_blksz(data_offset, volume->blk_sz)
			   + block_increment);
	vol_block_count = zft_div_blksz(volume->size, volume->blk_sz);
	if (new_block_pos < 0) {
		TRACE(ft_t_noise,
		      "new_block_pos " LL_X " < 0", LL(new_block_pos));
		zftape->resid     = (int)new_block_pos;
		new_block_pos = 0;
		exceed = 1;
	} else if (new_block_pos > vol_block_count) {
		TRACE(ft_t_noise,
		      "new_block_pos " LL_X " exceeds size of volume " LL_X,
		      LL(new_block_pos), LL(vol_block_count));
		zftape->resid     = (int)(vol_block_count - new_block_pos);
		new_block_pos = vol_block_count;
		exceed = 1;
	} else {
		exceed = 0;
	}

#if defined(CONFIG_ZFT_COMPRESSOR) || defined(CONFIG_ZFT_COMPRESSOR_MODULE)
	/* read-only compatibility with pre-ftape-4.x compressed volumes */
	if (zftape->use_compression && volume->use_compression) {
		TRACE_CATCH(zft_cmpr_lock(zftape, 1 /* try to load */),);
		result = (*zft_cmpr_ops->seek)(zftape->cmpr_handle,
					       new_block_pos, pos, volume,
					       &zftape->deblock_buf);
		pos->tape_pos  = zft_calc_tape_pos(zftape, pos->seg_pos);
		pos->tape_pos += pos->seg_byte_pos;
	} else
#endif
	{
		pos->volume_pos = zft_mul_blksz(new_block_pos, volume->blk_sz);
		pos->tape_pos   = zft_calc_tape_pos(zftape, volume->start_seg);
		pos->tape_pos  += pos->volume_pos;
		pos->seg_pos    = zft_calc_seg_byte_coord(zftape,
							  &pos->seg_byte_pos,
							  pos->tape_pos);
	}
	zftape->just_before_eof = volume->size == pos->volume_pos;
	if (zftape->just_before_eof) {
		/* why this? because zft_file_no checks agains start
		 * and end segment of a volume. We do not want to
		 * advance to the next volume with this function.
		 */
		TRACE(ft_t_noise, "set zftape->just_before_eof");
		zft_position_before_eof(zftape, pos, volume);
	}
	TRACE(ft_t_noise, "\n"
	      KERN_INFO "new_seg_pos : %d\n"
	      KERN_INFO "new_tape_pos: " LL_X "\n"
	      KERN_INFO "volume_pos  : " LL_X "\n"
	      KERN_INFO "vol_size    : " LL_X "\n"
	      KERN_INFO "seg_byte_pos: %d\n"
	      KERN_INFO "blk_sz  : %d", 
	      pos->seg_pos, LL(pos->tape_pos),
	      LL(pos->volume_pos),
	      LL(volume->size), pos->seg_byte_pos,
	      volume->blk_sz);
	if (!exceed) {
		zftape->resid = new_block_pos - zft_div_blksz(pos->volume_pos,
							  volume->blk_sz);
	}
	if (zftape->resid < 0) {
		zftape->resid = -zftape->resid;
	}
	TRACE_EXIT ((exceed || zftape->resid != 0) && result >= 0) ? -EINVAL : result;
}     

static int mt_fsr(zftape_info_t *zftape, int *arg)
{ 
	int result;
	TRACE_FUN(ft_t_flow);
	
#ifdef ZFT_OLD_SEEK_BLOCK_INTERFACE
	result = seek_block_in_volume(zftape,
				      zftape->pos.volume_pos, 
				      *arg,
				      &zftape->pos);
#else
	result = seek_block(zftape, zft_seek_forward, *arg);
#endif
	TRACE_EXIT result;
}

static int mt_bsr(zftape_info_t *zftape, int *arg)
{   
	int result;
	TRACE_FUN(ft_t_noise);
	
#ifdef ZFT_OLD_SEEK_BLOCK_INTERFACE
	result = seek_block_in_volume(zftape,
				      zftape->pos.volume_pos,
				      -*arg,
				      &zftape->pos);
#else
	result = seek_block(zftape, zft_seek_backwards, *arg);
#endif
	TRACE_EXIT result;
}

static int mt_weof(zftape_info_t *zftape, int *arg)
{
	int result;
	TRACE_FUN(ft_t_flow);
	
	TRACE_CATCH(zft_flush_buffers(zftape),);
	result = zft_weof(zftape, *arg, &zftape->pos);
	TRACE_EXIT result;
}

static int mt_rew(zftape_info_t *zftape, int *dummy)
{          
	int result;
	TRACE_FUN(ft_t_flow);
	
	if(zftape->header_read && zftape->vtbl_read) {
		(void)zft_def_idle_state(zftape);
	}
	result = ftape_seek_to_bot(zftape->ftape);
	zft_reset_position(zftape, &zftape->pos);
	TRACE_EXIT result;
}

static int mt_offl(zftape_info_t *zftape, int *dummy)
{
	int result;
	TRACE_FUN(ft_t_flow);
	
	zftape->force_lock = 0;
	if (zftape->offline) {
		(void)ftape_door_lock(zftape->ftape, 0);
		(void)ftape_door_open(zftape->ftape, 1); /* if supported */
		(void)ftape_get_drive_status(zftape->ftape);    /* update flags */
		TRACE_EXIT 0;
	}
	zftape->going_offline= 1;
	result = mt_rew(zftape, NULL);
	TRACE_EXIT result;
}

static int mt_nop(zftape_info_t *zftape, int *dummy)
{
	TRACE_FUN(ft_t_flow);
	/*  should we set tape status?
	 */
	if (!zftape->offline) { /* offline includes no_tape */
		(void)zft_def_idle_state(zftape);
	}
	TRACE_EXIT 0; 
}

static int mt_reten(zftape_info_t *zftape, int *dummy)
{  
	int result;
	TRACE_FUN(ft_t_flow);
	
	if(zftape->header_read && zftape->vtbl_read) {
		(void)zft_def_idle_state(zftape);
	}
	result = ftape_seek_to_eot(zftape->ftape);
	if (result >= 0) {
		result = ftape_seek_to_bot(zftape->ftape);
	}
	TRACE_EXIT(result);
}

static int fsfbsfm(zftape_info_t *zftape, int arg, zft_position *pos)
{ 
	const zft_volinfo *vtbl;
	__s64 block_pos;
	TRACE_FUN(ft_t_flow);
	
	/* What to do? This should seek to the next file-mark and
	 * position BEFORE. That is, a next write would just extend
	 * the current file.  Well. Let's just seek to the end of the
	 * current file, if count == 1.  If count > 1, then do a
	 * "mt_fsf(count - 1)", and then seek to the end of that file.
	 * If count == 0, do nothing
	 */
	if (arg == 0) {
		TRACE_EXIT 0;
	}
	zftape->just_before_eof = 0;
	TRACE_CATCH(zft_skip_volumes(zftape, arg < 0 ? arg : arg-1, pos),
		    if (arg > 0) {
			    zftape->resid ++; 
		    });
	vtbl      = zft_volume_by_segpos(zftape, pos->seg_pos);
	block_pos = zft_div_blksz(vtbl->size, vtbl->blk_sz);
	(void)seek_block_in_volume(zftape, 0, block_pos, pos);
	if (pos->volume_pos != vtbl->size) {
		zftape->just_before_eof = 0;
		zftape->resid = 1;
		/* we didn't managed to go there */
		TRACE_ABORT(-EIO, ft_t_err, 
			    "wanted file position " LL_X ", arrived at " LL_X, 
			    LL(vtbl->size), LL(pos->volume_pos));
	}
	zftape->just_before_eof = 1;
	TRACE_EXIT 0; 
}

static int mt_bsfm(zftape_info_t *zftape, int *arg)
{
	int result;
	TRACE_FUN(ft_t_flow);
	
	result = fsfbsfm(zftape, -*arg, &zftape->pos);
	TRACE_EXIT result;
}

static int mt_fsfm(zftape_info_t *zftape, int *arg)
{
	int result;
	TRACE_FUN(ft_t_flow);
	
	result = fsfbsfm(zftape, *arg, &zftape->pos);
	TRACE_EXIT result;
}

static int mt_eom(zftape_info_t *zftape, int *dummy)
{              
	TRACE_FUN(ft_t_flow);
	
	zft_skip_to_eom(zftape, &zftape->pos);
	TRACE_EXIT 0;
}

static int mt_erase(zftape_info_t *zftape, int *dummy)
{
	int result;
	TRACE_FUN(ft_t_flow);
	
	result = zft_erase(zftape);
	TRACE_EXIT result;
}

/*  Sets the new blocksize in BYTES
 *
 */
static int mt_setblk(zftape_info_t *zftape, int *new_size)
{
	TRACE_FUN(ft_t_flow);
	
	if((unsigned int)(*new_size) > ZFT_MAX_BLK_SZ) {
		TRACE_ABORT(-EINVAL, ft_t_info,
			    "desired blk_sz (%d) should be <= %d bytes",
			    *new_size, ZFT_MAX_BLK_SZ);
	}
	if ((*new_size & (FT_SECTOR_SIZE-1)) != 0) {
		TRACE_ABORT(-EINVAL, ft_t_info,
			"desired blk_sz (%d) must be a multiple of %d bytes",
			    *new_size, FT_SECTOR_SIZE);
	}
	if (*new_size == 0) {
		*new_size = 1;
	}
	zftape->blk_sz = *new_size;
	TRACE_EXIT 0;
} 

static int mt_setdensity(zftape_info_t *zftape, int *arg)
{
	TRACE_FUN(ft_t_flow);
	
	SET_TRACE_LEVEL(*arg);
	TRACE(TRACE_LEVEL, "tracing set to %d", TRACE_LEVEL);
	if ((int)TRACE_LEVEL != *arg) {
		TRACE_EXIT -EINVAL;
	}
	TRACE_EXIT 0;
}          

static int mt_seek(zftape_info_t *zftape, int *new_block_pos)
{ 
	int result= 0;
	TRACE_FUN(ft_t_any);
	
#ifdef ZFT_OLD_SEEK_BLOCK_INTERFACE
	result = seek_block_in_volume(zftape,
				      0,
				      (__s64)*new_block_pos,
				      &zftape->pos);
#else
	result = seek_block(zftape, zft_seek_absolute, *new_block_pos);
#endif
	TRACE_EXIT result;
}

/*  OK, this is totally different from SCSI, but the worst thing that can 
 *  happen is that there is not enough defragmentated memory that can be 
 *  allocated. Also, there is a hardwired limit of 16 dma buffers in the 
 *  stock ftape module. This shouldn't bring the system down.
 *
 * NOTE: the argument specifies the total number of dma buffers to use.
 *       The driver needs at least 3 buffers to function at all.
 * 
 */
static int mt_setdrvbuffer(zftape_info_t *zftape, int *cnt)
{
	TRACE_FUN(ft_t_flow);

	if (*cnt < 3) {
		TRACE_EXIT -EINVAL;
	}
	TRACE_CATCH(fdc_set_nr_buffers(zftape->ftape->fdc, *cnt),);
	TRACE_EXIT 0;
}

/* return the block position from start of volume 
 */
static int mt_tell(zftape_info_t *zftape, int *arg)
{
	const zft_volinfo *vol = zft_volume_by_segpos(zftape,
						      zftape->pos.seg_pos);
	TRACE_FUN(ft_t_flow);
	
	*arg   = zft_div_blksz(zftape->pos.volume_pos,
			       zft_block_size(zftape, vol));
#ifndef ZFT_OLD_SEEK_BLOCK_INTERFACE
	*arg +=  zft_blk_offs(zftape, vol); /* add the offset */
#endif       
	TRACE_EXIT 0;
}

static int mt_lock(zftape_info_t *zftape, int *arg)
{
	zftape->force_lock = 1;
	return ftape_door_lock(zftape->ftape, 1);
}

static int mt_unlock(zftape_info_t *zftape, int *arg)
{
	zftape->force_lock = 0;
	return ftape_door_lock(zftape->ftape, 0);
}

static int mt_load(zftape_info_t *zftape, int *dummy)
{
	ftape_info_t *ftape = zftape->ftape;
	TRACE_FUN(ft_t_flow);

	if (!ftape->no_tape && !zftape->offline) {
		TRACE_EXIT 0; /* nothing to do */
	}
	/*  we won't get a "new tape" status when no cartridge
	 *  has been loaded. This is the difference to mt_reset()
	 */
	zftape->offline = 0;
	zftape->force_lock = 0;
	if (ftape->no_tape) {
		ft_trace_t old_level = TRACE_LEVEL;
		SET_TRACE_LEVEL(ft_t_bug);
		(void)ftape_door_lock(zftape->ftape, 0); /* just to be sure */
		SET_TRACE_LEVEL(old_level);
		TRACE_CATCH(ftape_door_open(ftape, 0),); /* close the door  */
		/* use an disable/enable pair to initialize with new
		 * knowledge
		 */
		ftape_disable(FTAPE_SEL(zftape->unit));
		TRACE_CATCH(ftape_enable(FTAPE_SEL(zftape->unit)),);
	}
	/*  fake a re-open of the device. This will set all flage and
	 *  allocate buffers as appropriate. The "fake" condition
	 *  tells the open routine to reinitialize the driver.
	 */
	TRACE_EXIT _zft_open(zftape->unit/* dummy */, -1/* fake reopen */);
}


static int mt_unload(zftape_info_t *zftape, int *dummy)
{
	int result;
	TRACE_FUN(ft_t_flow);
	
	result = mt_offl(zftape, dummy);
	TRACE_EXIT result;
}

static int mt_setpart(zftape_info_t *zftape, int *arg)
{
	TRACE_FUN(ft_t_flow);
	
	if(zftape->header_read && zftape->vtbl_read) {
		(void)zft_def_idle_state(zftape);
		TRACE_CATCH(ftape_seek_to_bot(zftape->ftape),);
		zft_reset_position(zftape, &zftape->pos);
		TRACE_CATCH(zft_update_header_segments(zftape),);
	}
	zft_init_driver(zftape);
	TRACE_EXIT ftape_set_partition(zftape->ftape, *arg);
}


/*  check whether write access is allowed. Write access is denied when
 *  + zftape->write_protected == 1 -- this accounts for either hard write 
 *                                protection of the cartridge or for 
 *                                O_RDONLY access mode of the tape device
 *  + zftape->offline == 1         -- this meany that there is either no tape 
 *                                or that the MTOFFLINE ioctl has been 
 *                                previously issued (`soft eject')
 *  + ftape->formatted == 0        -- this means that the cartridge is not
 *                                formatted
 *  Then we distinuguish two cases. When zft_qic_mode is TRUE, then we try
 *  to emulate a `traditional' (aka SCSI like) UN*X tape device. Therefore we
 *  deny writes when
 *  + zftape->qic_mode ==1 && 
 *       (!zftape->tape_at_lbot() &&   -- tape no at logical BOT
 *        !(zft_tape_at_eom() ||   -- tape not at logical EOM (or EOD)
 *          (zft_tape_at_eom() &&
 *           (zftape->old_ftape ||  -- we can't add new volume to tapes 
 *                                       written by old ftape because ftape
 *                                       don't use the volume table
 *            zftape->exvt_extension)))) -- EXVT extension is not supported.
 *                                          So we can't write to the tape in
 *                                          this case.
 *
 *  when the drive is in true raw mode (aka /dev/rawft0) then we don't 
 *  care about LBOT and EOM conditions. This device is intended for a 
 *  user level program that wants to truly implement the QIC-80 compliance
 *  at the logical data layout level of the cartridge, i.e. implement all
 *  that volume table and volume directory stuff etc.<
 */
int zft_check_write_access(zftape_info_t *zftape, zft_position *pos)
{
	ftape_info_t *ftape = zftape->ftape;
	TRACE_FUN(ft_t_flow);

	if (zftape->offline) { /* offline includes no_tape */
		TRACE_ABORT(-ENXIO,
			    ft_t_info, "tape is offline or no cartridge");
	}
	if (!ftape->formatted) {
		TRACE_ABORT(-EACCES, ft_t_info, "tape is not formatted");
	} 
	if (zftape->write_protected) {
		TRACE_ABORT(-EACCES, ft_t_info, "cartridge write protected");
	} 
	if (zftape->qic_mode) {
		/* must read the header segments to get the volume table
		 * extracted
		 */
		if (!zftape->header_read) {
			TRACE_CATCH(zft_read_header_segments(zftape),);
		}
		if (!zftape->vtbl_read) {
			TRACE_CATCH(zft_read_volume_table(zftape),);
			zft_reset_position(zftape, &zftape->pos);
		}

		/*  check BOT condition */
		if (!zft_tape_at_lbot(zftape, pos)) {
			/*  protect cartridges written by old ftape if
			 *  not at BOT because they use the vtbl
			 *  segment for storing data
			 */
			if (zft_vtbl_soft_ro(zftape)) {
				TRACE(ft_t_warn,
				      "vtbl r/o condition, cannot write");
				TRACE_EXIT -EACCES;
			}
			/*  not at BOT, but allow writes at EOD, of course
			 */
			if (!zft_tape_at_eod(zftape, pos)) {
				TRACE_ABORT(-EACCES, ft_t_info,
					    "tape not at BOT and not at EOD");
			}
		}
		/*  fine. Now the tape is either at BOT or at EOD. */
	}
	/* or in raw mode in which case we don't care about BOT and EOD */
	TRACE_EXIT 0;
}

/*  decide when we should lock the module in memory, even when calling
 *  the release routine. This really is necessary for use with
 *  kerneld/kmod.
 *
 *  NOTE: we MUST NOT use zft_write_protected, because this includes
 *  the file access mode as well which has no meaning with our 
 *  asynchronous update scheme.
 *
 *  Ugly, ugly. We need to look the module if we changed the block size.
 *  How sad! Need persistent modules storage!
 *
 *  NOTE: I don't want to lock the module if the number of dma buffers 
 *  has been changed. It's enough! Stop the story! Give me persisitent
 *  module storage! Do it!
 */
int zft_dirty(zftape_info_t *zftape)
{
	ftape_info_t *ftape = zftape->ftape;

	if (!ftape || !ftape->formatted || zftape->offline) { 
		/* cannot be dirty if not formatted or offline */
		return 0;
	}
	if (zftape->blk_sz != CONFIG_ZFT_DFLT_BLK_SZ) {
		/* blocksize changed, must lock */
		return 1;
	}
	if (!zft_tape_at_lbot(zftape, &zftape->pos)) {
		/* somewhere inside a volume, lock tape */
		return 1;
	}
	if (zftape->volume_table_changed || zftape->header_changed) {
		/* header segments dirty if tape not write protected */
		return !(ftape->write_protected || zft_vtbl_soft_ro(zftape));
	}
	return 0;
}

/*      OPEN routine called by kernel-interface code
 *
 *      NOTE: this is also called by mt_reset() with dev_minor == -1
 *            to fake a reopen after a reset.
 */
int _zft_open(unsigned int dev_minor, unsigned int access_mode)
{
	int sel = FTAPE_SEL(dev_minor);
	zftape_info_t *zftape = zftapes[sel];
	ftape_info_t *ftape;
	TRACE_FUN(ft_t_flow);

	if ((int)access_mode == -1) {
		/* fake reopen */
		access_mode = zftape->file_access_mode;
		ftape = zftape->ftape;
		/* reset all static data to defaults, we don't need to set
		 * the driver flags as this is a fake reopen.
		 */
		zft_init_driver(zftape);
	} else {
		unsigned int old_unit;

		if (zftape == NULL) {
			zftape = zftapes[sel] =
				ftape_kmalloc(sel, sizeof(*zftape), 1);
			if (zftape == NULL) {
				TRACE_ABORT(-EAGAIN, ft_t_err,
					    "insufficient memory to create zftape structure");
			}
			zftape->blk_sz   = CONFIG_ZFT_DFLT_BLK_SZ;
			
			zftape->qic_mode = (dev_minor & ZFT_RAW_MODE) ? 0 : 1;
			TRACE(ft_t_info, "Opening in qic_mode: %d", zftape->qic_mode);
			
			/* initialize the driver flags now
			 * s.t. zft_set_flags() works.
			 */
			zftape->unit = old_unit = dev_minor;
			TRACE_CATCH(zft_set_flags(zftape, zftape->unit),
				    ftape_kfree(FTAPE_SEL(sel),
						&zftapes[sel],
						sizeof(*zftapes[sel])));
			zft_init_driver(zftape);
			
			if (zftape->qic_mode == 0) {
				zft_reset_position(zftape, &zftape->pos);
			}
			
		} else {
			old_unit     = zftape->unit;
			zftape->unit = dev_minor;
			/* decode the minor bits, failure indicates
			 * wrong combination of minor device bits. We
			 * should bail out in this case.
			 */
			TRACE_CATCH(zft_set_flags(zftape, zftape->unit),
				    zftape->unit = old_unit);
		}
		zftape->file_access_mode = access_mode;
#ifndef CONFIG_FT_NO_TRACE_AT_ALL
		zftape->tracing = &ftape_tracings[sel];
		zftape->function_nest_level = &ftape_function_nest_levels[sel];
#endif
		TRACE_CATCH(ftape_enable(sel), zftape->unit = old_unit);
		ftape = zftape->ftape = ftape_get_status(sel);

		/* zft_set_flags() has been called, so that
		 * zft_reset_position() works correctly!!!
		 */
		if (ftape->new_tape || ftape->no_tape || !ftape->formatted) {
			/* reset all static data to defaults,
			 */
			zft_init_driver(zftape);
			zft_uninit_mem(zftape);
		}
	}
	/*  no need for most of the buffers when no tape or not
	 *  formatted.  for the read/write operations, it is the
	 *  regardless whether there is no tape, a not-formatted tape
	 *  or the whether the driver is soft offline.  
	 *  Nevertheless we allow some ioctls with non-formatted tapes, 
	 *  like rewind and reset.
	 */
	if (ftape->no_tape || !ftape->formatted) {
		zft_uninit_mem(zftape);
	}
	if (ftape->no_tape) {
		zftape->offline = 1; /* so we need not test two variables */
	} else if (ftape->new_tape) {
		zftape->offline = 0; /* should reset offline when new tape */
	}
	if ((access_mode == O_WRONLY || access_mode == O_RDWR) &&
	    (ftape->write_protected || ftape->no_tape)) {
		ftape_disable(sel); /* resets ft_no_tape */
		TRACE_ABORT(ftape->no_tape ? -ENXIO : -EROFS,
			    ft_t_warn, "wrong access mode %s cartridge",
			    ftape->no_tape ? "without a" : "with write protected");
	}
#if defined(CONFIG_ZFT_COMPRESSOR) || defined(CONFIG_ZFT_COMPRESSOR_MODULE)
	/* Hack: read-only compatibility with pre-ftape-4.x compressed
	 * volumes
	 */
	if ((access_mode == O_WRONLY || access_mode == O_RDWR) &&
	    zftape->use_compression) {
		TRACE(ft_t_warn, "Warning: "
		      "Writing of compressed archives no longer supported");
		ftape_disable(sel); /* resets ft_no_tape */
		TRACE_EXIT -EROFS;
	}
#endif
	zftape->write_protected = (access_mode == O_RDONLY || 
				   ftape->write_protected != 0);
	if (zftape->write_protected) {
		TRACE(ft_t_noise,
		      "read only access mode: %d, "
		      "drive write protected: %d", 
		      access_mode == O_RDONLY,
		      ftape->write_protected != 0);
	}

	/* zft_seg_pos should be greater than the vtbl segpos but not
	 * if in compatability mode and only after we read in the
	 * header segments
	 *
	 * might also be a problem if the user makes a backup with a
	 * *qft* device and rewinds it with a raw device.
	 */
	if (zftape->qic_mode         &&
	    !zftape->old_ftape       &&
	    zftape->pos.seg_pos >= 0 &&
	    zftape->header_read      && 
	    zftape->vtbl_read        &&
	    zftape->pos.seg_pos <= ftape->first_data_segment) {
		TRACE(ft_t_noise, "you probably mixed up the zftape devices!");
		zft_reset_position(zftape, &zftape->pos); 
	}
	ftape_door_lock(zftape->ftape, 1); /* lock the door, if supported */
	TRACE_EXIT 0;
}

/*      RELEASE routine called by kernel-interface code
 */
int _zft_close(zftape_info_t *zftape, unsigned int dev_minor)
{
	ftape_info_t *ftape = zftape->ftape;
	int result = 0;
	TRACE_FUN(ft_t_flow);
	
	if (zftape->offline) {
		(void)ftape_door_lock(ftape, 0); /* unlock the door,
						  * if supported
						  */
		zft_uninit_mem(zftape); /* just in case */
		/* call the hardware release routine. Puts the drive offline */
		ftape_disable(FTAPE_SEL(dev_minor));
		TRACE_EXIT 0;
	}
	if (zftape->header_read && (zftape->vtbl_read || !zftape->qic_mode) &&
	    !(ftape->write_protected || zft_vtbl_soft_ro(zftape))) {
		result = zft_flush_buffers(zftape);
		TRACE(ft_t_noise, "writing file mark at current position");
		if (zftape->qic_mode &&
		    zft_close_volume(zftape, &zftape->pos) == 0) {
			zft_move_past_eof(zftape, &zftape->pos);
			/* high level driver status, forces creation
			 * of a new volume when calling ftape_write
			 * again and not zft_just_before_eof */
			zftape->io_state = zft_idle;  
		}
		if (zft_tape_at_lbot(zftape, &zftape->pos) ||
		    !(zftape->unit & FTAPE_NO_REWIND)) {
			if (result >= 0) {
				result = zft_update_header_segments(zftape);
			} else {
				TRACE(ft_t_err,
				"Error: unable to update header segments");
			}
			/* no need to lock the FDC drivers any more */
			zft_release_deblock_buffer(zftape,
						   &zftape->deblock_buf);
		}
	}
	ftape_abort_operation(ftape);
	if (!(zftape->unit & FTAPE_NO_REWIND)) {
		TRACE(ft_t_noise, "rewinding tape");
		if (ftape_seek_to_bot(ftape) < 0 && result >= 0) {
			result = -EIO; /* keep old value */
		}
		zft_reset_position(zftape, &zftape->pos);
	} 
	if (zftape->going_offline) {
		zft_init_driver(zftape);
		zft_uninit_mem(zftape);
		zftape->going_offline = 0;
		zftape->offline   = 1;
		(void)ftape_door_lock(ftape, 0); /* unlock and open the door */
		(void)ftape_door_open(ftape, 1); /* if this is supported */
		(void)ftape_get_drive_status(ftape); /* update flags */

		ftape_disable(FTAPE_SEL(dev_minor));
		TRACE_EXIT result;
	}
	if (zft_dirty(zftape)) {
		TRACE(ft_t_noise, "Keeping module locked in memory because:\n"
		      KERN_INFO "header segments need updating: %s\n"
		      KERN_INFO "tape not at BOT              : %s",
		      (zftape->volume_table_changed || zftape->header_changed) 
		      ? "yes" : "no",
		      zft_tape_at_lbot(zftape, &zftape->pos) ? "no" : "yes");
		/* don't unlock the door */
	} else {
#if defined(CONFIG_ZFT_COMPRESSOR) || defined(CONFIG_ZFT_COMPRESSOR_MODULE)
	/* read-only compatibility with pre-ftape-4.x compressed volumes */
		if (zft_cmpr_lock(zftape, 0 /* don't load */) == 0) {
			/* unlock it again */
			(*zft_cmpr_ops->reset)(zftape->cmpr_handle);
		}
#endif
	}
	if (!zftape->volume_table_changed   &&
	    !zftape->header_changed         &&
	    !zftape->bad_sector_map_changed &&
	    !zftape->label_changed          &&
	    !zftape->force_lock) {
		(void)ftape_door_lock(ftape, 0); /* unlock the door, if supported */		
	}
	/* call the hardware release routine. Puts the drive offline */
	ftape_disable(FTAPE_SEL(dev_minor));
	TRACE_EXIT result;
}

/*
 *  the wrapper function around the wrapper MTIOCTOP ioctl
 */
static int mtioctop(zftape_info_t *zftape, struct mtop *mtop, int arg_size)
{
	ftape_info_t *ftape = zftape->ftape;
	int result = 0;
	const fun_entry *mt_fun_entry;
	TRACE_FUN(ft_t_flow);
	
	if (arg_size != sizeof(struct mtop) || mtop->mt_op >= NR_MT_CMDS) {
		TRACE_EXIT -EINVAL;
	}
	TRACE(ft_t_noise, "calling MTIOCTOP command: %s",
	      mt_funs[mtop->mt_op].name);
	mt_fun_entry= &mt_funs[mtop->mt_op];
	/* Don't set the residue count when calling mt_nop!!!!!!!! */
	if (mtop->mt_op != MTNOP) {
		zftape->resid = mtop->mt_count;
	}
	if (!mt_fun_entry->offline && zftape->offline) {
		if (ftape->no_tape) {
			TRACE_ABORT(-ENXIO, ft_t_info, "no tape present");
		} else {
			TRACE_ABORT(-ENXIO, ft_t_info, "drive is offline");
		}
	}
	if (!mt_fun_entry->not_formatted && !ftape->formatted) {
		TRACE_ABORT(-EACCES, ft_t_info, "tape is not formatted");
	}
	if (!mt_fun_entry->write_protected) {
		TRACE_CATCH(zft_check_write_access(zftape, &zftape->pos),);
	}
	if (mt_fun_entry->need_idle_state &&
	    !(zftape->offline || !ftape->formatted)) {
		TRACE_CATCH(zft_def_idle_state(zftape),);
	}
	if (!zftape->qic_mode && !mt_fun_entry->raw_mode) {
		TRACE_ABORT(-EACCES, ft_t_info, 
"Drive needs to be in QIC-80 compatibility mode for this command");
	}
	result = (mt_fun_entry->function)(zftape, &mtop->mt_count);
	if (zftape->header_read &&
	    zftape->vtbl_read   &&
	    zft_tape_at_lbot(zftape, &zftape->pos)) {
		TRACE_CATCH(zft_update_header_segments(zftape),);
	}
	/* Don't clear the residue count when calling mt_nop!!!!!!!! */
	if (result >= 0 && mtop->mt_op != MTNOP) {
		zftape->resid = 0;
	}
	TRACE_EXIT result;
}

/*
 *  standard MTIOCGET ioctl
 */
static int mtiocget(zftape_info_t *zftape, struct mtget *mtget, int arg_size)
{
	ftape_info_t *ftape = zftape->ftape;
	const zft_volinfo *volume;
	__s64 max_tape_pos;
	TRACE_FUN(ft_t_flow);
	
	if (arg_size != sizeof(struct mtget)) {
		TRACE_ABORT(-EINVAL, ft_t_info, "bad argument size: %d",
			    arg_size);
	}
	mtget->mt_type  = ftape->drive_type.vendor_id + 0x800000;
	mtget->mt_dsreg = ftape->last_status.space;
	mtget->mt_erreg = ftape->last_error.space; /* error register */
	mtget->mt_resid = zftape->resid; /* residuum of writes, reads and
					  * MTIOCTOP commands 
					  */
	if (!zftape->offline) { /* neither no_tape nor soft offline */
		mtget->mt_gstat = GMT_ONLINE(~0UL);
		/* should rather return the status of the cartridge
		 * than the access mode of the file, therefor use
		 * ft_write_protected, not zft_write_protected
		 *
		 * However, in some case (VTBL is trashed, or old
		 * ftape-2.x cartridge) the cartridge is "soft"
		 * write-protected. In this case we also set the
		 * write-protect bit.
		 */
		if (ftape->write_protected || zft_vtbl_soft_ro(zftape)) {
			mtget->mt_gstat |= GMT_WR_PROT(~0UL);
		}
		/* this catches non-formatted */
		if((zftape->header_read && zftape->vtbl_read) ||
		   (zftape->header_read && !zftape->qic_mode)) {
			const zft_position *pos = &zftape->pos;

			volume = zft_volume_by_segpos(zftape, pos->seg_pos);
			mtget->mt_fileno = volume->count;
			max_tape_pos = zftape->capacity - zftape->blk_sz;
			if (zft_tape_at_eod(zftape, pos)) {
				mtget->mt_gstat |= GMT_EOD(~0UL);
			}
			if (pos->tape_pos > max_tape_pos) {
				mtget->mt_gstat |= GMT_EOT(~0UL);
			}
			mt_tell(zftape, &mtget->mt_blkno);
			if (zftape->just_before_eof) {
				mtget->mt_gstat |= GMT_EOF(~0UL);
			}
			if (zft_tape_at_lbot(zftape, pos)) {
				mtget->mt_gstat |= GMT_BOT(~0UL);
			}
		} else {
			mtget->mt_fileno = mtget->mt_blkno = -1;
			if (mtget->mt_dsreg & QIC_STATUS_AT_BOT) {
				mtget->mt_gstat |= GMT_BOT(~0UL);
			}
		}
	} else {
		if (ftape->no_tape) {
			mtget->mt_gstat = GMT_DR_OPEN(~0UL);
		} else {
			mtget->mt_gstat = 0UL;
		}
 		mtget->mt_fileno = mtget->mt_blkno = -1;
	}
	TRACE_EXIT 0;
}

#ifdef MTIOCRDFTSEG
/*
 *  Read a floppy tape segment. This is useful for manipulating the
 *  volume table, and read the old header segment before re-formatting
 *  the cartridge.
 */
static int mtiocrdftseg(zftape_info_t *zftape,
			struct mtftseg * mtftseg, int arg_size)
{
	ftape_info_t *ftape = zftape->ftape;
	void         *buffer;
	int          result;
	TRACE_FUN(ft_t_flow);
	
	TRACE(ft_t_noise, "Mag tape ioctl command: MTIOCRDFTSEG");
	if (zftape->qic_mode) {
		TRACE_ABORT(-EACCES, ft_t_info,
			    "driver needs to be in raw mode for this ioctl");
	} 
	if (arg_size != sizeof(struct mtftseg)) {
		TRACE_ABORT(-EINVAL, ft_t_info, "bad argument size: %d",
			    arg_size);
	}
	if (zftape->offline) {
		TRACE_EXIT -ENXIO;
	}
	if (mtftseg->mt_mode != FT_RD_SINGLE &&
	    mtftseg->mt_mode != FT_RD_AHEAD) {
		TRACE_ABORT(-EINVAL, ft_t_info, "invalid read mode");
	}
	if (!ftape->formatted) {
		TRACE_EXIT -EACCES; /* -ENXIO ? */

	}
	if (!zftape->header_read) {
		TRACE_CATCH(zft_def_idle_state(zftape),);
	}
	if (mtftseg->mt_segno > ftape->last_data_segment) {
		TRACE_ABORT(-EINVAL, ft_t_info, "segment number is too large");
	}
	if (mtftseg->mt_segno == zftape->ftape->header_segment_1 ||
	    mtftseg->mt_segno == zftape->ftape->header_segment_2) {
		/*  This hack allows fast access to the header segment
		 *  The header segment has been read anyway, no need
		 *  to re-read it
		 */
		buffer = zftape->hseg_buf;
		mtftseg->mt_result = FT_SEGMENT_SIZE;
	} else if (mtftseg->mt_segno < zftape->ftape->first_data_segment) {
		buffer = NULL;
		mtftseg->mt_result = -EIO;
	} else if (mtftseg->mt_segno == zftape->ftape->first_data_segment) {
		if (!zftape->vtbl_read) {
			result = zft_read_volume_table(zftape);
			mtftseg->mt_result =
				result < 0 ? result : zftape->vtbl_size;
		} else {
			mtftseg->mt_result = zftape->vtbl_size;
		}
		buffer = zftape->vtbl_buf;
	} else {
		mtftseg->mt_result = zft_fetch_segment(zftape,
						       mtftseg->mt_segno,
						       &zftape->deblock_buf,
						       mtftseg->mt_mode);
		buffer = zftape->deblock_buf;
	}

	if (mtftseg->mt_result < 0) {
		/*  a negativ result is not an ioctl error. if the
		 *  user wants to read damaged tapes, it's up to
		 *  her/him
		 */
		TRACE_EXIT 0;
	}
#if 1
	/* the following check can be removed when no longer needed
	 */
	else if (buffer == NULL) {
		TRACE(ft_t_err, "no deblock buffer");
		TRACE_EXIT -EIO;
	}
#endif
	if (copy_to_user(mtftseg->mt_data, buffer, mtftseg->mt_result) != 0) {
		TRACE_EXIT -EFAULT;
	}
	TRACE_EXIT 0;
}
#endif

#ifdef MTIOCWRFTSEG
/*
 *  write a floppy tape segment. This version features writing of
 *  deleted address marks, and gracefully ignores the (software)
 *  ft_formatted flag to support writing of header segments after
 *  formatting.
 */
static int mtiocwrftseg(zftape_info_t *zftape,
			struct mtftseg * mtftseg, int arg_size)
{
	ftape_info_t *ftape = zftape->ftape;
	int result;
	TRACE_FUN(ft_t_flow);
	
	TRACE(ft_t_noise, "Mag tape ioctl command: MTIOCWRFTSEG");
	if (zftape->write_protected) {
		TRACE_EXIT -EROFS;
	}
	if (zftape->qic_mode) {
		TRACE_ABORT(-EACCES, ft_t_info,
			    "driver needs to be in raw mode for this ioctl");
	} 
	if (arg_size != sizeof(struct mtftseg)) {
		TRACE_ABORT(-EINVAL, ft_t_info, "bad argument size: %d",
			    arg_size);
	}
	if (zftape->offline) {
		TRACE_EXIT -ENXIO;
	}
	if (mtftseg->mt_mode != FT_WR_ASYNC   && 
	    mtftseg->mt_mode != FT_WR_MULTI   &&
	    mtftseg->mt_mode != FT_WR_SINGLE  &&
	    mtftseg->mt_mode != FT_WR_DELETE) {
		TRACE_ABORT(-EINVAL, ft_t_info, "invalid write mode");
	}
	/*
	 *  We don't check for ft_formatted, because this gives
	 *  only the software status of the driver.
	 *
	 *  We assume that the user knows what it is
	 *  doing. And rely on the low level stuff to fail
	 *  when the tape isn't formatted. We only make sure
	 *  that The header segment buffer is allocated,
	 *  because it holds the bad sector map.
	 */
	if (zftape->hseg_buf == NULL) {
		TRACE_EXIT -ENXIO;
	}
	if ((zftape->deblock_buf =
	     fdc_get_deblock_buffer(ftape->fdc)) == NULL) {
		TRACE_ABORT(-EIO, ft_t_bug, "No deblock buffer");
	}
	if (mtftseg->mt_mode != FT_WR_DELETE) {
		if (copy_from_user(zftape->deblock_buf, 
				   mtftseg->mt_data,
				   FT_SEGMENT_SIZE) != 0) {
			TRACE_EXIT -EFAULT;
		}
	}
	mtftseg->mt_result = ftape_write_segment(ftape,
						 mtftseg->mt_segno, 
						 &zftape->deblock_buf,
						 mtftseg->mt_mode);
	zftape->deblock_segment = -1;
	if (mtftseg->mt_result >= 0 && mtftseg->mt_mode == FT_WR_SINGLE) {
		/*  
		 *  a negativ result is not an ioctl error. if
		 *  the user wants to write damaged tapes,
		 *  it's up to her/him
		 */
		if ((result = ftape_loop_until_writes_done(ftape)) < 0) {
			mtftseg->mt_result = result;
		}
	}
	if (mtftseg->mt_segno < zftape->ftape->first_data_segment) {
		/* mark header unread. Another approach would be
		 * to simply copy mtftseg->mt_data to zftape->hseg
		 */
		zftape->vtbl_read = zftape->header_read = 0;
	} else if (mtftseg->mt_segno == zftape->ftape->first_data_segment) {
		zftape->vtbl_read = 0;
	}
	TRACE_EXIT 0;
}
#endif
  
#ifdef MTIOCVOLINFO
/*
 *  get information about volume positioned at.
 */
static int mtiocvolinfo(zftape_info_t *zftape,
			struct mtvolinfo *volinfo, int arg_size)
{
	ftape_info_t *ftape = zftape->ftape;
	const zft_volinfo *volume;
	TRACE_FUN(ft_t_flow);
	
	TRACE(ft_t_noise, "Mag tape ioctl command: MTIOCVOLINFO");
	if (arg_size != sizeof(struct mtvolinfo)) {
		TRACE_ABORT(-EINVAL,
			    ft_t_info, "bad argument size: %d", arg_size);
	}
	if (zftape->offline) {
		TRACE_EXIT -ENXIO;
	}
	if (!ftape->formatted) {
		TRACE_EXIT -EACCES;
	}
	TRACE_CATCH(zft_def_idle_state(zftape),);
	volume = zft_volume_by_segpos(zftape, zftape->pos.seg_pos);
	volinfo->mt_volno   = volume->count;
	volinfo->mt_blksz   = volume->blk_sz == 1 ? 0 : volume->blk_sz;
	volinfo->mt_size    = volume->size >> 10;
	volinfo->mt_rawsize = ((zft_calc_tape_pos(zftape, volume->end_seg + 1) >> 10) -
			       (zft_calc_tape_pos(zftape, volume->start_seg) >> 10));
#if defined(CONFIG_ZFT_COMPRESSOR) || defined(CONFIG_ZFT_COMPRESSOR_MODULE)
	/* read-only compatibility with pre-ftape-4.x compressed volumes */
	volinfo->mt_cmpr    = volume->use_compression;
#endif
	TRACE_EXIT 0;
}
#endif

#ifdef CONFIG_ZFT_OBSOLETE  
static int mtioc_zftape_getblksz(zftape_info_t *zftape,
				 struct mtblksz *blksz, int arg_size)
{
	ftape_info_t *ftape = zftape->ftape;
	TRACE_FUN(ft_t_flow);
	
	TRACE(ft_t_noise, "\n"
	      KERN_INFO "Mag tape ioctl command: MTIOC_ZTAPE_GETBLKSZ\n"
	      KERN_INFO "This ioctl is here merely for compatibility.\n"
	      KERN_INFO "Please use MTIOCVOLINFO instead");
	if (arg_size != sizeof(struct mtblksz)) {
		TRACE_ABORT(-EINVAL,
			    ft_t_info, "bad argument size: %d", arg_size);
	}
	if (zftape->offline) {
		TRACE_EXIT -ENXIO;
	}
	if (!ftape->formatted) {
		TRACE_EXIT -EACCES;
	}
	TRACE_CATCH(zft_def_idle_state(zftape),);
	blksz->mt_blksz = zft_volume_by_segpos(zftape, zftape->pos.seg_pos)->blk_sz;
	TRACE_EXIT 0;
}
#endif

#ifdef MTIOCGETSIZE
/*
 *  get the capacity of the tape cartridge.
 */
static int mtiocgetsize(zftape_info_t *zftape,
			struct mttapesize *size, int arg_size)
{
	ftape_info_t *ftape = zftape->ftape;
	TRACE_FUN(ft_t_flow);
	
	TRACE(ft_t_noise, "Mag tape ioctl command: MTIOC_ZFTAPE_GETSIZE");
	if (arg_size != sizeof(struct mttapesize)) {
		TRACE_ABORT(-EINVAL,
			    ft_t_info, "bad argument size: %d", arg_size);
	}
	if (zftape->offline) {
		TRACE_EXIT -ENXIO;
	}
	if (!ftape->formatted) {
		TRACE_EXIT -EACCES;
	}
	TRACE_CATCH(zft_def_idle_state(zftape),);
	size->mt_capacity = (unsigned int)(zftape->capacity>>10);
	size->mt_used     = (unsigned int)(zft_get_eom_pos(zftape)>>10);
	TRACE_EXIT 0;
}
#endif

static int mtiocpos(zftape_info_t *zftape,
		    struct mtpos *mtpos, int arg_size)
{
	int result;
	TRACE_FUN(ft_t_flow);
	
	TRACE(ft_t_noise, "Mag tape ioctl command: MTIOCPOS");
	if (arg_size != sizeof(struct mtpos)) {
		TRACE_ABORT(-EINVAL,
			    ft_t_info, "bad argument size: %d", arg_size);
	}
	if (zftape->offline) {
		TRACE_EXIT -ENXIO;
	}
	if (!zftape->ftape->formatted) {
		TRACE_EXIT -EACCES;
	}
	TRACE_CATCH(zft_def_idle_state(zftape),);
	result = mt_tell(zftape, (int *)&mtpos->mt_blkno);
	TRACE_EXIT result;
}

#ifdef MTIOCFTFORMAT
/*
 * formatting of floppy tape cartridges. This is intended to be used
 * together with the MTIOCFTCMD ioctl and the new mmap feature 
 */

/* 
 *  This function uses ftape_decode_header_segment() to inform the low
 *  level ftape module about the new parameters.
 *
 *  It erases the hseg_buf. The calling process must specify all
 *  parameters to assure proper operation.
 *
 *  return values: -EINVAL - wrong argument size
 *                 -EINVAL - if ftape_decode_header_segment() failed.
 */
static int set_format_parms(zftape_info_t *zftape,
			    struct ftfmtparms *p, __u8 *hseg_buf)
{
	ftape_info_t *ftape = zftape->ftape;
	ft_trace_t old_level = TRACE_LEVEL;
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_noise, "MTIOCFTFORMAT operation FTFMT_SETPARMS");
	memset(hseg_buf, 0, FT_SEGMENT_SIZE);
	PUT4(hseg_buf, FT_SIGNATURE, FT_HSEG_MAGIC);

	/*  fill in user specified parameters
	 */
	hseg_buf[FT_FMT_CODE] = (__u8)p->ft_fmtcode;
	PUT2(hseg_buf, FT_SPT, p->ft_spt);
	hseg_buf[FT_TPC]      = (__u8)p->ft_tpc;
	hseg_buf[FT_FHM]      = (__u8)p->ft_fhm;
	hseg_buf[FT_FTM]      = (__u8)p->ft_ftm;

	/*  fill in sane defaults to make ftape happy.
	 */ 
	hseg_buf[FT_FSM] = (__u8)128; /* 128 is hard wired all over ftape */
	if (p->ft_fmtcode == fmt_big) {
		PUT4(hseg_buf, FT_6_HSEG_1,   0);
		PUT4(hseg_buf, FT_6_HSEG_2,   1);
		PUT4(hseg_buf, FT_6_FRST_SEG, 2);
		PUT4(hseg_buf, FT_6_LAST_SEG, p->ft_spt * p->ft_tpc - 1);
	} else {
		PUT2(hseg_buf, FT_HSEG_1,    0);
		PUT2(hseg_buf, FT_HSEG_2,    1);
		PUT2(hseg_buf, FT_FRST_SEG,  2);
		PUT2(hseg_buf, FT_LAST_SEG, p->ft_spt * p->ft_tpc - 1);
	}

	/* gap3 and format filler byte
	 */
	ftape->fdc->gap3 = (__u8)p->ft_gap3;
	ftape->fdc->ffb  = (__u8)p->ft_ffb;

	/*  Synchronize with the low level module. This is particularly
	 *  needed for unformatted cartridges as the QIC std was previously 
	 *  unknown BUT is needed to set data rate and to calculate timeouts.
	 */
	TRACE_CATCH(ftape_calibrate_data_rate(ftape,
					      p->ft_qicstd&QIC_TAPE_STD_MASK),
		    _res = -EINVAL);

	/*  The following will also recalcualte the timeouts for the tape
	 *  length and QIC std we want to format to.
	 *  abort with -EINVAL rather than -EIO
	 */
	SET_TRACE_LEVEL(ft_t_warn);
	TRACE_CATCH(ftape_decode_header_segment(ftape, hseg_buf),
		    SET_TRACE_LEVEL(old_level); _res = -EINVAL);
	SET_TRACE_LEVEL(old_level);
	TRACE_EXIT 0;
}

/*
 *  Return the internal SOFTWARE status of the kernel driver. This does
 *  NOT query the tape drive about its status.
 */
static int get_format_parms(zftape_info_t *zftape,
			    struct ftfmtparms *p, __u8 *hseg_buffer)
{
	ftape_info_t *ftape = zftape->ftape;
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_noise, "MTIOCFTFORMAT operation FTFMT_GETPARMS");
	p->ft_qicstd  = ftape->qic_std;
	p->ft_fmtcode = ftape->format_code;
	p->ft_fhm     = hseg_buffer[FT_FHM];
	p->ft_ftm     = hseg_buffer[FT_FTM];
	p->ft_spt     = ftape->segments_per_track;
	p->ft_tpc     = ftape->tracks_per_tape;
	p->ft_gap3    = ftape->fdc->gap3;
	p->ft_ffb     = ftape->fdc->ffb;
	TRACE_EXIT 0;
}

static int mtiocftformat(zftape_info_t *zftape,
			 struct mtftformat *mtftformat, int arg_size)
{
	ftape_info_t *ftape = zftape->ftape;
	int result;
	union fmt_arg *arg = &mtftformat->fmt_arg;
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_noise, "Mag tape ioctl command: MTIOCFTFORMAT");
	if (arg_size != sizeof(struct mtftformat)) {
		TRACE_ABORT(-EINVAL,
			    ft_t_info, "bad argument size: %d", arg_size);
	}
	if (zftape->offline) {
		if (ftape->no_tape) {
			TRACE_ABORT(-ENXIO, ft_t_info, "no tape present");
		} else {
			TRACE_ABORT(-ENXIO, ft_t_info, "drive is offline");
		}
	}
	if (zftape->qic_mode) {
		TRACE_ABORT(-EACCES, ft_t_info,
			    "driver needs to be in raw mode for this ioctl");
	} 
	if (mtftformat->fmt_magic != FTFMT_MAGIC &&
	    mtftformat->fmt_op != FTFMT_API_VERSION) {
		TRACE_ABORT(-EINVAL, ft_t_info,
			    "Wrong version of the format ioctl API");
	}
	if (zftape->hseg_buf == NULL) {
		TRACE_CATCH(zft_vcalloc_once(zftape, &zftape->hseg_buf, FT_SEGMENT_SIZE),);
	}
	zftape->header_read = zftape->vtbl_read = 0;
	switch(mtftformat->fmt_op) {
	case FTFMT_SET_PARMS:
		TRACE_CATCH(set_format_parms(zftape, &arg->fmt_parms, zftape->hseg_buf),);
		TRACE_EXIT 0;
	case FTFMT_GET_PARMS:
		TRACE_CATCH(get_format_parms(zftape,
					     &arg->fmt_parms,
					     zftape->hseg_buf),);
		TRACE_EXIT 0;
	case FTFMT_FORMAT_TRACK:
		if ((ftape->formatted &&
		     zft_check_write_access(zftape, &zftape->pos) < 0) ||
		    (!ftape->formatted && zftape->write_protected)) {
			TRACE_ABORT(-EACCES, ft_t_info, "Write access denied");
		}
		TRACE_CATCH(ftape_format_track(ftape,
					       arg->fmt_track.ft_track),);
		TRACE_EXIT 0;
	case FTFMT_STATUS:
		TRACE_CATCH(ftape_format_status(ftape,
						&arg->fmt_status.ft_segment),);
		TRACE_EXIT 0;
	case FTFMT_VERIFY:
		TRACE_CATCH(ftape_verify_segment(ftape,
						 arg->fmt_verify.ft_segment,
				(SectorMap *)&arg->fmt_verify.ft_bsm),);
		TRACE_EXIT 0;
	case FTFMT_API_VERSION:
		mtftformat->fmt_magic = FTFMT_MAGIC;
		TRACE_EXIT 0;
	default:
		TRACE_ABORT(-EINVAL, ft_t_err, "Invalid format operation");
	}
	TRACE_EXIT result;
}
#endif

#ifdef MTIOCFTCMD
/*
 *  send a QIC-117 command to the drive, with optional timeouts,
 *  parameter and result bits. This is intended to be used together
 *  with the formatting ioctl.
 */
static int mtiocftcmd(zftape_info_t *zftape,
		      struct mtftcmd *ftcmd, int arg_size)
{
	ftape_info_t *ftape = zftape->ftape;
	int i;
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_noise, "Mag tape ioctl command: MTIOCFTCMD");

	if (zftape->qic_mode) {
		TRACE_ABORT(-EACCES, ft_t_info,
			    "driver needs to be in raw mode for this ioctl");
	} 
	if (arg_size != sizeof(struct mtftcmd)) {
		TRACE_ABORT(-EINVAL,
			    ft_t_info, "bad argument size: %d", arg_size);
	}
	if (ftcmd->ft_wait_before) {
		TRACE_CATCH(ftape_ready_wait(ftape,
					     ftcmd->ft_wait_before,
					     &ftcmd->ft_status),);
	}
	if (ftcmd->ft_status & QIC_STATUS_ERROR)
		goto ftmtcmd_error;
	if (ftcmd->ft_result_bits != 0) {
		TRACE_CATCH(ftape_report_operation(ftape,
						   &ftcmd->ft_result,
						   ftcmd->ft_cmd,
						   ftcmd->ft_result_bits),);
	} else {
		TRACE_CATCH(ftape_command(ftape, ftcmd->ft_cmd),);
		if (ftcmd->ft_status & QIC_STATUS_ERROR)
			goto ftmtcmd_error;
		for (i = 0; i < ftcmd->ft_parm_cnt; i++) {
			TRACE_CATCH(ftape_parameter(ftape,
						    ftcmd->ft_parms[i]&0x0f),);
			if (ftcmd->ft_status & QIC_STATUS_ERROR)
				goto ftmtcmd_error;
		}
	}
	if (ftcmd->ft_wait_after != 0) {
		TRACE_CATCH(ftape_ready_wait(ftape,
					     ftcmd->ft_wait_after,
					     &ftcmd->ft_status),);
	}
ftmtcmd_error:	       
	if (ftcmd->ft_status & QIC_STATUS_ERROR) {
		TRACE(ft_t_noise, "error status set");
		TRACE_CATCH(ftape_report_error(ftape,
					       &ftcmd->ft_error,
					       &ftcmd->ft_cmd, 1),);
	}
	TRACE_EXIT 0; /* this is not an i/o error */
}
#endif

#ifdef MTIOCFTMODE
/* send a QIC-117 command to the drive, with optional timeouts,
 * parameter and result bits. This is intended to be used together
 * with the formatting ioctl.
 */
static int mtiocftmode(zftape_info_t *zftape,
		       struct mtftmode *ftmode, int arg_size)
{
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_noise, "Mag tape ioctl command: MTIOCFTMODE");

	if (arg_size != sizeof(struct mtftmode)) {
		TRACE_ABORT(-EINVAL,
			    ft_t_info, "bad argument size: %d", arg_size);
	}
	
	/* "==" means "different mode" */
	if (zftape->qic_mode == ftmode->ft_rawmode) {
	     zft_reset_position(zftape, &zftape->pos);
	}
	zftape->qic_mode = !ftmode->ft_rawmode;

	TRACE_EXIT 0; /* this is not an i/o error */
}
#endif

/*  IOCTL routine called by kernel-interface code
 */
int _zft_ioctl(zftape_info_t *zftape, unsigned int command, void * arg)
{
	int result;
	union { struct mtop       mtop;
	        struct mtget      mtget;
		struct mtpos      mtpos;
#ifdef MTIOCRDFTSEG
		struct mtftseg    mtftseg;
#endif
#ifdef MTIOCVOLINFO
		struct mtvolinfo  mtvolinfo;
#endif
#ifdef MTIOCGETSIZE
		struct mttapesize mttapesize;
#endif
#ifdef MTIOCFTFORMAT
		struct mtftformat mtftformat;
#endif
#ifdef CONFIG_ZFT_OBSOLETE
		struct mtblksz mtblksz;
#endif
#ifdef MTIOCFTCMD
		struct mtftcmd mtftcmd;
#endif
#ifdef MTIOCFTMODE
		struct mtftmode mtftmode;
#endif
	} krnl_arg;
	int arg_size = _IOC_SIZE(command);
	int dir = _IOC_DIR(command);
	TRACE_FUN(ft_t_flow);

	/* This check will only catch arguments that are too large !
	 */
	if (dir & (_IOC_READ | _IOC_WRITE) && arg_size > sizeof(krnl_arg)) {
		TRACE_ABORT(-EINVAL,
			    ft_t_info, "bad argument size: %d", arg_size);
	}
	if (dir & _IOC_WRITE) {
		if (copy_from_user(&krnl_arg, arg, arg_size) != 0) {
			TRACE_EXIT -EFAULT;
		}
	}
	TRACE(ft_t_flow, "called with ioctl command: 0x%08x", command);
	switch (command) {
	case MTIOCTOP:
		result = mtioctop(zftape, &krnl_arg.mtop, arg_size);
		break;
	case MTIOCGET:
		result = mtiocget(zftape, &krnl_arg.mtget, arg_size);
		break;
	case MTIOCPOS:
		result = mtiocpos(zftape, &krnl_arg.mtpos, arg_size);
		break;
#ifdef MTIOCVOLINFO
	case MTIOCVOLINFO:
		result = mtiocvolinfo(zftape, &krnl_arg.mtvolinfo, arg_size);
		break;
#endif
#ifdef CONFIG_ZFT_OBSOLETE
	case MTIOC_ZFTAPE_GETBLKSZ:
		result = mtioc_zftape_getblksz(zftape, &krnl_arg.mtblksz, arg_size);
		break;
#endif
#ifdef MTIOCRDFTSEG
	case MTIOCRDFTSEG: /* read a segment via ioctl */
		result = mtiocrdftseg(zftape, &krnl_arg.mtftseg, arg_size);
		break;
#endif
#ifdef MTIOCWRFTSEG
	case MTIOCWRFTSEG: /* write a segment via ioctl */
		result = mtiocwrftseg(zftape, &krnl_arg.mtftseg, arg_size);
		break;
#endif
#ifdef MTIOCGETSIZE
	case MTIOCGETSIZE:
		result = mtiocgetsize(zftape, &krnl_arg.mttapesize, arg_size);
		break;
#endif
#ifdef MTIOCFTFORMAT
	case MTIOCFTFORMAT:
		result = mtiocftformat(zftape, &krnl_arg.mtftformat, arg_size);
		break;
#endif
#ifdef MTIOCFTCMD
	case MTIOCFTCMD:
		result = mtiocftcmd(zftape, &krnl_arg.mtftcmd, arg_size);
		break;
#endif
#ifdef MTIOCFTMODE
	case MTIOCFTMODE:
		result = mtiocftmode(zftape, &krnl_arg.mtftmode, arg_size);
		break;
#endif
	default:
		result = -EINVAL;
		break;
	}
	if ((result >= 0) && (dir & _IOC_READ)) {
		if (copy_to_user(arg, &krnl_arg, arg_size) != 0) {
			TRACE_EXIT -EFAULT;
		}
	}
	TRACE_EXIT result;
}
