#ifndef _FTAPE_VENDORS_H
#define _FTAPE_VENDORS_H

/*
 *      Copyright (C) 1993-1996 Bas Laarhoven,
 *                (C) 1996-1999 Claus-Justus Heine.

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
 * $RCSfile: ftape-vendors.h,v $
 * $Revision: 1.11 $
 * $Date: 1999/02/26 13:07:01 $
 *
 *      This file contains the supported drive types with their
 *      QIC-117 spec. vendor code and drive dependent configuration
 *      information.
 */

typedef enum {
	unknown_wake_up = 0,
	no_wake_up,
	wake_up_colorado,
	wake_up_mountain,
	wake_up_insight,
} wake_up_types;

typedef struct {
	wake_up_types wake_up;	/* see wake_up_types */
	char *name;		/* Text describing the drive */
} wakeup_method;

/*  Note: order of entries in WAKEUP_METHODS must be so that a variable
 *        of type wake_up_types can be used as an index in the array.
 */
#define WAKEUP_METHODS { \
  { unknown_wake_up,    "Unknown" }, \
  { no_wake_up,         "None" }, \
  { wake_up_colorado,   "Colorado" }, \
  { wake_up_mountain,   "Mountain" }, \
  { wake_up_insight,    "Motor-on" }, \
}

/*  Currently, only one flag:
 */
#define DITTO_MAX_EXT 1
#define FT_CANT_FORMAT 2 /* can't format */

typedef struct {
	unsigned int vendor_id;	/* vendor id from drive */
	int speed;		/* maximum tape transport speed (ips) */
	wake_up_types wake_up;	/* see wake_up_types */
	unsigned long flags;    /* various flags, e.g. cmd set extensions */
	char *name;		/* Text describing the drive */
} vendor_struct;

#define UNKNOWN_VENDOR (-1)

#define QIC117_VENDORS {						      \
/* see _vendor_struct */						      \
  { 0x00000,  82, wake_up_colorado, 0, "Colorado DJ-10 (old)" },	      \
  { 0x00047,  90, wake_up_colorado, 0, "Colorado DJ-10/DJ-20" },	      \
  { 0x011c2,  84, wake_up_colorado, 0, "Colorado 700" },		      \
  { 0x011c3,  90, wake_up_colorado, 0, "Colorado 1400" },		      \
  { 0x011c4,  84, wake_up_colorado, 0, "Colorado DJ-10/DJ-20 (new)" },	      \
  { 0x011c5,  84, wake_up_colorado, 0, "HP Colorado T1000" },		      \
  { 0x011c6,  90, wake_up_colorado, 0, "HP Colorado T3000" },		      \
  { 0x00005,  45, wake_up_mountain, 0, "Archive 5580i" },		      \
  { 0x10005,  50, wake_up_insight,  0, "Insight 80Mb, Irwin 80SX" },	      \
  { 0x00140,  74, wake_up_mountain, 0, "Archive S.Hornet [Identity/Escom]" }, \
  { 0x00146,  72, wake_up_mountain, 0, "Archive 31250Q [Escom]" },	      \
  { 0x0014a, 100, wake_up_mountain, 0, "Archive XL9250i [Conner/Escom]" },    \
  { 0x0014c,  98, wake_up_mountain, 0, "Conner C250MQT" },		      \
  { 0x0014e,  80, wake_up_mountain, 0, "Conner C250MQ" },		      \
  { 0x00150,  80, wake_up_mountain, 0, "Conner TSM420R/TST800R" },	      \
  { 0x00152,  80, wake_up_mountain, 0, "Conner TSM850R" },		      \
  { 0x00156,  80, wake_up_mountain, 0, "Conner TSM850R/1700R/TST3200R" },     \
  { 0x00180,   0, wake_up_mountain, 0, "Summit SE 150" },		      \
  { 0x00181,  85, wake_up_mountain, 0, "Summit SE 250, Mountain FS8000" },    \
  { 0x001c1,  82, no_wake_up,       0, "Wangtek 3040F" },		      \
  { 0x001c8,  64, no_wake_up,       0, "Wangtek 3080F" },		      \
  { 0x001c8,  64, wake_up_colorado, 0, "Wangtek 3080F" },		      \
  { 0x001ca,  67, no_wake_up,       0, "Wangtek 3080F (new)" },		      \
  { 0x001cc,  77, wake_up_colorado, 0, "Wangtek 3200 / Teac 700" },	      \
  { 0x001cd,  75, wake_up_colorado, 0, "Reveal TB1400" },		      \
  { 0x00380,  85, wake_up_colorado, 0, "Exabyte Eagle-96" },		      \
  { 0x00381,  85, wake_up_colorado, 0, "Exabyte Eagle TR-3" },		      \
  { 0x00382,  85, wake_up_colorado, 0, "Exabyte Eagle TR-3" },		      \
  { 0x003ce,  77, wake_up_colorado, 0, "Teac 800" },			      \
  { 0x003cf,   0, wake_up_colorado, 0, "Teac FT3010TR" },		      \
  { 0x08880,  64, no_wake_up,       0, "Iomega 250, Ditto 800" },	      \
  { 0x08880,  64, wake_up_colorado, 0, "Iomega 250, Ditto 800" },	      \
  { 0x08880,  64, wake_up_insight,  0, "Iomega 250, Ditto 800" },	      \
  { 0x08881,  80, wake_up_colorado, 0, "Iomega 700" },			      \
  { 0x08882,  80, wake_up_colorado, 0, "Iomega 3200" },			      \
  { 0x08883,  80, wake_up_colorado, FT_CANT_FORMAT, "Iomega DITTO 2GB" },     \
  { 0x08885,  85, wake_up_colorado,					      \
	    DITTO_MAX_EXT | FT_CANT_FORMAT, "Iomega DITTO MAX" },	      \
  { 0x08886,  85, wake_up_colorado,					      \
	    DITTO_MAX_EXT, "Iomega MerKat Formatter" },			      \
  { 0x00021,  70, no_wake_up,       0, "AIWA CT-803" },			      \
  { 0x004c0,  80, no_wake_up,       0, "AIWA TD-S1600" },		      \
  { 0x00021,   0, wake_up_mountain, 0, "COREtape QIC80" },		      \
  { 0x00441,   0, wake_up_mountain, 0, "ComByte DoublePlay" },		      \
  { 0x00481, 127, wake_up_mountain, 0, "PERTEC MyTape 800" },		      \
  { 0x00483, 130, wake_up_mountain, 0, "PERTEC MyTape 3200" },		      \
  { UNKNOWN_VENDOR, 0, no_wake_up, 0, "unknown" }			      \
}

