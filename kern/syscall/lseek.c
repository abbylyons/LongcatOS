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
#include <filetable.h>
#include <kern/seek.h>
#include <kern/stat.h>

/* Helper function for seeking in files */

int
sys_lseek(int fd, off_t pos, int whence, off_t *retval) {
	/* Get the file handle and check if it's valid */
    struct file_handle *fh = ft_get(fd, curproc);
    if (fh == NULL)  return EBADF;

    lock_acquire(fh->fh_use_lock);

    /* Check whether the file is seekable */
    if (!VOP_ISSEEKABLE(fh->fh_file)) {
    	lock_release(fh->fh_use_lock);
    	return ESPIPE;
    }
    
    /* Update the file's offset */
    struct stat statbuf;
    int err;
    switch (whence) {
    	case SEEK_SET:
    	/* Set offset from beginning of file */
    	if (pos < 0)  goto cleanup;
		fh->fh_off = pos;
		break;
    	
    	case SEEK_CUR:
    	/* Set offset from current offset */
    	if (pos + fh->fh_off < 0)  goto cleanup;
		fh->fh_off += pos;
		break;
    	
    	case SEEK_END:
    	/* Set offset from end of file */
        err = VOP_STAT(fh->fh_file, &statbuf);
        if (err) {
            lock_release(fh->fh_use_lock);
            return err;
        }
    	if ((off_t)statbuf.st_size + pos < 0)  goto cleanup;
        fh->fh_off = statbuf.st_size + pos;
        break;
    	
    	default:
    	/* Invalid whence */
        goto cleanup;
    }
    
    *retval = fh->fh_off;
    lock_release(fh->fh_use_lock);
    return 0;

    cleanup:
    lock_release(fh->fh_use_lock);
    return EINVAL;
}
