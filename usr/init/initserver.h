#ifndef _USR_INIT_INITSERVER_H_
#define _USR_INIT_INITSERVER_H_

#include <aos/rpc.h>

typedef void (* recv_number_callback_t)(struct lmp_chan *, uint64_t numb);
typedef void (* recv_string_callback_t)(struct lmp_chan *, char *string);

enum pending_state {
        EmptyState = 0,
        StringTransmit = 1,
};

struct callback_state {
    struct aos_rpc rpc;
    uint32_t count; ///< How much was read from the client already.
    uint32_t total_length;
    enum pending_state pending_state;
    char *string;
};

errval_t initserver_init(void);

#endif
