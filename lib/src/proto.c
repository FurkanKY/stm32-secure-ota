#include "sota/proto.h"

#include <string.h>

#include "sota/crc32.h"

#define HELLO_SIZE 24u

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

size_t sota_proto_encode(uint8_t type, uint8_t seq, const uint8_t *payload, uint16_t len,
                         uint8_t *out)
{
    out[0] = SOTA_PROTO_SOF0;
    out[1] = SOTA_PROTO_SOF1;
    out[2] = type;
    out[3] = seq;
    put16(&out[4], len);
    if (len > 0) {
        memcpy(&out[SOTA_PROTO_HDR_SIZE], payload, len);
    }
    /* The CRC covers type, seq, length and payload, not the SOF bytes. */
    put32(&out[SOTA_PROTO_HDR_SIZE + len], sota_crc32(0, &out[2], 4u + len));
    return SOTA_PROTO_HDR_SIZE + len + SOTA_PROTO_CRC_SIZE;
}

void sota_proto_init(sota_proto_t *p, sota_update_t *update, uint32_t boot_version,
                     sota_send_fn send, void *send_ctx)
{
    memset(p, 0, sizeof(*p));
    p->update = update;
    p->boot_version = boot_version;
    p->send = send;
    p->send_ctx = send_ctx;
}

static void respond(sota_proto_t *p, uint8_t type, uint8_t seq, const uint8_t *payload,
                    uint16_t len)
{
    uint8_t frame[SOTA_PROTO_HDR_SIZE + HELLO_SIZE + SOTA_PROTO_CRC_SIZE];
    size_t n = sota_proto_encode(type | SOTA_RESPONSE, seq, payload, len, frame);

    p->send(p->send_ctx, frame, n);
}

/* Version from a slot's header, or 0 when the slot holds no image at all.
 * Informational only: nothing is verified here. */
static uint32_t slot_version(const sota_update_t *u, unsigned slot)
{
    sota_image_header_t hdr;

    if (u->flash->read(u->flash, u->layout->slot[slot].base, &hdr, sizeof(hdr)) != 0 ||
        hdr.magic != SOTA_IMAGE_MAGIC) {
        return 0;
    }
    return hdr.version;
}

static uint16_t do_hello(sota_proto_t *p, uint8_t *out)
{
    const sota_update_t *u = p->update;
    const sota_bootstate_t *s = u->state;

    out[0] = (uint8_t)SOTA_OK;
    out[1] = SOTA_PROTO_VERSION;
    put16(&out[2], SOTA_PROTO_MAX_CHUNK);
    put32(&out[4], p->boot_version);
    out[8] = s->active;
    out[9] = (uint8_t)sota_update_target_slot(s);
    out[10] = s->slot[0];
    out[11] = s->slot[1];
    put32(&out[12], s->security_floor);
    put32(&out[16], slot_version(u, 0));
    put32(&out[20], slot_version(u, 1));
    return HELLO_SIZE;
}

static uint16_t do_data(sota_proto_t *p, const uint8_t *payload, uint16_t len, uint8_t *out)
{
    sota_update_t *u = p->update;
    sota_err_t err = SOTA_E_ARG;

    if (len >= 4u && len - 4u <= SOTA_PROTO_MAX_CHUNK) {
        uint32_t offset = get32(payload);
        uint32_t n = len - 4u;

        if (u->in_progress && n > 0 && offset + n <= u->received) {
            err = SOTA_OK; /* retransmission after a lost response */
        } else {
            err = sota_update_write(u, offset, payload + 4, n);
        }
    }
    out[0] = (uint8_t)err;
    put32(&out[1], u->received); /* where the host should continue */
    return 5;
}

static void handle(sota_proto_t *p, uint8_t type, uint8_t seq, const uint8_t *payload,
                   uint16_t len)
{
    uint8_t out[HELLO_SIZE];
    uint16_t out_len = 1;
    unsigned slot = 0;

    switch (type) {
    case SOTA_CMD_HELLO:
        out_len = do_hello(p, out);
        break;
    case SOTA_CMD_BEGIN:
        out[0] = (uint8_t)(len == SOTA_HEADER_SIZE ? sota_update_begin(p->update, payload, &slot)
                                                   : SOTA_E_ARG);
        out[1] = (uint8_t)slot;
        out_len = 2;
        break;
    case SOTA_CMD_DATA:
        out_len = do_data(p, payload, len, out);
        break;
    case SOTA_CMD_FINISH:
        out[0] = (uint8_t)sota_update_finish(p->update);
        break;
    case SOTA_CMD_RESET:
        out[0] = (uint8_t)SOTA_OK;
        break;
    default:
        out[0] = (uint8_t)SOTA_E_ARG;
        break;
    }
    respond(p, type, seq, out, out_len);
    if (type == SOTA_CMD_RESET) {
        p->reset_requested = true;
    }
}

bool sota_proto_feed(sota_proto_t *p, uint8_t byte)
{
    if (p->rx_len == 0) {
        if (byte == SOTA_PROTO_SOF0) {
            p->rx[p->rx_len++] = byte;
        }
        return false;
    }
    if (p->rx_len == 1) {
        if (byte == SOTA_PROTO_SOF1) {
            p->rx[p->rx_len++] = byte;
        } else if (byte != SOTA_PROTO_SOF0) {
            p->rx_len = 0;
        }
        return false;
    }

    p->rx[p->rx_len++] = byte;
    if (p->rx_len == SOTA_PROTO_HDR_SIZE) {
        uint16_t len = (uint16_t)(p->rx[4] | p->rx[5] << 8);

        if (len > SOTA_PROTO_MAX_PAYLOAD) {
            p->rx_len = 0; /* cannot be a frame of ours, resynchronise */
            return false;
        }
        p->rx_need = SOTA_PROTO_HDR_SIZE + len + SOTA_PROTO_CRC_SIZE;
    }
    if (p->rx_len < SOTA_PROTO_HDR_SIZE || p->rx_len < p->rx_need) {
        return false;
    }

    uint16_t len = (uint16_t)(p->rx_need - SOTA_PROTO_HDR_SIZE - SOTA_PROTO_CRC_SIZE);
    p->rx_len = 0;
    if (get32(&p->rx[SOTA_PROTO_HDR_SIZE + len]) != sota_crc32(0, &p->rx[2], 4u + len)) {
        return false;
    }
    handle(p, p->rx[2], p->rx[3], &p->rx[SOTA_PROTO_HDR_SIZE], len);
    return true;
}
