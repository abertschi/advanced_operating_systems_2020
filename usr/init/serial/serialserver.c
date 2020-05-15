#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/aos_rpc_ump.h>
#include <rpc/server/ump.h>

#include "serialserver.h"
#include "serial_driver.h"
#include <grading.h>
#include <aos/syscalls.h>
#include <aos/string.h>

static struct rpc_ump_server server;

// ring buffer to store read data
static struct serial_read_data ring_buffer;

static struct serialserver_state serial_state;

// TODO: replace this with uuid
static uint64_t session_ctr;

__unused
static void putchar_sys(char c)
{
    errval_t err;

    grading_rpc_handler_serial_putchar(c);

    err = sys_print((const char *) &c, 1);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "sys_print() failed");
    }
}

__unused
static void do_putchar_usr(char c)
{
    errval_t err;

    grading_rpc_handler_serial_putchar(c);

    err = serial_driver_write(c);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "sys_print() failed");
    }
}

__unused
static bool read_data_empty(void)
{
    return ring_buffer.head == ring_buffer.tail;
}

__unused
static void read_data_advance(void)
{
    if (ring_buffer.full) {
        ring_buffer.tail = (ring_buffer.tail + 1) % READ_DATA_SLOTS;
//        debug_printf("read buffer is full\n");
    }
    ring_buffer.head = (ring_buffer.head + 1) % READ_DATA_SLOTS;
    ring_buffer.full = ring_buffer.head == ring_buffer.tail;
}

__unused
static void read_data_retreat(void)
{
    ring_buffer.tail = (ring_buffer.tail + 1) % READ_DATA_SLOTS;
    ring_buffer.full = false;
}

__unused
static void read_data_put(struct serial_read_slot data)
{
    ring_buffer.data[ring_buffer.head] = data;
    read_data_advance();
}

__unused
static errval_t read_data_get(struct serial_read_slot **ret_data)
{
    if (read_data_empty()) {
        return LPUART_ERR_NO_DATA;
    }
    *ret_data = &ring_buffer.data[ring_buffer.tail];
    read_data_retreat();
    return SYS_ERR_OK;
}

static errval_t reply_char(
        struct aos_rpc *rpc,
        uint64_t session,
        char c,
        enum rpc_message_status status
)
{

    errval_t err;

    char buf[sizeof(struct rpc_message) + sizeof(struct serial_getchar_reply)];
    struct rpc_message *msg = (struct rpc_message *) &buf;

    msg->cap = NULL_CAP;
    msg->msg.method = Method_Serial_Getchar;
    msg->msg.payload_length = sizeof(struct serial_getchar_reply);
    msg->msg.status = status;
    struct serial_getchar_reply payload = {
            .session = session,
            .data = c
    };
    memcpy(&msg->msg.payload, &payload, sizeof(struct serial_getchar_reply));

    // debug_printf("send: %c, session: %d, status: %d, method: %d\n", c, session, status, msg->msg.method);
    err = aos_rpc_ump_send_message(rpc, msg);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "ump_send_message failed\n");
        return err;
    }

    return SYS_ERR_OK;
}

// TODO: replace this by uuid or similar

static uint64_t new_session(void)
{
    uint64_t s = session_ctr;
    session_ctr++;
    return s;
}

__unused
static void
do_getchar_sys(struct aos_rpc *rpc, struct rpc_message *req, struct serial_getchar_req *req_getchar)
{
    errval_t err;
    char c;

    // TODO Currently, if this callback blocks (which is does if
    // the callback calls sys_getchar) the server cannot process
    // other requests. This could be solved by giving this callback
    // another callback to send the response, so that the server
    // doesn't have to wait for this callback to complete.

    err = sys_getchar(&c);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "sys_getchar() failed");
    }

    err = reply_char(rpc, req_getchar->session, c, Status_Ok);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "reply_char() failed");
    }
}

