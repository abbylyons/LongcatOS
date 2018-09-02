/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _COREMAP_H_
#define _COREMAP_H_

#include <wchan.h>
#include <addrspace.h>
#include <cpu.h>
#include <spinlock.h>
#include <array.h>
#include <types.h>

/*
 *  Struct for a core map entry
 */
struct cm_entry {
    struct addrspace *cme_as;   /* pointer to the address space that owns this page */
    vaddr_t cme_vaddr;          /* the virtual address in the address space */
    int cme_swap_location;      /* location of this page in the swap device */
    struct cpu *cme_owner_cpu;  /* which cpu the thread which has this PTE runs on */
    unsigned cme_dirty:1;       /* whether page has been written to */
    unsigned cme_tlb:1;         /* whether page is in tlb */
    unsigned cme_busy:1;        /* whether page is busy */
    unsigned cme_kernel:1;      /* whether page is in a contiguous kernel block */
    unsigned cme_kpage:1;       /* whether page belongs to the kernel */
    unsigned cme_exists:1;      /* whether page exists in ram */
};

/*
 *  Struct of the core map.
 *  The core map is responsible for keeping track of physical pages
 *  ram pages will be 512MB / 4KB
 */
struct coremap {
    struct cm_entry cm_entries[RAM_PAGES];  /* the core map entries */
    struct spinlock cm_lock;                /* lock protecting this struct */
    struct wchan *cm_wchan;                 /* wait channel for core map busy bits */
    struct wchan *cm_tlb_wchan;             /* wait channel for core map tlb bits */
    int cm_num_pages;                       /* number of existing pages */
    int cm_num_kpages;                      /* number of existing kernel pages */
    int cm_num_dirty;                       /* number of dirty paged */
    int cm_clock_head;                      /* clock head for paging algo */
};

/* This is the structure for the kernel coremap*/
extern struct coremap *k_coremap;



#endif /* _COREMAP_H_ */
