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

#ifndef _COPYBUFF_H_
#define _COPYBUFF_H_

#include <synch.h>
#include <limits.h>

/* structure used for holding char buffers for
 * copying arguments during execv
 */
struct copy_buffer {
    char cb_buffs[CPY_BUF_MAX][ARG_MAX];    /* char buffers */
    struct semaphore *cb_sem;               /* semaphore uesd to restirct access */
    struct lock *cb_lock;                   /* lock used for allocating buffers */
    struct proc *cb_alloc[CPY_BUF_MAX];     /* used for tracking allocated buffers */
};

/* Create a copy buffer. */
struct copy_buffer *cb_create(void);

/* Destroy a copy buffer. */
void cb_destroy(struct copy_buffer *cb);


/* Acquire a copy buffer. May put thread to sleep if none available. */
char *cb_acquire(struct copy_buffer *cb);

/* release a copy buffer. */
void cb_release(struct copy_buffer *cb);


#endif /* _COPYBUFF_H_ */
