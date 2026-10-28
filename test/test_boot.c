/* Boot decision policy, driven by a scripted probe instead of real images. */
#include <string.h>

#include "sota/boot.h"
#include "test_support.h"
#include "unity.h"

enum { A = 0, B = 1 };

typedef struct {
    sota_slot_info_t info[SOTA_NUM_SLOTS];
    unsigned calls[SOTA_NUM_SLOTS];
} script_t;

static script_t script;

static void scripted_probe(void *ctx, unsigned slot, sota_slot_info_t *info)
{
    script_t *s = ctx;

    s->calls[slot]++;
    *info = s->info[slot];
}

void setUp(void)
{
    memset(&script, 0, sizeof(script));
}

void tearDown(void) {}

static void slot_image(unsigned slot, bool valid, uint32_t counter)
{
    script.info[slot].valid = valid;
    script.info[slot].security_counter = counter;
}

static sota_decision_t decide(const sota_bootstate_t *stored)
{
    sota_decision_t d;

    sota_boot_decide(stored, stored != NULL, scripted_probe, &script, &d);
    return d;
}

static sota_bootstate_t state(uint8_t a, uint8_t b, uint8_t active, uint8_t attempts,
                              uint32_t floor)
{
    sota_bootstate_t s = {.slot = {a, b}, .active = active, .attempts = attempts,
                          .security_floor = floor};
    return s;
}

/* --- first boot, no journal -------------------------------------------- */

static void test_factory_image_in_a_is_adopted(void)
{
    slot_image(A, true, 5);
    sota_decision_t d = decide(NULL);

    TEST_ASSERT_EQUAL_INT(A, d.slot);
    TEST_ASSERT_TRUE(d.changed);
    TEST_ASSERT_EQUAL_UINT8(SOTA_SLOT_CONFIRMED, d.state.slot[A]);
    TEST_ASSERT_EQUAL_UINT8(SOTA_SLOT_EMPTY, d.state.slot[B]);
    TEST_ASSERT_EQUAL_UINT32(5, d.state.security_floor);
}

static void test_factory_image_in_b_only(void)
{
    slot_image(B, true, 0);
    sota_decision_t d = decide(NULL);

    TEST_ASSERT_EQUAL_INT(B, d.slot);
    TEST_ASSERT_EQUAL_UINT8(B, d.state.active);
}

static void test_blank_device_enters_recovery(void)
{
    sota_decision_t d = decide(NULL);

    TEST_ASSERT_EQUAL_INT(SOTA_RECOVERY, d.slot);
}

/* --- normal boot --------------------------------------------------------- */

static void test_confirmed_active_boots_without_writing(void)
{
    sota_bootstate_t s = state(SOTA_SLOT_CONFIRMED, SOTA_SLOT_CONFIRMED, B, 0, 1);
    slot_image(A, true, 1);
    slot_image(B, true, 1);
    sota_decision_t d = decide(&s);

    TEST_ASSERT_EQUAL_INT(B, d.slot);
    TEST_ASSERT_FALSE(d.changed);
    TEST_ASSERT_EQUAL_UINT(0, script.calls[A]); /* the other slot is not hashed */
}

static void test_corrupted_active_falls_back_to_other_confirmed(void)
{
    sota_bootstate_t s = state(SOTA_SLOT_CONFIRMED, SOTA_SLOT_CONFIRMED, A, 0, 1);
    slot_image(A, false, 0);
    slot_image(B, true, 1);
    sota_decision_t d = decide(&s);

    TEST_ASSERT_EQUAL_INT(B, d.slot);
    TEST_ASSERT_EQUAL_UINT8(SOTA_SLOT_BAD, d.state.slot[A]);
    TEST_ASSERT_EQUAL_UINT8(B, d.state.active);
    TEST_ASSERT_TRUE(d.changed);
}

static void test_old_image_below_floor_is_not_booted(void)
{
    sota_bootstate_t s = state(SOTA_SLOT_CONFIRMED, SOTA_SLOT_CONFIRMED, A, 0, 3);
    slot_image(A, true, 3);
    slot_image(B, true, 2); /* superseded by a security fix */
    s.active = B;
    sota_decision_t d = decide(&s);

    TEST_ASSERT_EQUAL_INT(A, d.slot);
    TEST_ASSERT_EQUAL_UINT8(SOTA_SLOT_BAD, d.state.slot[B]);
}

/* --- trial boot and rollback -------------------------------------------- */

static void test_pending_image_gets_trial_boot(void)
{
    sota_bootstate_t s = state(SOTA_SLOT_CONFIRMED, SOTA_SLOT_PENDING, A, 0, 1);
    slot_image(A, true, 1);
    slot_image(B, true, 2);
    sota_decision_t d = decide(&s);

    TEST_ASSERT_EQUAL_INT(B, d.slot);
    TEST_ASSERT_EQUAL_UINT8(1, d.state.attempts);
    TEST_ASSERT_TRUE(d.changed); /* attempt is counted before the jump */
    TEST_ASSERT_EQUAL_UINT8(A, d.state.active);
}

