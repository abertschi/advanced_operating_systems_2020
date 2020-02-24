/**
 * \file
 * \brief Userlevel capability predicates (to be generated by Hamlet).
 */

/*
 * Copyright (c) 2009, 2010, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef CAP_PREDICATES_H
#define CAP_PREDICATES_H

#include <sys/cdefs.h>

#include <aos/debug.h>

__BEGIN_DECLS

struct capability;
enum objtype;

bool is_ancestor(struct capability *, struct capability *);
bool is_copy(struct capability *, struct capability *);
bool is_well_founded(enum objtype, enum objtype);
bool is_equal_type(enum objtype, enum objtype);
int8_t compare_caps(struct capability *, struct capability *, bool);
genpaddr_t get_address(struct capability *);
gensize_t get_size(struct capability *);
uint8_t get_type_root(enum objtype);

//#ifndef IN_KERNEL
// XXX: Hack to enable cap_predicates.c to build in userland
static inline lpaddr_t mem_to_local_phys(lvaddr_t addr)
{
    USER_PANIC("NYI");
    return 0;
}

//#endif 

__END_DECLS

#endif

