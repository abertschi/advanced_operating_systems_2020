#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/aos_rpc_lmp.h>

#include <rpc/server/lmp.h>
#include <aos/aos_rpc_lmp_marshal.h>

#include "processserver.h"

static struct rpc_lmp_server server;

static spawn_callback_t spawn_cb = NULL;
static get_name_callback_t get_name_cb = NULL;
static get_all_pids_callback_t get_all_pids_cb = NULL;


static inline void init_server_state(struct processserver_state *server_state)
{
    server_state->process_head.next = &server_state->process_tail;
    server_state->process_head.prev = NULL;
    server_state->process_tail.prev = &server_state->process_head;
    server_state->process_tail.next = NULL;
    server_state->processlist = &server_state->process_head;
    server_state->process_head.name = NULL;
    server_state->process_tail.name = NULL;
    server_state->num_proc = 0;

    domainid_t pid;
    add_to_proc_list(server_state, "init", &pid);
}

static void add_process_info(struct processserver_state *server_state, struct process_info *process_info)
{
    server_state->process_tail.prev->next = process_info;
    process_info->prev = server_state->process_tail.prev;
    process_info->next = &server_state->process_tail;
    process_info->pid = server_state->num_proc;
    server_state->process_tail.prev = process_info;
    server_state->num_proc++;
}

errval_t add_to_proc_list(struct processserver_state *server_state, char *name, domainid_t *pid)
{
    struct process_info *new_process = calloc(1, sizeof(struct process_info));
    if (new_process == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }
    size_t name_size = strlen(name) + 1;    // strlen doesn't include '\0'
    new_process->name = malloc(name_size);
    if (new_process->name == NULL) {
        free(new_process);
        return LIB_ERR_MALLOC_FAIL;
    }
    strncpy(new_process->name, name, name_size);

    add_process_info(server_state, new_process);

    *pid = new_process->pid;

    return SYS_ERR_OK;
}

/**
 * Packs the current running processes into a pid_array
 *
 * @param ret_pid_array contains all pids in this process server state, has to be delted by caller
 * @return errors
 */
errval_t get_all_pids(struct processserver_state *server_state, size_t *ret_num_pids, domainid_t **ret_pids)
{
    *ret_pids = calloc(1, server_state->num_proc * sizeof(domainid_t));
    if (*ret_pids == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }
    *ret_num_pids = server_state->num_proc;
    struct process_info *curr = server_state->process_head.next;
    size_t curr_idx = 0;
    while (curr != &(server_state->process_tail)) {
        (*ret_pids)[curr_idx] = curr->pid;
        curr_idx++;
        curr = curr->next;
    }
    assert(curr_idx == server_state->num_proc);
    return SYS_ERR_OK;
}

errval_t get_name_by_pid(struct processserver_state *server_state, domainid_t pid, char **ret_name) {
    struct process_info *curr = server_state->process_head.next;
    char *found_name = NULL;
    while (curr != &(server_state->process_tail)) {
        if (curr->pid == pid) {
            found_name = curr->name;
            break;
        }
        curr = curr->next;
    }
    if (found_name == NULL) {
        return SYS_ERR_NOT_IMPLEMENTED;
    }
    const size_t name_size = strlen(found_name) + 1;
    *ret_name = malloc(name_size);
    strncpy(*ret_name, found_name, name_size);

    return SYS_ERR_OK;
}

inline
static errval_t handle_spawn_process(struct processserver_state *server_state, struct rpc_message_part *rpc_msg_part, struct rpc_message **ret_msg) {
    errval_t err;
    char *name = rpc_msg_part->payload + sizeof(coreid_t);
    coreid_t core = *((coreid_t *)rpc_msg_part->payload);
    domainid_t pid;
    enum rpc_message_status status = Status_Ok;
    // PAGEFAULT
    err = spawn_cb(server_state, name, core, &pid);
    if (err_is_fail(err)) {
        status = Spawn_Failed;
    }
    const size_t payload_length = sizeof(struct process_pid_array) + sizeof(domainid_t);
    *ret_msg = malloc(sizeof(struct rpc_message) + payload_length);
    if (*ret_msg == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }
    struct process_pid_array *pid_array = (struct process_pid_array *) &(*ret_msg)->msg.payload;
    (*ret_msg)->cap = NULL_CAP;
    (*ret_msg)->msg.payload_length = payload_length;
    (*ret_msg)->msg.method = Method_Spawn_Process;
    (*ret_msg)->msg.status = status;
    pid_array->pid_count = 1;
    pid_array->pids[0] = pid;

    return SYS_ERR_OK;
}

