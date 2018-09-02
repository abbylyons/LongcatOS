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
#include <current.h>
#include <proc.h>
#include <vnode.h>
#include <vfs.h>
#include <uio.h>
#include <filetable.h>
#include <kern/fcntl.h>
#include <copyinout.h>

/* Helper functions for reading and writing */

int
sys_read(int fd, userptr_t buf, size_t buflen, size_t *retval) {
    return readwrite(fd, buf, buflen, retval, 0);
}

int
sys_write(int fd, const_userptr_t buf, size_t nbytes, size_t *retval) {
    return readwrite(fd, (userptr_t) buf, nbytes, retval, 1);
}

/* rw is 0 if reading, nonzero otherwise */
int
readwrite(int fd, userptr_t buf, size_t nbytes, size_t *retval, uint8_t rw) {
    /* Get the file handle and check if it's valid */
    struct file_handle *fh = ft_get(fd, curproc);
    if (fh == NULL || (!rw && (fh->fh_open_flags & O_ACCMODE) == O_WRONLY) 
                || (rw && (fh->fh_open_flags & O_ACCMODE) == O_RDONLY)) {
        return EBADF;
    }

    /* Acquire the handle's use lock */
    bool seekable = VOP_ISSEEKABLE(fh->fh_file);
    if (seekable)  lock_acquire(fh->fh_use_lock);

    /* Make a new uio and call vop_read or vop_write */
    struct iovec iov;
    struct uio ku;
    int err;
    err = 0;
    if (rw) {
        uio_uinit(&iov, &ku, curproc->p_addrspace, buf, nbytes, fh->fh_off, UIO_WRITE);
        err = VOP_WRITE(fh->fh_file, &ku);
    }
    else {
        uio_uinit(&iov, &ku, curproc->p_addrspace, buf, nbytes, fh->fh_off, UIO_READ);
        err = VOP_READ(fh->fh_file, &ku);
    }
    if (err) {
        if (seekable)  lock_release(fh->fh_use_lock);
        return err;
    }

    /* Update file handle's offset */
    if (seekable) {
        fh->fh_off = ku.uio_offset;
        *retval = nbytes- ku.uio_resid;
        lock_release(fh->fh_use_lock);
    }
    else {
        *retval = ku.uio_offset;
    }
    
    return 0;
}
