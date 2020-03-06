/**
 * \file
 * \brief A library for managing physical memory (i.e., caps)
 */

#include <stdint.h>
#include <string.h>

#include <mm/mm.h>
#include <aos/debug.h>
#include <aos/solution.h>
#include <aos/cap_predicates.h>

static struct mmnode mm_head;
static struct mmnode mm_tail;

errval_t mm_init(struct mm *mm,
                 enum objtype objtype,
                 slab_refill_func_t slab_refill_func,
                 slot_alloc_t slot_alloc_func,
                 slot_refill_t slot_refill_func,
                 void *slot_alloc_inst)
{
    assert(mm != NULL);

    debug_printf("mm_init(mm=%p, objtype=%d, ...)\n", mm, objtype);

    mm->slot_alloc = slot_alloc_func;
    mm->slot_refill = slot_refill_func;
    mm->slot_alloc_inst = slot_alloc_inst;
    mm->objtype = objtype;

    slab_init(&mm->slabs, sizeof(struct mmnode), slab_default_refill);

    static uint8_t initial_slab_buffer[PAGE_SIZE];
    slab_grow(&mm->slabs, (void *) initial_slab_buffer, PAGE_SIZE);

    mm->head = &mm_head;
    mm_head.next = &mm_tail;
    mm_tail.prev = &mm_head;

    return SYS_ERR_OK;
}

void mm_destroy(struct mm *mm)
{
    assert(!"NYI");
}

static inline void print_node(char *buffer, size_t length, struct mmnode *node)
{
    assert(buffer != NULL);
    assert(node != NULL);

    if (node == &mm_head)
        snprintf(buffer, 16, "head");
    else if (node == &mm_tail)
        snprintf(buffer, 16, "tail");
    else if (node->type == NodeType_Free)
        snprintf(buffer, 16, "%p*", node);
    else
        snprintf(buffer, 16, "%p", node);

    buffer[length - 1] = '\0';
}

static inline void print_list(struct mm *mm)
{
    printf("Forward: ");

    struct mmnode *node;
    for (node = mm->head; node != NULL; node = node->next) {
        char buffer[16];
        print_node(buffer, 16, node);

        if (node->next == NULL)
            printf("%s", buffer);
        else
            printf("%s <--> ", buffer);
    }

    printf("\n");
}

errval_t mm_add(struct mm *mm, struct capref cap, genpaddr_t base, size_t size)
{
    assert(mm != NULL);

    debug_printf("mm_add(mm=%p, &cap=%p, base=0x%"PRIxGENPADDR", size=0x%zx)\n", mm, &cap, base, size);

    struct mmnode *next;
    for (next = mm->head->next; next != &mm_tail; next = next->next) {
        if (next->base == base)
            return MM_ERR_ALREADY_PRESENT;
        else if (next->base > base)
            break;
    }

    struct mmnode *node = slab_alloc(&mm->slabs);
    if (node == NULL)
        return MM_ERR_MM_ADD;

    node->type = NodeType_Free;
    node->cap.cap = cap;
    node->cap.base = base;
    node->cap.size = size;
    node->base = base;
    node->size = size;

    // Add the new node into the linked list.
    // [next->prev] <--> [node] <--> [next]

    next->prev->next = node;
    node->prev = next->prev;
    next->prev = node;
    node->next = next;

    debug_printf("Inserted new node %p at base 0x%"PRIxGENPADDR" with size 0x%"PRIxGENSIZE"\n", node, node->base, node->size);
    print_list(mm);

    return SYS_ERR_OK;
}

