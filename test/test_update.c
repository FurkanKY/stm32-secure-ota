/* Update engine: the full install, confirm, rollback cycle on fake flash. */
#include <string.h>

#include "sota/update.h"
#include "test_support.h"
#include "unity.h"

enum { A = 0, B = 1 };

static fake_flash_t *flash;
static vector_t img_a1, img_b2, img_a3, img_b0;
static sota_journal_t journal;
static sota_bootstate_t state;
static sota_update_t up;

void setUp(void)
{
    flash = fake_flash_new(11);
    img_a1 = vector_load("image_a_v1");
    img_b2 = vector_load("image_b_v2");
    img_a3 = vector_load("image_a_v3");
    img_b0 = vector_load("image_b_v0");

    /* Factory state: v1 in slot A, first boot adopts it. */
    install_image(flash, A, &img_a1);
    TEST_ASSERT_EQUAL_INT(A, run_bootloader(flash, NULL));
}

void tearDown(void)
{
    TEST_ASSERT_FALSE_MESSAGE(flash->violation, "flash programming rule violated");
    vector_free(&img_a1);
    vector_free(&img_b2);
    vector_free(&img_a3);
    vector_free(&img_b0);
    fake_flash_free(flash);
}

static void open_session(void)
{
    TEST_ASSERT_EQUAL(SOTA_OK, sota_journal_open(&journal, &flash->ops, &test_layout, &state));
    sota_update_init(&up, &flash->ops, &test_layout, test_keyring(), &journal, &state);
}

static sota_err_t send_payload(const vector_t *img, uint32_t chunk)
{
    uint32_t len = img->len - SOTA_HEADER_SIZE;

    for (uint32_t off = 0; off < len; off += chunk) {
        uint32_t n = len - off < chunk ? len - off : chunk;
        sota_err_t err = sota_update_write(&up, off, img->data + SOTA_HEADER_SIZE + off, n);
        if (err != SOTA_OK) {
            return err;
        }
    }
    return SOTA_OK;
}

static sota_err_t full_update(const vector_t *img, uint32_t chunk)
{
    unsigned slot;
    sota_err_t err;

    open_session();
    err = sota_update_begin(&up, img->data, &slot);
    if (err == SOTA_OK) {
        err = send_payload(img, chunk);
    }
    if (err == SOTA_OK) {
        err = sota_update_finish(&up);
    }
    return err;
}

static void confirm(unsigned slot, uint32_t counter)
{
    TEST_ASSERT_EQUAL(SOTA_OK, sota_journal_open(&journal, &flash->ops, &test_layout, &state));
    TEST_ASSERT_EQUAL(SOTA_OK, sota_boot_confirm(&journal, &state, slot, counter));
}

static void test_update_trial_and_confirm(void)
{
    sota_bootstate_t s;

    TEST_ASSERT_EQUAL(SOTA_OK, full_update(&img_b2, 256));
    TEST_ASSERT_EQUAL_UINT8(SOTA_SLOT_PENDING, state.slot[B]);
    TEST_ASSERT_EQUAL_MEMORY(img_b2.data, fake_flash_ptr(flash, test_layout.slot[B].base),
                             img_b2.len);

    TEST_ASSERT_EQUAL_INT(B, run_bootloader(flash, &s));
    TEST_ASSERT_EQUAL_UINT8(1, s.attempts);
    confirm(B, 2);

    TEST_ASSERT_EQUAL_INT(B, run_bootloader(flash, &s));
    TEST_ASSERT_EQUAL_UINT8(SOTA_SLOT_CONFIRMED, s.slot[B]);
    TEST_ASSERT_EQUAL_UINT32(2, s.security_floor);
}

static void test_odd_chunk_sizes_are_reassembled(void)
{
    static const uint32_t chunks[] = {1, 7, 13, 61, 4096};

    for (size_t i = 0; i < sizeof(chunks) / sizeof(chunks[0]); i++) {
        TEST_ASSERT_EQUAL(SOTA_OK, full_update(&img_b2, chunks[i]));
        TEST_ASSERT_EQUAL_MEMORY(img_b2.data, fake_flash_ptr(flash, test_layout.slot[B].base),
                                 img_b2.len);
    }
}

static void test_never_confirmed_update_rolls_back(void)
{
    TEST_ASSERT_EQUAL(SOTA_OK, full_update(&img_b2, 512));
    for (unsigned i = 0; i < SOTA_MAX_BOOT_ATTEMPTS; i++) {
        TEST_ASSERT_EQUAL_INT(B, run_bootloader(flash, NULL));
    }
    TEST_ASSERT_EQUAL_INT(A, run_bootloader(flash, NULL));
    TEST_ASSERT_EQUAL_INT(A, run_bootloader(flash, NULL));
}

