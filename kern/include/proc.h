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

#ifndef _PROC_H_
#define _PROC_H_

/*
 * Definition of a process.
 *
 * Note: curproc is defined by <current.h>.
 */

#include <spinlock.h>
#include <copybuff.h>
#include <sfs.h>

struct addrspace;
struct thread;
struct vnode;

/* possible state of a process */
typedef enum {
    P_ALIVE,            /* state of an alive process */
    P_ZOMBIE            /* state of a process that has exited, but hasn't been reaped */
} proc_state_t;

/*
 * linked list node for keeping track of children
 */
struct p_node {
    struct p_node *pn_next;
    pid_t pn_pid;
};

/*
 * Process structure.
 *
 * Note that we only count the number of threads in each process.
 * (And, unless you implement multithreaded user processes, this
 * number will not exceed 1 except in kproc.) If you want to know
 * exactly which threads are in the process, e.g. for debugging, add
 * an array and a sleeplock to protect it. (You can't use a spinlock
 * to protect an array because arrays need to be able to call
 * kmalloc.)
 *
 * You will most likely be adding stuff to this structure, so you may
 * find you need a sleeplock in here for other reasons as well.
 * However, note that p_addrspace must be protected by a spinlock:
 * thread_switch needs to be able to fetch the current address space
 * without sleeping.
 */
struct proc {
	char *p_name;                       /* Name of this process */
	unsigned p_numthreads;              /* Number of threads in this process */

	/* VM */
	struct addrspace *p_addrspace;      /* virtual address space */

	/* VFS */
	struct vnode *p_cwd;                /* current working directory */

    /* SFS */
    struct fs *p_fs;                    /* current file system */

    uint8_t p_fds[OPEN_MAX];            /* table of indices into the kernel file table */
    pid_t p_pid;                        /* pid of this process */
    int p_exit_code;                    /* code the process exited with */
    proc_state_t p_state;               /* current state of the process */
    pid_t p_parent;                     /* pid of the process's parent */
    struct p_node *p_children;          /* the process' children */

    /* SYNCH STUFF */
    struct spinlock p_lock;             /* Lock for this structure */
    struct cv *p_cv;                    /* cv used for waitpid */
    struct lock *p_waitlock;            /* lock used for waitpid */

};

/* struct of the global process table */
struct proc_table {
    struct proc *pt_procs[PROC_MAX];    /* list of processes */
    pid_t pt_most_recent_pid;           /* the most recently allocated pid */
    pid_t pt_coffin;                    /* coffin for orphaned zombie processes */
    struct lock *pt_lock;               /* lock to protect the table */
    struct copy_buffer *pt_cb;          /* copy buffers used for copying arguments in execv */
};


/* This is the process structure for the kernel and for kernel-only threads. */
extern struct proc *kproc;

/* This is the structure for the kernel process table*/
extern struct proc_table *k_proctable;

/* these are used for waitpid, for waiting until thread has removed the process */
extern struct lock *k_waitlock;
extern struct cv *k_waitcv;

/* Call once during system startup to allocate data structures. */
void proc_bootstrap(void);

/* Create a proc structure. */
struct proc *proc_create(const char *name);

/* Create a fresh process for use by runprogram(). */
struct proc *proc_create_runprogram(const char *name);

/* Destroy a process. */
void proc_destroy(struct proc *proc);

/* Attach a thread to a process. Must not already have a process. */
int proc_addthread(struct proc *proc, struct thread *t);

/* Detach a thread from its process. */
void proc_remthread(struct thread *t);

/* Fetch the address space of the current process. */
struct addrspace *proc_getas(void);

/* Change the address space of the current process, and return the old one. */
struct addrspace *proc_setas(struct addrspace *);

/*
 * Convenience function to initialize a new proc_table.
 * This should be called by the kernel only.
 */
struct proc_table *pt_init(void);

/* Call once during system startup to allocate data structures. */
void pt_bootstrap(void);

/*
 *  returns an unused pid. You must hold the kernel process table's
 *  lock before running this to avoid race conditions
 */
pid_t pt_get_open_pid(void);

/*
 *  Returns the process associated with the given pid
 */
struct proc *pt_get_proc(pid_t pid);

/*
 * checks and clears the coffin
 */
void pt_reap_coffin(void);

/* 
 * Inserts a pid into the coffin
 */
void pt_bury_proc(pid_t pid, bool holding_lock);

#endif /* _PROC_H_ */
