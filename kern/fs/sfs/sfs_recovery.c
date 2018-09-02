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
#include <kern/errno.h>
#include <limits.h>
#include <lib.h>
#include <uio.h>
#include <synch.h>
#include <vfs.h>
#include <buf.h>
#include <sfs.h>
#include "sfsprivate.h"
#include <bitmap.h>


/*
 * Checks if we should skip a block because writing in it
 * could overwrite userdata.
 * Returns True if block should be skipped 
 */
bool is_block_protected(struct pbarray *protected_blocks, sfs_lsn_t cur_lsn, uint32_t ino)
{
    unsigned arr_len = pbarray_num(protected_blocks);
    for (unsigned i = 0; i < arr_len; i++) {
        struct protected_block *block = pbarray_get(protected_blocks, i);
        if (block->pb_block == ino) {
            if (block->pb_lsn > cur_lsn) {
                return true;
            } else {
                return false;
            }
        }
    }
    return false;
}

/*
 * Checks if block is in protected_blocks array 
 */
bool is_block_in_array(struct pbarray *protected_blocks, uint32_t ino)
{
    unsigned arr_len = pbarray_num(protected_blocks);
    for (unsigned i = 0; i < arr_len; i++) {
        struct protected_block *block = pbarray_get(protected_blocks, i);
        if (block->pb_block == ino) {
            return true;
        }
    }
    return false;
}


/*
 * creates a new protected block and adds it to the array
 * if it's not already in it.
 */
int add_protected_block(struct pbarray *protected_blocks, sfs_lsn_t cur_lsn, uint32_t ino)
{

    /* check if block is already in the list */
    unsigned arr_len = pbarray_num(protected_blocks);
    for (unsigned i = 0; i < arr_len; i++) {
        struct protected_block *block = pbarray_get(protected_blocks, i);
        if (block->pb_block == ino) {
            return 0;
        }
    }
    
    struct protected_block *pb = kmalloc(sizeof(struct protected_block));
    if (pb == NULL) {
        return ENOMEM;
    }

    pb->pb_block = ino;
    pb->pb_lsn = cur_lsn;

    return pbarray_add(protected_blocks, pb, NULL);

}

/* removes and frees elements in a protected blocks array
 * and frees the array
 */
void cleanup_protected_blocks(struct pbarray *protected_blocks)
{
    while(pbarray_num(protected_blocks) > 0) {
        struct protected_block *pb = pbarray_get(protected_blocks, 0);
        kfree(pb);
        pbarray_remove(protected_blocks, 0);
    }
    pbarray_destroy(protected_blocks); 
}

/*
 * function for finding an transaction number in an array.
 * if index is passed in, returns the index of
 */
bool find_tnx(struct lsnarray *arr, sfs_lsn_t tnx, uint64_t *index) {
    unsigned arr_len = lsnarray_num(arr);
    for (unsigned i = 0; i < arr_len; i++) {
        sfs_lsn_t *cur_tnx = lsnarray_get(arr, i);
        if (*cur_tnx == tnx) {
            if (index) {
                *index = (uint64_t) i;
            }
            return true;
        }
    }
    return false;
}

/*
 * cleans up an used lsn artay by freeing all the blocks
 * and destroying the array
 */
void cleanup_lsn_array(struct lsnarray *arr)
{
    while(lsnarray_num(arr) > 0) {
        sfs_lsn_t *lsn = lsnarray_get(arr, 0);
        kfree(lsn);
        lsnarray_remove(arr, 0);
    }
    lsnarray_destroy(arr);
}


/*
 * Function for calculating the checksum on a block of data
 * We use the fletcher32 checksum algorithm.
 * The algorithm is a slightly modified version of the one found at
 * https://en.wikipedia.org/wiki/Fletcher%27s_checksum
 */
uint32_t fletcher32(uint16_t *data)
{
    uint32_t sum1 = 0xffff, sum2 = 0xffff;
    size_t tlen;
    size_t words = SFS_BLOCKSIZE/2;

    while (words) {
        tlen = ((words >= 359) ? 359 : words);
        words -= tlen;
        do {
            sum2 += sum1 += *data++;
            tlen--;
        } while (tlen);
        sum1 = (sum1 & 0xffff) + (sum1 >> 16);
        sum2 = (sum2 & 0xffff) + (sum2 >> 16);
    }
    /* Second reduction step to reduce sums to 16 bits */
    sum1 = (sum1 & 0xffff) + (sum1 >> 16);
    sum2 = (sum2 & 0xffff) + (sum2 >> 16);
    return (sum2 << 16) | sum1;
}

