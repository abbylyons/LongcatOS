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
 * File-level (vnode) interface routines.
 */
#include <types.h>
#include <sfs.h>
#include "sfsprivate.h"
#include <thread.h>
#include <buf.h>
#include <synch.h>
#include <syscall.h>
#include <thread.h>
#include <proc.h>
#include <current.h>


/* Updates buffer metadata. Check fs before calling */
void update_buffer_metadata (struct buf *buffer, sfs_lsn_t tnx) {
    lock_acquire(metadatalock);
	struct sfs_metadata *md = buffer_get_fsdata(buffer);
	if (md->md_oldtnx == 0)  md->md_oldtnx = tnx;
	if (md->md_newtnx < tnx)  md->md_newtnx = tnx;
	buffer_set_fsdata(buffer, md);
	lock_release(metadatalock);
}


/* Infinite loop that checkpoints when woken up */
void
checkpoint_thread_f(void *data1, unsigned long data2)
{
    (void) data2;
    struct sfs_fs *sfs = (struct sfs_fs *)data1;
    sfs->sfs_checkpoint_thread = curthread;
    sfs->sfs_checkpoint_proc = curproc;
    
    while (sfs->sfs_checkpoint_run) {
        /* wait until we need to checkpoint */
        if (sfs_jphys_getodometer(sfs->sfs_jphys) < sfs->sfs_checkpoint_bound) {
            lock_acquire(sfs->sfs_checkpoint_lk);
            cv_wait(sfs->sfs_checkpoint_cv, sfs->sfs_checkpoint_lk);
            lock_release(sfs->sfs_checkpoint_lk);
        }
        checkpoint(sfs);
    }
    kern__exit(0, 0);
}


/* Does one round of checkpointing */
void
checkpoint(struct sfs_fs *sfs) {
    /* find oldest lsn of incomplete transactions */
    sfs_lsn_t lsn_keep = sfs_jphys_peeknextlsn(sfs);
    lock_acquire(sfs->sfs_active_tnx_lk);
    unsigned arr_len = lsnarray_num(sfs->sfs_active_tnx);
    for (unsigned i = 0; i < arr_len; i++) {
        sfs_lsn_t *cur_tnx = lsnarray_get(sfs->sfs_active_tnx, i);
        KASSERT(*cur_tnx > 0);
        if (*cur_tnx < lsn_keep) {
            lsn_keep = *cur_tnx;
        }
    }
    lock_release(sfs->sfs_active_tnx_lk);
    
    /* find oldest lsn of dirty buffers */
    lock_acquire(metadatalock);
    sfs_lsn_t dirty_buf_lsn =
        bufarray_find_oldest_dirty_lsn(&sfs->sfs_absfs);
    if (dirty_buf_lsn < lsn_keep) {
        lsn_keep = dirty_buf_lsn;
    }

    /* find oldest lsn of freemap */
    if (sfs->sfs_freemapdata.md_oldtnx > 0 && 
        sfs->sfs_freemapdata.md_oldtnx < lsn_keep) {
        lsn_keep = sfs->sfs_freemapdata.md_oldtnx;
    }
    lock_release(metadatalock);

    /* trim journal and reset odometer */
    sfs_jphys_trim(sfs, lsn_keep);
    sfs_jphys_clearodometer(sfs->sfs_jphys);
}


/* intiializes checkpointing thread */
void
checkpoint_thread_init(struct sfs_fs *sfs) {
    int res = fork_common(&sfs->sfs_checkpoint_proc);
    if (res) {
        panic("forking checkpointer failed");
    }

    res = thread_fork("checkpointer", sfs->sfs_checkpoint_proc, checkpoint_thread_f, sfs, 1);
    if (res) {
        panic("forking checkpointer failed");
    }

    return;
}
