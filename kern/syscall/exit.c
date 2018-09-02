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
#include <current.h>
#include <proc.h>
#include <spinlock.h>
#include <filetable.h>
#include <thread.h>
#include <kern/wait.h>
#include <addrspace.h>

/*
 * helper function for clearing any of the children
 * that have already exited
 */
static
void
clear_children()
{
    struct p_node *cur_child = curproc->p_children;
    while (cur_child != NULL) {
        int status;
        struct p_node *next_child = cur_child->pn_next;
        kern_waitpid(cur_child->pn_pid, &status, WNOHANG);
        cur_child = next_child;
    }
}


/*
 * Helper function for the _exit system call.
 */
void kern__exit(int exitcode, int signal)
{
    struct proc *parent;

    /* clear the coffin if necessary */
    pt_reap_coffin();

    /* acquire the spinlock of the current proc to avoid deadlock */
    lock_acquire(curproc->p_waitlock);

    /* change the state of the process */
    curproc->p_state = P_ZOMBIE;
    
    /* copy in the exit code */
    if (signal == -1) {
        curproc->p_exit_code = _MKWAIT_EXIT(exitcode);
    } else {
        curproc->p_exit_code = _MKWAIT_SIG(signal);
    }
    /* close the existing file descriptors */
    for (int i = 0; i < OPEN_MAX; i++) {
        if (curproc->p_fds[i] != FD_FREE) {
            ft_close(curproc, i);
        }
    }   
    /* clear any children that have already exited */
    clear_children();

    /* get the parent */
    parent = pt_get_proc(curproc->p_parent);
    
    /* check if parent is alive */
    if (parent == NULL || parent->p_state == P_ZOMBIE) {

        /* parent is dead. Put us in the coffin */
        pt_bury_proc(curproc->p_pid, false);
    } else {
        
        /* parent is alive wake up waiters*/
        cv_signal(curproc->p_cv, curproc->p_waitlock);
    }
    
    lock_release(curproc->p_waitlock);
    /* kill the current thread */
    thread_exit();

}

/*
 * wrapper function for the _exit system call
 */
void sys__exit(int exitcode)
{
    kern__exit(exitcode, -1);
}