/* 
 * helper function for preparing an inode for usage
 */
int load_inode(struct sfs_fs *sfs, struct sfs_vnode **sv, unsigned type, uint32_t ino, struct sfs_dinode **inodeptr)
{
    int result;
    /* load the inode */
    result = sfs_loadvnode(sfs, ino, type, sv);
    if (result) {
        return result;
    }

    lock_acquire((*sv)->sv_lock);
    
    result = sfs_dinode_load(*sv);
    if (result) {
        return result;
    }
    
    *inodeptr = sfs_dinode_map(*sv);
    return 0;
}

/*
 * releases an inode after usage
 */
void unload_inode(struct sfs_vnode *sv)
{
    lock_release(sv->sv_lock);
    sfs_reclaim_light(&sv->sv_absvn);
}

void consider_morgue(struct sfs_fs *sfs, uint16_t linkcount, uint32_t ino)
{

    if (linkcount != 0)  return;

    lock_acquire(sfs->sfs_morgue_sv->sv_lock);

    if(sfs_dir_link(sfs->sfs_morgue_sv, &sfs->sfs_morguename[0], ino, NULL)) {
        panic("failed to move sthg into the morgue");
    }

    sfs->sfs_morguename[3]++;
    
    for(int i = 3; i >= 0; i--) {
        if (sfs->sfs_morguename[i] == 0) {
            sfs->sfs_morguename[i]++;
            if (i != 0) {
                sfs->sfs_morguename[i-1]++;
            } else {
                // complete reset
                sfs->sfs_morguename[0] = 1;
                sfs->sfs_morguename[1] = 1;
                sfs->sfs_morguename[2] = 1;
                sfs->sfs_morguename[3] = 1;
            }
        } else {
            break;
        }
    }

    lock_release(sfs->sfs_morgue_sv->sv_lock);

}

static int parse_change_direntry(void *data, journal_direction_p direction, struct pbarray *protected_blocks, sfs_lsn_t lsn, struct lsnarray *aborted,struct sfs_fs *sfs)
{
    int result;
    struct sfs_vnode *sv;
    struct buf *iobuffer;
	struct sfs_dinode *ioptr;

    /* load in data */
    struct change_direntry_le cd_le;
    memcpy((void *)&cd_le, data, sizeof(struct change_direntry_le));

    /* check if transaction is aborted */
    if (find_tnx(aborted, cd_le.le_tnx, NULL)) {
        return 0;
    }

    if (is_block_protected(protected_blocks, lsn, cd_le.le_ino)) {
        return 0;
    }

    /* load the inode */
    if (direction == P_UNDO && !sfs_bused(sfs, cd_le.le_ino)) {
        // If undoing and block is unallocated, then nothing to undo
        return 0;
    }

    result = buffer_read(&sfs->sfs_absfs, (daddr_t) cd_le.le_ino,
            SFS_BLOCKSIZE, &iobuffer);

    if (result) {
        return result;
    }

	ioptr = buffer_map(iobuffer);

    if(ioptr->sfi_type != SFS_TYPE_DIR) {
        // directory never got written to disk. Nothing to do 
	    buffer_release(iobuffer);
        return 0;
    }

	buffer_release(iobuffer);

    result = sfs_loadvnode(sfs, cd_le.le_ino, SFS_TYPE_DIR, &sv);
    if (result) {
        return result;
    }
    lock_acquire(sv->sv_lock);

    /* set up the entry */
    struct sfs_direntry sd;
    int slots;

    bzero(&sd, sizeof(sd));
    sd.sfd_ino = (direction == P_REDO) ? cd_le.le_newino : cd_le.le_oldino;
    if (direction == P_REDO) {
        strcpy(sd.sfd_name, cd_le.le_newname);
    } else {
        strcpy(sd.sfd_name, cd_le.le_oldname);
    }

    /* check if direntry made it to the disk if undoing */
    if (direction == P_UNDO) {
        if(sfs_dir_nentries(sv, &slots)) {
            panic("dir nentries failed in recovery\n");
        }
        if (slots < (int)cd_le.le_direntry) {
            /* the slot was never allocated, then nothing to do */
            unload_inode(sv);
            return 0;
        }
    }
    
    /* write the entry */
    result = sfs_writedir(sv, cd_le.le_direntry, &sd);
    
    /* cleanup */
    unload_inode(sv);
           
    return (result == -1) ? 0 : result;
}

