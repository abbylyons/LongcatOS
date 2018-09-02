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
#include <current.h>
#include <addrspace.h>
#include <pagetable.h>
#include <limits.h>
#include <kern/errno.h>
#include <coremap.h>
#include <swap.h>

/*
 * Readjusts the current heap size
 */
int
sys_sbrk(int amount, int *retval) {

    int err = 0;
    int ppn = -1;
    struct pgtable *pde;
    struct pt_entry *pte;
    struct cm_entry *cme;
    vaddr_t vaddr;

    if (amount % PAGE_SIZE != 0) {
        err = EINVAL;
        goto cleanup2;
    }

    struct addrspace *as = curproc->p_addrspace;
    KASSERT(as->as_heap_start % PAGE_SIZE == 0);
    lock_acquire(as->as_lock);

    if (amount == 0) {
        *retval = as->as_heap_start + as->as_heap_size;
        lock_release(as->as_lock);
        return 0;
    }

    if (as->as_heap_start + as->as_heap_size + amount > STACK_MIN && amount > 0) {
        err = ENOMEM;
        goto cleanup1;
    }

    if ((int)as->as_heap_size + amount < 0 && amount < 0) {
        err = EINVAL;
        goto cleanup1;
    }

    if (amount > 0) {
        /* growing the heap */
        int pages_to_create = amount / PAGE_SIZE;
        for(int i = 0; i < pages_to_create; i++) {
            vaddr = as->as_heap_size + as->as_heap_start + i*PAGE_SIZE;        
            pde = as->as_pd[VADDR_TO_PT(vaddr)];
            if (pde == NULL) {
                pde = kmalloc(sizeof(struct pgtable));
                if (pde == NULL) {
                    err = ENOMEM;
                    goto cleanup1;
                }
                pgt_init(pde);
                as->as_pd[VADDR_TO_PT(vaddr)] = pde;
            }
            pte = &(pde->pt_ptes[VADDR_TO_PTE(vaddr)]);
            pte_acquire(as, pte);
            pte->pte_valid = 0;
            pte->pte_ppn = 0;
            pte->pte_writeable = 1;
            pte->pte_present = 0;
            pte->pte_zeroed = 1;
            pte_release(as, pte, ppn);
        } 
    } else {
        /* shrinking the heap */
        int pages_to_free = -amount / PAGE_SIZE;
        for(int i = 1; i <= pages_to_free; i++) {
            ppn = -1;
            vaddr = as->as_heap_size + as->as_heap_start - i*PAGE_SIZE;        
            pde = as->as_pd[VADDR_TO_PT(vaddr)];
            if (pde == NULL)  continue;
            pte = &(pde->pt_ptes[VADDR_TO_PTE(vaddr)]);
            pte_acquire(as, pte);
            if (pte->pte_present) {
                ppn = pte->pte_ppn;
                cme = &(k_coremap->cm_entries[pte->pte_ppn]);
                KASSERT(cme->cme_kpage == 0);
                if (cme->cme_tlb == 1) {
                    KASSERT(cme->cme_owner_cpu == curcpu);
                    struct tlbshootdown tlbs;
                    tlbs.tlbs_cpu = cme->cme_owner_cpu;
                    tlbs.tlbs_flush_all = false;
                    tlbs.tlbs_vaddr = cme->cme_vaddr;
                    vm_tlbshootdown(&tlbs);
                }
                if (cme->cme_swap_location != 0) {
                    swap_destroy_block(cme->cme_swap_location, k_swap_tracker);
                }
                cme->cme_as = NULL;
                cme->cme_vaddr = 0;
                cme->cme_swap_location = 0;
                cme->cme_dirty = 0;
                cme->cme_kernel = 0;
                pte->pte_present = 0;
            } else if (pte->pte_zeroed == 0) {
                swap_destroy_block(pte->pte_ppn, k_swap_tracker);
            }

            pte->pte_ppn = 0;
            pte->pte_valid = 0;
            pte_release(as, pte, ppn);
        }
    }
    
    
    *retval = as->as_heap_start + as->as_heap_size;
    as->as_heap_size += amount;
    lock_release(as->as_lock);
    return 0;


cleanup1:
    lock_release(as->as_lock);
cleanup2:
    *retval = -1;
    return err;

}
