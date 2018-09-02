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


#include <vmstats.h>
#include <lib.h>

void
vmstats_init(struct vmstats *vms)
{
    vms->vms_page_faults = 0;
    vms->vms_write_page_faults = 0;
    vms->vms_vm_faults = 0;
    vms->vms_daemon_runs = 0;
    vms->vms_tlb_shootdowns = 0;
}

int
vmstats_reset(int n, char **a)
{
    (void) n;
    (void) a;
    vmstats_init(&k_vmstats);
    return 0;
}

int
vmstats_report(int n, char **a)
{
    (void) n;
    (void) a;
    struct vmstats *vms = &k_vmstats;
    kprintf("Number of page faults: %d\nNumber of page faults that required a synchronous write: %d\nNumber of vm faults: %d\nNumber of TLB shootdowns %d\nNumber of daemon runs: %d\n", vms->vms_page_faults, vms->vms_write_page_faults, vms->vms_vm_faults, vms->vms_tlb_shootdowns, vms->vms_daemon_runs);
    return 0;
}

