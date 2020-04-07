/**
 * \file
 * \brief Barrelfish paging helpers.
 */

/*
 * Copyright (c) 2012, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef LIBBARRELFISH_PAGING_H
#define LIBBARRELFISH_PAGING_H

#include <aos/capabilities.h>
#include <aos/paging_types.h>
#include <aos/slab.h>
#include <aos/slot_alloc.h>
#include <barrelfish_kpi/paging_arch.h>
#include <errors/errno.h>

// This must not be removed, as the function interface origins from the paging module.
#include <aos/paging_region.h>

struct paging_state;
struct paging_region;

#define THREAD_UNRECOVERABLE_PAGEFAULT_CODE (-1)

struct thread;
errval_t paging_init_state(
    struct paging_state *st,
    lvaddr_t start_vaddr,
    struct capref cap_l0,
    struct slot_allocator *ca
);

errval_t paging_init_state_foreign(
    struct paging_state *st,
    lvaddr_t start_vaddr,
    struct capref cap_l0,
    struct slot_allocator *ca
);

/// initialize self-paging module
errval_t paging_init(
    void
);

void paging_init_onthread(
    struct thread *t
);

/**
 * \brief Find a bit of free virtual address space that is large enough to
 *        accomodate a buffer of size `bytes`.
 */
errval_t paging_alloc(
    struct paging_state *st,
    void **buf,
    size_t bytes,
    size_t alignment
);

/**
 * Functions to map a user provided frame.
 */
/// Map user provided frame with given flags while allocating VA space for it
errval_t paging_map_frame_attr(
    struct paging_state *st,
    void **buf,
    size_t bytes,
    struct capref frame,
    int flags,
    void *arg1,
    void *arg2
);

/// Map user provided frame at user provided VA with given flags.
errval_t paging_map_fixed_attr(
    struct paging_state *st,
    lvaddr_t vaddr,
    struct capref frame,
    size_t bytes,
    int flags
);

/**
 * refill slab allocator without causing a page fault
 */
errval_t slab_refill_no_pagefault(
    struct slab_allocator *slabs,
    struct capref frame,
    size_t minbytes
);

/**
 * \brief unmap region starting at address `region`.
 * NOTE: this function is currently here to make libbarrelfish compile. As
 * noted on paging_region_unmap we ignore unmap requests right now.
 */
errval_t paging_unmap(
    struct paging_state *st,
    const void *region
);

/// Map user provided frame while allocating VA space for it
static inline errval_t paging_map_frame(
    struct paging_state *st,
    void **buf,
    size_t bytes,
    struct capref frame,
    void *arg1,
    void *arg2
)
{
    return paging_map_frame_attr(st, buf, bytes, frame, VREGION_FLAGS_READ_WRITE, arg1, arg2
);
}

/// Map complete user provided frame while allocating VA space for it
static inline errval_t paging_map_frame_complete(
    struct paging_state *st,
    void **buf,
    struct capref frame,
    void *arg1,
    void *arg2
)
{
    errval_t err;

    struct frame_identity id;
    err = frame_identify(frame, &id);
    if (err_is_fail(err)) {
        return err;
    }

    return paging_map_frame_attr(st, buf, id.bytes, frame, VREGION_FLAGS_READ_WRITE, arg1, arg2);
}

/// Map user provided frame at user provided VA.
static inline errval_t paging_map_fixed(
    struct paging_state *st,
    lvaddr_t vaddr,
    struct capref frame,
    size_t bytes
)
{
    return paging_map_fixed_attr(st, vaddr, frame, bytes, VREGION_FLAGS_READ_WRITE);
}

static inline lvaddr_t paging_genvaddr_to_lvaddr(
    genvaddr_t genvaddr
)
{
    return (lvaddr_t) genvaddr;
}

#endif // LIBBARRELFISH_PAGING_H
