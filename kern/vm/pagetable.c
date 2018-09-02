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

#include <pagetable.h>
#include <coremap.h>
#include <lib.h>
#include <limits.h>
#include <swap.h>
#include <cpu.h>
#include <current.h>

/*
 * invalidates all page table entries to initialize the pgtable
 */
void
pgt_init(struct pgtable *pgt)
{
    for(int i = 0; i < PT_SIZE; i++) {
        pgt->pt_ptes[i].pte_valid = 0;
        pgt->pt_ptes[i].pte_padding = 0;
    }
}

/*
 *  cleans up page table
 */
void
pgt_destroy(struct pgtable *pgt, struct addrspace *as)
{
    struct cm_entry *cme;
    for (int i = 0; i < PT_SIZE; i++) {
        if (pgt->pt_ptes[i].pte_valid == 1) {
            if (pgt->pt_ptes[i].pte_present == 1) {
                int ret = pte_acquire(as, &pgt->pt_ptes[i]);
                if (ret == -1) {
                    KASSERT(!pgt->pt_ptes[i].pte_present);
                    if (pgt->pt_ptes[i].pte_ppn > 0) {
                        swap_destroy_block(pgt->pt_ptes[i].pte_ppn, k_swap_tracker);
                    }
                    continue;
                }
                spinlock_acquire(&k_coremap->cm_lock);
                cme = &(k_coremap->cm_entries[pgt->pt_ptes[i].pte_ppn]);
                if (cme->cme_dirty == 1) {
                    k_coremap->cm_num_dirty--;
                    cme->cme_dirty = 0;
                }
                KASSERT(cme->cme_kernel == 0);
                
                cme->cme_tlb = 0;
                cme->cme_vaddr = 0;
                cme->cme_as = NULL;

                off_t swap_location = cme->cme_swap_location;
                spinlock_release(&k_coremap->cm_lock);
                pte_release(as, &pgt->pt_ptes[i], ret);
                if (swap_location > 0) {
                    swap_destroy_block(swap_location, k_swap_tracker);
                }
            } else {
                if (pgt->pt_ptes[i].pte_ppn > 0) {
                    swap_destroy_block(pgt->pt_ptes[i].pte_ppn, k_swap_tracker);
                }
            }
        }
    }
    kfree(pgt);
}