static void send_getchar_reply(struct aos_rpc *rpc)
{
    errval_t err;

    struct serial_read_slot *slot = NULL;
    err = read_data_get(&slot);
    assert(err_is_ok(err));

    char res_char = slot->val;
    serial_session_t session = serial_state.curr_read_session;
    if (IS_CHAR_LINEBREAK(res_char)) {
        // debug_printf("is linebreak\n");
        serial_state.curr_read_session = SERIAL_GETCHAR_SESSION_UNDEF;
    }

    err = reply_char(rpc, session, res_char, Status_Ok);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "reply_char() failed");
    }
}


__unused
static void
do_getchar_usr(struct aos_rpc *rpc, struct rpc_message *req, struct serial_getchar_req *req_getchar)
{
    errval_t err;
    if (req_getchar->session == SERIAL_GETCHAR_SESSION_UNDEF) {
        req_getchar->session = new_session();
        debug_printf("new serial session: %d\n", req_getchar->session);
    }

    // read is occupied, try again
    if (serial_state.curr_read_session != SERIAL_GETCHAR_SESSION_UNDEF &&
        serial_state.curr_read_session != req_getchar->session) {
        err = reply_char(rpc, req_getchar->session, 0, Serial_Getchar_Occupied);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "reply_char() failed");
        }
        debug_printf("session is occuped\n");
        return;
    }

    // read is free
    if (serial_state.curr_read_session == SERIAL_GETCHAR_SESSION_UNDEF) {
        serial_state.curr_read_session = req_getchar->session;

        // disable read iqr to prevent race conditions
        dispatcher_handle_t d = disp_disable();
        ring_buffer.tail = ring_buffer.head;
        disp_enable(d);
    }

    if (read_data_empty()) {
        // debug_printf("deferred request\n"); // TODO
        serial_state.deferred_rpc = rpc;
        return;

    } else {
        send_getchar_reply(rpc);
    }
}

__unused
static void read_irq_cb(char c)
{
    struct serial_read_slot data = {.val = c};
    read_data_put(data);

    if (serial_state.deferred_rpc != NULL) {
        send_getchar_reply(serial_state.deferred_rpc);
        serial_state.deferred_rpc = NULL;
    }
}


static void service_recv_cb(
        struct rpc_message *msg,
        void *callback_state,
        struct aos_rpc *rpc,
        void *server_state)
{

    char c;
    switch (msg->msg.method) {
        case Method_Serial_Putchar:
            memcpy(&c, msg->msg.payload, sizeof(char));
            // TODO: Macro
//             putchar_sys(c);
            debug_printf("do_putchar_usr: %d\n", c);
            do_putchar_usr(c);
            break;
        case Method_Serial_Getchar:

            if (msg->msg.payload_length != sizeof(struct serial_getchar_req)) {
                debug_printf("invalid req. for Method_Serial_Getchar");
                return;
            }

            struct serial_getchar_req *req_getchar = (struct serial_getchar_req *) &msg->msg.payload;

            grading_rpc_handler_serial_getchar();

//            do_getchar_sys(rpc, msg, req_getchar);
            do_getchar_usr(rpc, msg, req_getchar);

            break;
        default:
            break;
    }
}


errval_t serialserver_add_client(struct aos_rpc *rpc, coreid_t mpid)
{
    return rpc_ump_server_add_client(&server, rpc);
}

errval_t serialserver_serve_next(void)
{
    return rpc_ump_server_serve_next(&server);
}

errval_t serialserver_init(void)
{
    errval_t err;
    err = serial_driver_init();
    if (err_is_fail(err)) {
        debug_printf("error in shell_init(): %s\n", err_getstring(err));
        return err;
    }

    memset(&ring_buffer, 0, sizeof(ring_buffer));

    // TODO
    session_ctr = 0;

    memset(&serial_state, 0, sizeof(struct serialserver_state));
    serial_state.curr_read_session = SERIAL_GETCHAR_SESSION_UNDEF;

    err = serial_driver_set_read_cb(read_irq_cb);
    if (err_is_fail(err)) {
        return err;
    }

    err = rpc_ump_server_init(&server,
                              service_recv_cb,
                              NULL,
                              NULL,
                              NULL);

    if (err_is_fail(err)) {
        debug_printf("rpc_ump_server_init() failed: %s\n", err_getstring(err));
        return err_push(err, RPC_ERR_INITIALIZATION);
    }

    return SYS_ERR_OK;
}