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

#include <swap.h>
#include <vnode.h>
#include <vfs.h>
#include <vm.h>
#include <kern/stat.h>
#include <uio.h>

/* Swap space functions */
static void can_swap(void) {
    KASSERT(k_can_swap);
}

void swap_init(struct swap_tracker **swap) {
    struct swap_tracker *new_swap = kmalloc(sizeof(struct swap_tracker));
    if (new_swap == NULL) {
        panic("swap init failed");
    }

    spinlock_init(&new_swap->st_lock);

    if (vfs_swapon("lhd0:", &new_swap->st_vnode)) {
        panic("swap file init failed");
    }

    struct stat statbuf;
    if (VOP_STAT(new_swap->st_vnode, &statbuf)) {
        panic("swap file size failed");
    }
    new_swap->st_size = (int)(statbuf.st_size / PAGE_SIZE);

    new_swap->st_bitmap = bitmap_create(new_swap->st_size);
    if (new_swap->st_bitmap == NULL) {
        panic("swap bitmap init failed");
    }
    bitmap_mark(new_swap->st_bitmap, 0);
    
    *swap = new_swap;

    k_can_swap = 1;
}


off_t swap_find_free(struct swap_tracker *swap) {
    can_swap();
    spinlock_acquire(&swap->st_lock);
    unsigned index;
    int err = bitmap_alloc(swap->st_bitmap, &index);
    spinlock_release(&swap->st_lock);
    if (err)  panic("Ran out of swap space");
    return (off_t)index;
}


int swap_read(int ppn, int swap_location, struct swap_tracker *swap) {
    KASSERT(ppn > 0);
    KASSERT(swap_location > 0);
    can_swap();
    struct iovec iov;
    struct uio myuio;
    uio_kinit(&iov, &myuio, (void *)CM_INDEX_TO_KVADDR(ppn), PAGE_SIZE, 
        swap_location * PAGE_SIZE, UIO_READ);
    KASSERT(bitmap_isset(swap->st_bitmap, swap_location));
    return VOP_READ(swap->st_vnode, &myuio);
}


int swap_write(int ppn, int swap_location, struct swap_tracker *swap) {
    KASSERT(ppn > 0);
    KASSERT(swap_location > 0);
    can_swap();
    struct iovec iov;
    struct uio myuio;
    uio_kinit(&iov, &myuio, (void *)CM_INDEX_TO_KVADDR(ppn), PAGE_SIZE, 
        swap_location * PAGE_SIZE, UIO_WRITE);
    KASSERT(bitmap_isset(swap->st_bitmap, swap_location));
    return VOP_WRITE(swap->st_vnode, &myuio);
}


void swap_destroy_block(int swap_location, struct swap_tracker *swap) {
    KASSERT(swap_location > 0);
    can_swap();
    spinlock_acquire(&swap->st_lock);
    if (bitmap_isset(swap->st_bitmap, swap_location)) {
        bitmap_unmark(swap->st_bitmap, swap_location);
    }
    spinlock_release(&swap->st_lock);
}
