#include "sota/isotp.h"

#include <string.h>

#define PCI_SF 0x0u
#define PCI_FF 0x1u
#define PCI_CF 0x2u
#define PCI_FC 0x3u

#define FC_CTS      0x0u
#define FC_WAIT     0x1u
#define FC_OVERFLOW 0x2u

void sota_isotp_init(sota_isotp_t *t, uint32_t tx_id, sota_can_send_fn send, void *ctx)
{
    memset(t, 0, sizeof(*t));
    t->tx_id = tx_id;
    t->send = send;
    t->ctx = ctx;
    t->rx_block_size = SOTA_ISOTP_BLOCK_SIZE;
    t->tx_state = ISOTP_TX_IDLE;
}

/* Hand a frame to the driver, or park it until a mailbox frees up. */
static void emit(sota_isotp_t *t, const uint8_t frame[8])
{
    if (t->send(t->ctx, t->tx_id, frame) != 0) {
        memcpy(t->pending, frame, 8);
        t->pending_valid = true;
    }
}

static void frame_init(uint8_t frame[8])
{
    memset(frame, SOTA_ISOTP_PAD, 8);
}

static bool try_send_fc(sota_isotp_t *t)
{
    uint8_t frame[8];

    frame_init(frame);
    frame[0] = (uint8_t)(PCI_FC << 4 | t->fc_status);
    frame[1] = t->rx_block_size;
    frame[2] = t->rx_stmin;
    if (t->send(t->ctx, t->tx_id, frame) != 0) {
        return false;
    }
    t->fc_pending = false;
    return true;
}

/* STmin as sent on the bus, in whole milliseconds (sub-ms values round up). */
static uint8_t stmin_ms(uint8_t raw)
{
    if (raw <= 0x7Fu) {
        return raw;
    }
    if (raw >= 0xF1u && raw <= 0xF9u) {
        return 1;
    }
    return 0x7F; /* reserved: be conservative */
}

uint16_t sota_isotp_on_frame(sota_isotp_t *t, const uint8_t *data, uint8_t dlc,
                             uint32_t now_ms)
{
    if (dlc < 1) {
        return 0;
    }

    switch (data[0] >> 4) {
    case PCI_SF: {
        uint8_t len = data[0] & 0x0Fu;

        if (len == 0 || len > 7 || len > dlc - 1) {
            return 0;
        }
        memcpy(t->rx_buf, &data[1], len);
        t->rx_active = false;
        return len;
    }
    case PCI_FF: {
        uint16_t len = (uint16_t)((data[0] & 0x0Fu) << 8 | data[1]);

        if (dlc < 8 || len < 8) {
            return 0;
        }
        if (len > SOTA_ISOTP_MAX) {
            t->rx_active = false;
            t->fc_status = FC_OVERFLOW;
            t->fc_pending = true;
            try_send_fc(t);
            return 0;
        }
        memcpy(t->rx_buf, &data[2], 6);
        t->rx_len = 6;
        t->rx_expected = len;
        t->rx_sn = 1;
        t->rx_block_left = t->rx_block_size;
        t->rx_active = true;
        t->rx_last_ms = now_ms;
        t->fc_status = FC_CTS;
        t->fc_pending = true;
        try_send_fc(t);
        return 0;
    }
    case PCI_CF: {
        if (!t->rx_active) {
            return 0;
        }
        if ((data[0] & 0x0Fu) != t->rx_sn) {
            t->rx_active = false; /* lost a frame: drop the message */
            return 0;
        }
        uint16_t n = (uint16_t)(t->rx_expected - t->rx_len);
        if (n > 7) {
            n = 7;
        }
        if (n > dlc - 1u) {
            t->rx_active = false;
            return 0;
        }
        memcpy(&t->rx_buf[t->rx_len], &data[1], n);
        t->rx_len = (uint16_t)(t->rx_len + n);
        t->rx_sn = (t->rx_sn + 1u) & 0x0Fu;
        t->rx_last_ms = now_ms;
        if (t->rx_len >= t->rx_expected) {
            t->rx_active = false;
            return t->rx_expected;
        }
        if (t->rx_block_size != 0 && --t->rx_block_left == 0) {
            t->rx_block_left = t->rx_block_size;
            t->fc_status = FC_CTS;
            t->fc_pending = true;
            try_send_fc(t);
        }
        return 0;
    }
    case PCI_FC:
        if (t->tx_state != ISOTP_TX_WAIT_FC || dlc < 3) {
            return 0;
        }
        switch (data[0] & 0x0Fu) {
        case FC_CTS:
            t->tx_block_size = data[1];
            t->tx_block_left = data[1];
            t->tx_stmin_ms = stmin_ms(data[2]);
            t->tx_state = ISOTP_TX_SENDING;
            if (t->tx_off == 6) {
                t->tx_last_ms = now_ms - t->tx_stmin_ms; /* first CF may go now */
            }
            /* Later blocks keep the time of the last CF: STmin spans blocks. */
            sota_isotp_poll(t, now_ms);
            break;
        case FC_WAIT:
            t->tx_last_ms = now_ms;
            break;
        default:
            t->tx_state = ISOTP_TX_IDLE; /* overflow or invalid: abort */
            break;
        }
        return 0;
    default:
        return 0;
    }
}

