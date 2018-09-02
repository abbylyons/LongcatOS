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
#include <copyinout.h>
#include <syscall.h>
#include <limits.h>
#include <vfs.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <addrspace.h>

/*
 *  linked list node for keeping track of pointers to args
 */
struct a_node {
    struct a_node *an_next;
    char *an_addr;
};

static
void
free_nodes(struct a_node *head)
{
    while (head != NULL) {
        struct a_node *next = head->an_next;
        kfree(head);
        head = next;
    }
}

static
void
switch_as(struct addrspace *as)
{
    as_deactivate();
    proc_setas(as);
    as_activate();
}

/*
 *  function which handles most of the execv syscall.
 *  It takes care of creating a new address space,
 *  copying in the argument vector and finally
 *  switching into the new process
 */

int
sys_execv(const_userptr_t program, userptr_t args) {

    
    /* acquire a copy buffer */
    char* argbuf = cb_acquire(k_proctable->pt_cb);
    struct addrspace *new_as, *old_as;
    struct vnode *v;
    vaddr_t entrypoint, stackptr;
    int result, argc;
    size_t copied = 0;
    size_t gotIn, gotOut;
    struct a_node *arg_list_head = NULL;

    if ((void *)program == NULL) {
        cb_release(k_proctable->pt_cb);
        return EFAULT;
    }

    /* copy in the program name */
    result = copyinstr(program, argbuf, PATH_MAX, &gotIn);
    if (result) {
        cb_release(k_proctable->pt_cb);
        return result;
    }
    /* Open the file. */
    result = vfs_open(argbuf, O_RDONLY, 0, &v);
    if (result) {
        cb_release(k_proctable->pt_cb);
        return result;
    }

    /* We have to have come from an existing process */
    KASSERT(proc_getas() != NULL); 
    
    /* count argc */
    argc = 0;

    while(true) {

        struct a_node *new_node = kmalloc(sizeof(struct a_node));
        if (new_node == NULL) {
            vfs_close(v);
            free_nodes(arg_list_head);
            cb_release(k_proctable->pt_cb);
            return ENOMEM;
        }
        result = copyin(args + argc*sizeof(char *),
                (void *) &new_node->an_addr, sizeof (char *)); 
        if (result) {
            vfs_close(v);
            free_nodes(arg_list_head);
            cb_release(k_proctable->pt_cb);
            return result;
        }
        if (new_node->an_addr == NULL) {
            kfree(new_node);
            break;
        }
        new_node->an_next = arg_list_head;
        arg_list_head = new_node;
        argc++;
    }

    /* Create a new address space. */
    new_as = as_create();
    if (new_as == NULL) {
        free_nodes(arg_list_head);
        vfs_close(v);
        cb_release(k_proctable->pt_cb);
        return ENOMEM;
	}

    /* Switch to it and activate it. */
    as_deactivate();
    old_as = proc_setas(new_as);
    as_activate();

    /* Load the executable. */
    result = load_elf(v, &entrypoint);
    
    /* Done with the file now. */
    vfs_close(v);
    if (result) {
        switch_as(old_as);
        free_nodes(arg_list_head); 
        cb_release(k_proctable->pt_cb);
        /* p_addrspace will go away when curproc is destroyed */
        return result;
    }

    /* Define the user stack in the address space */
    result = as_define_stack(new_as, &stackptr);
    if (result) {
        as_destroy(new_as);
        switch_as(old_as);
        cb_release(k_proctable->pt_cb);
        return result;
    }

    /* buffer for keeping track of copied arguments */
    char **argptrs = kmalloc(sizeof(char *) * argc);

    /* length of current argument */
    size_t padding;

    /* swap to old address space */
    as_deactivate();
    proc_setas(old_as);
    as_activate();
    
    /* start copying arguments */
    for (int i = argc-1; i >= 0; i--) {
        
        /* Copy the argument over */
        result = copyinstr((const_userptr_t) arg_list_head->an_addr, argbuf, ARG_MAX, &gotIn);
        if (result) {
            as_destroy(new_as);
            free_nodes(arg_list_head);
            cb_release(k_proctable->pt_cb);
            return result;
        }

        struct a_node *tmp = arg_list_head;
        arg_list_head = arg_list_head->an_next;
        kfree(tmp);

        /* calculate padding amount to guarantee 4 byte alignment */
        padding = (4 - gotIn % 4) % 4;
        copied += gotIn;
        if (copied > ARG_MAX) {
            as_destroy(new_as);
            free_nodes(arg_list_head);
            cb_release(k_proctable->pt_cb);
            return E2BIG;
        }

        /* swap back to the new address space */
        as_deactivate();
        proc_setas(new_as);
        as_activate();

        /* Copy the chunk into the new address space */
        result = copyoutstr((const char *) argbuf, (userptr_t) stackptr - gotIn - padding, gotIn, &gotOut);
        if (result) {
            switch_as(old_as);
            as_destroy(new_as);
            free_nodes(arg_list_head);
            cb_release(k_proctable->pt_cb);
            return result;
        }

        KASSERT(gotIn == gotOut);

        /* swap back to the old address space */
        as_deactivate();
        proc_setas(old_as);
        as_activate();

        /* move the stackpointer */
        stackptr-= (gotIn + padding);

        /* make sure alignment is right */
        KASSERT(stackptr % 4 == 0);

        /* keep track of the location of the argument */
        argptrs[i] = (char *)stackptr;

        
    }
    
    /* release the copy buffer */
    cb_release(k_proctable->pt_cb);

    /* Make sure all pointers got freed */
    KASSERT(arg_list_head == NULL);

    /* swap to new address space */
    as_deactivate();
    proc_setas(new_as);
    as_activate();

    /* insert 4 bytes of padding */
    stackptr -= 4;
    char null = '\0';
    for (unsigned int j = 0; j < 4; j++) {
        result = copyout((const char *) &null, (userptr_t) stackptr + j, 1);
        if (result) {
            switch_as(old_as);
            as_destroy(new_as);
            kfree(argptrs);
            return result;
        }
    }

    /* copy in the pointers to the arguments */
    for (int i = argc-1; i >= 0; i--) {

        /* move the stack pointer down*/
        stackptr -= sizeof(char *);

        /* copy the pointer */
        result = copyout((const void *) &argptrs[i],
                (userptr_t) stackptr, sizeof(char *));
        if (result) {
            switch_as(old_as); 
            as_destroy(new_as);
            kfree(argptrs);
            return result;
        }
    }

    kfree(argptrs);

    /* destroy the old address space */
    as_destroy(old_as);

    /* Warp to user mode. */
    enter_new_process(argc, (userptr_t) stackptr, NULL,
                      stackptr, entrypoint);

    /* enter_new_process does not return. */
    panic("execv returned\n");
    return EINVAL;
}
