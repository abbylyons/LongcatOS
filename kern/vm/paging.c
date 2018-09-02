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


/* This file implements paging functions! */

#include <paging.h>
#include <addrspace.h>
#include <pagetable.h>
#include <vm.h>
#include <coremap.h>
#include <cpu.h>
#include <spinlock.h>
#include <syscall.h>
#include <proc.h>
#include <current.h>
#include <limits.h>
#include <kern/signal.h>
#include <kern/errno.h>
#include <swap.h>
#include <mips/tlb.h>
#include <spl.h>
#include <clock.h>
#include <vmstats.h>

static
bool
in_stack(vaddr_t faultaddress) {
    return (faultaddress >= STACK_MIN && faultaddress < STACK_MAX);
}

int
page_fault(vaddr_t faultaddress) {
    k_vmstats.vms_page_faults++;
    struct addrspace *as = curproc->p_addrspace;
    KASSERT(lock_do_i_hold(as->as_lock));
    KASSERT(spinlock_do_i_hold(&k_coremap->cm_lock));

    /* Handle invalid addresses */
    if (faultaddress >= KERNEL_VADDR_START && faultaddress < KERNEL_VADDR_END) {
        lock_release(as->as_lock);
        spinlock_release(&k_coremap->cm_lock);
        KASSERT(curthread->t_machdep.tm_badfaultfunc == NULL);
        kern__exit(0, SIGSEGV);
    }
    
    struct pgtable *pde = as->as_pd[VADDR_TO_PT(faultaddress)];
    struct pt_entry *pte = &pde->pt_ptes[VADDR_TO_PTE(faultaddress)];
    KASSERT(pde != NULL);
    if (!pte->pte_zeroed && !in_stack(faultaddress) && pte->pte_present) {
        lock_release(as->as_lock);
        spinlock_release(&k_coremap->cm_lock);
        KASSERT(curthread->t_machdep.tm_badfaultfunc == NULL);
        kern__exit(0, SIGSEGV);
    }

    /* Handle pages in swap */
    int ret = page_swapin(faultaddress);
    KASSERT(ret != 0 || faultaddress == k_coremap->cm_entries[pte->pte_ppn].cme_vaddr);
    KASSERT(pte->pte_padding == 0);
    return ret;
}


int 
page_swapin(vaddr_t vaddress) {
    KASSERT(lock_do_i_hold(curproc->p_addrspace->as_lock));
    KASSERT(spinlock_do_i_hold(&k_coremap->cm_lock));

    /* Find a free location */
    int ppn = page_get(1);
    if (ppn < 0) {
        return ENOMEM;
    }
    KASSERT(vaddress != 0);
    KASSERT(ppn < k_coremap->cm_num_pages);
    struct cm_entry *cme = &k_coremap->cm_entries[ppn];
    KASSERT(cme->cme_as == NULL && !cme->cme_kpage && cme->cme_busy);
    
    /* If the page is in swap, bring it into memory */
    int pdi = VADDR_TO_PT(vaddress);
    struct pgtable *pde = curproc->p_addrspace->as_pd[pdi];
    KASSERT(pde != NULL);
    int pti = VADDR_TO_PTE(vaddress);
    struct pt_entry *pte = &(pde->pt_ptes[pti]);
    int swap_location =
        pte->pte_valid && 
        !pte->pte_zeroed ?
        pte->pte_ppn : 0;
    if (swap_location) {
        spinlock_release(&k_coremap->cm_lock);
        if (swap_read(ppn, swap_location, k_swap_tracker)) {
            panic("swap read failed");
        }
        spinlock_acquire(&k_coremap->cm_lock);
    } else {
        /* zero out page */
        as_zero_region(CM_INDEX_TO_KVADDR(ppn), 1);
    }

    /* Update the coremap */
    cme->cme_as = curproc->p_addrspace;
    cme->cme_vaddr = vaddress;
    cme->cme_swap_location = swap_location;
    cme->cme_owner_cpu = curcpu;
    cme->cme_dirty = 0;
    cme->cme_tlb = 0;
    cme->cme_busy = 0;
    cme->cme_kernel = 0;
    cme->cme_kpage = 0;
    cme->cme_exists = 1;

    /* Update the PTE
     * No need to acquire the PTE here, because we already hold the as_lock
     * and the page isn't in the coremap yet
     */
    
    KASSERT(pte->pte_padding == 0);
    KASSERT(pte->pte_present == 0);
    KASSERT(ppn != 0);
    pte->pte_ppn = ppn;
    pte->pte_valid = 1;
    pte->pte_writeable = 1;
    pte->pte_present = 1;
    pte->pte_zeroed = 0;

    wchan_wakeall(k_coremap->cm_wchan, &k_coremap->cm_lock);

    return 0;
}

