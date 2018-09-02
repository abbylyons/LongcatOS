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
#include <copybuff.h>
#include <current.h>

struct copy_buffer *
cb_create(void)
{
    struct copy_buffer *cb;

    cb = kmalloc(sizeof(struct copy_buffer));
    if (cb == NULL)  return NULL;

    cb->cb_lock = lock_create("cb lock");
    if (cb->cb_lock == NULL)  goto cleanup2;
    cb->cb_sem = sem_create("cb sem", CPY_BUF_MAX);
    if (cb->cb_sem == NULL)  goto cleanup1;

    for (int i = 0; i < CPY_BUF_MAX; i++) {
        cb->cb_alloc[i] = NULL;
    } 
    
    return cb;

cleanup1:
    lock_destroy(cb->cb_lock);
cleanup2:
    kfree(cb);
    return NULL;
}

void
cb_destroy(struct copy_buffer *cb)
{
    sem_destroy(cb->cb_sem);
    lock_destroy(cb->cb_lock);
    kfree(cb);
}

char *
cb_acquire(struct copy_buffer *cb) {
    
    int chosen = -1;
    
    P(cb->cb_sem);
    lock_acquire(cb->cb_lock);
    for (int i = 0; i < CPY_BUF_MAX; i++) {
        if (cb->cb_alloc[i] == NULL) {
            chosen = i;
            cb->cb_alloc[i] = curproc;
            break;
        }
    }

    KASSERT(chosen != -1);
   
    lock_release(cb->cb_lock);

    return cb->cb_buffs[chosen];
}

void
cb_release(struct copy_buffer *cb)
{
    lock_acquire(cb->cb_lock);
    for (int i = 0; i < CPY_BUF_MAX; i++) {
        if (cb->cb_alloc[i] == curproc) {
            cb->cb_alloc[i] = NULL;
            lock_release(cb->cb_lock);
            V(cb->cb_sem);
            return;
        }
    }

    /* if we got here, then we didn't hold a copy buffer */
    panic("tried to relase a copy buffer that user didn't hold\n"); 
}