static int parse_zero_block(void *data, journal_direction_p direction,
        struct sfs_fs *sfs, struct pbarray *protected_blocks, sfs_lsn_t lsn, struct lsnarray *aborted)
{
    if (direction == P_UNDO) {
        /* can't undo a block zeroing */
        return 0;
    }
    
    /* load in data */
    struct block_le b_le;
    memcpy((void *)&b_le, data, sizeof(struct block_le));

    /* check if transaction is aborted */
    if (find_tnx(aborted, b_le.le_tnx, NULL)) {
        return 0;
    }

    if (is_block_protected(protected_blocks, lsn, (uint32_t)b_le.le_blocknum)) {
        return 0;
    }

    return sfs_clearblock_internal(sfs, b_le.le_blocknum, NULL, 0);
}

static int parse_alloc_block(void *data, journal_direction_p direction,
        struct sfs_fs *sfs, struct lsnarray *aborted)
{
    /* load in data */
    struct block_le b_le;
    memcpy((void *)&b_le, data, sizeof(struct block_le));

    /* check if transaction is aborted */
    if (find_tnx(aborted, b_le.le_tnx, NULL) && direction == P_REDO) {
        return 0;
    }

    /* change the bitmap */
	lock_acquire(sfs->sfs_freemaplock);
    
    if (direction == P_REDO &&
            !bitmap_isset(sfs->sfs_freemap, (unsigned) b_le.le_blocknum)) {
        bitmap_mark(sfs->sfs_freemap, (unsigned) b_le.le_blocknum);
    } else if (direction == P_UNDO &&
            bitmap_isset(sfs->sfs_freemap, (unsigned) b_le.le_blocknum)) {
        bitmap_unmark(sfs->sfs_freemap, (unsigned) b_le.le_blocknum);
    }

	sfs->sfs_freemapdirty = true;
	lock_release(sfs->sfs_freemaplock);

    return 0;

}

static int parse_free_block(void *data, journal_direction_p direction,
        struct sfs_fs *sfs, struct lsnarray *aborted)
{

    /* load in data */
    struct block_le b_le;
    memcpy((void *)&b_le, data, sizeof(struct block_le));

    /* check if transaction is aborted */
    if (find_tnx(aborted, b_le.le_tnx, NULL) && direction == P_REDO) {
        return 0;
    }

    /* change the bitmap */
	lock_acquire(sfs->sfs_freemaplock);
    
    if (direction == P_REDO &&
            bitmap_isset(sfs->sfs_freemap, (unsigned) b_le.le_blocknum)) {
        bitmap_unmark(sfs->sfs_freemap, (unsigned) b_le.le_blocknum);
    } else if (direction == P_UNDO &&
            !bitmap_isset(sfs->sfs_freemap, (unsigned) b_le.le_blocknum)) {
        bitmap_mark(sfs->sfs_freemap, (unsigned) b_le.le_blocknum);
    }

	sfs->sfs_freemapdirty = true;
	lock_release(sfs->sfs_freemaplock);

    return 0;
}

static int parse_change_size(void *data, journal_direction_p direction,
        struct pbarray *protected_blocks, sfs_lsn_t lsn,
        struct lsnarray *aborted, struct sfs_fs *sfs)
{
    int result;
    struct buf *iobuffer;
	struct sfs_dinode *ioptr;

    
    /* load in data */
    struct change_size_le cs_le;
    memcpy((void *)&cs_le, data, sizeof(struct change_size_le));

    /* check if transaction is aborted */
    if (find_tnx(aborted, cs_le.le_tnx, NULL)) {
        return 0;
    }

    if (is_block_protected(protected_blocks, lsn, cs_le.le_ino)) {
        return 0;
    }

    result = buffer_read(&sfs->sfs_absfs, (daddr_t) cs_le.le_ino,
            SFS_BLOCKSIZE, &iobuffer);

    if (result) {
        return result;
    }

	ioptr = buffer_map(iobuffer);

