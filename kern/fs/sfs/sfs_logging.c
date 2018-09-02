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
#include <proc.h>
#include <current.h>
#include <sfs.h>
#include "sfsprivate.h"
#include <synch.h>
#include <stdarg.h>
#include <kern/errno.h>
#include <lib.h>

void write_record(struct sfs_fs *sfs, uint8_t record_type, ...) {
    
    /* Return immediately if in recovery mode */
    if (!jphys_writermode(sfs))  return;

    va_list valist;
    va_start(valist, record_type);
    void *rec = NULL;
    size_t len;
    uint8_t unlock_record = 0;
    
    /* Create the right kind of record */
    /* this is ugly af, sorry :( */
    struct transaction_le transaction;
    struct change_direntry_le change_direntry;
    struct block_le block;
    struct change_size_le change_size;
    struct change_linkcount_le change_linkcount;
    struct change_indirect_ptr_le change_indirect_ptr;
    struct change_ptr_le change_ptr;
    struct write_block_le write_block;
    struct change_inode_type_le change_inode_type;
    struct change_block_obj_le change_block_obj;
    KASSERT(record_type == START_TRANSACTION || curthread->t_tnx != 0);

    switch(record_type) {
        /* Start/end/abort transactions */
        case START_TRANSACTION:
            /* and yes, I'm aware that I am abusing peeknextlsn */
            lock_acquire(sfs->sfs_recordlock);
            unlock_record = 1;
            sfs_lsn_t *new_tnx = kmalloc(sizeof(sfs_lsn_t));
            curthread->t_tnx = sfs_jphys_peeknextlsn(sfs);
            *new_tnx = curthread->t_tnx;
            if (new_tnx == NULL) {
                panic("adding transaction to list failed\n");
            }
            lock_acquire(sfs->sfs_active_tnx_lk);
            if (lsnarray_add(sfs->sfs_active_tnx, new_tnx, NULL)) {
                panic("adding transaction to list failed\n");
            }
            lock_release(sfs->sfs_active_tnx_lk);
        case ABORT_TRANSACTION:
        case END_TRANSACTION:
            len = sizeof(transaction);
            transaction.le_tnx = curthread->t_tnx;
            transaction.le_func = va_arg(valist, fs_logfunc_t);
            rec = (void *)&transaction;
            break;
        
        case CHANGE_DIRENTRY:
            len = sizeof(change_direntry);
            memset(&(change_direntry.le_newname[0]), 0, sizeof(char) * SFS_NAMELEN); 
            memset(&(change_direntry.le_oldname[0]), 0, sizeof(char) * SFS_NAMELEN); 
            change_direntry.le_tnx = curthread->t_tnx;
            change_direntry.le_ino = va_arg(valist, uint32_t);
            change_direntry.le_direntry = va_arg(valist, uint32_t);
            change_direntry.le_oldino = va_arg(valist, uint32_t);
            strcpy(change_direntry.le_oldname, va_arg(valist, char *));
            if (strlen(change_direntry.le_oldname) == 0) {
                change_direntry.le_oldname[0] = '\0';
            }
            change_direntry.le_newino = va_arg(valist, uint32_t);
            strcpy(change_direntry.le_newname, va_arg(valist, char *));
            rec = (void *)&change_direntry;
            break;

        /* Block operations */
        case ZERO_BLOCK:
        case FREE_BLOCK:
        case ALLOC_BLOCK:
            len = sizeof(block);
            block.le_tnx = curthread->t_tnx;
            block.le_blocknum = va_arg(valist, daddr_t);
            rec = (void *)&block;
            break;
        
        case CHANGE_SIZE:
            len = sizeof(change_size);
            change_size.le_tnx = curthread->t_tnx;
            change_size.le_ino = va_arg(valist, uint32_t);
            change_size.le_oldsize = va_arg(valist, uint32_t);
            change_size.le_newsize = va_arg(valist, uint32_t);
            change_size.le_type = (uint16_t)va_arg(valist, uint32_t);
            rec = (void *)&change_size;
            break;

        case CHANGE_LINK_CNT:
            len = sizeof(change_linkcount);
            change_linkcount.le_tnx = curthread->t_tnx;
            change_linkcount.le_ino = va_arg(valist, uint32_t);
            change_linkcount.le_oldcount = va_arg(valist, uint32_t);
            change_linkcount.le_newcount = va_arg(valist, uint32_t);
            change_linkcount.le_inodetype = va_arg(valist, unsigned);
            rec = (void *)&change_linkcount;
            break;

        case CHANGE_INDIRECT_PTR:
            len = sizeof(change_indirect_ptr);
            change_indirect_ptr.le_tnx = curthread->t_tnx;
            change_indirect_ptr.le_ino = va_arg(valist, uint32_t);
            change_indirect_ptr.le_level = va_arg(valist, indirection_level_t);
            change_indirect_ptr.le_oldptr = va_arg(valist, uint32_t);
            change_indirect_ptr.le_newptr = va_arg(valist, uint32_t);
            change_indirect_ptr.le_type = (uint16_t)va_arg(valist, uint32_t);
            rec = (void *)&change_indirect_ptr;
            break;

        /* Update direct pointer or inode in indirect ptr */
        case CHANGE_DIRECT_PTR:
        case CHANGE_INO_IN_INDIRECT:
            len = sizeof(change_ptr);
            change_ptr.le_tnx = curthread->t_tnx;
            change_ptr.le_ino = va_arg(valist, uint32_t);
            change_ptr.le_ptrnum = va_arg(valist, uint32_t);
            change_ptr.le_oldptr = va_arg(valist, uint32_t);
            change_ptr.le_newptr = va_arg(valist, uint32_t);
            change_ptr.le_type = (uint16_t)va_arg(valist, uint32_t);
            rec = (void *)&change_ptr;
            break;

        case WRITE_BLOCK:
            len = sizeof(write_block);
            write_block.le_tnx = curthread->t_tnx;
            write_block.le_block = va_arg(valist, uint32_t);
            write_block.le_checksum = va_arg(valist, uint32_t);
            rec = (void *)&write_block;
            break;

        case CHANGE_INODE_TYPE:
            len = sizeof(change_inode_type);
            change_inode_type.le_tnx = curthread->t_tnx;
            change_inode_type.le_ino = va_arg(valist, uint32_t);
            change_inode_type.le_oldtype = va_arg(valist, uint32_t);
            change_inode_type.le_newtype = va_arg(valist, uint32_t);
            rec = (void *)&change_inode_type;
            break;

        case CHANGE_BLOCK_OBJ:
            len = sizeof(change_block_obj);
            change_block_obj.le_tnx = curthread->t_tnx;
            change_block_obj.le_blocknum = va_arg(valist, uint32_t);
            change_block_obj.le_offset = va_arg(valist, uint32_t);
            change_block_obj.le_oldval = va_arg(valist, uint32_t);
            change_block_obj.le_newval = va_arg(valist, uint32_t);
            rec = (void *)&change_block_obj;
            break;

        
        default:
            panic("Attempted to write unrecognized journal record type");
    }
    va_end(valist);

    /* Actually write the record */
    sfs_jphys_write(sfs, NULL, NULL, record_type, rec, len);
    
    /* Remove tnx if transaction is over */
    if (record_type == END_TRANSACTION || record_type == ABORT_TRANSACTION) {
        uint64_t indx;
        lock_acquire(sfs->sfs_active_tnx_lk);
        if(!find_tnx(sfs->sfs_active_tnx, curthread->t_tnx, &indx)) {
            panic("tried to end a transaction that's not active\n");
        }
        lsnarray_remove(sfs->sfs_active_tnx, indx);
        lock_release(sfs->sfs_active_tnx_lk);
        curthread->t_tnx = 0;
    }
    
    /* All done! */
    if (unlock_record)  lock_release(sfs->sfs_recordlock);
    if (sfs_jphys_getodometer(sfs->sfs_jphys) >= sfs->sfs_checkpoint_bound) {
        lock_acquire(sfs->sfs_checkpoint_lk);
        cv_broadcast(sfs->sfs_checkpoint_cv, sfs->sfs_checkpoint_lk);
        lock_release(sfs->sfs_checkpoint_lk);
    }
    
}
