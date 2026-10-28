/*
 * ISO 15765-2 (ISO-TP) over classic CAN, normal addressing, one channel.
 *
 * Carries messages of up to SOTA_ISOTP_MAX bytes in 8-byte CAN frames:
 *   single frame       0x0L  data...            (L = 1..7)
 *   first frame        0x1H LL data[6]          (12-bit length)
 *   consecutive frame  0x2N data[7]             (N = sequence number mod 16)
 *   flow control       0x3S BS STmin            (S: 0 continue, 1 wait, 2 overflow)
 * Frames are padded to 8 bytes with 0xCC.
 *
 * Polled and allocation-free: feed received frames with sota_isotp_on_frame(),
 * call sota_isotp_poll() often to push out consecutive frames, and pass a
 * millisecond clock for the timeouts.
 */
#ifndef SOTA_ISOTP_H
#define SOTA_ISOTP_H

#include <stdbool.h>
#include <stdint.h>

#define SOTA_ISOTP_MAX       1100u  /* largest message: an update frame is 1038 */
#define SOTA_ISOTP_PAD       0xCCu
#define SOTA_ISOTP_TIMEOUT_MS 1000u /* N_Bs / N_Cr */

/* Block size we ask senders for. bxCAN and most CAN controllers buffer only
 * three received frames; with a block size of 3 the sender stops for a flow
 * control after every three consecutive frames, so a polled receiver can never
 * overrun its FIFO however fast the frames arrive. */
#define SOTA_ISOTP_BLOCK_SIZE 3u

/* Default identifiers (11-bit): host requests and device responses. */
#define SOTA_CAN_ID_REQUEST  0x7E0u
#define SOTA_CAN_ID_RESPONSE 0x7E8u

/* Send one CAN frame; return 0 on success, nonzero if no mailbox is free
 * (the frame is retried on the next poll). */
typedef int (*sota_can_send_fn)(void *ctx, uint32_t id, const uint8_t data[8]);

typedef enum {
    ISOTP_TX_IDLE,
    ISOTP_TX_WAIT_FC,
    ISOTP_TX_SENDING,
} sota_isotp_tx_state_t;

typedef struct {
    uint32_t tx_id;
    sota_can_send_fn send;
    void *ctx;
    uint8_t rx_block_size; /* what we ask senders for in our flow control */
    uint8_t rx_stmin;

    /* reception */
    uint8_t rx_buf[SOTA_ISOTP_MAX];
    uint16_t rx_len;
    uint16_t rx_expected;
    uint8_t rx_sn;
    uint8_t rx_block_left;
    bool rx_active;
    bool fc_pending; /* flow control still to be sent */
    uint8_t fc_status;
    uint32_t rx_last_ms;

    /* transmission */
    uint8_t tx_buf[SOTA_ISOTP_MAX];
    uint16_t tx_len;
    uint16_t tx_off;
    uint8_t tx_sn;
    uint8_t tx_block_size;
    uint8_t tx_block_left;
    uint8_t tx_stmin_ms;
    sota_isotp_tx_state_t tx_state;
    uint32_t tx_last_ms;
    uint8_t pending[8]; /* frame waiting for a free mailbox */
    bool pending_valid;
} sota_isotp_t;

void sota_isotp_init(sota_isotp_t *t, uint32_t tx_id, sota_can_send_fn send, void *ctx);

/* Process a frame received on our request id. Returns the length of a message
 * that is now complete in t->rx_buf, or 0. */
uint16_t sota_isotp_on_frame(sota_isotp_t *t, const uint8_t *data, uint8_t dlc,
                             uint32_t now_ms);

/* Queue a message for transmission. Returns false if busy or too long. */
bool sota_isotp_send(sota_isotp_t *t, const uint8_t *msg, uint16_t len, uint32_t now_ms);

/* Push out pending frames and handle timeouts. */
void sota_isotp_poll(sota_isotp_t *t, uint32_t now_ms);

static inline bool sota_isotp_tx_busy(const sota_isotp_t *t)
{
    return t->tx_state != ISOTP_TX_IDLE || t->pending_valid;
}

#endif /* SOTA_ISOTP_H */