    if(ioptr->sfi_type != cs_le.le_type) {
        // directory never got written to disk. Zero and reinit
        bzero(ioptr, sizeof(struct sfs_dinode));
        ioptr->sfi_type = cs_le.le_type;
    }

    ioptr->sfi_size = (direction == P_REDO) ? cs_le.le_newsize : cs_le.le_oldsize;
    
    buffer_mark_dirty(iobuffer);
    
    /* cleanup */
	buffer_release(iobuffer);

    return 0;

}


static int parse_change_link_cnt(void *data, journal_direction_p direction,
        struct pbarray *protected_blocks, sfs_lsn_t lsn,
        struct lsnarray *aborted, struct sfs_fs *sfs)
{
    int result;
    struct buf *iobuffer;
	struct sfs_dinode *ioptr;

    /* load in data */
    struct change_linkcount_le cl_le;
    memcpy((void *)&cl_le, data, sizeof(struct change_linkcount_le));

    /* check if transaction is aborted */
    if (find_tnx(aborted, cl_le.le_tnx, NULL)) {
        return 0;
    }

    if (is_block_protected(protected_blocks, lsn, cl_le.le_ino)) {
        return 0;
    }

    result = buffer_read(&sfs->sfs_absfs, (daddr_t) cl_le.le_ino,
            SFS_BLOCKSIZE, &iobuffer);

    if (result) {
        return result;
    }

	ioptr = buffer_map(iobuffer);
    
    if(ioptr->sfi_type != cl_le.le_inodetype) {
        // file never got written to disk. Zero and reinit
        bzero(ioptr, sizeof(struct sfs_dinode));
        ioptr->sfi_type = cl_le.le_inodetype;
    }

    ioptr->sfi_linkcount = (direction == P_REDO) ? cl_le.le_newcount : cl_le.le_oldcount;
    
    buffer_mark_dirty(iobuffer);
    
    /* cleanup */
	buffer_release(iobuffer);


    return 0;
}


static int parse_change_indirect_ptr(void *data, journal_direction_p direction,
        struct pbarray *protected_blocks, sfs_lsn_t lsn,
        struct lsnarray *aborted, struct sfs_fs *sfs)
{
    int result;
    struct buf *iobuffer;
	struct sfs_dinode *ioptr;

    /* load in data */
    struct change_indirect_ptr_le cip_le;
    memcpy((void *)&cip_le, data, sizeof(struct change_indirect_ptr_le));

    /* check if transaction is aborted */
    if (find_tnx(aborted, cip_le.le_tnx, NULL)) {
        return 0;
    }

    if (is_block_protected(protected_blocks, lsn, cip_le.le_ino)) {
        return 0;
    }

    result = buffer_read(&sfs->sfs_absfs, (daddr_t) cip_le.le_ino,
            SFS_BLOCKSIZE, &iobuffer);

    if (result) {
        return result;
    }

	ioptr = buffer_map(iobuffer);

    if(ioptr->sfi_type != cip_le.le_type) {
        // file never got written to disk. Zero and reinit
        bzero(ioptr, sizeof(struct sfs_dinode));
        ioptr->sfi_type = cip_le.le_type;
    }

    uint32_t ptr_to_set = (direction == P_REDO) ? cip_le.le_newptr : cip_le.le_oldptr;

    switch(cip_le.le_level) {
        
        case P_SINGLE:
        ioptr->sfi_indirect = ptr_to_set;
        break;

        case P_DOUBLE:
        ioptr->sfi_dindirect = ptr_to_set;
        break;

        case P_TRIPLE:
        ioptr->sfi_tindirect = ptr_to_set;
        break;

        default:
        panic("tried to change invalid indirection level pointer");

    }
    
    buffer_mark_dirty(iobuffer);
    
    /* cleanup */
	buffer_release(iobuffer);
 
    return 0;
}

static int parse_change_direct_ptr(void *data, journal_direction_p direction,
        struct pbarray *protected_blocks, sfs_lsn_t lsn,
        struct lsnarray *aborted, struct sfs_fs *sfs)
{
    int result;
    struct buf *iobuffer;
	struct sfs_dinode *ioptr;
    
    /* load in data */
    struct change_ptr_le cp_le;
    memcpy((void *)&cp_le, data, sizeof(struct change_ptr_le));

    /* check if transaction is aborted */
    if (find_tnx(aborted, cp_le.le_tnx, NULL)) {
        return 0;
    }

