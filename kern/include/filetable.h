/*
 * Copyright (c) 2013
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

#ifndef _FILETABLE_H_
#define _FILETABLE_H_

#include <limits.h>
#include <synch.h>
#include <proc.h>

/*
 * File handle structure
 *
 * A file handle keeps count of an open file and its offset
 */
struct file_handle {
    struct vnode *fh_file;      /* vfs node associated with the fd */
    off_t fh_off;               /* offset in the current file */
    uint32_t fh_refcount;       /* how many FD's reference this entry */
    struct spinlock fh_ref_lock;/* lock for updating refcount */
    struct lock *fh_use_lock;   /* lock for using the file */
    int fh_open_flags;          /* flags with which the file was opened */
};

/*
 * kernel level file_table's structure
 */
struct file_table {
    struct file_handle *ft_fhs[FT_MAX]; /* file handles that are open */
    struct lock *ft_lock;               /* lock */
};

/* This is the structure for the kernel file table*/
extern struct file_table *k_filetable;


/* Convenience function to initialize a new file_table */
struct file_table *ft_init(void);

/* Gets the file_handle related to the given fd */
struct file_handle *ft_get(int fd, struct proc *proc);

/* Closes a fd for the given process and fd */
void ft_close(struct proc *proc, int fd);

/* Convenience function to initialize a new file_handle */
struct file_handle *fh_init(struct vnode *file, int flags);

/* Convenience function for closing a file_handle */
struct file_handle *fh_close(struct file_handle *fh);

/* Increments the refcount of a given file handle */
void fh_incref(struct file_handle *fh);

/* Increments the refcount of a given file handle */
void fh_decref(struct file_handle *fh);

#endif /* _FILETABLE_H_ */
