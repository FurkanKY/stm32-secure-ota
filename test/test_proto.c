/* Update protocol: framing, resync, retransmission and a full update. */
#include <string.h>

#include "sota/crc32.h"
#include "sota/proto.h"
#include "test_support.h"
#include "unity.h"

enum { A = 0, B = 1 };

static fake_flash_t *flash;
static sota_journal_t journal;
static sota_bootstate_t state;
static sota_update_t up;
static sota_proto_t proto;
static vector_t img_a1, img_b2;

/* Last response captured from the send callback. */
static uint8_t resp[64];
static size_t resp_len;
static unsigned responses;

static void capture(void *ctx, const uint8_t *data, size_t len)
{
    (void)ctx;
    TEST_ASSERT_TRUE(len <= sizeof(resp));
    memcpy(resp, data, len);
    resp_len = len;
    responses++;
}

void setUp(void)
{
    flash = fake_flash_new(5);
    img_a1 = vector_load("image_a_v1");
    img_b2 = vector_load("image_b_v2");
    install_image(flash, A, &img_a1);
    TEST_ASSERT_EQUAL_INT(A, run_bootloader(flash, NULL));

    TEST_ASSERT_EQUAL(SOTA_OK, sota_journal_open(&journal, &flash->ops, &test_layout, &state));
    sota_update_init(&up, &flash->ops, &test_layout, test_keyring(), &journal, &state);
    sota_proto_init(&proto, &up, 0x00010000u, capture, NULL);
    responses = 0;
}

void tearDown(void)
{
    vector_free(&img_a1);
    vector_free(&img_b2);
    fake_flash_free(flash);
}

static void feed(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        sota_proto_feed(&proto, data[i]);
    }
}

/* Send one request and return the response status byte. */
static int8_t request(uint8_t type, uint8_t seq, const uint8_t *payload, uint16_t len)
{
    static uint8_t frame[SOTA_PROTO_MAX_FRAME];
    unsigned before = responses;

    feed(frame, sota_proto_encode(type, seq, payload, len, frame));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(before + 1, responses, "no response");
    TEST_ASSERT_EQUAL_HEX8(type | SOTA_RESPONSE, resp[2]);
    TEST_ASSERT_EQUAL_UINT8(seq, resp[3]);
    uint16_t plen = (uint16_t)(resp[4] | resp[5] << 8);
    uint32_t crc = (uint32_t)resp[6 + plen] | (uint32_t)resp[7 + plen] << 8 |
                   (uint32_t)resp[8 + plen] << 16 | (uint32_t)resp[9 + plen] << 24;
    TEST_ASSERT_EQUAL_HEX32(sota_crc32(0, &resp[2], 4u + plen), crc);
    return (int8_t)resp[6];
}

