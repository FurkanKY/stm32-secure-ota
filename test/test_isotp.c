/* ISO-TP: two endpoints wired back to back through an in-memory CAN bus. */
#include <string.h>

#include "sota/isotp.h"
#include "unity.h"

#define BUS_DEPTH 4096

typedef struct {
    uint32_t id;
    uint8_t data[8];
} can_frame_t;

typedef struct {
    can_frame_t frames[BUS_DEPTH];
    unsigned head, tail;
    unsigned refuse_every; /* simulate full mailboxes: refuse every Nth send */
    unsigned fifo_depth;   /* receiver buffer; frames beyond it are lost */
    unsigned sends;
    unsigned lost;
} bus_t;

static bus_t to_device, to_host;
static sota_isotp_t host, device;
static uint32_t now;

static int bus_send(void *ctx, uint32_t id, const uint8_t data[8])
{
    bus_t *bus = ctx;

    bus->sends++;
    if (bus->refuse_every && bus->sends % bus->refuse_every == 0) {
        return -1;
    }
    if (bus->fifo_depth && bus->tail - bus->head >= bus->fifo_depth) {
        bus->lost++; /* sent fine, but the receiver's FIFO overflowed */
        return 0;
    }
    TEST_ASSERT_TRUE(bus->tail - bus->head < BUS_DEPTH);
    bus->frames[bus->tail % BUS_DEPTH].id = id;
    memcpy(bus->frames[bus->tail % BUS_DEPTH].data, data, 8);
    bus->tail++;
    return 0;
}

static bool bus_take(bus_t *bus, can_frame_t *out)
{
    if (bus->head == bus->tail) {
        return false;
    }
    *out = bus->frames[bus->head++ % BUS_DEPTH];
    return true;
}

void setUp(void)
{
    memset(&to_device, 0, sizeof(to_device));
    memset(&to_host, 0, sizeof(to_host));
    sota_isotp_init(&host, SOTA_CAN_ID_REQUEST, bus_send, &to_device);
    sota_isotp_init(&device, SOTA_CAN_ID_RESPONSE, bus_send, &to_host);
    now = 0;
}

void tearDown(void) {}

/* Move frames both ways until nothing happens; return a message completed at
 * the device (its length), or 0. */
static uint16_t run(unsigned max_ms)
{
    uint16_t done = 0;

    for (unsigned ms = 0; ms <= max_ms && !done; ms++, now++) {
        can_frame_t f;

        sota_isotp_poll(&host, now);
        sota_isotp_poll(&device, now);
        while (bus_take(&to_device, &f) && !done) {
            TEST_ASSERT_EQUAL_HEX32(SOTA_CAN_ID_REQUEST, f.id);
            done = sota_isotp_on_frame(&device, f.data, 8, now);
        }
        while (bus_take(&to_host, &f)) {
            TEST_ASSERT_EQUAL_HEX32(SOTA_CAN_ID_RESPONSE, f.id);
            sota_isotp_on_frame(&host, f.data, 8, now);
        }
    }
    return done;
}

static void fill(uint8_t *buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(i * 31u + 7u);
    }
}

static void test_single_frame(void)
{
    static const uint8_t msg[] = {1, 2, 3, 4, 5};
    can_frame_t f;

    TEST_ASSERT_TRUE(sota_isotp_send(&host, msg, sizeof(msg), now));
    TEST_ASSERT_TRUE(bus_take(&to_device, &f));
    static const uint8_t expect[8] = {0x05, 1, 2, 3, 4, 5, 0xCC, 0xCC};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, f.data, 8);
    TEST_ASSERT_EQUAL_UINT16(5, sota_isotp_on_frame(&device, f.data, 8, now));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, device.rx_buf, 5);
}

static void test_wire_format_of_a_segmented_message(void)
{
    uint8_t msg[20];
    can_frame_t f;

    fill(msg, sizeof(msg));
    TEST_ASSERT_TRUE(sota_isotp_send(&host, msg, sizeof(msg), now));

    TEST_ASSERT_TRUE(bus_take(&to_device, &f)); /* first frame: 0x1, len 20 */
    TEST_ASSERT_EQUAL_HEX8(0x10, f.data[0]);
    TEST_ASSERT_EQUAL_HEX8(20, f.data[1]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, &f.data[2], 6);
    TEST_ASSERT_FALSE(bus_take(&to_device, &f)); /* waits for flow control */

    sota_isotp_on_frame(&device, msg, 0, now); /* dlc 0 is ignored */
    can_frame_t first;
    memcpy(first.data, (uint8_t[]){0x10, 20, msg[0], msg[1], msg[2], msg[3], msg[4], msg[5]}, 8);
    TEST_ASSERT_EQUAL_UINT16(0, sota_isotp_on_frame(&device, first.data, 8, now));
    TEST_ASSERT_TRUE(bus_take(&to_host, &f)); /* flow control: CTS, BS 3, STmin 0 */
    static const uint8_t fc[8] = {0x30, 3, 0, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(fc, f.data, 8);

    sota_isotp_on_frame(&host, f.data, 8, now);
    TEST_ASSERT_TRUE(bus_take(&to_device, &f));
    TEST_ASSERT_EQUAL_HEX8(0x21, f.data[0]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&msg[6], &f.data[1], 7);
    TEST_ASSERT_TRUE(bus_take(&to_device, &f));
    TEST_ASSERT_EQUAL_HEX8(0x22, f.data[0]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&msg[13], &f.data[1], 7);
    TEST_ASSERT_FALSE(bus_take(&to_device, &f));
}

static void test_largest_update_frame_round_trip(void)
{
    static uint8_t msg[1038];

    fill(msg, sizeof(msg));
    TEST_ASSERT_TRUE(sota_isotp_send(&host, msg, sizeof(msg), now));
    TEST_ASSERT_EQUAL_UINT16(sizeof(msg), run(100));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, device.rx_buf, sizeof(msg));
    TEST_ASSERT_FALSE(sota_isotp_tx_busy(&host));
}

