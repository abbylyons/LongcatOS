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

#ifndef _KERN_SFS_H_
#define _KERN_SFS_H_

/* Type for log sequence numbers */
typedef uint64_t sfs_lsn_t;


/*
 * SFS definitions visible to userspace. This covers the on-disk format
 * and is used by tools that work on SFS volumes, such as mksfs.
 */

#define SFS_MAGIC         0xabadf001    /* magic number identifying us */
#define SFS_BLOCKSIZE     512           /* size of our blocks */
#define SFS_VOLNAME_SIZE  32            /* max length of volume name */
#define SFS_NDIRECT       15            /* # of direct blocks in inode */
#define SFS_NINDIRECT     1             /* # of indirect blocks in inode */
#define SFS_NDINDIRECT    1             /* # of 2x indirect blocks in inode */
#define SFS_NTINDIRECT    1             /* # of 3x indirect blocks in inode */
#define SFS_DBPERIDB      128           /* # direct blks per indirect blk */
#define SFS_NAMELEN       60            /* max length of filename */
#define SFS_SUPER_BLOCK   0             /* block the superblock lives in */
#define SFS_FREEMAP_START 3             /* 1st block of the freemap */
#define SFS_MORGUE_BLOCK  2             /* morgue block */
#define SFS_NOINO         0             /* inode # for free dir entry */
#define SFS_ROOTDIR_INO   1             /* loc'n of the root dir inode */

/* Number of bits in a block */
#define SFS_BITSPERBLOCK (SFS_BLOCKSIZE * CHAR_BIT)

/* Utility macro */
#define SFS_ROUNDUP(a,b)       ((((a)+(b)-1)/(b))*b)

/* Size of free block bitmap (in bits) */
#define SFS_FREEMAPBITS(nblocks) SFS_ROUNDUP(nblocks, SFS_BITSPERBLOCK)

/* Size of free block bitmap (in blocks) */
#define SFS_FREEMAPBLOCKS(nblocks)  (SFS_FREEMAPBITS(nblocks)/SFS_BITSPERBLOCK)

/* File types for sfi_type */
#define SFS_TYPE_INVAL          0       /* Should not appear on disk */
#define SFS_TYPE_FILE           1
#define SFS_TYPE_DIR            2

/* high-level fs functions that need logging */
typedef enum {
    P_WRITE,
    P_RECLAIM,
    P_TRUNCATE,
    P_CREAT,
    P_MKDIR,
    P_LINK,
    P_RMDIR,
    P_RENAME,
    P_REMOVE,
    P_MORGUE
} fs_logfunc_t;

/* Start/End/Abort Transaction */
struct transaction_le {
    sfs_lsn_t le_tnx;       /* Transaction id*/
    fs_logfunc_t le_func;   /* Which function started this transaction */
};

/* Change Directory entry */
struct change_direntry_le {
    sfs_lsn_t le_tnx;               /* Transaction id */
    uint32_t le_ino;                /* Inode number */
    uint32_t le_direntry;           /* Directory entry to change */
    uint32_t le_oldino;             /* Old inode in the direntry */
    char le_oldname[SFS_NAMELEN];   /* Old name in the direnty */
    uint32_t le_newino;             /* New inode in the direntry */
    char le_newname[SFS_NAMELEN];   /* New name in the direnty */
};

/* free/zero/alloc block */
struct block_le {
    sfs_lsn_t le_tnx;       /* Transaction id */
    uint32_t le_blocknum;   /* number of the block to modify */
};

struct change_block_obj_le {
    sfs_lsn_t le_tnx;       /* Transaction id */
    uint32_t le_blocknum;   /* number of the block to modify */
    uint32_t le_offset;     /* offset into the block object */
    uint32_t le_oldval;     /* old value in the block object */
    uint32_t le_newval;     /* new value in the block object */
};

/* Change Size */
struct change_size_le {
    sfs_lsn_t le_tnx;       /* Transaction id */
    uint32_t le_ino;        /* Inode number */
    uint32_t le_oldsize;    /* Old size of the file */  
    uint32_t le_newsize;    /* New size of the file */  
    uint32_t le_type;       /* type of the inode */
};

/* Change Linkcount */
struct change_linkcount_le {
    sfs_lsn_t le_tnx;       /* Transaction id */
    uint32_t le_ino;        /* Inode number */
    uint16_t le_oldcount;   /* Old linkcount of the inode */  
    uint16_t le_newcount;   /* New linkcunt of the inode */  
    unsigned le_inodetype;  /* the type of the object whose linkcount we're modifying */
};

/* Change Indirect Ptr */

typedef enum {
    P_SINGLE,
    P_DOUBLE,
    P_TRIPLE
} indirection_level_t;

struct change_indirect_ptr_le {
    sfs_lsn_t le_tnx;               /* Transaction id */
    uint32_t le_ino;                /* Inode number */
    indirection_level_t le_level;   /* which indirect ptr to change */
    uint32_t le_oldptr;             /* ino of the old block it pointed to */
    uint32_t le_newptr;             /* ino of the new block it will point to */
    uint16_t le_type;               /* type of the inode */
};

