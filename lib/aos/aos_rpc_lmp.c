#include <aos/aos.h>
#include <aos/capabilities.h>
#include <aos/aos_rpc.h>
#include <aos/aos_rpc_lmp.h>

static struct aos_rpc *init_channel = NULL;
static struct aos_rpc *memory_channel = NULL;
static struct aos_rpc *process_channel = NULL;
static struct aos_rpc *serial_channel = NULL;

void aos_rpc_lmp_handler_print(char* string, uintptr_t* val, struct capref* cap)
{
    if (string) {
        debug_printf("||TEST %s length %zu \n", string, strlen(string));
    }

    if (val) {
        debug_printf("||TEST %d \n", *val);
    }

    if (cap && !capref_is_null(*cap)) {
        char buf[256];
        debug_print_cap_at_capref(buf, 256, *cap);
        debug_printf("||TEST %s \n", buf);
    }
}

// TODO: Properly handle errors.
errval_t aos_rpc_lmp_init(struct aos_rpc *rpc)
{
    errval_t err;

    struct lmp_chan *lc = malloc(sizeof(struct lmp_chan));

    lmp_chan_init(lc);

    struct capref cap_ep;
    err = endpoint_create(DEFAULT_LMP_BUF_WORDS, &cap_ep, &lc->endpoint);
    if (err_is_fail(err)) {
        debug_printf("endpoint_create() failed: %s\n", err_getstring(err));
        return err_push(err, LIB_ERR_ENDPOINT_CREATE);
    }

    lc->local_cap = cap_ep;
    lc->remote_cap = cap_chan_init;

    err = lmp_chan_alloc_recv_slot(lc);
    if (err_is_fail(err)) {
        debug_printf("lmp_chan_alloc_recv_slot() failed: %s\n", err_getstring(err));
        return err;
    }

    err = lmp_chan_register_recv(lc, get_default_waitset(), MKCLOSURE(recv_cb, &lc));
    if (err_is_fail(err)) {
        debug_printf("lmp_chan_register_recv() failed: %s\n", err_getstring(err));
        return err;
    }

    err = lmp_chan_send0(lc, LMP_SEND_FLAGS_DEFAULT, cap_ep);
    if (err_is_fail(err)) {
        debug_printf("lmp_chan_send0() failed: %s\n", err_getstring(err));
        return err;
    }

    err = event_dispatch(get_default_waitset());
    if (err_is_fail(err)) {
        debug_printf("event_dispatch() failed: %s\n", err_getstring(err));
        return err;
    }

    // TODO: Upgrade to service channel.

    return SYS_ERR_OK;
}

static errval_t lmp_send_message(struct lmp_chan *c, struct rpc_message *msg, lmp_send_flags_t flags)
{
    if (msg->cap == NULL) {
        msg->cap = &NULL_CAP;
    }

    uint32_t size_sent = 0;
    const uint64_t lmp_msg_length_bytes = sizeof(uint64_t ) * LMP_MSG_LENGTH;
    const uint64_t msg_size = sizeof(msg->msg.method) + sizeof(msg->msg.payload_length) + msg->msg.payload_length;
    bool first = true;
    uintptr_t buf[LMP_MSG_LENGTH];

    errval_t err = SYS_ERR_OK;
    while(size_sent < msg_size) {
        uint64_t to_send = MIN(lmp_msg_length_bytes, msg_size - size_sent);
        // TODO copy payload!
        memcpy(buf, msg, to_send);
        memset((char *) buf + to_send, 0, (lmp_msg_length_bytes - to_send));
        err = lmp_chan_send4(c, flags, (first ? *msg->cap : NULL_CAP), buf[0], buf[1], buf[2], buf[3]);
        if (err_is_fail(err)) {
            break;
        }
        size_sent += to_send;
        first = false;
    }
    return err;
}






errval_t aos_rpc_lmp_recv_number(struct aos_rpc *rpc, aos_rpc_lmp_recv_number_callback_t callback) {
    rpc->init_state.recv_number_cb = callback;
    errval_t err = lmp_chan_register_recv(&rpc->init_state.rpc_lmp_chan_init, get_default_waitset(),
            MKCLOSURE(aos_rpc_lmp_recv_number_handler, rpc));
    return err;
}

