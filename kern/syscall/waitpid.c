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
#include <limits.h>
#include <syscall.h>
#include <kern/errno.h>
#include <current.h>
#include <proc.h>
#include <spinlock.h>
#include <kern/wait.h>
#include <copyinout.h>

/*
 * helper function for removing a child from the parent's list of children
 */
static
void
remove_child(struct proc *child)
{
    /* remove child from list of children */
    struct p_node *cur = curproc->p_children;
    struct p_node *prev = NULL;
    while (cur != NULL) {
        if (cur->pn_pid == child->p_pid)  break;
        prev = cur;
        cur = cur->pn_next;
    }

    /* We should have found the child */
    KASSERT(cur != NULL);
    
    if (prev != NULL) {
        prev->pn_next = cur->pn_next;
    } else {
        curproc->p_children = cur->pn_next;
    }
    kfree(cur);

}

/*
 * Helper function for the waitpid system call.
 */
static
int
waitpid_common(pid_t pid, userptr_t status, int options, bool kdest)
{
    
    struct proc *child;
    int retval = 0;
    bool copy = true;
    
    /* check options */
    if ((options & (~WNOHANG)) > 0)  return EINVAL;

    /* check if status is a valid pointer */
    if ((int *)status == NULL)  copy = false;
    if (!kdest && copy) {
        size_t stoplen;
        retval = copycheck((const_userptr_t)status, sizeof(int), &stoplen);
        if (retval || stoplen != sizeof(int))  return retval;
    }

    /* check to make sure the pid valid */
    if (pid >= PID_MAX ||  pid <= PID_INVALID ||
            pid == curproc->p_pid) {
        return ESRCH;
    }

    /* get the child */
    child = pt_get_proc(pid);
    if (child == NULL)  return ESRCH;

    /* check that we are the parent */
    if (child->p_parent != curproc->p_pid)  return ECHILD;

    /* acquire the child's lock */
    lock_acquire(child->p_waitlock);

    /* check if the child has already exited */
    if (child->p_state != P_ZOMBIE) {

        /* child is still alive. Check options */
        if ((options & WNOHANG) > 0) {
            lock_release(child->p_waitlock);
            return -1;
        }

        /* wait on child. Acquire the waitlock, then relase the spinlock */
        cv_wait(child->p_cv, child->p_waitlock);
    }
      
    /* copy the exit code */
    if (copy) {
        if (kdest) {
            *((int *)status) = child->p_exit_code;
        } else {
            retval = copyout(&child->p_exit_code, status, sizeof(int));
        }
    }

    /* release the lock */
    lock_release(child->p_waitlock);

    remove_child(child);

    if (child->p_numthreads > 0) {
       lock_acquire(k_waitlock);
       cv_wait(k_waitcv, k_waitlock);
       lock_release(k_waitlock);
    }
 
    /* clean the process */
    proc_destroy(child);

    if (retval)  return retval;

    /* All done! */
    return 0;

}

/*
 *  executes the waitpid syscall
 */
int
sys_waitpid(pid_t pid, userptr_t status, int options) {
    return waitpid_common(pid, status, options, false);
}

/*
 *  executes the waitpid syscall if the status is in the kernel
 */
int
kern_waitpid(pid_t pid, int *status, int options) {
    return waitpid_common(pid, (userptr_t)status, options, true);
}