static void test_unconfirmed_image_rolls_back(void)
{
    sota_bootstate_t s = state(SOTA_SLOT_CONFIRMED, SOTA_SLOT_PENDING, A, 0, 1);
    slot_image(A, true, 1);
    slot_image(B, true, 2);

    for (unsigned i = 0; i < SOTA_MAX_BOOT_ATTEMPTS; i++) {
        sota_decision_t d = decide(&s);
        TEST_ASSERT_EQUAL_INT(B, d.slot);
        s = d.state; /* app hangs, watchdog resets, no confirm */
    }
    sota_decision_t d = decide(&s);
    TEST_ASSERT_EQUAL_INT(A, d.slot);
    TEST_ASSERT_EQUAL_UINT8(SOTA_SLOT_BAD, d.state.slot[B]);
    TEST_ASSERT_EQUAL_UINT8(0, d.state.attempts);
}

static void test_invalid_pending_image_is_dropped(void)
{
    sota_bootstate_t s = state(SOTA_SLOT_CONFIRMED, SOTA_SLOT_PENDING, A, 0, 1);
    slot_image(A, true, 1);
    slot_image(B, false, 0);
    sota_decision_t d = decide(&s);

    TEST_ASSERT_EQUAL_INT(A, d.slot);
    TEST_ASSERT_EQUAL_UINT8(SOTA_SLOT_BAD, d.state.slot[B]);
}

static void test_pending_below_floor_is_dropped(void)
{
    sota_bootstate_t s = state(SOTA_SLOT_CONFIRMED, SOTA_SLOT_PENDING, A, 0, 4);
    slot_image(A, true, 4);
    slot_image(B, true, 3);
    sota_decision_t d = decide(&s);

    TEST_ASSERT_EQUAL_INT(A, d.slot);
    TEST_ASSERT_EQUAL_UINT8(SOTA_SLOT_BAD, d.state.slot[B]);
}

static void test_nothing_bootable_enters_recovery(void)
{
    sota_bootstate_t s = state(SOTA_SLOT_CONFIRMED, SOTA_SLOT_BAD, A, 0, 1);
    slot_image(A, false, 0);
    sota_decision_t d = decide(&s);

    TEST_ASSERT_EQUAL_INT(SOTA_RECOVERY, d.slot);
    TEST_ASSERT_EQUAL_UINT8(SOTA_SLOT_BAD, d.state.slot[A]);
}

/* --- confirmation -------------------------------------------------------- */

static void test_confirm_switches_active_and_raises_floor(void)
{
    fake_flash_t *f = fake_flash_new(3);
    sota_journal_t j;
    sota_bootstate_t s;
    sota_bootstate_t reread;

    sota_journal_open(&j, &f->ops, &test_layout, &s);
    s = state(SOTA_SLOT_CONFIRMED, SOTA_SLOT_PENDING, A, 1, 1);
    TEST_ASSERT_EQUAL(SOTA_OK, sota_boot_confirm(&j, &s, B, 2));
    TEST_ASSERT_EQUAL(SOTA_OK, sota_journal_open(&j, &f->ops, &test_layout, &reread));

    TEST_ASSERT_EQUAL_UINT8(SOTA_SLOT_CONFIRMED, reread.slot[B]);
    TEST_ASSERT_EQUAL_UINT8(B, reread.active);
    TEST_ASSERT_EQUAL_UINT8(0, reread.attempts);
    TEST_ASSERT_EQUAL_UINT32(2, reread.security_floor);
    fake_flash_free(f);
}

static void test_confirm_is_idempotent_and_does_not_write(void)
{
    fake_flash_t *f = fake_flash_new(3);
    sota_journal_t j;
    sota_bootstate_t s;

    sota_journal_open(&j, &f->ops, &test_layout, &s);
    s = state(SOTA_SLOT_CONFIRMED, SOTA_SLOT_EMPTY, A, 0, 1);
    TEST_ASSERT_EQUAL(SOTA_OK, sota_boot_confirm(&j, &s, A, 1));
    TEST_ASSERT_EQUAL_UINT(0, f->writes);
    fake_flash_free(f);
}

static void test_confirm_rejects_slot_that_is_not_pending(void)
{
    fake_flash_t *f = fake_flash_new(3);
    sota_journal_t j;
    sota_bootstate_t s;

    sota_journal_open(&j, &f->ops, &test_layout, &s);
    s = state(SOTA_SLOT_CONFIRMED, SOTA_SLOT_BAD, A, 0, 1);
    TEST_ASSERT_EQUAL(SOTA_E_STATE, sota_boot_confirm(&j, &s, B, 1));
    fake_flash_free(f);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_factory_image_in_a_is_adopted);
    RUN_TEST(test_factory_image_in_b_only);
    RUN_TEST(test_blank_device_enters_recovery);
    RUN_TEST(test_confirmed_active_boots_without_writing);
    RUN_TEST(test_corrupted_active_falls_back_to_other_confirmed);
    RUN_TEST(test_old_image_below_floor_is_not_booted);
    RUN_TEST(test_pending_image_gets_trial_boot);
    RUN_TEST(test_unconfirmed_image_rolls_back);
    RUN_TEST(test_invalid_pending_image_is_dropped);
    RUN_TEST(test_pending_below_floor_is_dropped);
    RUN_TEST(test_nothing_bootable_enters_recovery);
    RUN_TEST(test_confirm_switches_active_and_raises_floor);
    RUN_TEST(test_confirm_is_idempotent_and_does_not_write);
    RUN_TEST(test_confirm_rejects_slot_that_is_not_pending);
    return UNITY_END();
}
