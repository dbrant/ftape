#ifndef _WRAPPER_H_
#define _WRAPPER_H_

/* This file contains some wrappers needed by ftape. This is a
 * fragment of the `include/linux/wrapper.h' file supplied by 
 * newer kernel versions (> 1.2.13)
 */

#define inode_inc_count(i) i->i_count++
#define vma_set_inode(v,i) v->vm_inode = i
#define vma_get_flags(v) v->vm_flags
#define vma_get_offset(v) v->vm_offset
#define vma_get_start(v) v->vm_start
#define vma_get_end(v) v->vm_end
#define vma_get_page_prot(v) v->vm_page_prot

#define mem_map_reserve(p) mem_map[p] |= MAP_PAGE_RESERVED
#define mem_map_unreserve(p) mem_map[p] &= ~MAP_PAGE_RESERVED

#endif
