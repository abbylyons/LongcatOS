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

#include <types.h>
#include <syscall.h>
#include <kern/errno.h>
#include <kern/limits.h>
#include <current.h>
#include <proc.h>
#include <vnode.h>
#include <vfs.h>
#include <filetable.h>
#include <copyinout.h>

/*
 * Helper function for the open system call.
 * Initializes a new file handle and returns a file descriptor
 */
int
sys_open(const_userptr_t filename, int flags, mode_t mode, int *retval) {

    int err;

    /* Find the first free file descriptor */
    int fd = -1;
    for (int i = 0; i < OPEN_MAX; ++i) {
        if (curproc->p_fds[i] == FD_FREE) {
            fd = i;
            break;
        }
    }
    if (fd == -1)  return EMFILE;
    
    /* Acquire the file table lock */
    lock_acquire(k_filetable->ft_lock);

    /* Find the first empty slot in the file table */
    int ft_index = -1;
    for (int i = 0; i < FD_FREE; ++i) { /* cannot use FD_FREE's slot */
        if (k_filetable->ft_fhs[i] == NULL) {
            ft_index = i;
            break;
        }
    }
    if (ft_index == -1) {
        lock_release(k_filetable->ft_lock);
        return ENFILE;
    }

    /* Get a new vnode */
    struct vnode *vn = NULL;
    size_t got_in;
    char filebuf[PATH_MAX];
    err = copyinstr(filename, filebuf, PATH_MAX, &got_in);
    if (err) {
        lock_release(k_filetable->ft_lock);
        return EFAULT;
    }
    err = vfs_open(filebuf, flags, mode, &vn);
    if (err) {
        lock_release(k_filetable->ft_lock);
        return err;
    }

    /* Create a new file handle */
    struct file_handle *new_fh = fh_init(vn, flags & 3);
    if (new_fh == NULL) {
        vfs_close(vn);
        lock_release(k_filetable->ft_lock);
        return ENOMEM;
    }

    /* Update the file table entry */
    k_filetable->ft_fhs[ft_index] = new_fh;

    /* Release the file table lock */
    lock_release(k_filetable->ft_lock);

    /* Update the file descriptor table*/
    KASSERT(curproc->p_fds[fd] == FD_FREE);
    curproc->p_fds[fd] = ft_index;
    *retval = fd;
    return 0;
}
