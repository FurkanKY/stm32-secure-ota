/*
 * Power-cut sweep.
 *
 * Run a whole update cycle (boot, install B, trial boot, confirm) once to
 * count its flash operations. Then repeat it on a fresh device for every k,
 * cutting power during operation k. After each cut the device must:
 *   - still boot a valid image (never fall into recovery),
 *   - never lower the anti-rollback floor,
 *   - finish the update when the cycle is simply run again.
 */
#include <stdio.h>
#include <string.h>

#include "sota/update.h"
#include "test_support.h"
#include "unity.h"

enum { A = 0, B = 1 };

static vector_t img_a1, img_b2;

void setUp(void)
{
    img_a1 = vector_load("image_a_v1");
    img_b2 = vector_load("image_b_v2");
}

void tearDown(void)
{
    vector_free(&img_a1);
    vector_free(&img_b2);
}

/* Returns 0 if the whole cycle ran, -1 if the flash failed part-way. */
static int update_cycle(fake_flash_t *f, uint32_t chunk)
{
    sota_journal_t j;
    sota_bootstate_t s;
    sota_update_t u;
    uint32_t len = img_b2.len - SOTA_HEADER_SIZE;

    if (run_bootloader(f, NULL) < 0) {
        return -1;
    }

    if (sota_journal_open(&j, &f->ops, &test_layout, &s) != SOTA_OK) {
        return -1;
    }
    sota_update_init(&u, &f->ops, &test_layout, test_keyring(), &j, &s);
    if (sota_update_begin(&u, img_b2.data, NULL) != SOTA_OK) {
        return -1;
    }
    for (uint32_t off = 0; off < len; off += chunk) {
        uint32_t n = len - off < chunk ? len - off : chunk;
        if (sota_update_write(&u, off, img_b2.data + SOTA_HEADER_SIZE + off, n) != SOTA_OK) {
            return -1;
        }
    }
    if (sota_update_finish(&u) != SOTA_OK) {
        return -1;
    }

    if (run_bootloader(f, NULL) != B) {
        return -1;
    }
    if (sota_journal_open(&j, &f->ops, &test_layout, &s) != SOTA_OK ||
        sota_boot_confirm(&j, &s, B, 2) != SOTA_OK) {
        return -1;
    }
    return 0;
}

static fake_flash_t *factory_device(uint32_t seed, unsigned journal_prefill)
{
    fake_flash_t *f = fake_flash_new(seed);

    install_image(f, A, &img_a1);
    TEST_ASSERT_EQUAL_INT(A, run_bootloader(f, NULL));

    /* Push the journal close to a page boundary so rollover happens mid-cycle. */
    if (journal_prefill > 0) {
        sota_journal_t j;
        sota_bootstate_t s;
        sota_journal_open(&j, &f->ops, &test_layout, &s);
        for (unsigned i = 0; i < journal_prefill; i++) {
            TEST_ASSERT_EQUAL(SOTA_OK, sota_journal_append(&j, &s));
        }
    }
    return f;
}

static void sweep(uint32_t chunk, unsigned journal_prefill)
{
    fake_flash_t *f = factory_device(1, journal_prefill);
    unsigned before = f->writes + f->erases;
    unsigned erases_before = f->erases;

    TEST_ASSERT_EQUAL_INT(0, update_cycle(f, chunk));
    unsigned ops = f->writes + f->erases - before;
    /* Slot B is three sectors; a journal rollover adds one page erase. */
    TEST_ASSERT_EQUAL_UINT(journal_prefill ? 4 : 3, f->erases - erases_before);
    fake_flash_free(f);

    char note[64];
    snprintf(note, sizeof(note), "sweeping %u power-cut points", ops);
    TEST_MESSAGE(note);

    for (unsigned k = 0; k < ops; k++) {
        char msg[96];
        sota_bootstate_t s;
        sota_image_header_t hdr;

        snprintf(msg, sizeof(msg), "power cut at operation %u of %u", k, ops);
        f = factory_device(1000 + k, journal_prefill);

        fake_flash_cut_after(f, (long)k);
        TEST_ASSERT_EQUAL_INT_MESSAGE(-1, update_cycle(f, chunk), msg);
        fake_flash_power_on(f);

        int slot = run_bootloader(f, &s);
        TEST_ASSERT_TRUE_MESSAGE(slot == A || slot == B, msg);
        TEST_ASSERT_EQUAL_MESSAGE(SOTA_OK,
                                  sota_image_verify(&f->ops, &test_layout.slot[slot],
                                                    test_keyring(), &hdr),
                                  msg);
        TEST_ASSERT_TRUE_MESSAGE(s.security_floor >= 1, msg);

        /* Retry from scratch: the update must go through. */
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, update_cycle(f, chunk), msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(B, run_bootloader(f, &s), msg);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(SOTA_SLOT_CONFIRMED, s.slot[B], msg);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, s.security_floor, msg);
        TEST_ASSERT_FALSE_MESSAGE(f->violation, msg);
        fake_flash_free(f);
    }
}

static void test_power_cut_anywhere_in_update(void)
{
    sweep(512, 0);
}

static void test_power_cut_with_odd_chunks(void)
{
    sweep(77, 0);
}

static void test_power_cut_across_journal_rollover(void)
{
    /* 0x4000 / 32 = 512 records per page; the factory boot wrote one. */
    sweep(1024, 509);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_power_cut_anywhere_in_update);
    RUN_TEST(test_power_cut_with_odd_chunks);
    RUN_TEST(test_power_cut_across_journal_rollover);
    return UNITY_END();
}