static void test_forged_header_rejected_before_erase(void)
{
    uint8_t header[SOTA_HEADER_SIZE];
    unsigned writes, erases;

    memcpy(header, img_b2.data, sizeof(header));
    header[0x10] ^= 1; /* bump the version without re-signing */
    open_session();
    writes = flash->writes;
    erases = flash->erases;
    TEST_ASSERT_EQUAL(SOTA_E_SIGNATURE, sota_update_begin(&up, header, NULL));
    TEST_ASSERT_EQUAL_UINT(writes, flash->writes);
    TEST_ASSERT_EQUAL_UINT(erases, flash->erases);
}

static void test_payload_corrupted_in_transit(void)
{
    sota_bootstate_t s;
    unsigned slot;

    open_session();
    TEST_ASSERT_EQUAL(SOTA_OK, sota_update_begin(&up, img_b2.data, &slot));
    img_b2.data[SOTA_HEADER_SIZE + 3000] ^= 0x20;
    TEST_ASSERT_EQUAL(SOTA_OK, send_payload(&img_b2, 128));
    TEST_ASSERT_EQUAL(SOTA_E_HASH, sota_update_finish(&up));

    /* Header never written, slot stays empty, device keeps running A. */
    TEST_ASSERT_EQUAL_HEX8(0xFF, *fake_flash_ptr(flash, test_layout.slot[B].base));
    TEST_ASSERT_EQUAL_INT(A, run_bootloader(flash, &s));
    TEST_ASSERT_EQUAL_UINT8(SOTA_SLOT_EMPTY, s.slot[B]);
}

static void test_anti_rollback(void)
{
    TEST_ASSERT_EQUAL(SOTA_OK, full_update(&img_b2, 1024));
    run_bootloader(flash, NULL);
    confirm(B, 2);

    /* v1 has counter 1 < floor 2: refused. v3 shares counter 2: allowed. */
    open_session();
    TEST_ASSERT_EQUAL(SOTA_E_ROLLBACK, sota_update_begin(&up, img_a1.data, NULL));
    TEST_ASSERT_EQUAL(SOTA_OK, full_update(&img_a3, 1024));
}

static void test_image_below_factory_floor_rejected(void)
{
    /* The factory image set the floor to its own counter (1). */
    open_session();
    TEST_ASSERT_EQUAL(SOTA_E_ROLLBACK, sota_update_begin(&up, img_b0.data, NULL));
}

static void test_active_slot_is_never_overwritten(void)
{
    open_session();
    TEST_ASSERT_EQUAL(SOTA_E_STATE, sota_update_begin(&up, img_a3.data, NULL));
}

static void test_protocol_misuse_rejected(void)
{
    uint8_t chunk[16] = {0};

    open_session();
    TEST_ASSERT_EQUAL(SOTA_E_STATE, sota_update_write(&up, 0, chunk, sizeof(chunk)));
    TEST_ASSERT_EQUAL(SOTA_E_STATE, sota_update_finish(&up));

    TEST_ASSERT_EQUAL(SOTA_OK, sota_update_begin(&up, img_b2.data, NULL));
    TEST_ASSERT_EQUAL(SOTA_E_ARG, sota_update_write(&up, 16, chunk, sizeof(chunk)));
    TEST_ASSERT_EQUAL(SOTA_OK, sota_update_write(&up, 0, chunk, sizeof(chunk)));
    TEST_ASSERT_EQUAL(SOTA_E_ARG, sota_update_finish(&up)); /* incomplete */
    TEST_ASSERT_EQUAL(SOTA_E_ARG, sota_update_write(&up, 16, img_b2.data, img_b2.len));
}

static void test_target_slot_is_the_inactive_one(void)
{
    sota_bootstate_t s = {.active = A};

    TEST_ASSERT_EQUAL_UINT(B, sota_update_target_slot(&s));
    s.active = B;
    TEST_ASSERT_EQUAL_UINT(A, sota_update_target_slot(&s));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_update_trial_and_confirm);
    RUN_TEST(test_odd_chunk_sizes_are_reassembled);
    RUN_TEST(test_never_confirmed_update_rolls_back);
    RUN_TEST(test_forged_header_rejected_before_erase);
    RUN_TEST(test_payload_corrupted_in_transit);
    RUN_TEST(test_anti_rollback);
    RUN_TEST(test_image_below_factory_floor_rejected);
    RUN_TEST(test_active_slot_is_never_overwritten);
    RUN_TEST(test_protocol_misuse_rejected);
    RUN_TEST(test_target_slot_is_the_inactive_one);
    return UNITY_END();
}
