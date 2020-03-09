/**
 * \file
 * \brief A library for managing physical memory (i.e., caps)
 */

#include <mm/mm.h>
#include <aos/debug.h>
#include <aos/solution.h>


/** slab refill function for slab allocator managed by mm.c */
static inline
errval_t mm_slab_refill_func(struct slab_allocator *slabs) {
    DEBUG_BEGIN;
    DEBUG_END;
    return LIB_ERR_NOT_IMPLEMENTED;
}


/**
 * Init memory manager
 * @param instance
 * @param objtype what kind of capability mm allocates
 * @param slab_refill_func slab provides mapped memory (think malloc),
 *        needs to be refilled periodically not to run out of mem (see mm#slab_allocator)
 *
 * @param slot_alloc_func alloc slots in a capability (slot_alloc_prealloc)
 * @param slot_refill_func function to create new cnode (l2c)
 *        if no more slots available for new capabilities
 * @param slot_alloc_inst instance to slot allocator
 *
 *
 * err = mm_init(&aos_mm, ObjType_RAM, NULL,
 *                  slot_alloc_prealloc, slot_prealloc_refill,
 *                  &init_slot_alloc);
 *
 * @return
 */
errval_t mm_init(struct mm *mm, enum objtype objtype,
                 slab_refill_func_t slab_refill_func,
                 slot_alloc_t slot_alloc_func,
                 slot_refill_t slot_refill_func,
                 void *slot_alloc_inst) {
    DEBUG_BEGIN;
    mm->slot_refill = slot_refill_func;
    mm->slot_alloc_inst = slot_alloc_inst; // TODO deref and set mm instance
    mm->slot_refill = slot_refill_func;
    mm->slot_alloc = slot_alloc_func;
    mm->objtype = objtype;

    if (slab_refill_func == NULL) {
        slab_refill_func = mm_slab_refill_func;
    }
    uint64_t blocksize = sizeof(struct mmnode);
    DEBUG_PRINTF("set blocksize for slab in mm %d bytes\n")

    slab_init(&mm->slabs, blocksize, slab_refill_func);
    mm->slabs.refill_func = slab_refill_func;

    DEBUG_END;
    return SYS_ERR_OK;
}

void mm_destroy(struct mm *mm) {
    DEBUG_BEGIN;
    assert(!"NYI");
    DEBUG_END;
}


static inline
errval_t create_node_without_capinfo(struct mm *mm,
                                     enum nodetype type, genpaddr_t base, size_t size,
                                     struct mmnode **res) {
    assert(sizeof(struct mmnode) >= mm->slabs.blocksize);

    // TODO-BEAN: implement slab_refill function
    struct mmnode *node = *res;
    node = slab_alloc(&mm->slabs);

    if (node == NULL) {
        DEBUG_PRINTF("failed to alloc a new slab block. no memory from slab\n");
        return LIB_ERR_SLAB_ALLOC_FAIL;
    }
    node->type = type;
    node->size = size;
    node->base = base;
    node->next = NULL;
    node->prev = NULL;
    node->capinfo = (struct capinfo) {
            .origin = NULL,
            .base = 0,
            .size = 0,
            .cap = {},
    };

    return SYS_ERR_OK;
}

/**
 * Add memory capabilities to mm (assume: cap not already added)
 *
 * @param mm this ref
 * @param cap capability to the ram region
 * @param base base address of the ram region
 * @param size size of the ram region
 *
 * @return
 */
errval_t mm_add(struct mm *mm, struct capref cap, genpaddr_t base, size_t size) {
    DEBUG_BEGIN;
    // TODO-BEAN: handle failure ALREADY_PRESENT
    // is base always aligned to PAGE_SIZE?

    errval_t err;
    struct mmnode *node = NULL;
    err = create_node_without_capinfo(mm, NodeType_Free, base, size, &node);
    if (err_is_fail(err)) { return err_push(err, MM_ERR_MM_ADD); }

    node->capinfo = (struct capinfo) {
            .cap = cap,
            .size = size,
            .base = base,
            .origin = &node->capinfo // mm node of unsplit RAM cap refers to its own capinfo
    };
    if (mm->head == NULL) {
        assert(mm->tail == NULL);
        mm->head = node;
        mm->tail = node;
    } else {
        struct mmnode *last = mm->tail;
        last->next = node;
        node->prev = last;
    }
//    DEBUG_PRINTF("adding RAM region (%p/%zu)\n", base, size);
    DEBUG_END;
    return SYS_ERR_OK;
}


/** does current node fulfill requirement to be picked as suitable free node */
static inline
bool is_node_suitable_alloc(struct mmnode *node, size_t size) {
    return node->size >= size && node->type == NodeType_Free;
}

/** Enqueue node behind other in linked list of mmnode
 *  from;
 *  other->prev <-> other <-> other->next
 *  to;
 *  other->prev <-> new_node <-> other <-> other->next */
static inline
void enqueue_node_before_other(struct mm *mm, struct mmnode *new_node, struct mmnode *other) {
    struct mmnode *_old_prev = other->prev;
    struct mmnode *_old_next = other->next;

    if (other == mm->head) {
        mm->head = new_node;
    }
    new_node->next = other;
    if (other->prev != NULL) {
        other->prev->next = new_node;
    }
    new_node->next = other;
    other->prev = new_node;

    assert(other->prev == new_node);
    assert(new_node->next == other);
    assert(new_node->prev == _old_prev);
    assert(other->next == _old_next);
}

