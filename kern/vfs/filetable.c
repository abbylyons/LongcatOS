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

#include <types.h>
#include <kern/fcntl.h>
#include <limits.h>
#include <filetable.h>
#include <current.h>
#include <vfs.h>
#include <vnode.h>

/* This is the structure for the kernel file table*/
struct file_table *k_filetable;

/*
 * Convenience function to initialize a new file_table
 */
struct file_table *
ft_init(void)
{
    struct file_table *ft;
    ft = kmalloc(sizeof(struct file_table));
    struct file_handle *con_fh[3];

    if (ft == NULL)  return NULL;

    // initialize stdin, stdout, stderr:
    char *con_path = kstrdup("con:");
    struct vnode *con_dev[3];
    int result = vfs_open(con_path, O_RDONLY, 0, &con_dev[0]);
    if (result != 0)  goto cleanup7;
    con_fh[0] = fh_init(con_dev[0], O_RDONLY);
    if (con_fh[0] == NULL) goto cleanup6;
    // stdin
    ft->ft_fhs[0] = con_fh[0];

    for (int i = 1; i < 3; i++) {
        con_path = kstrdup("con:");
        result = vfs_open(con_path, O_WRONLY, 0, &con_dev[i]);
        if (result != 0) {
            if (i == 1)  goto cleanup5;
            goto cleanup3;
        }
        con_fh[i] = fh_init(con_dev[i], O_WRONLY);
        if (con_fh[i] == NULL) {
            if (i == 1)  goto cleanup4;
            goto cleanup2;
        }
        // stdout, stderr
        ft->ft_fhs[i] = con_fh[i];
    }

    kfree(con_path);
    
    // initialize all the entries to be empty except for
    // stdin, stderr, stdout
    for (int i = 3; i < FT_MAX; i++) {
        ft->ft_fhs[i] = NULL;
    }
    ft->ft_lock = lock_create("K_FT_lock");
    if (ft->ft_lock == NULL)  goto cleanup1;

    return ft;

cleanup1:
    fh_close(con_fh[2]);
cleanup2:
    kfree(con_path);
    vfs_close(con_dev[2]);
cleanup3:
    fh_close(con_fh[1]);
cleanup4:
    vfs_close(con_dev[1]);
cleanup5:
    fh_close(con_fh[0]);
cleanup6:
    vfs_close(con_dev[0]);
cleanup7:
    kfree(ft);
    return NULL;

}

/*
 * gets the file_handle related to the given fd
 */
struct file_handle *
ft_get(int fd, struct proc *proc)
{
    if (fd < 0 || fd >= OPEN_MAX)  return NULL;

    struct file_handle *fh;

    uint8_t ftid = proc->p_fds[fd];

    if (ftid == FD_FREE)  return NULL;

    lock_acquire(k_filetable->ft_lock);
    fh = k_filetable->ft_fhs[ftid];
    lock_release(k_filetable->ft_lock);

    /* Make sure pointer is valid */
    KASSERT(fh != NULL);
    return fh;
}

/*
 *  Closes a fd for the given process and fd
 */
void
ft_close(struct proc *proc, int fd)
{
    struct file_handle *fh;

    KASSERT(fd >= 0 && fd < OPEN_MAX);

    lock_acquire(k_filetable->ft_lock);
    uint8_t ftid = proc->p_fds[fd];
    fh = k_filetable->ft_fhs[ftid];
    /* Make sure pointer is valid */
    KASSERT(fh != NULL);
    fh = fh_close(fh);
    k_filetable->ft_fhs[ftid] = fh;
    
    /* update the fd */
    proc->p_fds[fd] = FD_FREE;
    lock_release(k_filetable->ft_lock);
}

/*
 * Convenience function to initialize a new file_handle
 */
struct file_handle *
fh_init(struct vnode *file, int flags)
{
    struct file_handle *fh;
    fh = kmalloc(sizeof(*fh));
    if (fh == NULL)  return NULL;

    fh->fh_off = 0;
    fh->fh_refcount = 1;
    spinlock_init(&fh->fh_ref_lock);
    fh->fh_use_lock = lock_create(curproc->p_name);
    if (fh->fh_use_lock == NULL) {
        kfree(fh);
        return NULL;
    }
    fh->fh_open_flags = flags;
    fh->fh_file = file;

    return fh;
}

/*
 * Convenience function for closing a file_handle
 */

struct file_handle *
fh_close(struct file_handle *fh)
{
    /* Make sure pointer is valid */
    KASSERT(fh != NULL);

    /* If file is still being referenced, decrease refcount and return */
    spinlock_acquire(&fh->fh_ref_lock);
    if (fh->fh_refcount > 1) {
        fh->fh_refcount--;
        spinlock_release(&fh->fh_ref_lock);
        return fh;
    }
    spinlock_release(&fh->fh_ref_lock);

    /* Clean up */
    spinlock_cleanup(&fh->fh_ref_lock);
    lock_destroy(fh->fh_use_lock);

    vfs_close(fh->fh_file);

    kfree(fh);
    return NULL;
}

/*
 * Increments the refcount of a given file handle
 */
void
fh_incref(struct file_handle *fh)
{
    /* Make sure pointer is valid */
    KASSERT(fh != NULL);

    spinlock_acquire(&fh->fh_ref_lock);
    fh->fh_refcount++;
    spinlock_release(&fh->fh_ref_lock);
}

/*
 * Increments the refcount of a given file handle
 */
void
fh_decref(struct file_handle *fh)
{
    /* Make sure pointer is valid */
    KASSERT(fh != NULL);

    spinlock_acquire(&fh->fh_ref_lock);
    fh->fh_refcount--;
    spinlock_release(&fh->fh_ref_lock);
}