errval_t mm_alloc_aligned(struct mm *mm, size_t size, size_t alignment, struct capref *retcap)
{
    assert(mm != NULL);

    errval_t err;

    debug_printf("mm_alloc_aligned(mm=%p, size=0x%zx, alignment=0x%zx, retcap=%p)\n", mm, size, alignment, retcap);

    struct mmnode *best = NULL;
    size_t best_size = SIZE_MAX;
    size_t best_padding_size = 0;

    // Find the smallest node that is still free and can hold the requested size.
    for (struct mmnode *next = mm->head->next; next != &mm_tail; next = next->next) {
        size_t padding_size = (next->base % alignment > 0) ? (alignment - (next->base % alignment)) : 0;

        // We only care about free nodes.
        if (next->type != NodeType_Free)
            continue;

        // We only care about nodes of sufficient size.
        if (next->size < size + padding_size)
            continue;

        // We want the smallest node possible.
        if (next->size <= best_size) {
            best = next;
            best_size = next->size;
            best_padding_size = padding_size;
        }
    }

    if (best == NULL)
        return MM_ERR_NOT_FOUND;

    debug_printf("Found free node %p at base 0x%"PRIxGENPADDR" with size 0x%"PRIxGENSIZE"\n", best, best->base, best->size);

    const genpaddr_t best_base = best->base;

    // Retype the aligned part of the node with the requested size.
    err = mm->slot_alloc(mm->slot_alloc_inst, 1, retcap);
    if (err_is_fail(err))
        return err_push(err, MM_ERR_NEW_NODE);

    err = mm->slot_refill(mm->slot_alloc_inst);
    if (err_is_fail(err))
        return err_push(err, MM_ERR_SLOT_MM_ALLOC);

    err = cap_retype(*retcap, best->cap.cap, best->base - best->cap.base + best_padding_size, mm->objtype, size, 1);
    // TODO: Return the correct error code.
    if (err_is_fail(err))
        return err_push(err, MM_ERR_MISSING_CAPS);

    /*
     * Next we have to split the node and mark the requested part as being
     * allocated. This will result in the following layout.
     * [best->prev] <--> [padding] <--> [best] <--> [leftover] <--> [best-next]
     */

    best->type = NodeType_Allocated;
    best->base = best_base + best_padding_size;
    best->size = size;

    debug_printf("Splitting node %p: new base is 0x%"PRIxGENPADDR", new size is 0x%"PRIxGENSIZE"\n", best, best->base, best->size);

    // Only create a leftover node if there is space left.
    if (best_padding_size + size < best_size) {
        struct mmnode *leftover = slab_alloc(&mm->slabs);
        if (leftover == NULL)
            return MM_ERR_MM_ADD;

        leftover->cap = best->cap;
        leftover->type = NodeType_Free;
        leftover->base = best_base + best_padding_size + size;
        leftover->size = best_size - best_padding_size - size;

        best->next->prev = leftover;
        leftover->next = best->next;
        best->next = leftover;
        leftover->prev = best;

        debug_printf("Inserted leftover node %p at base 0x%"PRIxGENPADDR" with size 0x%"PRIxGENSIZE"\n", leftover, leftover->base, leftover->size);
    }

    // Only create a padding node if padding is necessary.
    if (best_padding_size > 0) {
        struct mmnode *padding = slab_alloc(&mm->slabs);
        if (padding == NULL)
            return MM_ERR_MM_ADD;

        padding->cap = best->cap;
        padding->type = NodeType_Free;
        padding->base = best_base;
        padding->size = best_padding_size;

        best->prev->next = padding;
        padding->prev = best->prev;
        best->prev = padding;
        padding->next = best;

        debug_printf("Inserted padding node %p at base 0x%"PRIxGENPADDR" with size 0x%"PRIxGENSIZE"\n", padding, padding->base, padding->size);
    }

    print_list(mm);

    return SYS_ERR_OK;
}

errval_t mm_alloc(struct mm *mm, size_t size, struct capref *retcap)
{
    return mm_alloc_aligned(mm, size, BASE_PAGE_SIZE, retcap);
}

errval_t mm_free(struct mm *mm, struct capref cap, genpaddr_t base, gensize_t size)
{
    assert(mm != NULL);

    errval_t err;

    debug_printf("mm_free(mm=%p, &cap=%p, base=0x%"PRIxGENPADDR", size=0x%"PRIxGENSIZE")\n", mm, &cap, base, size);

    struct mmnode *node;

    for (node = mm->head->next; node != &mm_tail; node = node->next) {
        // We can only free allocated nodes.
        if (node->type != NodeType_Allocated)
            continue;

        // The sizes must match.
        if (node->size != size)
            continue;

        if (node->base == base)
            break;
    }

    if (node == &mm_tail)
        return MM_ERR_NOT_FOUND;

    node->type = NodeType_Free;

    /*
     * Next, we need to merge the node with its neighbors. This is necessary so
     * fragmentation does not lead to smaller and smaller chunks of memory. We
     * can only merge with a neighbor if
     * - the neighbor is not head or tail of our list,
     * - the neighbor is free, and
     * - the node and the neighbor origin from the same root capability that
     *   was initially passed from the kernel.
     */

    // Merge the node with next neighbor if possible.
    if (node->next != &mm_tail &&
        node->next->type == NodeType_Free &&
        node->cap.base == node->next->cap.base) {

        struct mmnode *old = node->next;
        node->size += node->next->size;
        node->next->next->prev = node;
        node->next = node->next->next;
        slab_free(&mm->slabs, old);
    }

    // Merge the node with previous neighbor if possible.
    if (node->prev != &mm_head &&
        node->prev->type == NodeType_Free &&
        node->cap.base == node->prev->cap.base) {

        struct mmnode *old = node->prev;
        node->base = node->prev->base;
        node->size += node->prev->size;
        node->prev->prev->next = node;
        node->prev = node->prev->prev;
        slab_free(&mm->slabs, old);
    }

    print_list(mm);

    // TODO: Revoke the capability to prevent further use.
    //err = cap_revoke(cap);
    //if (err_is_fail(err))
    //    return err_push(err, MM_ERR_MM_FREE);

    err = cap_destroy(cap);
    if (err_is_fail(err))
        return err_push(err, MM_ERR_MM_FREE);

    return SYS_ERR_OK;
}