/* Change Direct Ptr */
/* Change Ino in Indirect */
struct change_ptr_le {
    sfs_lsn_t le_tnx;           /* Transaction id */
    uint32_t le_ino;            /* Inode number */
    uint32_t le_ptrnum;         /* which direct ptr to change */
    uint32_t le_oldptr;         /* ino of the old block it pointed to */
    uint32_t le_newptr;         /* ino of the new block it will point to */
    uint16_t le_type;           /* type of the inode */
};

/* Write Block */
struct write_block_le {
    sfs_lsn_t le_tnx;       /* Transaction id */
    uint32_t le_block;      /* block that is being written into */
    uint32_t le_checksum;   /* Checksum of the written data */
};

/* Change Inode Type */ 
struct change_inode_type_le {
    sfs_lsn_t le_tnx;           /* Transaction id */
    uint32_t le_ino;            /* Inode number */
    uint16_t le_oldtype;        /* Old type of the inode */
    uint16_t le_newtype;        /* New type of the inode */
};


/*
 * Metadata for dirty buffers and freemap
 */
struct sfs_metadata {
    sfs_lsn_t md_oldtnx;   /* tnx of oldest record to modify this */
    sfs_lsn_t md_newtnx;   /* tnx of newest record to modify this */
};

struct lock *metadatalock; /* lock for buffer and freemap metadata */


/*
 * On-disk superblock
 */
struct sfs_superblock {
	uint32_t sb_magic;		/* Magic number; should be SFS_MAGIC */
	uint32_t sb_nblocks;			/* Number of blocks in fs */
	char sb_volname[SFS_VOLNAME_SIZE];	/* Name of this volume */
	uint32_t sb_journalstart;		/* First block in journal */
	uint32_t sb_journalblocks;		/* # of blocks in journal */
	uint32_t reserved[116];			/* unused, set to 0 */
};

/*
 * On-disk inode
 */
struct sfs_dinode {
	uint32_t sfi_size;			/* Size of this file (bytes) */
	uint16_t sfi_type;			/* One of SFS_TYPE_* above */
	uint16_t sfi_linkcount;			/* # hard links to this file */
	uint32_t sfi_direct[SFS_NDIRECT];	/* Direct blocks */
	uint32_t sfi_indirect;			/* Indirect block */
	uint32_t sfi_dindirect;   /* Double indirect block */
	uint32_t sfi_tindirect;   /* Triple indirect block */
	uint32_t sfi_waste[128-5-SFS_NDIRECT];	/* unused space, set to 0 */
};

/*
 * On-disk directory entry
 */
struct sfs_direntry {
	uint32_t sfd_ino;			/* Inode number */
	char sfd_name[SFS_NAMELEN];		/* Filename */
};

/*
 * On-disk journal container types and constants
 */

/*
 * On-disk bit-packed type for use in record headers; contains the
 * container-level information for a journal record, namely:
 *     48-bit LSN
 *     8-bit length, in 2-octet units
 *     7-bit type code
 *     1-bit type code class
 *
 * The type code class is either SFS_JPHYS_CONTAINER, for container-
 * level records, or SFS_JPHYS_CLIENT, for records defined by higher-
 * level code.
 *
 * The length is stored in 2-octet units so we only need 8 bits for a
 * record of up to one whole block.
 *
 * The length includes the header. (struct sfs_jphys_header)
 *
 * Note that a coninfo whose integer value is 0 is not valid; this
 * prevents us from getting confused by still-zeroed journal blocks.
 */
#define SFS_CONINFO_CLASS(ci)	((ci) >> 63)  /* client vs. container record */
#define SFS_CONINFO_TYPE(ci)	(((ci) >> 56) & 0x7f)	/* record type */
#define SFS_CONINFO_LEN(ci)	((((ci) >> 48) & 0xff)*2) /* record length */
#define SFS_CONINFO_LSN(ci)	((ci) & 0xffffffffffff)	/* log sequence no. */
#define SFS_MKCONINFO(cl, ty, len, lsn) \
	(						\
		((uint64_t)(cl) << 63) |		\
		((uint64_t)(ty) << 56) |		\
		((uint64_t)((len + 1) / 2) << 48) |	\
		(lsn)					\
	)

/* symbolic names for the type code classes */
#define SFS_JPHYS_CONTAINER	0
#define SFS_JPHYS_CLIENT	1

/* container-level record types (allowable range 0-127) */
#define SFS_JPHYS_INVALID	    0		/* No record here */
#define SFS_JPHYS_PAD		    1		/* Padding */
#define SFS_JPHYS_TRIM		    2		/* Log trim record */
#define START_TRANSACTION       3
#define ABORT_TRANSACTION       4
#define END_TRANSACTION         5
#define CHANGE_DIRENTRY         6
#define ZERO_BLOCK              7
#define ALLOC_BLOCK             8
#define FREE_BLOCK              9
#define CHANGE_SIZE             10
#define CHANGE_LINK_CNT         11
#define CHANGE_INDIRECT_PTR     12
#define CHANGE_DIRECT_PTR       13
#define CHANGE_INO_IN_INDIRECT  14
#define WRITE_BLOCK             15
#define CHANGE_INODE_TYPE       16
#define CHANGE_BLOCK_OBJ        17


/* The record header */
struct sfs_jphys_header {
	uint64_t jh_coninfo;			/* Container info */
};

/* Contents for SFS_JPHYS_TRIM */
struct sfs_jphys_trim {
	uint64_t jt_taillsn;			/* Tail LSN */
};


#endif /* _KERN_SFS_H_ */