int 
page_get(unsigned from_page_fault) {
    KASSERT(spinlock_do_i_hold(&k_coremap->cm_lock));
    
    int clean_ppn = -1;

    #ifdef USE_LAST_CLEAN_PAGING
    /* Algorithm 1: look for a clean or free page first, if neither
       exist then evict a random page */
    for (int i = 0; i < k_coremap->cm_num_pages; ++i) {
        if (k_coremap->cm_entries[i].cme_as == NULL &&
            k_coremap->cm_entries[i].cme_kpage == 0 &&
            k_coremap->cm_entries[i].cme_busy == 0) {
            /* free page found */
            k_coremap->cm_entries[i].cme_busy = 1;
            return i;
        }
        else if (k_coremap->cm_entries[i].cme_dirty == 0 &&
            k_coremap->cm_entries[i].cme_kpage == 0 &&
            k_coremap->cm_entries[i].cme_busy == 0 &&
            k_coremap->cm_entries[i].cme_swap_location != 0) {
            clean_ppn = i;
        }
    }

    /* Clean page found */
    if (clean_ppn >= 0 && random() % 10 >= 1) {
        k_coremap->cm_entries[clean_ppn].cme_busy = 1;
    }
    
    /* No clean or free page found; write out a random non-kernel page */
    else {
        do {
            clean_ppn = random() % k_coremap->cm_num_pages;
        } while (k_coremap->cm_entries[clean_ppn].cme_kpage ||
            k_coremap->cm_entries[clean_ppn].cme_busy);
        k_coremap->cm_entries[clean_ppn].cme_busy = 1;
        int err = page_write_out(clean_ppn);
        if (err) {
            k_coremap->cm_entries[clean_ppn].cme_busy = 0;
            wchan_wakeall(k_coremap->cm_wchan, &k_coremap->cm_lock);
            return -1;
        }
        if (from_page_fault)  k_vmstats.vms_write_page_faults++;
    }

    #endif

    #ifdef USE_CLOCK_PAGING
    /* Algorithm 2: look for a free page first. If one is not found, then
     * try to find a clean page starting at the current clock head.
     * If a clean page isn't found, then evict the first evictable
     * page that the clockhead finds*/
    
    /* try to find a free page */
    for (int i = 0; i < k_coremap->cm_num_pages; ++i) {
        if (k_coremap->cm_entries[i].cme_as == 0 &&
            k_coremap->cm_entries[i].cme_kpage == 0 &&
            k_coremap->cm_entries[i].cme_busy == 0) {
            /* free page found */
            k_coremap->cm_entries[i].cme_busy = 1;
            return i;
        }
    }

    /* try to get a clean page */
    for (int i = 0; i < k_coremap->cm_num_pages; ++i) {
        int clock = k_coremap->cm_clock_head;
        if (k_coremap->cm_entries[clock].cme_as == NULL &&
            k_coremap->cm_entries[clock].cme_kpage == 0 &&
            k_coremap->cm_entries[clock].cme_busy == 0 &&
            k_coremap->cm_entries[clock].cme_swap_location != 0) {
            /* clean page found */
            clean_ppn = clock;
            break;
        }
        k_coremap->cm_clock_head++;
        if (k_coremap->cm_clock_head >= k_coremap->cm_num_pages) {
            k_coremap->cm_clock_head = 0;
        }
    }

    /* Clean page found */
    if (clean_ppn >= 0) {
        k_coremap->cm_entries[clean_ppn].cme_busy = 1;
    }
    
    /* No clean or free page found; write out a non-kernel page */
    else {
        do {
            clean_ppn = k_coremap->cm_clock_head;
            k_coremap->cm_clock_head++;
            if (k_coremap->cm_clock_head >= k_coremap->cm_num_pages) {
                k_coremap->cm_clock_head = 0;
            }
        } while (k_coremap->cm_entries[clean_ppn].cme_kpage ||
            k_coremap->cm_entries[clean_ppn].cme_busy);
        k_coremap->cm_entries[clean_ppn].cme_busy = 1;
        int err = page_write_out(clean_ppn);
        if (err) {
            k_coremap->cm_entries[clean_ppn].cme_busy = 0;
            wchan_wakeall(k_coremap->cm_wchan, &k_coremap->cm_lock);
            return -1;
        }
        if (from_page_fault)  k_vmstats.vms_write_page_faults++;
    }
    #endif

    /* No free page found; update (possibly newly) cleaned page information */
    KASSERT(clean_ppn != 0 && clean_ppn != -1);
    struct cm_entry *cme= &k_coremap->cm_entries[clean_ppn];
    KASSERT(cme->cme_vaddr != 0);
    struct pgtable *pde = cme->cme_as->as_pd[VADDR_TO_PT(cme->cme_vaddr)];
    KASSERT(pde != NULL);

    /* Shoot down tlb */
    if (cme->cme_tlb == 1) {
        struct tlbshootdown t;
        t.tlbs_cpu = cme->cme_owner_cpu;
        t.tlbs_vaddr = cme->cme_vaddr;
        t.tlbs_flush_all = false;
        vm_tlbshootdown(&t);
        while (cme->cme_tlb) {
            wchan_sleep(k_coremap->cm_tlb_wchan, &k_coremap->cm_lock);
        }
        KASSERT(cme->cme_tlb == 0);
    }

    /* Update PTE */
    KASSERT(cme->cme_as->as_pd[VADDR_TO_PT(cme->cme_vaddr)]->
        pt_ptes[VADDR_TO_PTE(cme->cme_vaddr)].pte_ppn == clean_ppn);
    struct pt_entry *pte = &(pde->pt_ptes[VADDR_TO_PTE(cme->cme_vaddr)]);

    KASSERT(cme->cme_swap_location != 0);
    pte->pte_ppn = cme->cme_swap_location;
    pte->pte_present = 0;
    KASSERT(pte->pte_padding == 0);

    /* Remove from coremap */
    cme->cme_as = NULL;
    cme->cme_vaddr = 0;
    cme->cme_swap_location = 0;
    cme->cme_owner_cpu = NULL; 
    cme->cme_dirty = 0;
    cme->cme_tlb = 0;
    cme->cme_kernel = 0;
    cme->cme_kpage = 0;
    cme->cme_exists = 1;

    return clean_ppn;
}


