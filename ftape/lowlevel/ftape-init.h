#ifndef _FTAPE_INIT_H
#define _FTAPE_INIT_H

/*
 * Copyright (C) 1993-1996 Bas Laarhoven,
 *           (C) 1996-2000 Claus-Justus Heine.

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
 * $RCSfile: ftape-init.h,v $
 * $Revision: 1.14 $
 * $Date: 2000/07/03 10:13:06 $
 *
 * This file contains the definitions for the interface to 
 * the Linux kernel for floppy tape driver ftape.
 *
 */

/* #include <linux/config.h> - not needed in modern kernels */
#include <linux/linkage.h>
#include <linux/signal.h>

#if LINUX_VERSION_CODE >= KERNEL_VER(2,1,16)
#include <linux/init.h>
#else
#define __initdata
#define __initlocaldata
#define __initfunc(__arg) __arg
#define __init
#endif

#if LINUX_VERSION_CODE < KERNEL_VER(2,3,16)
#define set_current_state(newstate) current->state = (newstate)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VER(2,1,68)
#define _NEVER_BLOCK    (sigmask(SIGKILL) | sigmask(SIGSTOP))
#define _DONT_BLOCK     (_NEVER_BLOCK | sigmask(SIGINT))

static inline void ft_sigblockall(sigset_t * oldmask)
{
       sigset_t newmask;
       sigfillset(&newmask);
       sigdelset(&newmask, SIGKILL);
       sigdelset(&newmask, SIGSTOP);
       sigdelset(&newmask, SIGINT);
       *oldmask = current->blocked;
       set_current_blocked(&newmask);
}
static inline void ft_sigrestore(sigset_t* oldmask)
{
       set_current_blocked(oldmask);
}
static inline int ft_sigtest(unsigned long mask)
{
	return signal_pending(current) ? 1 : 0;
}
static inline int ft_killed(void)
{
	return signal_pending(current);
}

#else

#define _FT_S(nr) (1<<((nr)-1))
#define _NEVER_BLOCK    (_FT_S(SIGKILL)|_FT_S(SIGSTOP))
#define _DONT_BLOCK     (_NEVER_BLOCK|_FT_S(SIGINT))
#define _BLOCK_ALL      (0xffffffffL)

extern inline void ft_sigblockall(sigset_t *oldset)
{
	*oldset = current->blocked;
	current->blocked = _BLOCK_ALL;
}
extern inline void ft_sigrestore(sigset_t *oldset)
{
	current->blocked = *oldset;
}
extern inline int ft_sigtest(unsigned long mask)
{
	return (current->signal & mask) != 0;
}
extern inline int ft_killed(void)
{
	return (current->signal & ~current->blocked) != 0;
}

#endif

#ifndef QIC117_TAPE_MAJOR
#define QIC117_TAPE_MAJOR 27
#endif

/*      ftape-init.c defined global variables.
 */
#if defined(MODULE) && LINUX_VERSION_CODE <= KERNEL_VER(1,2,13)
extern char kernel_version[];
#endif

#ifdef CONFIG_SMP
# define FT_SMP_STRING "SMP"
#else
# define FT_SMP_STRING "UP"
#endif

#define FTAPE_BANNER							     \
FTAPE_VERSION"\n\n"							     \
"(c) 1993-1996 Bas Laarhoven\n"						     \
"(c) 1995-1996 Kai Harrekilde-Petersen\n"				     \
"(c) 1996-2000 Claus-Justus Heine <heine@instmath.rwth-aachen.de)\n\n"	     \
"QIC-117 driver for QIC-40/80/3010/3020/Ditto 2GB/MAX floppy tape drives.\n" \
"Compiled for modern Linux kernel ("FT_SMP_STRING").\n"

/*      ftape-init.c defined global functions not defined in ftape.h
 */
#ifdef MODULE
asmlinkage extern int  init_module   (void);
asmlinkage extern void cleanup_module(void);
#endif

#endif
