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
#include <filetable.h>

/*
 * Helper function for changing file descriptors.
 */
int
sys_dup2(int oldfd, int newfd, int *retval) {
    /* Check that oldfd and newfd are valid */
    struct file_handle *old_fh = ft_get(oldfd, curproc);
    if (old_fh == NULL || newfd < 0 || newfd >= OPEN_MAX)  return EBADF;
    uint8_t ftid = curproc->p_fds[oldfd];
    if (ftid == FD_FREE)  return EBADF;

    /* Make sure oldfd and newfd are not the same */
    if (oldfd == newfd) {
        *retval = newfd;
        return 0;
    }

    /* Check whether newfd is an open file */
    if (curproc->p_fds[newfd] != FD_FREE) {
        kprintf("old file\n");
        int err = sys_close(newfd);
        if (err)  return err;
    }

    /* Update the reference count */
    fh_incref(old_fh); 

    /* Update file descriptor table */
    curproc->p_fds[newfd] = curproc->p_fds[oldfd];

    /* All done!*/
    *retval = newfd;
    return 0;
}
