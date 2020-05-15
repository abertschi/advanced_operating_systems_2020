#ifndef __ARP_H__
#define __ARP_H__

#include <aos/aos.h>

#include "ethernet.h"

#define ARP_HASHTABLE_BUCKETS (256)

struct arp_state {
    uint64_t mac;
    uint32_t ip;
    struct ethernet_state *eth_state;
    collections_hash_table *entries;
};

struct arp_entry {
    uint64_t mac;
};

errval_t arp_initialize(
    struct arp_state *state,
    const uint64_t mac,
    const uint32_t ip_address,
    struct ethernet_state *eth_state
);

errval_t arp_query(
    struct arp_state *state,
    const uint32_t ip_address,
    uint64_t *mac
);

errval_t arp_process(
    struct arp_state *state,
    lvaddr_t base
);

#endif