static void test_block_size_and_stmin_are_honoured(void)
{
    static uint8_t msg[200];
    unsigned start = now;

    device.rx_block_size = 4;
    device.rx_stmin = 5; /* ms */
    fill(msg, sizeof(msg));
    TEST_ASSERT_TRUE(sota_isotp_send(&host, msg, sizeof(msg), now));
    TEST_ASSERT_EQUAL_UINT16(sizeof(msg), run(1000));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, device.rx_buf, sizeof(msg));

    /* 28 consecutive frames at least 5 ms apart */
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(27 * 5, now - start);
    /* one FC after the FF plus one after every 4 CFs (except after the last) */
    TEST_ASSERT_EQUAL_UINT(1 + 6, to_host.sends);
}

static void test_survives_full_mailboxes(void)
{
    static uint8_t msg[500];

    to_device.refuse_every = 3;
    to_host.refuse_every = 2;
    fill(msg, sizeof(msg));
    TEST_ASSERT_TRUE(sota_isotp_send(&host, msg, sizeof(msg), now));
    TEST_ASSERT_EQUAL_UINT16(sizeof(msg), run(200));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, device.rx_buf, sizeof(msg));
}

static void test_three_frame_fifo_never_overruns(void)
{
    /* Frames arrive all at once (as in a simulator, or a fast PC adapter)
     * into a 3-deep receive FIFO. The default block size keeps it safe. */
    static uint8_t msg[1038];

    to_device.fifo_depth = 3;
    fill(msg, sizeof(msg));
    TEST_ASSERT_TRUE(sota_isotp_send(&host, msg, sizeof(msg), now));
    TEST_ASSERT_EQUAL_UINT16(sizeof(msg), run(100));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, device.rx_buf, sizeof(msg));
    TEST_ASSERT_EQUAL_UINT(0, to_device.lost);
}

static void test_without_block_size_a_small_fifo_overruns(void)
{
    static uint8_t msg[100];

    to_device.fifo_depth = 3;
    device.rx_block_size = 0; /* "send everything at once" */
    fill(msg, sizeof(msg));
    TEST_ASSERT_TRUE(sota_isotp_send(&host, msg, sizeof(msg), now));
    TEST_ASSERT_EQUAL_UINT16(0, run(100));
    TEST_ASSERT_GREATER_THAN_UINT(0, to_device.lost);
}

static void test_send_while_busy_is_refused(void)
{
    uint8_t msg[50] = {0};

    TEST_ASSERT_TRUE(sota_isotp_send(&host, msg, sizeof(msg), now));
    TEST_ASSERT_FALSE(sota_isotp_send(&host, msg, 3, now));
    TEST_ASSERT_FALSE(sota_isotp_send(&host, msg, 0, now));
}

static void test_lost_consecutive_frame_drops_message(void)
{
    static const uint8_t ff[8] = {0x10, 20, 0, 1, 2, 3, 4, 5};
    static const uint8_t cf2[8] = {0x22, 0, 0, 0, 0, 0, 0, 0}; /* 0x21 never arrived */

    sota_isotp_on_frame(&device, ff, 8, now);
    TEST_ASSERT_TRUE(device.rx_active);
    TEST_ASSERT_EQUAL_UINT16(0, sota_isotp_on_frame(&device, cf2, 8, now));
    TEST_ASSERT_FALSE(device.rx_active);
}

static void test_receiver_times_out(void)
{
    static const uint8_t ff[8] = {0x10, 20, 0, 1, 2, 3, 4, 5};

    sota_isotp_on_frame(&device, ff, 8, now);
    sota_isotp_poll(&device, now + SOTA_ISOTP_TIMEOUT_MS + 1);
    TEST_ASSERT_FALSE(device.rx_active);
}

static void test_sender_gives_up_without_flow_control(void)
{
    uint8_t msg[50] = {0};

    TEST_ASSERT_TRUE(sota_isotp_send(&host, msg, sizeof(msg), now));
    sota_isotp_poll(&host, now + SOTA_ISOTP_TIMEOUT_MS + 1);
    TEST_ASSERT_FALSE(sota_isotp_tx_busy(&host));
}

static void test_oversized_message_gets_overflow(void)
{
    uint8_t ff[8] = {0x1F, 0xFF, 0, 0, 0, 0, 0, 0}; /* 4095 bytes */
    can_frame_t f;

    sota_isotp_on_frame(&device, ff, 8, now);
    TEST_ASSERT_FALSE(device.rx_active);
    TEST_ASSERT_TRUE(bus_take(&to_host, &f));
    TEST_ASSERT_EQUAL_HEX8(0x32, f.data[0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_single_frame);
    RUN_TEST(test_wire_format_of_a_segmented_message);
    RUN_TEST(test_largest_update_frame_round_trip);
    RUN_TEST(test_block_size_and_stmin_are_honoured);
    RUN_TEST(test_survives_full_mailboxes);
    RUN_TEST(test_three_frame_fifo_never_overruns);
    RUN_TEST(test_without_block_size_a_small_fifo_overruns);
    RUN_TEST(test_send_while_busy_is_refused);
    RUN_TEST(test_lost_consecutive_frame_drops_message);
    RUN_TEST(test_receiver_times_out);
    RUN_TEST(test_sender_gives_up_without_flow_control);
    RUN_TEST(test_oversized_message_gets_overflow);
    return UNITY_END();
}
