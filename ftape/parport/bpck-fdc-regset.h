#ifndef _BPCK_FDC_REGSET_H_
#define _BPCK_FDC_REGSET_H_

/*
 * Copyright (C) 1998 Claus-Justus Heine

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
 * $RCSfile: bpck-fdc-regset.h,v $
 * $Revision: 1.3 $
 * $Date: 1998/12/18 22:38:19 $
 *
 *       Register layout of the parport backpack fdc drivers.
 *
 */

#define FT_BPCK_REG_STAT    0x00 /* query interrupt status. */
#define FT_BPCK_REG_CTRL    0x04 /* transfer mode and others */
#define FT_BPCK_REG_0x05    0x05
#define FT_BPCK_REG_0x06    0x06
#define FT_BPCK_REG_0x07    0x07
#define FT_BPCK_REG_CLEAR   0x0b /* clears at least the CRC checksum */
#define FT_BPCK_REG_IRQ     0x0f /* IRQ status! Contents valid after IRQ */
#define FT_BPCK_REG_TEST    0x13 /* used to test the parport transfer mode */
#define FT_BPCK_REG_0x1a    0x1a
#define FT_BPCK_REG_CRC     0x22
#define FT_BPCK_REG_PROTO   0x24 /* program parport protocol */
#define FT_BPCK_REG_MEMADDR 0x28
#define FT_BPCK_REG_DMAADDR 0x2c /* DMA base address */
#define FT_BPCK_REG_DMASIZE 0x30 /* DMA size, count - 1 */
#define FT_BPCK_REG_ECC     0x34
#define FT_BPCK_FDC_REGSET  0x40 /* FDC registers show up here */
#define FT_BPCK_MEMORY      0xa0 /* memory area in mapped mode */

/*****************************************************************************
 *
 * register 0x00 seems to indicate the interrupt status of the parport
 * FDC. The register seems to be RO. I have examined the following
 * values in the DOSEmu trace log:
 */
#define FT_BPCK_STAT_IRQ  0x01 /* irq is pending */
/*
 *
 */
#define FT_BPCK_STAT_0x10       0x10 /* always set. Don't know */
#define FT_BPCK_STAT_ECC_ERROR  0x20 /* ECC error pending, cleared after
				      * correcting the error
				      */
/*
 *  I don't know anything about the following flags
 */
#define FT_BPCK_STAT_0x04 0x04
#define FT_BPCK_STAT_0x40 0x40

/*****************************************************************************
 *  
 *  register 0xf
 *  This seems to be a R/W register. 0x0f sets 0x01 if an IRQ is pending.
 */
#define FT_BPCK_IRQ_IRQ    0x01 /* always set if IRQ pending */
#define FT_BPCK_IRQ_SOFTEN 0x10 /* enable reg. 0x00 indication of irqs */
#define FT_BPCK_IRQ_HARDEN 0x18 /* enable ACK line and thusly generate
				  * parport IRQS
				  */

/*****************************************************************************
 *  
 *  register 0x04
 *  Seems to be WREONLY
 */
#define FT_BPCK_CTRL_SPP    0x00
#define FT_BPCK_CTRL_PS2    0x10
#define FT_BPCK_CTRL_EPP    0x18 
/*
 *  Seems also used to program DMA direction.
 */
#define FT_BPCK_CTRL_DMA_READ  0x03
#define FT_BPCK_CTRL_DMA_WRITE 0x02
/*
 *  yet unknown.
 */
#define FT_BPCK_CTRL_0x04   0x04
#define FT_BPCK_CTRL_0x20   0x20
/*
 */
#define FT_BPCK_CTRL_WRITE  0x80 /* prepare for data write, non-EPP */

/*****************************************************************************
 *  
 *  register 0x05
 *
 *  Unknown. Maybe needed for mode switching. Maybe register 0x05 and
 *  0x07 are used for locking/unlocking. Could find out ...
 */
#define FT_BPCK_0x05_0x24 0x24