static inline
size_t alloc_align_size(size_t size) {
    return size + BASE_PAGE_SIZE - (size % BASE_PAGE_SIZE); // align size to page size
}

/**
 * Request aligned ram capability
 *
 * @param mm this instance
 * @param size size of capability to request
 * @param alignment alignment of address
 * @param retcap cap to return
 * @return
 */
errval_t mm_alloc_aligned(struct mm *mm, size_t size, size_t alignment, struct capref *retcap) {
    DEBUG_BEGIN;
    errval_t err;
    // TODO-BEAN: how to handle alignment?

    if (alignment == 0 || alignment % BASE_PAGE_SIZE != 0) { return LIB_ERR_ALIGNMENT; }
    size = alloc_align_size(size);

    struct mmnode *node = mm->head;
    while (node != NULL && !is_node_suitable_alloc(node, size)) { node = node->next; }
    if (node == NULL) { return MM_ERR_NOT_ENOUGH_RAM; }

    err = mm->slot_alloc(mm->slot_alloc_inst, 1, retcap);
    if (err_is_fail(err)) { return err_push(err, MM_ERR_SLOT_MM_ALLOC); }
    mm->slot_refill(mm->slot_alloc_inst);
    if (err_is_fail(err)) { return err_push(err, MM_ERR_SLOT_NOSLOTS); }

    // TODO: what if nothing else remaining

    // |-------|    |-|-----|
    // |  A    | -> |B|  A  |
    // |-------|    |-|-----|
    //
    // A: original node (node)
    // B: new node with requested size (return) (new_node)
    // B is enqueued before A and A is updated

    err = cap_retype(*retcap, node->capinfo.cap, 0, mm->objtype, size, 1);
    if (err_is_fail(err)) { err_push(err, MM_ERR_MISSING_CAPS); }

    // shrink existing node (stays NodeType_free)
    assert(node->type == NodeType_Free);
    node->type = NodeType_Free;
    node->base = node->base + size;
    node->size = node->size - size;

    // create new node (becomes NodeType_Allocated)
    struct mmnode *new_node = NULL;
    const genpaddr_t new_node_base = node->base;
    const gensize_t new_node_size = node->size;
    err = create_node_without_capinfo(mm, NodeType_Allocated, new_node_base, new_node_size, &new_node);
    if (err_is_fail(err)) { return err_push(err, MM_ERR_MM_ALLOC); }
    new_node->capinfo = (struct capinfo) {
            .origin = &node->capinfo,
            .cap = *retcap,
            .base = node->capinfo.base, //new_node_base, TODO
            .size = node->capinfo.size //new_node_size,  TODO
    };
    assert(new_node->capinfo.origin != node->capinfo.origin);

    enqueue_node_before_other(mm, new_node, node);
    DEBUG_END;
    return SYS_ERR_OK;
}

errval_t mm_alloc(struct mm *mm, size_t size, struct capref *retcap) {
    return mm_alloc_aligned(mm, size, BASE_PAGE_SIZE, retcap);
}

static inline
bool can_merge_node(struct mmnode *node, struct mmnode *other) {
    return other != NULL && other->type == NodeType_Free
            && node->capinfo.base == other->capinfo.base;
}

// TODO-BEAN: partial free? base addr is not base addr from capability?, fragmentaion?
errval_t mm_free(struct mm *mm, struct capref cap, genpaddr_t base, gensize_t size) {
    DEBUG_BEGIN;
    errval_t err;
    size = alloc_align_size(size);

    struct mmnode *current = mm->head;
    while (current != NULL) { // TODO: make this nicer
        if (current->base == base && current->size == size) { break;}
        current = current->next;
    }
    if (current == NULL) { return MM_ERR_MM_FREE_NOT_FOUND; }
    assert(current->type == NodeType_Allocated);
    assert(current->base == base);
    assert(current->size == size);
    err = cap_revoke(cap);
    if (err_is_fail(err)) { return err_push(err, MM_ERR_MM_FREE); }

    if (current != mm->tail && can_merge_node(current, current->next)) {
        // |-|-----|    |-------| A is free
        // |B|  A  | -> |   A   |
        // |-|-----|    |-------|
        // where B is current, A is current->next, and A is origin of B
        struct mmnode *origin = current->next;
        origin->size += current->size;
        origin->base = current->base;
        assert(origin->type == NodeType_Free);
        origin->prev = current->prev;
        if (current->prev != NULL) {
            current->prev->next = origin;
        }
        slab_free(&mm->slabs, current);
    }
    else if (current != mm->head && can_merge_node(current, current->prev)) {
        // |-----|-|    |-------| A is free
        // |  A  |B| -> |   A   |
        // |-----|-|    |-------|
        // where B is current, A is current->prev, and A is origin of B
        struct mmnode *origin = current->prev;
        origin->size += current->size;
        // origin->base stays the same
        assert(origin->type == NodeType_Free);
        origin->next = current->next;
        if (current->next != NULL) {
            current->next->prev = origin;
        }
        slab_free(&mm->slabs, current);
    }
    else {
        // current node is in between two NodeType_Alloacted nodes
        // or they dont share the same origin
        current->type = NodeType_Free;
    }
    DEBUG_END;
    return SYS_ERR_OK;
}