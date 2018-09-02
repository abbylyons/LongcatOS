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
#include <addrspace.h>
#include <vnode.h>
#include <filetable.h>

/*
 * Helper function for the fork system call.
 * It will initialize the new newproc process.
 */
int
sys_fork(struct proc **newproc) {

    int retval;

    retval = fork_common(newproc);
    if (retval)  return retval;

    /* copy over the old address space */
    retval = as_copy(curproc->p_addrspace, &(*newproc)->p_addrspace);
    if (retval) {
        proc_destroy(*newproc);
        return retval;
    }

    return 0;

}

int
fork_common(struct proc **newproc) {

    struct file_handle *fh;

    /* Acquire the process table's lock */
    lock_acquire(k_proctable->pt_lock);

    /* Find a new pid */
    pid_t newpid = pt_get_open_pid();
    if (newpid == PID_INVALID) {
        lock_release(k_proctable->pt_lock);
        return ENPROC;
    }

    /* create a new process */
    *newproc = proc_create("forked proc");
    if (*newproc == NULL) {
        lock_release(k_proctable->pt_lock);
        return ENOMEM;
    }

    /* point the process table entry to the new process */
    k_proctable->pt_procs[newpid % PROC_MAX] = *newproc;

    /* release the process table's lock */
    lock_release(k_proctable->pt_lock);

    /* set the values in the new process */
    lock_acquire(curproc->p_waitlock);
	if (curproc->p_cwd != NULL) {
		vnode_incref(curproc->p_cwd);
		(*newproc)->p_cwd = curproc->p_cwd;
	}

    (*newproc)->p_numthreads = 0;
    (*newproc)->p_parent = curproc->p_pid;
    (*newproc)->p_pid = newpid;
    (*newproc)->p_state = P_ALIVE;

    /* set SFS stuff */
    (*newproc)->p_fs = curproc->p_fs;

    /* copy over the file descriptors */
    for (int i = 0; i < OPEN_MAX; i++) {
        (*newproc)->p_fds[i] = curproc->p_fds[i];

        /*
         * increase the refcount of the file_handle
         * if the fd is valid
         */
        if ((*newproc)->p_fds[i] != FD_FREE) {
            fh = ft_get(i, *newproc);
            fh_incref(fh);
        }

    }

    /* add child to parent's list of children */
    struct p_node *child_node = kmalloc(sizeof(struct p_node));
    if (child_node == NULL) {
        lock_release(curproc->p_waitlock);
        proc_destroy(*newproc);
        return ENOMEM;
    }
    child_node->pn_pid = newpid;
    child_node->pn_next = curproc->p_children;
    curproc->p_children = child_node;
    
    lock_release(curproc->p_waitlock);

    /* All's good! */
    return 0;
}

