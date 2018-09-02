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
#include <addrspace.h>
#include <pagetable.h>
#include <vm.h>
#include <proc.h>
#include <limits.h>
#include <current.h>
#include <coremap.h>
#include <paging.h>
#include <clock.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	/*
	 * Initialize as needed.
	 */
    for (int i = 0; i < PD_SIZE; i++) {
        as->as_pd[i] = NULL;
    }
    as->as_heap_size = 0;
    as->as_heap_start = 0;
    as->as_lock = lock_create("as_lock");


	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{

	/* Make a new address space */
    struct addrspace *newas;
	newas = as_create();
	if (newas == NULL) {
		return ENOMEM;
	}

    /* Acquire necessary locks */
    lock_acquire(old->as_lock);

    /* Iterate through all page table directories */
    for (int pde_index = 0; pde_index < PD_SIZE; ++pde_index) {
        /* skip over kernel entries */
        if (pde_index == KERNEL_PT_START) {
            pde_index += KERNEL_PD_LEN - 1;
            continue;
        }

        /* Iterate through all PTEs */
        struct pgtable *pde = old->as_pd[pde_index];
        if (pde == NULL)  continue;
        for (int pte_index = 0; pte_index < PT_SIZE; ++pte_index) {
            
            /* Skip over invalid pages */
            struct pt_entry *pte = &(pde->pt_ptes[pte_index]);
            if (!pte->pte_valid)  continue;
            KASSERT(!pte->pte_present || pte->pte_ppn < k_coremap->cm_num_pages);
            int releaseppn = pte_acquire(old, pte);

            /* If the page is only in swap, bring it into memory */
            spinlock_acquire(&k_coremap->cm_lock);
            while (!pte->pte_present) {
                struct addrspace *prev_as = NULL;
                if (curproc->p_addrspace != old) {
                    as_deactivate();
                    prev_as = proc_setas(old);
                    as_activate();
                }
                int err = page_swapin(PDI_PTI_TO_VADDR(pde_index, pte_index));
                if (prev_as != NULL) {
                    as_deactivate();
                    proc_setas(prev_as);
                    as_activate();
                }
                if (err != 0) {
                    pte_release(old, pte, releaseppn);
                    spinlock_release(&k_coremap->cm_lock);
                    lock_release(old->as_lock);
                    return err;
                }
                
                releaseppn = pte_acquire(old, pte);
            }

            /* If the page is zeroed, skip everything */
            int new_ppn = -1;
            if (!pte->pte_zeroed) {
                /* Copy the page into an empty block in memory */
                new_ppn = page_get(0);
                KASSERT(new_ppn != 0);
                KASSERT(new_ppn != pte->pte_ppn);
                if (new_ppn == -1) {
                    pte_release(old, pte, releaseppn);
                    spinlock_release(&k_coremap->cm_lock);
                    lock_release(old->as_lock);
                    return ENOMEM;
                }
                KASSERT(new_ppn < k_coremap->cm_num_pages);
                k_coremap->cm_entries[new_ppn].cme_as = newas;
                k_coremap->cm_entries[new_ppn].cme_vaddr = 
                    PDI_PTI_TO_VADDR(pde_index, pte_index);
                k_coremap->cm_entries[new_ppn].cme_swap_location = 0;
                k_coremap->cm_entries[new_ppn].cme_owner_cpu = curcpu;
                k_coremap->cm_entries[new_ppn].cme_dirty = 0;
                k_coremap->cm_entries[new_ppn].cme_tlb = 0;
                k_coremap->cm_entries[new_ppn].cme_busy = 1;
                k_coremap->cm_entries[new_ppn].cme_kernel = 0;
                k_coremap->cm_entries[new_ppn].cme_kpage = 0;
                k_coremap->cm_entries[new_ppn].cme_exists = 1;
                spinlock_release(&k_coremap->cm_lock);
                KASSERT((void *)CM_INDEX_TO_KVADDR(new_ppn) != NULL); 
                memmove((void *)CM_INDEX_TO_KVADDR(new_ppn),
                    (const void *)CM_INDEX_TO_KVADDR(pte->pte_ppn),
                    PAGE_SIZE);
                spinlock_acquire(&k_coremap->cm_lock);
            }   
            spinlock_release(&k_coremap->cm_lock);
            
            /* Make a new pagetable if necessary */
            struct pgtable *new_pde = newas->as_pd[pde_index];
            if (new_pde == NULL) {
                new_pde = kmalloc(sizeof(struct pgtable));
                if (new_pde == NULL) {
                    pte_release(old, pte, releaseppn);
                    lock_release(old->as_lock);
                    return ENOMEM;
                }
                newas->as_pd[pde_index] = new_pde;
                pgt_init(new_pde);
            }

            /* Make a new PTE */
            KASSERT(new_ppn != 0);
            new_pde->pt_ptes[pte_index].pte_present = !pte->pte_zeroed;
            new_pde->pt_ptes[pte_index].pte_valid = 1;
            new_pde->pt_ptes[pte_index].pte_writeable = 1;
            new_pde->pt_ptes[pte_index].pte_ppn = 
                pte->pte_zeroed ? 0 : new_ppn;
            new_pde->pt_ptes[pte_index].pte_zeroed = pte->pte_zeroed;
            
            pte_release(old, pte, releaseppn);
        }

    }

    
    spinlock_acquire(&k_coremap->cm_lock);

    for (int i = 0; i < k_coremap->cm_num_pages; i++) {
        struct cm_entry *cme = &k_coremap->cm_entries[i];
        if (cme->cme_as == newas) {
            cme->cme_busy = 0;
        }
    }
    
    wchan_wakeall(k_coremap->cm_wchan, &k_coremap->cm_lock);
    
    spinlock_release(&k_coremap->cm_lock);

    /* We're done! */
    newas->as_heap_size = old->as_heap_size;
    newas->as_heap_start = old->as_heap_start;
    lock_release(old->as_lock);
	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	/*
	 * Clean up as needed.
	 */

    lock_acquire(as->as_lock);

    /* clean up page directories */
    for (int i = 0; i < PD_SIZE; i++) {
        if (as->as_pd[i] != NULL) {
            /* clean up page table */
            pgt_destroy(as->as_pd[i], as);
        }
    }
    
    lock_release(as->as_lock);
    lock_destroy(as->as_lock);

	kfree(as);
}