    if (is_block_protected(protected_blocks, lsn, cp_le.le_ino)) {
        return 0;
    }

    result = buffer_read(&sfs->sfs_absfs, (daddr_t) cp_le.le_ino,
            SFS_BLOCKSIZE, &iobuffer);

    if (result) {
        return result;
    }

	ioptr = buffer_map(iobuffer);

    if(ioptr->sfi_type != cp_le.le_type) {
        // file never got written to disk. Zero and reinit
        bzero(ioptr, sizeof(struct sfs_dinode));
        ioptr->sfi_type = cp_le.le_type;
    }

    uint32_t ptr_to_set = (direction == P_REDO) ? cp_le.le_newptr : cp_le.le_oldptr;

    KASSERT(cp_le.le_ptrnum < SFS_NDIRECT);

    ioptr->sfi_direct[cp_le.le_ptrnum] = ptr_to_set;

    buffer_mark_dirty(iobuffer);
    
    /* cleanup */
	buffer_release(iobuffer);
    return 0;

}

static int parse_change_ino_in_indirect(void *data, journal_direction_p direction,
        struct pbarray *protected_blocks, sfs_lsn_t lsn,
        struct lsnarray *aborted, struct sfs_fs *sfs)
{

    int result;
    struct buf *iobuffer;
	uint32_t *ioptr;

    /* load in data */
    struct change_ptr_le cp_le;
    memcpy((void *)&cp_le, data, sizeof(struct change_ptr_le));

    /* check if transaction is aborted */
    if (find_tnx(aborted, cp_le.le_tnx, NULL)) {
        return 0;
    }

    if (is_block_protected(protected_blocks, lsn, cp_le.le_ino)) {
        return 0;
    }
    
    result = buffer_read(&sfs->sfs_absfs, (daddr_t) cp_le.le_ino,
            SFS_BLOCKSIZE, &iobuffer);

    if (result) {
        return result;
    }

	ioptr = buffer_map(iobuffer);

    uint32_t ptr_to_set = (direction == P_REDO) ? cp_le.le_newptr : cp_le.le_oldptr;
    ioptr[cp_le.le_ptrnum] = ptr_to_set;

    buffer_mark_dirty(iobuffer);
    
    /* cleanup */
	buffer_release(iobuffer);
    
    return 0;
}

static int parse_change_block_obj(void *data, journal_direction_p direction,
        struct pbarray *protected_blocks, sfs_lsn_t lsn,
        struct lsnarray *aborted, struct sfs_fs *sfs)
{

    int result;
    struct buf *iobuffer;
	uint32_t *ioptr;

    /* load in data */
    struct change_block_obj_le cbo_le;
    memcpy((void *)&cbo_le, data, sizeof(struct change_block_obj_le));

    /* check if transaction is aborted */
    if (find_tnx(aborted, cbo_le.le_tnx, NULL)) {
        return 0;
    }

    if (is_block_protected(protected_blocks, lsn, cbo_le.le_blocknum)) {
        return 0;
    }

    result = buffer_read(&sfs->sfs_absfs, (daddr_t) cbo_le.le_blocknum,
            SFS_BLOCKSIZE, &iobuffer);

    if (result) {
        return result;
    }

	ioptr = buffer_map(iobuffer);

    KASSERT(cbo_le.le_offset < SFS_BLOCKSIZE/sizeof(uint32_t));

    ioptr[cbo_le.le_offset] = (direction == P_REDO) ? cbo_le.le_newval : cbo_le.le_oldval;

    buffer_mark_dirty(iobuffer);
    
    /* cleanup */
	buffer_release(iobuffer);
    return 0;
}

static int parse_write_block(void *data, struct sfs_fs *sfs, struct lsnarray *aborted)
{

    int result;
    struct buf *iobuffer;
	uint16_t *ioptr;

    /* load in data */
    struct write_block_le wb_le;
    memcpy((void *)&wb_le, data, sizeof(struct write_block_le));

    /* check if transaction is aborted */
    if (find_tnx(aborted, wb_le.le_tnx, NULL)) {
        return 0;
    }
    
    result = buffer_read(&sfs->sfs_absfs, (daddr_t) wb_le.le_block,
            SFS_BLOCKSIZE, &iobuffer);

    if (result) {
        return result;
    }

	ioptr = buffer_map(iobuffer);
    
    /* calculate the checksum */
    uint32_t checksum = fletcher32(ioptr);

	buffer_release(iobuffer);
    
    if (checksum != wb_le.le_checksum) {
        return sfs_clearblock_internal(sfs, wb_le.le_block, NULL, 0);
    }

    return 0;

}

