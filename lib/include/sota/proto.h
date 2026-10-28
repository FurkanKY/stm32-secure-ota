/*
 * Update protocol, spoken by the bootloader in recovery mode. The byte layout
 * is specified in docs/update-protocol.md and mirrored by
 * tools/otatool/proto.py.
 *
 * The host is the only one that starts an exchange: it sends a request frame
 * and waits for the response with the same sequence number. Frames with a bad
 * CRC are dropped without an answer; the host retransmits after a timeout.
 *
 * Feed received bytes with sota_proto_feed(); responses go out through the
 * send callback. Nothing here knows about UART or CAN.
 */
#ifndef SOTA_PROTO_H
#define SOTA_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sota/update.h"

#define SOTA_PROTO_VERSION   1u
#define SOTA_PROTO_SOF0      0xA5u
#define SOTA_PROTO_SOF1      0x5Au
#define SOTA_PROTO_HDR_SIZE  6u   /* SOF0 SOF1 type seq len16 */
#define SOTA_PROTO_CRC_SIZE  4u
#define SOTA_PROTO_MAX_CHUNK 1024u
#define SOTA_PROTO_MAX_PAYLOAD (4u + SOTA_PROTO_MAX_CHUNK) /* DATA: offset + chunk */
#define SOTA_PROTO_MAX_FRAME (SOTA_PROTO_HDR_SIZE + SOTA_PROTO_MAX_PAYLOAD + SOTA_PROTO_CRC_SIZE)

#define SOTA_CMD_HELLO  0x01u
#define SOTA_CMD_BEGIN  0x02u
#define SOTA_CMD_DATA   0x03u
#define SOTA_CMD_FINISH 0x04u
#define SOTA_CMD_RESET  0x05u
#define SOTA_RESPONSE   0x80u /* set in the type of every response */

_Static_assert(SOTA_PROTO_MAX_PAYLOAD >= SOTA_HEADER_SIZE, "BEGIN must fit");

typedef void (*sota_send_fn)(void *ctx, const uint8_t *data, size_t len);

typedef struct {
    sota_update_t *update;
    uint32_t boot_version;
    sota_send_fn send;
    void *send_ctx;
    bool reset_requested; /* set by RESET once its response has been sent */

    uint8_t rx[SOTA_PROTO_MAX_FRAME];
    size_t rx_len;
    size_t rx_need;
} sota_proto_t;

void sota_proto_init(sota_proto_t *p, sota_update_t *update, uint32_t boot_version,
                     sota_send_fn send, void *send_ctx);

/* Returns true when the byte completed a valid frame (which was answered). */
bool sota_proto_feed(sota_proto_t *p, uint8_t byte);

/* Build a frame into out (at least SOTA_PROTO_HDR_SIZE + len + 4 bytes).
 * Returns the frame length. */
size_t sota_proto_encode(uint8_t type, uint8_t seq, const uint8_t *payload, uint16_t len,
                         uint8_t *out);

#endif /* SOTA_PROTO_H */
