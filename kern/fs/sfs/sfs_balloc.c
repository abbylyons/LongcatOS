/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009, 2014
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

/*
 * SFS filesystem
 *
 * Block allocation.
 */
#include <types.h>
#include <lib.h>
#include <bitmap.h>
#include <synch.h>
#include <buf.h>
#include <sfs.h>
#include <proc.h>
#include <current.h>
#include "sfsprivate.h"


int
sfs_clearblock_internal(struct sfs_fs *sfs, daddr_t block, struct buf **bufret, int do_write)
{
    struct buf *buf;
	void *ptr;
	int result;

	result = buffer_get(&sfs->sfs_absfs, block, SFS_BLOCKSIZE, &buf);
	if (result) {
		return result;
	}

	ptr = buffer_map(buf);
    if (do_write) {
        write_record(sfs, ZERO_BLOCK, block);
    }
	bzero(ptr, SFS_BLOCKSIZE);
	update_buffer_metadata(buf, curthread->t_tnx);
	buffer_mark_valid(buf);
	buffer_mark_dirty(buf);

	if (bufret != NULL) {
		*bufret = buf;
	}
	else {
		buffer_release(buf);
	}

	return 0;

}
/*
 * Zero out a disk block.
 *
 * Uses one buffer; returns it if bufret is not NULL.
 */
int
sfs_clearblock(struct sfs_fs *sfs, daddr_t block, struct buf **bufret)
{
    return sfs_clearblock_internal(sfs, block, bufret, 1);
}

/*
 * Allocate a block.
 *
 * Returns the block number, plus a buffer for it if BUFRET isn't
 * null. The buffer, if any, is marked valid and dirty, and zeroed
 * out.
 *
 * Uses 1 buffer.
 */
int
sfs_balloc(struct sfs_fs *sfs, daddr_t *diskblock, struct buf **bufret)
{
	int result;

	lock_acquire(sfs->sfs_freemaplock);

	result = bitmap_alloc(sfs->sfs_freemap, diskblock);
	if (result) {
		lock_release(sfs->sfs_freemaplock);
		return result;
	}
	write_record(sfs, ALLOC_BLOCK, *diskblock);
	sfs->sfs_freemapdirty = true;
	lock_acquire(metadatalock);
	if (sfs->sfs_freemapdata.md_oldtnx == 0) {
		sfs->sfs_freemapdata.md_oldtnx = curthread->t_tnx;
	}
	sfs->sfs_freemapdata.md_newtnx = curthread->t_tnx;
	lock_release(metadatalock);

	lock_release(sfs->sfs_freemaplock);

	if (*diskblock >= sfs->sfs_sb.sb_nblocks) {
		panic("sfs: %s: balloc: invalid block %u\n",
		      sfs->sfs_sb.sb_volname, *diskblock);
	}

	/* Clear block before returning it */
	result = sfs_clearblock(sfs, *diskblock, bufret);
	if (result) {
		lock_acquire(sfs->sfs_freemaplock);
		bitmap_unmark(sfs->sfs_freemap, *diskblock);
		/* in case someone wrote it out during the clearblock */
		sfs->sfs_freemapdirty = true;
		lock_acquire(metadatalock);
		if (sfs->sfs_freemapdata.md_oldtnx == 0) {
			sfs->sfs_freemapdata.md_oldtnx = curthread->t_tnx;
		}
		sfs->sfs_freemapdata.md_newtnx = curthread->t_tnx;
		lock_release(metadatalock);
		lock_release(sfs->sfs_freemaplock);
	}
	return result;
}

/*
 * Free a block.
 *
 * We must already have the freemap locked. Note that in general it
 * is incorrect to then *release* the freemap lock until the enclosing
 * operation is complete, because otherwise someone else might allocate
 * the block before that point and cause consternation.
 *
 * Note: the caller should in general invalidate any buffers it has
 * for the block, either with buffer_release_and_invalidate() or
 * buffer_drop(), before coming here or at least before unlocking the
 * freemap. Otherwise if someone else allocates the block first their
 * buffer could get dropped instead... or if the caller still holds
 * the buffer, various exotic deadlocks become possible.
 *
 * We could call buffer_drop() explicitly here; but in some cases that
 * would generate redundant work.
 */
void
sfs_bfree_prelocked(struct sfs_fs *sfs, daddr_t diskblock)
{
	KASSERT(lock_do_i_hold(sfs->sfs_freemaplock));
	write_record(sfs, FREE_BLOCK, diskblock);
	bitmap_unmark(sfs->sfs_freemap, diskblock);
	lock_acquire(metadatalock);
	sfs->sfs_freemapdirty = true;
	if (sfs->sfs_freemapdata.md_oldtnx == 0) {
		sfs->sfs_freemapdata.md_oldtnx = curthread->t_tnx;
	}
	sfs->sfs_freemapdata.md_newtnx = curthread->t_tnx;
	lock_release(metadatalock);
}

/*
 * Check if a block is in use.
 */
int
sfs_bused(struct sfs_fs *sfs, daddr_t diskblock)
{
	int result;
	bool alreadylocked;

	if (diskblock >= sfs->sfs_sb.sb_nblocks) {
		panic("sfs: %s: sfs_bused called on out of range block %u\n",
		      sfs->sfs_sb.sb_volname, diskblock);
	}

	alreadylocked = lock_do_i_hold(sfs->sfs_freemaplock);
	if (!alreadylocked) {
		lock_acquire(sfs->sfs_freemaplock);
	}

	result = bitmap_isset(sfs->sfs_freemap, diskblock);

	if (!alreadylocked) {
		lock_release(sfs->sfs_freemaplock);
	}

	return result;
}

/*
 * Explicitly lock and unlock the freemap.
 */
bool
sfs_freemap_locked(struct sfs_fs *sfs)
{
	return lock_do_i_hold(sfs->sfs_freemaplock);
}

void
sfs_lock_freemap(struct sfs_fs *sfs)
{
	lock_acquire(sfs->sfs_freemaplock);
}

void
sfs_unlock_freemap(struct sfs_fs *sfs)
{
	lock_release(sfs->sfs_freemaplock);
}