errval_t aos_rpc_lmp_send_number(struct aos_rpc *rpc, uintptr_t num)
{
    struct rpc_message *msg = malloc(sizeof(struct rpc_message) + sizeof(num));
    if (msg == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }
    msg->msg.method = Method_Send_Number;
    msg->msg.payload_length = sizeof(num);
    msg->cap = &NULL_CAP;
    memcpy(msg->msg.payload, &num, sizeof(num));

    // TODO: init channel
    errval_t err = lmp_send_message(&rpc->init_state.rpc_lmp_chan_init, msg, LMP_SEND_FLAGS_DEFAULT);
    free(msg);
    return err;
}


errval_t
aos_rpc_lmp_send_string(struct aos_rpc *rpc, const char *string)
{
//    const uint32_t str_len = MIN(strlen(string), RPC_LMP_MAX_STR_LEN);
//    struct rpc_message *msg = malloc(sizeof(struct rpc_message) + str_len);
//    if (msg == NULL) {
//        return LIB_ERR_MALLOC_FAIL;
//    }
//    msg->method = Method_Send_String;
//    msg->payload_length = str_len;
//    msg->cap = NULL;
//    strncpy(msg->payload, string, str_len);
//
//    // TODO: init channel
//    errval_t err = lmp_send_message(&rpc->rpc_lmp_chan, msg, LMP_SEND_FLAGS_DEFAULT);
//    free(msg);
//    return err;
    return SYS_ERR_OK;
}

errval_t
aos_rpc_lmp_get_ram_cap(struct aos_rpc *rpc, size_t bytes, size_t alignment,
                    struct capref *ret_cap, size_t *ret_bytes)
{
//    errval_t err;
//    const size_t payload_length = sizeof(bytes) + sizeof(alignment);
//    struct rpc_message *msg = malloc(sizeof(struct rpc_message) + payload_length);
//    if (msg == NULL) {
//        return LIB_ERR_MALLOC_FAIL;
//    }
//    msg->method = Method_Request_Ram_Cap;
//    msg->payload_length = payload_length;
//    msg->cap = &NULL_CAP;
//    memcpy(msg->payload, &bytes, sizeof(bytes));
//    memcpy(msg->payload + sizeof(bytes), &alignment, sizeof(alignment));
//
//    err = lmp_send_message(&rpc->rpc_lmp_chan, msg, LMP_SEND_FLAGS_DEFAULT);
//    if (err_is_fail(err)) {
//        goto clean_up;
//    }
//    err = event_dispatch(get_default_waitset());
//    if (err_is_fail(err)) {
//        goto clean_up;
//    }
//    // TODO: implement functionality to request a RAM capability over the
//    // given channel and wait until it is delivered.
//
//    err = SYS_ERR_OK;
//    goto clean_up;
//
//    clean_up:
//    free(msg);
//    return err;
    return SYS_ERR_OK;
}

errval_t
aos_rpc_lmp_serial_getchar(struct aos_rpc *rpc, char *retc)
{
//    struct rpc_message *msg = malloc(sizeof(struct rpc_message));
//    if (msg == NULL) {
//        return LIB_ERR_MALLOC_FAIL;
//    }
//    msg->method = Method_Serial_Getchar;
//    msg->payload_length = 0;
//    msg->cap = NULL;
//
//    // TODO: init channel
//    errval_t err = lmp_send_message(&rpc->rpc_lmp_chan, msg, LMP_SEND_FLAGS_DEFAULT);
//    if (err_is_fail(err)) {
//        goto clean_up;
//    }
//    err = event_dispatch(get_default_waitset());
//    if (err_is_fail(err)) {
//        goto clean_up;
//    }
//
//    // TODO: how to get result
//    // read from rpc state and return result
//    *retc = -1; // TODO
//
//    err = SYS_ERR_OK;
//    goto clean_up;
//
//    clean_up:
//    free(msg);
//    return err;
    return SYS_ERR_OK;
}

errval_t
aos_rpc_lmp_serial_putchar(struct aos_rpc *rpc, char c)
{
//    struct rpc_message *msg = malloc(sizeof(struct rpc_message) + sizeof(c));
//    if (msg == NULL) {
//        return LIB_ERR_MALLOC_FAIL;
//    }
//    msg->method = Method_Serial_Putchar;
//    msg->payload_length = sizeof(c);
//    msg->cap = NULL;
//    memcpy(msg->payload, &c, sizeof(c));
//
//    // TODO: init channel
//    errval_t err = lmp_send_message(&rpc->rpc_lmp_chan, msg, LMP_SEND_FLAGS_DEFAULT);
//    free(msg);
//    return err;
    return SYS_ERR_OK;
}