int 
page_write_out(int ppn) {
    KASSERT(ppn > 0);
    KASSERT(spinlock_do_i_hold(&k_coremap->cm_lock));
    struct cm_entry *cme= &k_coremap->cm_entries[ppn];
    KASSERT(cme->cme_kpage == 0);
    KASSERT(cme->cme_busy == 1);

    /* Figure out where to write out the page */
    off_t swap_location = cme->cme_swap_location;
    if (swap_location == 0) {
        struct pt_entry *pte = &cme->cme_as->as_pd[VADDR_TO_PT(cme->cme_vaddr)]
            ->pt_ptes[VADDR_TO_PTE(cme->cme_vaddr)];
        swap_location = swap_find_free(k_swap_tracker);
        if (swap_location == 0)  panic("Swap location 0 was allocated");
        if (swap_location < 0)  return ENOMEM;
        KASSERT(cme->cme_vaddr != 0);
        pte->pte_zeroed = 0;
        KASSERT(pte->pte_ppn == ppn);
        KASSERT(pte->pte_padding == 0);
    }

    /* Write out the page */
    spinlock_release(&k_coremap->cm_lock);
    if (swap_write(ppn, swap_location, k_swap_tracker)) {
        panic("swap write failed");
    }
    spinlock_acquire(&k_coremap->cm_lock);

    /* Mark page as clean */
    if(cme->cme_dirty == 1) {
        k_coremap->cm_num_dirty--;
    }
    cme->cme_dirty = 0;
    cme->cme_swap_location = swap_location;

    /* Update tlb as clean */
    /* Shoot down tlb */
    if (cme->cme_tlb == 1) {
        struct tlbshootdown t;
        t.tlbs_cpu = cme->cme_owner_cpu;
        t.tlbs_vaddr = cme->cme_vaddr;
        t.tlbs_flush_all = false;
        vm_tlbshootdown(&t);
        while (cme->cme_tlb) {
            wchan_sleep(k_coremap->cm_tlb_wchan, &k_coremap->cm_lock);
        }
        KASSERT(cme->cme_tlb == 0);
    }
    
    return 0;
}