bool sota_isotp_send(sota_isotp_t *t, const uint8_t *msg, uint16_t len, uint32_t now_ms)
{
    uint8_t frame[8];

    if (sota_isotp_tx_busy(t) || len == 0 || len > SOTA_ISOTP_MAX) {
        return false;
    }
    memcpy(t->tx_buf, msg, len);
    t->tx_len = len;
    frame_init(frame);

    if (len <= 7) {
        frame[0] = (uint8_t)len;
        memcpy(&frame[1], msg, len);
        t->tx_state = ISOTP_TX_IDLE;
    } else {
        frame[0] = (uint8_t)(PCI_FF << 4 | (len >> 8));
        frame[1] = (uint8_t)len;
        memcpy(&frame[2], msg, 6);
        t->tx_off = 6;
        t->tx_sn = 1;
        t->tx_state = ISOTP_TX_WAIT_FC;
        t->tx_last_ms = now_ms;
    }
    emit(t, frame);
    return true;
}

void sota_isotp_poll(sota_isotp_t *t, uint32_t now_ms)
{
    if (t->pending_valid) {
        if (t->send(t->ctx, t->tx_id, t->pending) != 0) {
            return;
        }
        t->pending_valid = false;
    }
    if (t->fc_pending && !try_send_fc(t)) {
        return;
    }
    if (t->rx_active && now_ms - t->rx_last_ms > SOTA_ISOTP_TIMEOUT_MS) {
        t->rx_active = false; /* N_Cr: the sender went quiet */
    }

    if (t->tx_state == ISOTP_TX_WAIT_FC) {
        if (now_ms - t->tx_last_ms > SOTA_ISOTP_TIMEOUT_MS) {
            t->tx_state = ISOTP_TX_IDLE; /* N_Bs: no flow control came */
        }
        return;
    }

    while (t->tx_state == ISOTP_TX_SENDING && !t->pending_valid) {
        uint8_t frame[8];

        if (t->tx_stmin_ms != 0 && now_ms - t->tx_last_ms < t->tx_stmin_ms) {
            return;
        }
        uint16_t n = (uint16_t)(t->tx_len - t->tx_off);
        if (n > 7) {
            n = 7;
        }
        frame_init(frame);
        frame[0] = (uint8_t)(PCI_CF << 4 | t->tx_sn);
        memcpy(&frame[1], &t->tx_buf[t->tx_off], n);
        emit(t, frame);
        t->tx_off = (uint16_t)(t->tx_off + n);
        t->tx_sn = (t->tx_sn + 1u) & 0x0Fu;
        t->tx_last_ms = now_ms;

        if (t->tx_off >= t->tx_len) {
            t->tx_state = ISOTP_TX_IDLE;
        } else if (t->tx_block_size != 0 && --t->tx_block_left == 0) {
            t->tx_state = ISOTP_TX_WAIT_FC;
        } else if (t->tx_stmin_ms != 0) {
            return; /* one frame per STmin */
        }
    }
}
