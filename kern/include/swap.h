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

#ifndef _SWAP_H_
#define _SWAP_H_

#include <limits.h>
#include <bitmap.h>
#include <types.h>
#include <spinlock.h>


/*
 * Data structure for tracking the swap space
 */
struct swap_tracker {
    struct bitmap *st_bitmap;  /* Bitmap to keep track of used blocks */
    struct spinlock st_lock;   /* Lock for this structure */
    struct vnode *st_vnode;    /* vnode of the swap device */
    int st_size;               /* number of blocks */
};


/*
 * Initializes all entries of the swap tracker
 */
void swap_init(struct swap_tracker **swap);


/*
 * Finds and returns the index of a free block.
 */
off_t swap_find_free(struct swap_tracker *swap);


/*
 * Read from the swap location into the designated ppn.
 */
int swap_read(int ppn, int swap_location, struct swap_tracker *swap);


/*
 * Write from the physical page into the swap location.
 */
int swap_write(int ppn, int swap_location, struct swap_tracker *swap);


/*
 * Free up a block in swap space.
 */
void swap_destroy_block(int swap_location, struct swap_tracker *swap);


/* This is the structure for the kernel swap tracker */
extern struct swap_tracker *k_swap_tracker;


/* False until swap is initialized */
extern unsigned k_can_swap;


#endif /* _SWAP_H_ */
