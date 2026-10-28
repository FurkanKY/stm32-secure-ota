/*
 * UART <-> CAN gateway for the update protocol.
 *
 * Runs on a second board (or a second simulated one) between a PC and the CAN
 * bus. Each protocol frame arriving on the UART is sent to the device as one
 * ISO-TP message on SOTA_CAN_ID_REQUEST; each ISO-TP message coming back on
 * SOTA_CAN_ID_RESPONSE is written to the UART unchanged. The gateway does not
 * interpret the frames, so `otatool` works the same through it as on a direct
 * UART link.
 */
#include "board.h"
#include "sota/isotp.h"
#include "sota/proto.h"

static sota_isotp_t tp;

static int can_send(void *ctx, uint32_t id, const uint8_t data[8])
{
    (void)ctx;
    return board_can_send(id, data);
}

/* Cut the UART byte stream into protocol frames using SOF and length only. */
typedef struct {
    uint8_t buf[SOTA_PROTO_MAX_FRAME];
    uint16_t len;
    uint16_t need;
} splitter_t;

static uint16_t split(splitter_t *s, uint8_t b)
{
    if (s->len == 0 && b != SOTA_PROTO_SOF0) {
        return 0;
    }
    if (s->len == 1 && b != SOTA_PROTO_SOF1) {
        s->len = b == SOTA_PROTO_SOF0 ? 1 : 0;
        return 0;
    }
    s->buf[s->len++] = b;
    if (s->len == SOTA_PROTO_HDR_SIZE) {
        uint16_t payload = (uint16_t)(s->buf[4] | s->buf[5] << 8);

        if (payload > SOTA_PROTO_MAX_PAYLOAD) {
            s->len = 0;
            return 0;
        }
        s->need = (uint16_t)(SOTA_PROTO_HDR_SIZE + payload + SOTA_PROTO_CRC_SIZE);
    }
    if (s->len >= SOTA_PROTO_HDR_SIZE && s->len == s->need) {
        uint16_t n = s->len;
        s->len = 0;
        return n;
    }
    return 0;
}

int main(void)
{
    static splitter_t uart_in;

    board_init();
    board_can_init(SOTA_CAN_ID_RESPONSE);
    sota_isotp_init(&tp, SOTA_CAN_ID_REQUEST, can_send, 0);
    console_write("\nsota-gateway: UART <-> CAN (ISO-TP 0x7E0/0x7E8, 500 kbit/s)\n");

    for (;;) {
        uint32_t now = board_millis();
        int c = console_getc();
        uint32_t id;
        uint8_t data[8];
        uint8_t dlc;

        if (c >= 0) {
            uint16_t n = split(&uart_in, (uint8_t)c);
            /* If the bus is still busy with the previous frame, drop this one;
             * the host retransmits after its timeout. */
            if (n > 0) {
                sota_isotp_send(&tp, uart_in.buf, n, now);
            }
        }
        while (board_can_recv(&id, data, &dlc)) {
            if (id == SOTA_CAN_ID_RESPONSE) {
                uint16_t n = sota_isotp_on_frame(&tp, data, dlc, now);
                if (n > 0) {
                    console_send(tp.rx_buf, n);
                }
            }
        }
        sota_isotp_poll(&tp, now);
    }
}