static int parse_change_inode_type(void *data, journal_direction_p direction,
        struct pbarray *protected_blocks, sfs_lsn_t lsn,
        struct lsnarray *aborted, struct sfs_fs *sfs)
{
    int result;
    struct buf *iobuffer;
	struct sfs_dinode *ioptr;

    /* load in data */
    struct change_inode_type_le cit_le;
    memcpy((void *)&cit_le, data, sizeof(struct change_inode_type_le));

    /* check if transaction is aborted */
    if (find_tnx(aborted, cit_le.le_tnx, NULL)) {
        return 0;
    }

    if (is_block_protected(protected_blocks, lsn, cit_le.le_ino)) {
        return 0;
    } 
    
    uint16_t type_to_change = (direction == P_REDO) ? cit_le.le_newtype : cit_le.le_oldtype;
    uint16_t cur_type = (direction == P_UNDO) ? cit_le.le_newtype : cit_le.le_oldtype;
    if (type_to_change != SFS_TYPE_INVAL &&
        type_to_change != SFS_TYPE_FILE &&
        type_to_change != SFS_TYPE_DIR) {
        panic("incorrect inode type %d in change inode type record", type_to_change);
    }

    result = buffer_read(&sfs->sfs_absfs, (daddr_t) cit_le.le_ino,
            SFS_BLOCKSIZE, &iobuffer);

    if (result) {
        return result;
    }

	ioptr = buffer_map(iobuffer);

    if(ioptr->sfi_type != cur_type) {
        // file never got written to disk. Zero and reinit
        bzero(ioptr, sizeof(struct sfs_dinode));
    }

    
    ioptr->sfi_type = type_to_change;

    buffer_mark_dirty(iobuffer);
    
    /* cleanup */
	buffer_release(iobuffer);
    return 0;
}

/*
 * processes a journal entry for recovery
 */
int process_journal_entry(uint8_t type, void *data,
        struct sfs_fs *sfs, journal_direction_p direction,
        struct pbarray *protected_blocks, sfs_lsn_t lsn,
        struct lsnarray *aborted)
{

    switch(type) {
        
        case START_TRANSACTION:
        /* nothing to do */
        return 0;

        case END_TRANSACTION:
        /* nothing to do */
        return 0;

        case ABORT_TRANSACTION:
        /* nothing to do */
        return 0;


        case CHANGE_DIRENTRY:
        return parse_change_direntry(data, direction, protected_blocks, lsn, aborted, sfs);

        case ZERO_BLOCK:
        return parse_zero_block(data, direction, sfs, protected_blocks, lsn, aborted);
        
        case ALLOC_BLOCK:
        return parse_alloc_block(data, direction, sfs, aborted);
        
        case FREE_BLOCK: 
        return parse_free_block(data, direction, sfs, aborted);
        
        case CHANGE_SIZE:
        return parse_change_size(data, direction, protected_blocks, lsn, aborted, sfs);

        case CHANGE_LINK_CNT:
        return parse_change_link_cnt(data, direction, protected_blocks, lsn, aborted, sfs);
        
        case CHANGE_INDIRECT_PTR:
        return parse_change_indirect_ptr(data, direction, protected_blocks, lsn, aborted, sfs);

        case CHANGE_DIRECT_PTR:
        return parse_change_direct_ptr(data, direction, protected_blocks, lsn, aborted, sfs);

        case CHANGE_INO_IN_INDIRECT:
        return parse_change_ino_in_indirect(data, direction, protected_blocks, lsn, aborted, sfs);
        case CHANGE_BLOCK_OBJ:
        return parse_change_block_obj(data, direction, protected_blocks, lsn, aborted, sfs);
       
        case WRITE_BLOCK:
        return parse_write_block(data, sfs, aborted);

        case CHANGE_INODE_TYPE:
        return parse_change_inode_type(data, direction, protected_blocks, lsn, aborted, sfs);

        default:
        panic("Unrecognized journal record type %d\n", type);
    }

    /* shouldn't reach here */
    return EINVAL;
}

