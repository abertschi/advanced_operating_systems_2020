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

errval_t aos_rpc_lmp_init(struct aos_rpc *rpc)
{
    lmp_chan_init(&rpc->lc);
    struct aos_rpc_lmp *rpc_lmp = malloc(sizeof(struct aos_rpc_lmp));
    if (rpc_lmp == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }
    memset(rpc_lmp, 0, sizeof(struct aos_rpc_lmp));
    waitset_init(&rpc_lmp->ws);
    rpc_lmp->err = SYS_ERR_OK;
    rpc->lmp = rpc_lmp;

    return SYS_ERR_OK;
}

static errval_t lmp_send_message(struct lmp_chan *c, struct rpc_message *msg, lmp_send_flags_t flags)
{
    if (msg->cap == NULL) {
        msg->cap = &NULL_CAP;
    }

    uint32_t size_sent = 0;
    const uint64_t lmp_msg_length_bytes = sizeof(uint64_t) * LMP_MSG_LENGTH;
    const uint64_t msg_size = sizeof(msg->msg) + msg->msg.payload_length;
    bool first = true;
    uintptr_t buf[LMP_MSG_LENGTH];

    errval_t err = SYS_ERR_OK;
    while(size_sent < msg_size) {
        uint64_t to_send = MIN(lmp_msg_length_bytes, msg_size - size_sent);
        memcpy(buf, ((char *)&msg->msg)+size_sent, to_send);
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

errval_t
aos_rpc_lmp_send_number(struct aos_rpc *rpc, uintptr_t num)
{
    struct rpc_message *msg = malloc(sizeof(struct rpc_message) + sizeof(num));
    if (msg == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }
    msg->msg.method = Method_Send_Number;
    msg->msg.payload_length = sizeof(num);
    msg->msg.status = Status_Ok;
    msg->cap = NULL;
    memcpy(msg->msg.payload, &num, sizeof(num));

    errval_t err = lmp_send_message(&rpc->lc, msg, LMP_SEND_FLAGS_DEFAULT);
    free(msg);
    return err;
}

errval_t
aos_rpc_lmp_send_string(struct aos_rpc *rpc, const char *string)
{
    const uint32_t str_len = MIN(strlen(string), RPC_LMP_MAX_STR_LEN);
    struct rpc_message *msg = malloc(sizeof(struct rpc_message) + str_len);
    if (msg == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }
    msg->msg.method = Method_Send_String;
    msg->msg.payload_length = str_len;
    msg->cap = NULL;
    msg->msg.status = Status_Ok;
    strncpy(msg->msg.payload, string, str_len);

    errval_t err = lmp_send_message(&rpc->lc, msg, LMP_SEND_FLAGS_DEFAULT);
    free(msg);
    return err;
}

static void client_ram_cb(void *arg) {
    debug_printf("client_ram_cb(...)\n");
    struct aos_rpc *rpc = arg;
    struct client_ram_state *ram_state = rpc->lmp->shared;
    struct lmp_chan *lc = &rpc->lc;

    struct capref cap;
    struct lmp_recv_msg msg = LMP_RECV_MSG_INIT;

    errval_t err = lmp_chan_recv(lc, &msg, &cap);
    if (err_is_fail(err) && lmp_err_is_transient(err)) {
        // reregister
        err = lmp_chan_register_recv(lc, &rpc->lmp->ws,
                                     MKCLOSURE(client_ram_cb, arg));
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "");
            rpc->lmp->err = err;
            return;
        }
    }
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "");
        rpc->lmp->err = err;
        return;
    }
    return_with_err(msg.buf.buflen < sizeof(struct rpc_message_part), rpc->lmp, "invalid buflen");
    struct rpc_message_part *msg_part = (struct rpc_message_part *)msg.words;
    return_with_err(msg_part->status != Status_Ok, rpc->lmp, "status not ok");
    return_with_err(msg_part->method != Method_Get_Ram_Cap, rpc->lmp, "wrong method in response");
    return_with_err(msg_part->payload_length != sizeof(size_t), rpc->lmp, "invalid payload len");

    memcpy(&ram_state->bytes, msg_part->payload, sizeof(size_t));
    ram_state->cap = cap;
    rpc->lmp->err = SYS_ERR_OK;
}

