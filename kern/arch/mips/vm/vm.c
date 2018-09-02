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
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <pagetable.h>
#include <vm.h>
#include <coremap.h>
#include <wchan.h>
#include <paging.h>
#include <kern/signal.h>
#include <syscall.h>
#include <swap.h>
#include <vmstats.h>

/* Kernel structures */
struct coremap *k_coremap;
struct swap_tracker *k_swap_tracker;
struct vmstats k_vmstats;

/* False until swap is initialized */
unsigned k_can_swap;

void
vm_bootstrap(void)
{
    k_can_swap = 0;
    vmstats_init(&k_vmstats);

    /* initialize core map */
    paddr_t lastpaddr = ram_getsize();

    /* calculate how much memory we need for kernel structures */
    size_t total_size = sizeof(struct coremap);

    int num_pages = (int) total_size / PAGE_SIZE;
    if (sizeof(struct coremap) % PAGE_SIZE != 0)  num_pages++;
    paddr_t paddr = ram_stealmem(num_pages);
    k_coremap = (struct coremap *)PADDR_TO_KVADDR(paddr);
    k_coremap->cm_num_kpages = 0;
    k_coremap->cm_num_dirty = 0;
    k_coremap->cm_clock_head = 0;
    spinlock_init(&k_coremap->cm_lock);
    paddr_t first_free = ram_getfirstfree();
    /* mark stolen kernel pages */
    int first_free_index = PADDR_TO_CM_INDEX(first_free);
    for (int i = 0; i < first_free_index; i++) {
        struct cm_entry *cme = &k_coremap->cm_entries[i];
        cme->cme_as = NULL;
        cme->cme_vaddr = (vaddr_t) CM_INDEX_TO_KVADDR(i);
        cme->cme_swap_location = 0;
        cme->cme_owner_cpu = NULL;
        cme->cme_dirty = 0;
        cme->cme_tlb = 0;
        cme->cme_busy = 0;
        cme->cme_kernel = (i == 0) ? 0 : 1;
        cme->cme_kpage = 1;
        cme->cme_exists = 1;
        k_coremap->cm_num_kpages++;
    }
    /* mark existing pages */
    int last_existing_index = PADDR_TO_CM_INDEX(lastpaddr);
    for (int i = first_free_index; i < last_existing_index; i++) {
        struct cm_entry *cme = &k_coremap->cm_entries[i];
        cme->cme_as = NULL;
        cme->cme_vaddr = 0;
        cme->cme_swap_location = 0;
        cme->cme_owner_cpu = NULL;
        cme->cme_dirty = 0;
        cme->cme_tlb = 0;
        cme->cme_busy = 0;
        cme->cme_kernel = 0;
        cme->cme_kpage = 0;
        cme->cme_exists = 1;
    }
    /* mark other pages */
    for (int i = last_existing_index; i < RAM_PAGES; i++) {
        struct cm_entry *cme= &k_coremap->cm_entries[i];
        cme->cme_as = NULL;
        cme->cme_vaddr = 0;
        cme->cme_swap_location = 0;
        cme->cme_owner_cpu = NULL;
        cme->cme_dirty = 0;
        cme->cme_tlb = 0;
        cme->cme_busy = 0;
        cme->cme_kernel = 0;
        cme->cme_kpage = 0;
        cme->cme_exists = 0;
    }

    k_coremap->cm_num_pages = last_existing_index;
    if (k_coremap->cm_num_pages - k_coremap->cm_num_kpages < MIN_USER_PAGES) {
        panic("kernel takes too much memory");
    }
    
    /* at this point kmalloc should work, so we can initialize
     * the kernel coremap's wchan
     */
    k_coremap->cm_wchan = wchan_create("kernel_wchan");
    k_coremap->cm_tlb_wchan = wchan_create("kernel_tlb_wchan");
    if (k_coremap->cm_wchan == NULL || k_coremap->cm_tlb_wchan == NULL) {
        panic("out of memory while booting up");
    }
    
}

