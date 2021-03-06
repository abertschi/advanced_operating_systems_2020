/**
 * \file
 * \brief Hello world application
 */

/*
 * Copyright (c) 2016 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, CAB F.78, Universitaetstr. 6, CH-8092 Zurich,
 * Attn: Systems Group.
 */

#include <stdio.h>

#include <aos/debug.h>
#include <grading.h>

int main(int argc, char *argv[])
{
    grading_setup_noninit(&argc, &argv);

    debug_printf("Hello, world! from userspace\n");

    for (int i = 0; i < argc; i++) {
        debug_printf("argv[%d]='%s'\n", i, argv[i]);
    }

    return EXIT_SUCCESS;
}