static uint32_t resp_u32(size_t off)
{
    const uint8_t *p = &resp[6 + off];
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int8_t send_chunk(uint32_t offset, const uint8_t *data, uint32_t n)
{
    uint8_t payload[4 + SOTA_PROTO_MAX_CHUNK];

    payload[0] = (uint8_t)offset;
    payload[1] = (uint8_t)(offset >> 8);
    payload[2] = (uint8_t)(offset >> 16);
    payload[3] = (uint8_t)(offset >> 24);
    memcpy(&payload[4], data, n);
    return request(SOTA_CMD_DATA, (uint8_t)offset, payload, (uint16_t)(4 + n));
}

static void test_hello_reports_device_state(void)
{
    TEST_ASSERT_EQUAL_INT8(SOTA_OK, request(SOTA_CMD_HELLO, 1, NULL, 0));
    TEST_ASSERT_EQUAL_UINT8(SOTA_PROTO_VERSION, resp[7]);
    TEST_ASSERT_EQUAL_UINT8(A, resp[6 + 8]);  /* active */
    TEST_ASSERT_EQUAL_UINT8(B, resp[6 + 9]);  /* target */
    TEST_ASSERT_EQUAL_UINT8(SOTA_SLOT_CONFIRMED, resp[6 + 10]);
    TEST_ASSERT_EQUAL_UINT32(1, resp_u32(12));          /* security floor */
    TEST_ASSERT_EQUAL_HEX32(0x01000000u, resp_u32(16)); /* slot A v1.0.0 */
    TEST_ASSERT_EQUAL_HEX32(0, resp_u32(20));           /* slot B empty */
}

static void test_full_update_over_protocol(void)
{
    uint32_t len = img_b2.len - SOTA_HEADER_SIZE;
    const uint8_t *payload = img_b2.data + SOTA_HEADER_SIZE;

    TEST_ASSERT_EQUAL_INT8(SOTA_OK, request(SOTA_CMD_BEGIN, 2, img_b2.data, SOTA_HEADER_SIZE));
    TEST_ASSERT_EQUAL_UINT8(B, resp[7]);
    for (uint32_t off = 0; off < len; off += SOTA_PROTO_MAX_CHUNK) {
        uint32_t n = len - off < SOTA_PROTO_MAX_CHUNK ? len - off : SOTA_PROTO_MAX_CHUNK;
        TEST_ASSERT_EQUAL_INT8(SOTA_OK, send_chunk(off, payload + off, n));
        TEST_ASSERT_EQUAL_UINT32(off + n, resp_u32(1));
    }
    TEST_ASSERT_EQUAL_INT8(SOTA_OK, request(SOTA_CMD_FINISH, 3, NULL, 0));
    TEST_ASSERT_EQUAL_INT8(SOTA_OK, request(SOTA_CMD_RESET, 4, NULL, 0));
    TEST_ASSERT_TRUE(proto.reset_requested);

    TEST_ASSERT_EQUAL_INT(B, run_bootloader(flash, NULL));
}

static void test_retransmitted_chunk_is_acknowledged_once(void)
{
    const uint8_t *payload = img_b2.data + SOTA_HEADER_SIZE;

    request(SOTA_CMD_BEGIN, 1, img_b2.data, SOTA_HEADER_SIZE);
    TEST_ASSERT_EQUAL_INT8(SOTA_OK, send_chunk(0, payload, 100));
    unsigned writes = flash->writes;
    TEST_ASSERT_EQUAL_INT8(SOTA_OK, send_chunk(0, payload, 100)); /* response was lost */
    TEST_ASSERT_EQUAL_UINT32(100, resp_u32(1));
    TEST_ASSERT_EQUAL_UINT(writes, flash->writes);
    TEST_ASSERT_EQUAL_INT8(SOTA_OK, send_chunk(100, payload + 100, 100));
}

static void test_gap_in_offsets_rejected_with_resume_point(void)
{
    const uint8_t *payload = img_b2.data + SOTA_HEADER_SIZE;

    request(SOTA_CMD_BEGIN, 1, img_b2.data, SOTA_HEADER_SIZE);
    send_chunk(0, payload, 64);
    TEST_ASSERT_EQUAL_INT8(SOTA_E_ARG, send_chunk(128, payload + 128, 64));
    TEST_ASSERT_EQUAL_UINT32(64, resp_u32(1));
}

static void test_bad_crc_is_dropped_silently(void)
{
    uint8_t frame[16];
    size_t n = sota_proto_encode(SOTA_CMD_HELLO, 9, NULL, 0, frame);

    frame[n - 1] ^= 0xFF;
    feed(frame, n);
    TEST_ASSERT_EQUAL_UINT(0, responses);
}

static void test_resynchronises_after_noise(void)
{
    static const uint8_t noise[] = {'u', 0xA5, 0x00, 0xA5, 0xA5, 'x', 0x5A, 0xFF};
    uint8_t frame[16];
    size_t n = sota_proto_encode(SOTA_CMD_HELLO, 7, NULL, 0, frame);

    feed(noise, sizeof(noise));
    feed(frame, n);
    TEST_ASSERT_EQUAL_UINT(1, responses);
    TEST_ASSERT_EQUAL_UINT8(7, resp[3]);
}

static void test_oversized_length_resynchronises(void)
{
    static const uint8_t bogus[] = {0xA5, 0x5A, SOTA_CMD_DATA, 0, 0xFF, 0xFF};
    uint8_t frame[16];

    feed(bogus, sizeof(bogus));
    feed(frame, sota_proto_encode(SOTA_CMD_HELLO, 3, NULL, 0, frame));
    TEST_ASSERT_EQUAL_UINT(1, responses);
}

static void test_begin_with_forged_header_reports_error(void)
{
    uint8_t header[SOTA_HEADER_SIZE];

    memcpy(header, img_b2.data, sizeof(header));
    header[0x14] = 9; /* raise the counter without re-signing */
    TEST_ASSERT_EQUAL_INT8(SOTA_E_SIGNATURE, request(SOTA_CMD_BEGIN, 1, header, sizeof(header)));
}

static void test_unknown_command(void)
{
    TEST_ASSERT_EQUAL_INT8(SOTA_E_ARG, request(0x42, 1, NULL, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_hello_reports_device_state);
    RUN_TEST(test_full_update_over_protocol);
    RUN_TEST(test_retransmitted_chunk_is_acknowledged_once);
    RUN_TEST(test_gap_in_offsets_rejected_with_resume_point);
    RUN_TEST(test_bad_crc_is_dropped_silently);
    RUN_TEST(test_resynchronises_after_noise);
    RUN_TEST(test_oversized_length_resynchronises);
    RUN_TEST(test_begin_with_forged_header_reports_error);
    RUN_TEST(test_unknown_command);
    return UNITY_END();
}