inline
static errval_t handle_process_get_name(struct processserver_state *server_state, struct rpc_message_part *rpc_msg_part, struct rpc_message **ret_msg) {
    errval_t  err;
    domainid_t pid = (domainid_t) rpc_msg_part->payload[0];
    enum rpc_message_status status = Status_Ok;
    char *name = NULL;
    err = get_name_cb(server_state, pid, &name);
    if (err_is_fail(err)) {
        status = Process_Get_Name_Failed;
    }
    const size_t payload_length = strnlen(name, RPC_LMP_MAX_STR_LEN) + 1; // strnlen no \0
    *ret_msg = calloc(1, sizeof(struct rpc_message) + payload_length);
    if (*ret_msg == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }
    char *result_name = (char *) &(*ret_msg)->msg.payload;
    strncpy(result_name, name, payload_length);
    free(name);
    (*ret_msg)->cap = NULL_CAP;
    (*ret_msg)->msg.payload_length = payload_length;
    (*ret_msg)->msg.method = Method_Process_Get_Name;
    (*ret_msg)->msg.status = status;

    return SYS_ERR_OK;
}

inline
static errval_t handle_process_get_all_pids(struct processserver_state *server_state, struct rpc_message_part *rpc_msg_part, struct rpc_message **ret_msg) {
    errval_t  err;
    size_t pid_count;
    domainid_t *pids = NULL;
    enum rpc_message_status status = Status_Ok;

    err = get_all_pids_cb(server_state, &pid_count, &pids);
    if (err_is_fail(err)) {
        status = Process_Get_All_Pids_Failed;
    }
    // TODO: pid_count sanitation
    const size_t payload_length = sizeof(struct rpc_message) + sizeof(domainid_t) * pid_count + sizeof(size_t);
    *ret_msg = malloc(payload_length);
    if (*ret_msg == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }
    struct process_pid_array *pid_array = (struct process_pid_array *) &(*ret_msg)->msg.payload;
    pid_array->pid_count = pid_count;
    memcpy(pid_array->pids, pids, sizeof(domainid_t) * pid_count);
    free(pids);

    (*ret_msg)->cap = NULL_CAP;
    (*ret_msg)->msg.payload_length = payload_length;
    (*ret_msg)->msg.method = Method_Process_Get_All_Pids;
    (*ret_msg)->msg.status = status;

    return SYS_ERR_OK;
}

static errval_t handle_complete_msg(struct processserver_state *server_state, struct rpc_message_part *rpc_msg_part, struct rpc_message **ret_msg) {
    switch (rpc_msg_part->method) {
        case Method_Spawn_Process: {
            return handle_spawn_process(server_state, rpc_msg_part, ret_msg);
        }
        case Method_Process_Get_Name: {
            return handle_process_get_name(server_state, rpc_msg_part, ret_msg);
        }
        case Method_Process_Get_All_Pids: {
            return handle_process_get_all_pids(server_state, rpc_msg_part, ret_msg);
        }
        // TODO: error on unknown message, introduce new errcode
        default: break;
    }
    return SYS_ERR_OK;
}

static inline
errval_t validate_lmp_header(struct lmp_recv_msg *msg) {
    if (msg->buf.buflen * sizeof(uintptr_t) < sizeof(struct rpc_message_part)) {
        DEBUG_PRINTF("invalid buflen\n");
        return LIB_ERR_LMP_INVALID_RESPONSE;
    }
    struct rpc_message_part *msg_part = (struct rpc_message_part *) msg->words;
    if (msg_part->status != Status_Ok) {
        DEBUG_PRINTF("status not ok\n");
        return LIB_ERR_LMP_INVALID_RESPONSE;
    }
    return SYS_ERR_OK;
}


static void service_recv_cb(struct rpc_message *msg, void *shared_state, struct lmp_chan *reply_chan, void *server_state)
{
    errval_t err;

    struct rpc_message *ret = NULL;

    err = handle_complete_msg((struct processserver_state *) server_state, &msg->msg, &ret);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "handle_complete_msg() failed");
        goto cleanup;
    }

    err = aos_rpc_lmp_send_message(reply_chan, ret, LMP_SEND_FLAGS_DEFAULT);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "aos_rpc_lmp_send_message() failed");
    }

cleanup:
    if (ret != NULL) {
        free(ret);
    }
}

// Initialize channel-specific data.
static void *state_init_cb(void *server_state)
{
    struct processserver_cb_state *state = NULL;

    return state;
}

// Free channel-specific data.
static void state_free_cb(void *server_state, void *callback_state)
{
    struct processserver_cb_state *state = callback_state;
    free(state);
}

errval_t processserver_init(
    spawn_callback_t new_spawn_cb,
    get_name_callback_t new_get_name_cb,
    get_all_pids_callback_t new_get_all_pids_cb
)
{
    debug_printf("processserver_init()\n");
    errval_t err;

    spawn_cb = new_spawn_cb;
    get_name_cb = new_get_name_cb;
    get_all_pids_cb = new_get_all_pids_cb;

    struct processserver_state *ps = calloc(1, sizeof(struct processserver_state));
    init_server_state(ps);

    err = rpc_lmp_server_init(&server, cap_chan_process, service_recv_cb, state_init_cb, state_free_cb, ps);
    if (err_is_fail(err)) {
        debug_printf("rpc_lmp_server_init() failed: %s\n", err_getstring(err));
        return err_push(err, RPC_ERR_INITIALIZATION);
    }

    return SYS_ERR_OK;
}
