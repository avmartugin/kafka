#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <librdkafka/rdkafka.h>

#include <common.h>
#include <queue.h>
#include <callbacks.h>

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Common callbacks handling
 */

log_msg_t *
new_log_msg(int level, const char *fac, const char *buf) {
    log_msg_t *msg = malloc(sizeof(log_msg_t));
    if (msg == NULL) {
        return NULL;
    }
    msg->level = level;
    msg->fac = malloc(sizeof(char) * strlen(fac) + 1);
    strcpy(msg->fac, fac);
    msg->buf = malloc(sizeof(char) * strlen(buf) + 1);
    strcpy(msg->buf, buf);
    return msg;
}

void
destroy_log_msg(log_msg_t *msg) {
    if (msg->fac != NULL) {
        free(msg->fac);
    }
    if (msg->buf != NULL) {
        free(msg->buf);
    }
    free(msg);
}

void
log_callback(const rd_kafka_t *rd_kafka, int level, const char *fac, const char *buf) {
    event_queues_t *event_queues = rd_kafka_opaque(rd_kafka);
    if (event_queues != NULL && event_queues->log_queue != NULL) {
        log_msg_t *msg = new_log_msg(level, fac, buf);
        if (msg != NULL) {
            if (queue_push(event_queues->log_queue, msg) != 0) {
                destroy_log_msg(msg);
            }
        }
    }
}

error_msg_t *
new_error_msg(int err, const char *reason) {
    error_msg_t *msg = malloc(sizeof(error_msg_t));
    if (msg == NULL) {
        return NULL;
    }
    msg->err = err;
    msg->reason = malloc(sizeof(char) * strlen(reason) + 1);
    strcpy(msg->reason, reason);
    return msg;
}

void
destroy_error_msg(error_msg_t *msg) {
    if (msg->reason != NULL) {
        free(msg->reason);
    }
    free(msg);
}

void
error_callback(rd_kafka_t *UNUSED(rd_kafka), int err, const char *reason, void *opaque) {
    event_queues_t *event_queues = opaque;
    if (event_queues != NULL && event_queues->error_queue != NULL) {
        error_msg_t *msg = new_error_msg(err, reason);
        if (msg != NULL) {
            if (queue_push(event_queues->error_queue, msg) != 0) {
                destroy_error_msg(msg);
            }
        }
    }
}

dr_msg_t *
new_dr_msg(int dr_callback, int err) {
    dr_msg_t *dr_msg;
    dr_msg = malloc(sizeof(dr_msg_t));
    dr_msg->dr_callback = dr_callback;
    dr_msg->err = err;
    return dr_msg;
}

void
destroy_dr_msg(dr_msg_t *dr_msg) {
    free(dr_msg);
}

void
msg_delivery_callback(rd_kafka_t *UNUSED(producer), const rd_kafka_message_t *msg, void *opaque) {
    event_queues_t *event_queues = opaque;
    if (msg->_private != NULL && event_queues != NULL && event_queues->delivery_queue != NULL) {
        dr_msg_t *dr_msg = msg->_private;
        if (dr_msg != NULL) {
            if (msg->err != RD_KAFKA_RESP_ERR_NO_ERROR) {
                dr_msg->err = msg->err;
            }
            queue_push(event_queues->delivery_queue, dr_msg);
        }
    }
}

event_queues_t *new_event_queues() {
    event_queues_t *event_queues = malloc(sizeof(event_queues_t));
    event_queues->error_queue = NULL;
    event_queues->error_cb_ref = LUA_REFNIL;
    event_queues->log_queue = NULL;
    event_queues->log_cb_ref = LUA_REFNIL;
    event_queues->delivery_queue = NULL;
    return event_queues;
}

void destroy_event_queues(struct lua_State *L, event_queues_t *event_queues) {
    if (event_queues->log_queue != NULL) {
        log_msg_t *msg = NULL;
        while (true) {
            msg = queue_pop(event_queues->log_queue);
            if (msg == NULL) {
                break;
            }
            destroy_log_msg(msg);
        }
        destroy_queue(event_queues->log_queue);
    }
    if (event_queues->error_queue != NULL) {
        error_msg_t *msg = NULL;
        while (true) {
            msg = queue_pop(event_queues->error_queue);
            if (msg == NULL) {
                break;
            }
            destroy_error_msg(msg);
        }
        destroy_queue(event_queues->error_queue);
    }
    if (event_queues->delivery_queue != NULL) {
        dr_msg_t *msg = NULL;
        while (true) {
            msg = queue_pop(event_queues->delivery_queue);
            if (msg == NULL) {
                break;
            }
            destroy_dr_msg(msg);
        }
        destroy_queue(event_queues->delivery_queue);
    }
    if (event_queues->error_cb_ref != LUA_REFNIL) {
        luaL_unref(L, LUA_REGISTRYINDEX, event_queues->error_cb_ref);
    }

    if (event_queues->log_cb_ref != LUA_REFNIL) {
        luaL_unref(L, LUA_REGISTRYINDEX, event_queues->log_cb_ref);
    }
    free(event_queues);
}