int
vm_fault(int faulttype, vaddr_t faultaddress) {
    
    k_vmstats.vms_vm_faults++;

    /* Catch early or erroneous vm_fault calls */
    if (curproc == NULL || curproc->p_addrspace == NULL || 
        curproc->p_addrspace->as_pd == NULL) {
        panic("vm_fault called in bootup");
    }
    if (faulttype != VM_FAULT_READ && faulttype != VM_FAULT_WRITE 
        && faulttype != VM_FAULT_READONLY) {
        return EINVAL;
    }

    /* Find the PTE */
    faultaddress = PAGE_ALIGN(faultaddress);
    struct addrspace *as = curproc->p_addrspace;
    KASSERT(as != NULL);
    lock_acquire(as->as_lock);
    if(faultaddress <= STACK_MIN &&
     faultaddress >= as->as_heap_start + as->as_heap_size) {
        lock_release(as->as_lock);
        return EFAULT;
    }
    int pdi = VADDR_TO_PT(faultaddress);
    struct pgtable *pde = as->as_pd[pdi];
    if (pde == NULL) {
        if (!(faultaddress > STACK_MIN && faultaddress <= STACK_MAX)) {
            lock_release(as->as_lock);
            return EFAULT;
        }
        pde = kmalloc(sizeof(struct pgtable));
        if (pde == NULL) {
            panic("EOM in vm_fault");
        }
        pgt_init(pde);
        as->as_pd[VADDR_TO_PT(faultaddress)] = pde;
    }
    struct pt_entry *pte = &(pde->pt_ptes[VADDR_TO_PTE(faultaddress)]);
    
    /* YAY synchronization */
    spinlock_acquire(&k_coremap->cm_lock);
    int release_ppn = pte_acquire(as, pte);    
    
    /* If the page is in swap or was never allocated, raise a page fault */
    if (!pte->pte_present || 
        (!pte->pte_valid && faultaddress > STACK_MIN && faultaddress < STACK_MAX)
        || pte->pte_zeroed) {
        int res = page_fault(faultaddress);
        if (res) {
            pte_release(as, pte, release_ppn);
            lock_release(as->as_lock);
            spinlock_release(&k_coremap->cm_lock);
            return res;
        } 
    }
    int spl = splhigh();
    KASSERT(pte->pte_present);
    KASSERT(pte->pte_ppn < k_coremap->cm_num_pages);
    int ppn = pte->pte_ppn;
    struct cm_entry *cme = &k_coremap->cm_entries[ppn];
    cme->cme_busy = 1;
    KASSERT(k_coremap->cm_entries[ppn].cme_as == as);
    KASSERT(k_coremap->cm_entries[ppn].cme_vaddr == faultaddress);

    /* Handle READ, WRITE, and READONLY faults */
    if ((faulttype == VM_FAULT_WRITE || faulttype == VM_FAULT_READONLY)
        && pte->pte_writeable != 1) {
        pte_release(as, pte, release_ppn);
        lock_release(as->as_lock);
        spinlock_release(&k_coremap->cm_lock);
        splx(spl);
        return EFAULT;
    }
    int index = tlb_probe(faultaddress, 0);
    if (faulttype == VM_FAULT_READ || faulttype == VM_FAULT_WRITE) {
        KASSERT(index < 0);
    }
    uint32_t entryhi, entrylo;

    /* Pick a random place in the TLB, evicting another TLB entry sometimes */
    if (index < 0) {
        index = random() % NUM_TLB;
        uint32_t entryhi, entrylo;
        tlb_read(&entryhi, &entrylo, index);
        if ((entrylo & TLBLO_VALID) == TLBLO_VALID) {
            k_coremap->cm_entries[PADDR_TO_CM_INDEX(entrylo)].cme_tlb = 0;
            wchan_wakeall(k_coremap->cm_tlb_wchan, &k_coremap->cm_lock);
        }
    }
    entryhi = faultaddress;
    entrylo = CM_INDEX_TO_PADDR(ppn) | TLBLO_VALID;
    if (faulttype == VM_FAULT_WRITE || faulttype == VM_FAULT_READONLY) {
        if (cme->cme_dirty == 0) {
            cme->cme_dirty = 1;
            k_coremap->cm_num_dirty++;
        }
        entrylo |= TLBLO_DIRTY;
    }
    tlb_write(entryhi, entrylo, index);
    cme->cme_tlb = 1;

    /* Clean up */
    KASSERT(pte->pte_padding == 0);
    cme->cme_busy = 0;
    wchan_wakeall(k_coremap->cm_wchan, &k_coremap->cm_lock);
    pte_release(as, pte, release_ppn);
    spinlock_release(&k_coremap->cm_lock);
    lock_release(as->as_lock);
    splx(spl);
    return 0;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(unsigned npages)
{
    spinlock_acquire(&k_coremap->cm_lock);

    if (k_coremap->cm_num_pages - (k_coremap->cm_num_kpages + npages) < 
        MIN_USER_PAGES) {
        spinlock_release(&k_coremap->cm_lock);
        return 0;
    }

    struct cm_entry *cme;

    int start_of_block = -1;
    int cur_block = 0;

    for (int num_tries = 0; num_tries < NUM_TRIES; ++num_tries) {
        /* look for a block of free pages */
        cme = &k_coremap->cm_entries[0];
        uint32_t pages_found = 0;
        while(cme->cme_exists == 1) {
            if (cme->cme_kpage == 0 && cme->cme_as == NULL && !cme->cme_busy) {
                if (pages_found == 0)  start_of_block = cur_block;
                pages_found++;
                if (pages_found == npages)  break;
            } else {
                pages_found = 0;
                start_of_block = -1;
            }
            cur_block++;
            if (cur_block >= RAM_PAGES)  break;
            cme = &k_coremap->cm_entries[cur_block];
        }
        /* check if block has enough pages */
        if (pages_found == npages)  break;

        /* couldn't find enough pages; use page_get to free user pages */
        start_of_block = -1;
        cur_block = 0;
        /* expedite single-page allocations */
        if (npages == 1) {
            start_of_block = page_get(0);
            if (start_of_block >= 0)  break;
        }
        /* evict some random user pages and try again */
        else {
            for (unsigned evicts = 0; evicts < npages - pages_found; ++evicts) {
                int new_ppn = page_get(0);
                if (new_ppn >= 0) {
                    k_coremap->cm_entries[new_ppn].cme_busy = 0;
                }
            }
        }
    }
    if (start_of_block == -1) {
        spinlock_release(&k_coremap->cm_lock);
        return 0;
    }

    for (uint32_t i = 0; i < npages; i++) {
        cme = &k_coremap->cm_entries[start_of_block+i];
        cme->cme_as = NULL;
        cme->cme_vaddr = CM_INDEX_TO_KVADDR(start_of_block+i);
        cme->cme_swap_location = 0;
        cme->cme_owner_cpu = NULL;
        cme->cme_dirty = 0;
        cme->cme_tlb = 0;
        cme->cme_kernel = (i == 0) ? 0 : 1;
        cme->cme_busy = 0;
        cme->cme_kpage = 1; 
    }

    as_zero_region(CM_INDEX_TO_KVADDR(start_of_block), npages);

    k_coremap->cm_num_kpages += npages;
    spinlock_release(&k_coremap->cm_lock);

	return CM_INDEX_TO_KVADDR(start_of_block);
}

void
free_kpages(vaddr_t addr)
{
    
    KASSERT(addr >= KERNEL_VADDR_START && addr < KERNEL_VADDR_END);

    spinlock_acquire(&k_coremap->cm_lock);

    int ppn = KVADDR_TO_PPN(addr);
    struct cm_entry* cme;
    cme = &k_coremap->cm_entries[ppn];
    KASSERT(cme->cme_busy == 0);
    KASSERT(cme->cme_kpage == 1);
    KASSERT(cme->cme_kernel == 0);
    KASSERT(cme->cme_as == NULL);
    cme->cme_kpage = 0;
    k_coremap->cm_num_kpages--;
    ppn++;
    cme = &k_coremap->cm_entries[ppn]; 
    while(cme->cme_kernel == 1 && cme->cme_kpage == 1) {
        KASSERT(cme->cme_busy == 0);
        KASSERT(cme->cme_as == NULL);
        cme->cme_kpage = 0;
        cme->cme_kernel = 0;
        ppn++;
        cme = &k_coremap->cm_entries[ppn];
        k_coremap->cm_num_kpages--;
    }

    spinlock_release(&k_coremap->cm_lock);
}

void vm_tlbshootdown(const struct tlbshootdown *t) {
    /* If this isn't the target cpu, send this over to the target cpu */
    if (t->tlbs_cpu != curcpu) {
        ipi_tlbshootdown(t->tlbs_cpu, t);
    }

    /* This is the target cpu */
    else {
        k_vmstats.vms_tlb_shootdowns++;
        /* Flush all entries */
        unsigned acquired = 0;
        if (!spinlock_do_i_hold(&k_coremap->cm_lock)) {
            spinlock_acquire(&k_coremap->cm_lock);
            acquired = 1;
        }
        int spl = splhigh();
        if (t->tlbs_flush_all) {
            for (int i = 0; i < NUM_TLB; ++i) {
                uint32_t entryhi, entrylo;
                tlb_read(&entryhi, &entrylo, i);
                tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
                /* Update coremap */
                KASSERT(TLBLO_TO_PPAGE(entrylo) < (unsigned)k_coremap->cm_num_pages);
                k_coremap->cm_entries[TLBLO_TO_PPAGE(entrylo)].cme_tlb = 0;
            }
        }
        /* Flush specified entry */
        else {
            int index = tlb_probe((int)t->tlbs_vaddr, 0);
            if (index < 0)  goto cleanup;
            uint32_t entryhi, entrylo;
            tlb_read(&entryhi, &entrylo, index);
            KASSERT(k_coremap->cm_entries[TLBLO_TO_PPAGE(entrylo)].cme_tlb == 1);
            tlb_write(TLBHI_INVALID(index), TLBLO_INVALID(), index);
            /* Update coremap */
            KASSERT(TLBLO_TO_PPAGE(entrylo) < (unsigned)k_coremap->cm_num_pages);
            k_coremap->cm_entries[TLBLO_TO_PPAGE(entrylo)].cme_tlb = 0;
        }
        wchan_wakeall(k_coremap->cm_tlb_wchan, &k_coremap->cm_lock);

cleanup:
        if (acquired == 1) {
            spinlock_release(&k_coremap->cm_lock);
        }
        splx(spl);
    }
} 