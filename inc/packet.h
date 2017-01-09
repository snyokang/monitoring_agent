/**
 * @file packet.h
 * @author Snyo
 */
#ifndef _PACKET_H_
#define _PACKET_H_

#include <stdio.h>
#include <stdarg.h>

#include "util.h"

#define PKTSZ 32768

#define packet_append(pkt, fmt, ...) \
    (pkt)->size += snprintf((pkt)->payload+pkt->size, PKTSZ-pkt->size, fmt, ##__VA_ARGS__)

typedef struct packet_t packet_t;

enum packet_type  {METRIC, REGISTER, ALERT};
enum packet_state {EMPTY, BEGIN, WROTE, READY, DONE, FREE};
enum packet_response {
    EINVALLICENSE = 201,
    ETARGETAUTHFAIL = 202,
    EAGENTAUTHFAIL = 203,
    ETARGETNOTFOUND = 301,
    ETARGETREG = 302,
    ETARGETPCH = 303,
    ETARGETVER = 401,
    EAGENTVER = 402,
    ENOTSUPPORT = 403,
    EAGENTREG = 404,
    EAGENTPCH = 405
};

struct packet_t {
    /* Metadata */
    epoch_t started;

    packet_type type;
    packet_state state;
    packet_response response;

    int size;
    int rollback_point;
    int reuse;
    int attempt;

    /* Paylaod */
    char payload[PKTSZ];

    /* Control */
    int spin;
    packet_t *next;

};

packet_t *packet_alloc(int type);
void packet_free(packet_t *pkt);
char *packet_fetch(packet_t *pkt);

/**
 * @brief Atomic change operation of the packet state
 * @detail to 'to' if the state is 'from'
 * @param pkt the packet
 * @param from the packet state must be
 * @param to the packet state will be
 * @return If success returns 0, else returns 1
 */
int packet_change_state(packet_t *pkt, enum packet_state from, enum packet_state to);
int packet_expired(packet_t *pkt);

static inline
void packet_transaction(packet_t *pkt) {
    pkt->rollback_point = pkt->size;
}

static inline
void packet_commit(packet_t *pkt) {
    pkt->rollback_point = pkt->size;
}

static inline
void packet_rollback(packet_t *pkt) {
    pkt->size = pkt->rollback_point;
    pkt->payload[pkt->size] = '\0';
}

static inline
int packet_gather(packet_t *pkt, const char *tag, void *func, void *module) {
    int rollback_point = pkt->size;

    packet_append(pkt, "%s\"%s\":{", pkt->payload[pkt->size-1]=='}'?",":"", tag);
    int res = ((int (*)(void *, packet_t *))func)(module, pkt);
    if(res != 0) {
        pkt->size = rollback_point;
        pkt->payload[pkt->size] = '\0';
        return res;
    }
    packet_append(pkt, "}");

    return 0;
}

#endif
