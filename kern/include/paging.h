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

#ifndef _PAGING_H_
#define _PAGING_H_

#include <types.h>

/*
 * Paging-related definitions.
 */

#define USE_CLOCK_PAGING
//#define USE_LAST_CLEAN_PAGING


/* Page fault handler. Returns 0 on success. 
 * 
 * Assumes address space and coremap locks are held. 
 */
int page_fault(vaddr_t faultaddress);

/* Swaps a page into memory. Returns 0 */
int page_swapin(vaddr_t vaddress);

/* Page eviction handler. Returns a free ppn.
 *
 * It first looks for a free physical page; if it can't find one, 
 * it uses the page eviction algorithm to pick a page to evict,
 * and writes it to swap if it's dirty. 
 *
 * The returned ppn will always be marked as busy in the coremap.
 * 
 * Assumes address space and coremap locks are held.
 */
int page_get(unsigned from_page_fault);

/* Writes a physical page out to swap, and updates corresponding coremap entry.
 * If the page has no swap location, it first finds a free swap location for it.
 * Assumes busy bit is already set high.
 *
 * Returns 0 on success. Assumes address space and coremap locks are held.
 * These locks will be released when writing out the page.
 */
int page_write_out(int ppn);

#endif /* _PAGING_H_ */