errval_t
aos_rpc_lmp_process_spawn(struct aos_rpc *rpc, char *cmdline,
                      coreid_t core, domainid_t *newpid)
{
    // TODO (M5): implement spawn new process rpc
    return SYS_ERR_OK;
}

errval_t
aos_rpc_lmp_process_get_name(struct aos_rpc *rpc, domainid_t pid, char **name)
{
    // TODO (M5): implement name lookup for process given a process id
    return SYS_ERR_OK;
}

errval_t
aos_rpc_lmp_process_get_all_pids(struct aos_rpc *rpc, domainid_t **pids,
                             size_t *pid_count)
{
    // TODO (M5): implement process id discovery
    return SYS_ERR_OK;
}

errval_t
aos_rpc_lmp_get_device_cap(struct aos_rpc *rpc, lpaddr_t paddr, size_t bytes,
                       struct capref *ret_cap)
{
    return LIB_ERR_NOT_IMPLEMENTED;
}

static struct aos_rpc *aos_rpc_lmp_setup_channel(struct capref remote_cap, const char *service_name)
{
    errval_t err;

    debug_printf("Setting up a new channel to %s.\n", service_name);

    struct aos_rpc *rpc = malloc(sizeof(struct aos_rpc));
    aos_rpc_init(&rpc);

    struct lmp_chan *lc = malloc(sizeof(struct lmp_chan));
    lmp_chan_init(lc);

    struct capref cap_ep;
    err = endpoint_create(DEFAULT_LMP_BUF_WORDS, &cap_ep, &lc->endpoint);
    if (err_is_fail(err)) {
        debug_printf("endpoint_create() failed: %s\n", err_getstring(err));
        return err_push(err, LIB_ERR_ENDPOINT_CREATE);
    }

    lc->local_cap = cap_ep;
    lc->remote_cap = remote_cap;

    err = lmp_chan_alloc_recv_slot(lc);
    if (err_is_fail(err)) {
        debug_printf("lmp_chan_alloc_recv_slot() failed: %s\n", err_getstring(err));
        return err;
    }

    err = lmp_chan_register_recv(lc, get_default_waitset(), MKCLOSURE(recv_cb, &lc));
    if (err_is_fail(err)) {
        debug_printf("lmp_chan_register_recv() failed: %s\n", err_getstring(err));
        return err;
    }

    err = lmp_chan_send0(lc, LMP_SEND_FLAGS_DEFAULT, cap_ep);
    if (err_is_fail(err)) {
        debug_printf("lmp_chan_send0() failed: %s\n", err_getstring(err));
        return err;
    }

    err = event_dispatch(get_default_waitset());
    if (err_is_fail(err)) {
        debug_printf("event_dispatch() failed: %s\n", err_getstring(err));
        return err;
    }

    return SYS_ERR_OK;
}

/**
 * \brief Returns the RPC channel to init.
 */
struct aos_rpc *aos_rpc_lmp_get_init_channel(void)
{
    if (init_channel == NULL) {
        init_channel = aos_rpc_lmp_setup_channel(cap_chan_init, "init");
    }

    return init_channel;
}

/**
 * \brief Returns the channel to the memory server.
 */
struct aos_rpc *aos_rpc_lmp_get_memory_channel(void)
{
    if (memory_channel == NULL) {
        debug_printf("Setting up a new channel to memory.\n");
        memory_channel = aos_rpc_lmp_setup_channel(cap_chan_memory, "memory");
    }

    return memory_channel;
}

/**
 * \brief Returns the channel to the process manager.
 */
struct aos_rpc *aos_rpc_lmp_get_process_channel(void)
{
    if (process_channel == NULL) {
        debug_printf("Setting up a new channel to process.\n");
        process_channel = aos_rpc_lmp_setup_channel(cap_chan_process, "process");
    }

    return process_channel;
}

/**
 * \brief Returns the channel to the serial console.
 */
struct aos_rpc *aos_rpc_lmp_get_serial_channel(void)
{
    if (serial_channel == NULL) {
        debug_printf("Setting up a new channel to serial.\n");
        serial_channel = aos_rpc_lmp_setup_channel(cap_chan_serial, "serial");
    }

    return serial_channel;
}