void
as_activate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	/*
	 * Write this.
	 */
    /* shoot down the current tlb */
    struct tlbshootdown tlbs;
    tlbs.tlbs_cpu = curcpu;
    tlbs.tlbs_flush_all = true;
    vm_tlbshootdown(&tlbs);
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		 int readable, int writeable, int executable)
{
	/* Abby is sad that there are no comments here */
    (void) readable;
    (void) executable;
    writeable = (writeable == 0) ? 0 : 1;
    int pde;
    int pti;
    struct pgtable *pgtable;
    struct pt_entry *pte;
    vaddr = PAGE_ALIGN(vaddr);
    if (as == NULL)  return EFAULT;
    if (memsize <= 0)  return EINVAL;
    if ((vaddr >= KERNEL_VADDR_START && vaddr < KERNEL_VADDR_END) ||
            (vaddr+memsize >= KERNEL_VADDR_START &&
             vaddr + memsize < KERNEL_VADDR_END) ||
            (vaddr < KERNEL_VADDR_START && vaddr+memsize > KERNEL_VADDR_START)) {
        return EINVAL;
    }
    lock_acquire(as->as_lock);
    
    size_t mem_defined = 0;
    while (mem_defined < memsize) {
        pde = VADDR_TO_PT(vaddr+mem_defined);
        pti = VADDR_TO_PTE(vaddr+mem_defined);
        pgtable = as->as_pd[pde];
        if (pgtable == NULL) {
            pgtable = kmalloc(sizeof(struct pgtable));
            if (pgtable == NULL) {
                lock_release(as->as_lock);
                return ENOMEM;
            }
            pgt_init(pgtable);
            as->as_pd[pde] = pgtable;
        }
        pte = &(pgtable->pt_ptes[pti]);
        pte_acquire(as, pte);
        if (pte->pte_valid == 0) {
            pte->pte_valid = 1;
            pte->pte_present = 0;
            pte->pte_zeroed = 1;
            pte->pte_writeable = writeable;
            pte->pte_ppn = 0;
        } else if (pte->pte_writeable != writeable) {
            pte->pte_writeable = writeable;
            struct cm_entry *cme = &k_coremap->cm_entries[pte->pte_ppn];
            if (cme->cme_tlb == 1) {
                struct tlbshootdown tlbs;
                tlbs.tlbs_cpu = cme->cme_owner_cpu;
                tlbs.tlbs_flush_all = false;
                tlbs.tlbs_vaddr = cme->cme_vaddr;
                vm_tlbshootdown(&tlbs);
                while (cme->cme_tlb) {
                    wchan_sleep(k_coremap->cm_wchan, &k_coremap->cm_lock);
                }
                KASSERT(k_coremap->cm_entries[pte->pte_ppn].cme_tlb == 0);
            }
        }
        int ppn = (pte->pte_present == 1) ? pte->pte_ppn : -1;
        pte_release(as, pte, ppn);
        mem_defined += PAGE_SIZE;
    }

    if(as->as_heap_start < vaddr + mem_defined) {
        as->as_heap_start = vaddr + mem_defined;
    }
    KASSERT(as->as_heap_start % PAGE_SIZE == 0);

    lock_release(as->as_lock);
    return 0;

}

