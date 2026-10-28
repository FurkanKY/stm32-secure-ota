#include <string.h>

#include "sota/crc32.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_check_value(void)
{
    /* The standard check value for CRC-32/ISO-HDLC. */
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, sota_crc32(0, "123456789", 9));
}

static void test_empty_input(void)
{
    TEST_ASSERT_EQUAL_HEX32(0u, sota_crc32(0, "", 0));
}

static void test_incremental_matches_one_shot(void)
{
    const char *msg = "secure boot journal record";
    size_t len = strlen(msg);
    uint32_t crc = sota_crc32(0, msg, 7);

    crc = sota_crc32(crc, msg + 7, len - 7);
    TEST_ASSERT_EQUAL_HEX32(sota_crc32(0, msg, len), crc);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_check_value);
    RUN_TEST(test_empty_input);
    RUN_TEST(test_incremental_matches_one_shot);
    return UNITY_END();
}