errval_t
aos_rpc_lmp_get_ram_cap(struct aos_rpc *rpc, size_t bytes, size_t alignment,
                    struct capref *ret_cap, size_t *ret_bytes)
{
    errval_t err;
    // create request message
    const size_t payload_length = sizeof(bytes) + sizeof(alignment);
    struct rpc_message *msg = malloc(sizeof(struct rpc_message) + payload_length);
    if (msg == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }
    msg->msg.method = Method_Get_Ram_Cap;
    msg->msg.payload_length = payload_length;
    msg->msg.status = Status_Ok;
    msg->cap = NULL;
    memcpy(msg->msg.payload, &bytes, sizeof(bytes));
    memcpy(msg->msg.payload + sizeof(bytes), &alignment, sizeof(alignment));

    // prepare and register receive handler state
    struct client_ram_state *ram_state = rpc->lmp->shared;
    rpc->lmp->shared = ram_state;
    err = lmp_chan_register_recv(&rpc->lc, &rpc->lmp->ws,
                                 MKCLOSURE(client_ram_cb, rpc));
    if (err_is_fail(err)) {
        goto clean_up;
    }

    // send ram request
    err = lmp_send_message(&rpc->lc, msg, LMP_SEND_FLAGS_DEFAULT);
    if (err_is_fail(err)) {
        goto clean_up;
    }

    // wait for response
    err = event_dispatch(&rpc->lmp->ws);
    if (err_is_fail(err)) {
        goto clean_up;
    }
    if (err_is_fail(rpc->lmp->err)) {
        err = rpc->lmp->err;
        goto clean_up;
    }

    *ret_cap = ram_state->cap;
    *ret_bytes = ram_state->bytes;

    err = SYS_ERR_OK;
    goto clean_up;

    clean_up:
    free(msg);
    return err;
}

/** client side callback to receive serial_getchar request **/
static
void serial_getchar_recv_cb(void *arg) {
    debug_printf("serial_getchar_recv_cb()\n");

    struct client_serial_getchar_state *state = (struct client_serial_getchar_state *) arg;
    struct aos_rpc *rpc = state->rpc;
    struct lmp_chan *lc = &rpc->lc;
    struct capref cap;
    struct lmp_recv_msg msg = LMP_RECV_MSG_INIT;

    errval_t err = lmp_chan_recv(lc, &msg, &cap);
    if (err_is_fail(err) && lmp_err_is_transient(err)) {
        // reregister
        // TODO use different waitset
        err = lmp_chan_register_recv(lc, get_default_waitset(), MKCLOSURE(serial_getchar_recv_cb, arg));
        if (err_is_fail(err)) {
            state->err = LIB_ERR_CHAN_REGISTER_RECV;
            return;
        }
    } else if (err_is_fail(err)) {
        state->err = err;
        return;
    }
    #define return_on_err(state, cond, msg) do { \
        if (cond) { \
            state->err = LIB_ERR_LMP_INVALID_RESPONSE; \
            DEBUG_ERR(state->err, msg); \
            return; \
        } \
    } while(0);

    return_on_err(state,msg.buf.buflen < sizeof(struct rpc_message_part), "invalid buflen");

    struct rpc_message_part *msg_part = (struct rpc_message_part *) msg.words;

    // TODO: return different error status on not ok
    return_on_err(state, msg_part->status != Status_Ok, "status not ok");
    return_on_err(state, msg_part->method != Method_Serial_Getchar, "wrong method in response");
    return_on_err(state, msg_part->payload_length != 1, "invalid payload len");

    state->c = msg_part->payload[0];
    state->err = SYS_ERR_OK;
}

errval_t
aos_rpc_lmp_serial_getchar(struct aos_rpc *rpc, char *retc)
{
    struct rpc_message *msg = malloc(sizeof(struct rpc_message));
    if (msg == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }
    msg->cap = NULL;
    msg->msg.method = Method_Serial_Getchar;
    msg->msg.payload_length = 0;
    msg->msg.status = Status_Ok;

    errval_t err = lmp_send_message(&rpc->lc, msg, LMP_SEND_FLAGS_DEFAULT);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "lmp_send_message failed\n");
        goto clean_up_msg;
    }
    struct client_serial_getchar_state *state = malloc(sizeof(struct client_serial_getchar_state));
    if (state == NULL) {
        err = LIB_ERR_MALLOC_FAIL;
        goto clean_up_msg;
    }
    memset(state, 0, sizeof(struct client_serial_getchar_state));
    state->rpc = rpc;

    // TODO: different waitset
    err = lmp_chan_register_recv(&rpc->lc, get_default_waitset(), MKCLOSURE(serial_getchar_recv_cb, state));
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "lmp_chan_register_recv failed");
        goto clean_up_state;
    }
    err = event_dispatch(get_default_waitset()); // TODO: different waitset
    if (err_is_fail(err)) {
        goto clean_up_state;
    }
    if (err_is_fail(state->err)) {
        err = state->err;
        goto clean_up_state;
    }
    *retc = state->c;
    err = SYS_ERR_OK;
    goto clean_up_state;

    // clean up:
    clean_up_state:
    free(state);

    clean_up_msg:
    free(msg);
    DEBUG_ERR(err, "aos_rpc_lmp_serial_getchar failed");
    return err;
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