int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */

	(void)as;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}


void
as_zero_region(vaddr_t vaddr, unsigned npages)
{
	bzero((void *)vaddr, npages * PAGE_SIZE);
}

int
pte_acquire(struct addrspace *as, struct pt_entry *pte) {
    KASSERT(lock_do_i_hold(as->as_lock));
    int retval = -1;
    if (pte->pte_present == 1) {
        unsigned acquired = 0;
        if (!spinlock_do_i_hold(&k_coremap->cm_lock)) {
            spinlock_acquire(&k_coremap->cm_lock);
            acquired = 1;
        }
        KASSERT(pte->pte_ppn >= 0 && pte->pte_ppn < k_coremap->cm_num_pages);
        struct cm_entry *cme = &k_coremap->cm_entries[pte->pte_ppn];
        KASSERT(cme != NULL);
        KASSERT(cme->cme_kpage == 0);
        while (cme->cme_busy && pte->pte_present) {
            wchan_sleep(k_coremap->cm_wchan, &k_coremap->cm_lock);
        }
        /* This assumes single-threaded processes */
        if (cme->cme_as != as || !pte->pte_present) {
            if (acquired == 1) {
                spinlock_release(&k_coremap->cm_lock);
            }
            return -1;
        }

        cme->cme_busy = 1;
        retval = pte->pte_ppn;
        if (acquired == 1) {
            spinlock_release(&k_coremap->cm_lock);
        }
    }
    return retval;
}

void
pte_release(struct addrspace *as, struct pt_entry *pte, int ppn) {
    (void) pte;
    KASSERT(lock_do_i_hold(as->as_lock));
    if (ppn >= 0) {
        unsigned acquired = 0;
        if (!spinlock_do_i_hold(&k_coremap->cm_lock)) {
            spinlock_acquire(&k_coremap->cm_lock);
            acquired = 1;
        }
        struct cm_entry *cme = &k_coremap->cm_entries[ppn];
        KASSERT(cme != NULL);
        KASSERT(cme->cme_kpage == 0);
        cme->cme_busy = 0;
        wchan_wakeall(k_coremap->cm_wchan, &k_coremap->cm_lock);
        if (acquired == 1) {
            spinlock_release(&k_coremap->cm_lock);
        }
    }
}
