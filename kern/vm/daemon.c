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


/* This file implements the paging daemon functions! */

#include <paging.h>
#include <thread.h>
#include <addrspace.h>
#include <pagetable.h>
#include <vm.h>
#include <coremap.h>
#include <spinlock.h>
#include <synch.h>
#include <daemon.h>
#include <vmstats.h>
#include <clock.h>
#include <syscall.h>

void
paging_daemon_thread(void *data1, unsigned long data2)
{
    (void) data1;
    (void) data2;
    
    struct cm_entry *cme;
    
    while (true) {
        spinlock_acquire(&k_coremap->cm_lock);
        if (k_coremap->cm_num_dirty*100/k_coremap->cm_num_pages >= PAGING_DAEMON_THRESHOLD) {
            k_vmstats.vms_daemon_runs++;
            for (int i = 0; i < k_coremap->cm_num_pages; i++) {
                cme = &k_coremap->cm_entries[i];
                if (cme->cme_exists == 0)  break;
                if (cme->cme_busy == 1 || cme->cme_dirty == 0 || cme->cme_kpage == 1 || cme->cme_as == NULL) {
                    continue;
                }
                cme->cme_busy = 1;
                int err = page_write_out(i);
                cme->cme_busy = 0; 
                wchan_wakeall(k_coremap->cm_wchan, &k_coremap->cm_lock);
                if (err) {
                    spinlock_release(&k_coremap->cm_lock);
                    panic("Writing daemon failed"); 
                }
            }
        }
        spinlock_release(&k_coremap->cm_lock);
        clocksleep(1);
    }
}


void
daemon_init(void) {

    struct proc *daemon_proc;

    int res = fork_common(&daemon_proc);
    if (res) {
        panic("forking paging daemon failed");
    }

    res = thread_fork("paging daemon", daemon_proc, paging_daemon_thread, NULL, 0);
    if (res) {
        panic("forking paging daemon failed");
    }

    return;
}
