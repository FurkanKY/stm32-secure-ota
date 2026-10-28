/* Boot state journal: persistence, page rollover and torn writes. */
#include <string.h>

#include "sota/bootstate.h"
#include "test_support.h"
#include "unity.h"

#define RECORDS_PER_PAGE (0x4000u / SOTA_RECORD_SIZE)

static fake_flash_t *flash;

void setUp(void)
{
    flash = fake_flash_new(7);
}

void tearDown(void)
{
    TEST_ASSERT_FALSE_MESSAGE(flash->violation, "flash programming rule violated");
    fake_flash_free(flash);
}

static sota_bootstate_t make_state(uint32_t n)
{
    sota_bootstate_t s = {
        .slot = {SOTA_SLOT_CONFIRMED, (uint8_t)(n % 4)},
        .active = (uint8_t)(n % 2),
        .attempts = (uint8_t)(n % 3),
        .security_floor = n,
    };
    return s;
}

static void assert_state_equal(const sota_bootstate_t *a, const sota_bootstate_t *b)
{
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a->slot, b->slot, SOTA_NUM_SLOTS);
    TEST_ASSERT_EQUAL_UINT8(a->active, b->active);
    TEST_ASSERT_EQUAL_UINT8(a->attempts, b->attempts);
    TEST_ASSERT_EQUAL_UINT32(a->security_floor, b->security_floor);
}

static sota_err_t reopen(sota_journal_t *j, sota_bootstate_t *s)
{
    return sota_journal_open(j, &flash->ops, &test_layout, s);
}

static void test_blank_journal_reports_no_state(void)
{
    sota_journal_t j;
    sota_bootstate_t s;

    TEST_ASSERT_EQUAL(SOTA_E_NO_STATE, reopen(&j, &s));
    TEST_ASSERT_EQUAL_UINT32(0, s.security_floor);
}

static void test_latest_record_wins(void)
{
    sota_journal_t j;
    sota_bootstate_t s;

    reopen(&j, &s);
    for (uint32_t n = 1; n <= 5; n++) {
        sota_bootstate_t w = make_state(n);
        TEST_ASSERT_EQUAL(SOTA_OK, sota_journal_append(&j, &w));
    }
    TEST_ASSERT_EQUAL(SOTA_OK, reopen(&j, &s));
    sota_bootstate_t expect = make_state(5);
    assert_state_equal(&expect, &s);
    TEST_ASSERT_EQUAL_UINT32(5, j.seq);
}

static void test_rollover_keeps_state_across_many_pages(void)
{
    sota_journal_t j;
    sota_bootstate_t s;
    uint32_t total = RECORDS_PER_PAGE * 5 + 3;

    reopen(&j, &s);
    for (uint32_t n = 1; n <= total; n++) {
        sota_bootstate_t w = make_state(n);
        TEST_ASSERT_EQUAL(SOTA_OK, sota_journal_append(&j, &w));
        if (n % 97 == 0 || n == total) { /* reopen now and then, like a reset */
            sota_bootstate_t r;
            TEST_ASSERT_EQUAL(SOTA_OK, reopen(&j, &r));
            assert_state_equal(&w, &r);
        }
    }
    /* Each page was erased once per rollover, nothing more. */
    TEST_ASSERT_EQUAL_UINT(5, flash->erases);
}

static void test_torn_record_falls_back_to_previous(void)
{
    sota_journal_t j;
    sota_bootstate_t s;
    sota_bootstate_t first = make_state(1);
    sota_bootstate_t second = make_state(2);
    sota_bootstate_t third = make_state(3);

    reopen(&j, &s);
    TEST_ASSERT_EQUAL(SOTA_OK, sota_journal_append(&j, &first));
    fake_flash_cut_after(flash, 0);
    TEST_ASSERT_NOT_EQUAL(SOTA_OK, sota_journal_append(&j, &second));

    fake_flash_power_on(flash);
    TEST_ASSERT_EQUAL(SOTA_OK, reopen(&j, &s));
    assert_state_equal(&first, &s);

    /* The torn location is skipped, the next write lands after it. */
    TEST_ASSERT_EQUAL(SOTA_OK, sota_journal_append(&j, &third));
    TEST_ASSERT_EQUAL(SOTA_OK, reopen(&j, &s));
    assert_state_equal(&third, &s);
}

static void test_cut_during_rollover_erase(void)
{
    sota_journal_t j;
    sota_bootstate_t s;
    sota_bootstate_t last = make_state(RECORDS_PER_PAGE * 2);

    reopen(&j, &s);
    for (uint32_t n = 1; n <= RECORDS_PER_PAGE * 2; n++) { /* fill both pages */
        sota_bootstate_t w = make_state(n);
        TEST_ASSERT_EQUAL(SOTA_OK, sota_journal_append(&j, &w));
    }

    fake_flash_cut_after(flash, 0); /* the next erase of page 0 is torn */
    sota_bootstate_t w = make_state(9999);
    TEST_ASSERT_NOT_EQUAL(SOTA_OK, sota_journal_append(&j, &w));

    fake_flash_power_on(flash);
    TEST_ASSERT_EQUAL(SOTA_OK, reopen(&j, &s));
    assert_state_equal(&last, &s);
    TEST_ASSERT_EQUAL(SOTA_OK, sota_journal_append(&j, &w));
    TEST_ASSERT_EQUAL(SOTA_OK, reopen(&j, &s));
    assert_state_equal(&w, &s);
}

static void test_corrupted_record_ignored(void)
{
    sota_journal_t j;
    sota_bootstate_t s;
    sota_bootstate_t first = make_state(1);
    sota_bootstate_t second = make_state(2);

    reopen(&j, &s);
    sota_journal_append(&j, &first);
    sota_journal_append(&j, &second);
    /* Flip a bit in the newest record: its CRC no longer matches. */
    fake_flash_ptr(flash, test_layout.state_page[0].base + SOTA_RECORD_SIZE + 12)[0] ^= 0x10;
    TEST_ASSERT_EQUAL(SOTA_OK, reopen(&j, &s));
    assert_state_equal(&first, &s);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_blank_journal_reports_no_state);
    RUN_TEST(test_latest_record_wins);
    RUN_TEST(test_rollover_keeps_state_across_many_pages);
    RUN_TEST(test_torn_record_falls_back_to_previous);
    RUN_TEST(test_cut_during_rollover_erase);
    RUN_TEST(test_corrupted_record_ignored);
    return UNITY_END();
}