static void client_recv_serivce_cb(void *args)
{
    debug_printf("client_recv_serivce_cb() was called!\n");
}

static void client_recv_open_cb(void *args)
{
    errval_t err;

    struct aos_rpc *rpc = (struct aos_rpc *) args;
    struct lmp_chan *lc = &rpc->lc;

    struct capref server_cap;
    struct lmp_recv_msg msg = LMP_RECV_MSG_INIT;

    err = lmp_chan_recv(lc, &msg, &server_cap);
    if (err_is_fail(err)) {
        debug_printf("lmp_chan_recv() failed: %s\n", err_getstring(err));
        return;
    }

    // In case no capability was sent, return.
    if (capref_is_null(server_cap)) {
        debug_printf("open_recv_cb() could not retrieve a capability.");
        return;
    }

    // We have to allocate a new slot, since the current slot may be used for
    // other transmissions.
    err = slot_alloc(&lc->remote_cap);
    if (err_is_fail(err)) {
        debug_printf("slot_alloc() failed: %s\n", err_getstring(err));
        return;
    }

    err = cap_copy(lc->remote_cap, server_cap);
    if (err_is_fail(err)) {
        debug_printf("cap_copy() failed: %s\n", err_getstring(err));
        return;
    }

    err = lmp_chan_register_recv(lc, get_default_waitset(), MKCLOSURE(client_recv_serivce_cb, rpc));
    if (err_is_fail(err)) {
        debug_printf("lmp_chan_register_recv() failed: %s\n", err_getstring(err));
        return;
    }
}

static struct aos_rpc *aos_rpc_lmp_setup_channel(struct capref remote_cap, const char *service_name)
{
    errval_t err;

    debug_printf("Setting up a new channel to %s.\n", service_name);

    struct aos_rpc *rpc = malloc(sizeof(struct aos_rpc));
    if (rpc == NULL) {
        return NULL;
    }
    err = aos_rpc_lmp_init(rpc);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "error in aos_rpc_lmp_init");
        return NULL;
    }

    struct lmp_chan *lc = &rpc->lc;

    struct capref cap_ep;
    err = endpoint_create(DEFAULT_LMP_BUF_WORDS, &cap_ep, &lc->endpoint);
    if (err_is_fail(err)) {
        debug_printf("endpoint_create() failed: %s\n", err_getstring(err));
        return NULL;
    }

    lc->local_cap = cap_ep;
    lc->remote_cap = remote_cap;

    err = lmp_chan_alloc_recv_slot(lc);
    if (err_is_fail(err)) {
        debug_printf("lmp_chan_alloc_recv_slot() failed: %s\n", err_getstring(err));
        return NULL;
    }

    // The closure will be removed from the waitset after it has been executed once.
    err = lmp_chan_register_recv(lc, get_default_waitset(), MKCLOSURE(client_recv_open_cb, rpc));
    if (err_is_fail(err)) {
        debug_printf("lmp_chan_register_recv() failed: %s\n", err_getstring(err));
        return NULL;
    }

    err = lmp_chan_send0(lc, LMP_SEND_FLAGS_DEFAULT, cap_ep);
    if (err_is_fail(err)) {
        debug_printf("lmp_chan_send0() failed: %s\n", err_getstring(err));
        return NULL;
    }

    // Wait for the callback to be executed.
    err = event_dispatch(get_default_waitset());
    if (err_is_fail(err)) {
        debug_printf("event_dispatch() failed: %s\n", err_getstring(err));
        return NULL;
    }

    return rpc;
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

        struct client_ram_state *ram_state = malloc(sizeof(struct client_ram_state));
        if (ram_state == NULL) {
            DEBUG_ERR(LIB_ERR_MALLOC_FAIL, "malloc failed");
            return NULL;
        }
        memory_channel->lmp->shared = ram_state;
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
