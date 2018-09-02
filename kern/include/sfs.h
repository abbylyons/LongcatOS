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

#ifndef _SFS_H_
#define _SFS_H_


/*
 * Header for SFS, the Simple File System.
 */


struct buf; /* in buf.h */

/* Type for log sequence numbers */
typedef uint64_t sfs_lsn_t;


/*
 * Get abstract structure definitions
 */
#include <fs.h>
#include <vnode.h>
#include <array.h>
#include <types.h>

/*
 * Get on-disk structures and constants that are made available to
 * userland for the benefit of mksfs, dumpsfs, etc.
 */
#include <kern/sfs.h>

/*
 * In-memory inode
 */
struct sfs_vnode {
	struct vnode sv_absvn;          /* abstract vnode structure */
	uint32_t sv_ino;                /* inode number */
	unsigned sv_type;		/* cache of sfi_type */
	struct buf *sv_dinobuf;		/* buffer holding dinode */
	uint32_t sv_dinobufcount;	/* # times dinobuf has been loaded */
	struct lock *sv_lock;		/* lock for vnode */
};


/*
 * In-memory info for a whole fs volume
 */
struct sfs_fs {
	struct fs sfs_absfs;                  /* abstract filesystem structure */
	struct sfs_superblock sfs_sb;	      /* copy of on-disk superblock */
	bool sfs_superdirty;                  /* true if superblock modified */
	struct device *sfs_device;            /* device mounted on */
	struct vnodearray *sfs_vnodes;        /* vnodes loaded into memory */
	struct bitmap *sfs_freemap;           /* blocks in use are marked 1 */
	bool sfs_freemapdirty;                /* true if freemap modified */
    struct sfs_metadata sfs_freemapdata;  /* freemap metadata */
	struct lock *sfs_vnlock;              /* lock for vnode table */
	struct lock *sfs_freemaplock;	      /* lock for freemap/superblock */
	struct lock *sfs_renamelock;          /* lock for sfs_rename() */
    struct lock *sfs_recordlock;          /* lock for writing records */
	struct sfs_jphys *sfs_jphys;          /* physical journal container */
    char sfs_morguename[5];               /* name for the next morgue entry */
    struct sfs_vnode *sfs_morgue_sv;      /* keep track of the morgue */

	/* Stuff for checkpointing */
    struct lock *sfs_checkpoint_lk;       /* lock for cv below */
    struct cv *sfs_checkpoint_cv;         /* cv to wake up checkpointing thread */
    sfs_lsn_t sfs_checkpoint_bound;       /* checkpoint every n records written */
	struct thread *sfs_checkpoint_thread; /* checkpointing thread */
	struct proc *sfs_checkpoint_proc;     /* checkpointing proc */
	bool sfs_checkpoint_run;              /* flag that tells checkpointer to run */
    struct lsnarray *sfs_active_tnx;      /* array of active transactions */
    struct lock *sfs_active_tnx_lk;       /* lock active tnx array */
    uint8_t sfs_in_recovery;              /* flag that determines if we are in recovery */
};

/*
 * Function for mounting a sfs (calls vfs_mount)
 */
int sfs_mount(const char *device);

#ifndef PROTECTED_BLOCK_INLINE
#define PROTECTED_BLOCK_INLINE INLINE
#endif

#ifndef SFS_LSN_T_INLINE
#define SFS_LSN_T_INLINE INLINE
#endif

DECLARRAY_BYTYPE(lsnarray, sfs_lsn_t, SFS_LSN_T_INLINE);
DEFARRAY_BYTYPE(lsnarray, sfs_lsn_t, SFS_LSN_T_INLINE);

/*
 * struct for keeping track of blocks we shouldn't
 * write into.
 */
struct protected_block {
    uint32_t pb_block;      /* block that is protected */
    sfs_lsn_t pb_lsn;       /* youngest allocation lsn */
};

DECLARRAY_BYTYPE(pbarray, struct protected_block, PROTECTED_BLOCK_INLINE);
DEFARRAY_BYTYPE(pbarray, struct protected_block, PROTECTED_BLOCK_INLINE);

/*
 * enum showing the pass direction for journal recovery
 */
typedef enum {
    P_UNDO,
    P_REDO
} journal_direction_p;

/*
 * processes a journal entry for recovery
 */
int process_journal_entry(uint8_t type, void *data,
        struct sfs_fs *sfs, journal_direction_p direction,
        struct pbarray *protected_blocks, sfs_lsn_t lsn,
        struct lsnarray *aborted);

/* 
 * Writes a journal record.
 *
 * Assigns a transaction number if recording a begin transaction.
 * Otherwise, the transaction number comes from curthread.
 * 
 * Arguments must be in the same order as listed in the record struct.
 */
void write_record(struct sfs_fs *sfs, uint8_t record_type, ...);

/* Updates buffer metadata. */
void update_buffer_metadata (struct buf *buffer, sfs_lsn_t tnx);

/* Prototypes for the checkpointing thread */
void checkpoint_thread_init(struct sfs_fs *sfs);
void checkpoint_thread_f(void *data1, unsigned long data2);
void checkpoint(struct sfs_fs *sfs);
struct sfs_jiter;
void
print_journal_info(struct sfs_jiter *ji);

#endif /* _SFS_H_ */