/* Shouldn't the remainder be moved to qic117.h ???
 *
 * Anyhow: The magic "vendor id" is made up of 6 bits of vendor
 * specific data, i.e. the model number or similar, following 10 bits
 * of a so called "make code" which makes the "vendor id"
 * vendor-unique.
 *
 * So, given an unknown vendor id, we have to shift it 6 bits to the
 * right to get the correct manufacturer.
 */
typedef struct qic117_make_code {
	unsigned int make:10; /* 0 .. 1023 */
	char *name;           /* guess what this is ... */
	__u8 rffb;            /* recommended format filler byte, 0 if no
			       * recommendation
			       */
} qic117_make_code;

#define QIC117_MODEL_BITS   6
#define QIC117_MODEL_MASK   0x003f
#define QIC117_MAKE_SHIFT   QIC117_MODEL_BITS
#define QIC117_MAKE_MASK    0xffc0
#define QIC117_UNKNOWN_MAKE 0x3ff
#define QIC117_MAKE_CODE(vid) \
	(((unsigned int)(vid) & QIC117_MAKE_MASK) >> QIC117_MAKE_SHIFT)
#define QIC117_MAKE_MODEL(vid) \
	((unsigned int)(vid) & QIC117_MODEL_MASK)

#define QIC117_MAKE_CODES {				\
  { 0, "Unassigned", 0x0 },				\
  { 1, "Alloy Computer Products", 0x0 },		\
  { 2, "3M", 0x0 },					\
  { 3, "Tandberg Data", 0x0 },				\
  { 4, "Colorado", 0x6b },				\
  { 5, "Archive/Conner", 0xaa },			\
  { 6, "Mountain/Summit Memory Systems", 0x6d },	\
  { 7, "Wangtek/Rexon/Tecmar", 0xfe },			\
  { 8, "Sony", 0x0 },					\
  { 9, "Cipher Data Products", 0x0 },			\
  { 10, "Irwin Magnetic Systems", 0x0 },		\
  { 11, "Braemar", 0x0 },				\
  { 12, "Verbatim", 0x0 },				\
  { 13, "Core International", 0x6b },			\
  { 14, "Exabyte", 0x6b },				\
  { 15, "Teac", 0xfe },					\
  { 16, "Gigatek", 0x0 },				\
  { 17, "ComByte", 0x6b },				\
  { 18, "PERTEC Memories", 0x6d },			\
  { 19, "Aiwa", 0x6b },					\
  { 71, "Colorado", 0x6b },				\
  { 546, "Iomega Inc", 0x6d },				\
  { QIC117_UNKNOWN_MAKE, "Unknown", 0x00 },		\
}

#endif /* _FTAPE_VENDORS_H */