/*****************************************************************************
 *  
 *  register 0x06
 *
 *  Maybe used for reading the EEPROM, if any is installed. The Ditto
 *  3200, e.g., doesn't have an EEPROM
 */

/*****************************************************************************
 *  
 *  register 0x07
 *  Seems to be need for switching between parport protocols.
 *
 */
#define FT_BPCK_0x07_0x01   0x01
#define FT_BPCK_0x07_0x05   0x05

/*****************************************************************************
 *  
 *  register 0x0b
 *
 *  Clears at least the CRC shift register. Maybe also other counters?
 */
#define FT_BPCK_CLEAR_CLEAR    0x0b

/*****************************************************************************
 *  
 *  register 0x1a
 *
 *  Don't know.
 */

/*****************************************************************************
 *  
 *  register 0x22
 *  CRC checksum register
 */
#define FT_BPCK_CRC_POL  0x1021  /* x^0 + x^5 + x^12 + x^16 */
#define FT_BPCK_CRC_INIT 0xffff  /* initial value of the CRC shift register */
#define FT_BPCK_CRC_MULX_N(b,n)  ((byte) << (n)) /* multiplication of incoming
						  * byte (polynomials) is a
						  * LEFT shift
						  */

/*****************************************************************************
 *  
 *  register 0x24
 *
 *  This _IS_ needed to program the bpck to use a specific parport protocol.
 *
 *  First byte written is the desired new protocol, as defined for
 *  register 0x04, second byte is 0xa4 (why ??)
 *
 *  The difference between programming the proto with register 0x04
 *  and 0x24 seems to be that the protocol programmed into 0x04 isn't
 *  remember until the connect. 0x04 can be used to switch protocols
 *  while still connected. 0x24 doesn't change the current proto, but
 *  takes affect when connecting next time.
 */
#define FT_BPCK_PROTO_BYTE_2 0xa4

/*****************************************************************************
 *  
 *  register 0x28
 *  memory address register. Memory address 0-0x1ffff. Memory address wraps
 *  around when reaching the 128k boundary. It is a three byte register.
 */

/*****************************************************************************
 *  
 *  register 0x2c
 *
 *  DMA base address register.
 */

/*****************************************************************************
 *  
 *  register 0x30
 *
 *  DMA count, value written is actual count - 1
 */

/*****************************************************************************
 *  
 *  register 0x34
 *
 *  Controlling the ECC coprocessor:
 *
 *  ECC_CHECK and ECC_GEN are mutual exclusive.
 *
 * ECC_CHECK has to be set when reading. In this case bit 5 (0x20) is
 * set in register 0x00 when the coprocessor detects an error.
 *
 * Setting ECC_GEN when writing computes the ECC syndromes. ECC_SIZE
 * has to be set accordingly (i.e. data + 3kb ECC syndromes)
 *
 * TODO: how to tell him to automatically correct the error?
 * Things that doesn't work:
 * setting ECC_GEN simply will correct new syndroms for broken data
 * 0x20 make the ECC_CHECK stop working.
 *
 * For now we leave it as it is. Register 0x00 will tell us if we
 * need to correct the data in software.
 */
#define FT_BPCK_ECC_GEN       0x80 /* generate when writing, correct when
				    * reading.
				    */
#define FT_BPCK_ECC_CHECK     0x40 /* only check consistency, don't correct
				    */
#define FT_BPCK_ECC_START(a) ((a)>>10)
#define FT_BPCK_ECC_SIZE(n)  (((n)>>10) - 1)


/*****************************************************************************
 *  
 *  register 0x40
 *
 *  Start of the FDC regset. Following seven register correspond to
 *  the FDC registers in the same order as they appear on a "normal"
 *  FDC chip.
 *
 */

/*****************************************************************************
 *  
 *  register 0xa0
 *
 *  Memory register. This is kind of a FIFO. First the memory address
 *  is written to 0x28, afterwards data is read out or written to
 *  register 0xa0 as a byte stream.
 */

#endif /*  _BPCK_FDC_REGSET_H_ */
