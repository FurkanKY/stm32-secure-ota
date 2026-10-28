/* Verification of images signed by otatool. */
#include <string.h>

#include "sota/image.h"
#include "test_support.h"
#include "unity.h"

static fake_flash_t *flash;
static vector_t img_a;

void setUp(void)
{
    flash = fake_flash_new(1);
    img_a = vector_load("image_a_v1");
}

void tearDown(void)
{
    vector_free(&img_a);
    fake_flash_free(flash);
}

static sota_err_t verify_slot(unsigned slot, sota_image_header_t *hdr)
{
    return sota_image_verify(&flash->ops, &test_layout.slot[slot], test_keyring(), hdr);
}

static void test_valid_image_verifies(void)
{
    sota_image_header_t hdr;

    install_image(flash, 0, &img_a);
    TEST_ASSERT_EQUAL(SOTA_OK, verify_slot(0, &hdr));
    TEST_ASSERT_EQUAL_HEX32(0x01000000u, hdr.version);
    TEST_ASSERT_EQUAL_UINT32(1, hdr.security_counter);
    TEST_ASSERT_EQUAL_HEX32(test_layout.slot[0].base + SOTA_HEADER_SIZE, hdr.load_addr);
}

static void test_erased_slot_has_no_magic(void)
{
    sota_image_header_t hdr;

    TEST_ASSERT_EQUAL(SOTA_E_MAGIC, verify_slot(0, &hdr));
}

static void test_image_in_wrong_slot_rejected(void)
{
    sota_image_header_t hdr;

    install_image(flash, 1, &img_a); /* linked for A, placed in B */
    TEST_ASSERT_EQUAL(SOTA_E_SLOT, verify_slot(1, &hdr));
}

static void test_flipped_payload_bit_rejected(void)
{
    sota_image_header_t hdr;

    install_image(flash, 0, &img_a);
    fake_flash_ptr(flash, test_layout.slot[0].base + SOTA_HEADER_SIZE + 1234)[0] ^= 0x04;
    TEST_ASSERT_EQUAL(SOTA_E_HASH, verify_slot(0, &hdr));
}

static void test_flipped_header_bit_rejected(void)
{
    sota_image_header_t hdr;

    install_image(flash, 0, &img_a);
    fake_flash_ptr(flash, test_layout.slot[0].base + 0x14)[0] ^= 0x01; /* counter */
    TEST_ASSERT_EQUAL(SOTA_E_SIGNATURE, verify_slot(0, &hdr));
}

static void test_nonzero_reserved_rejected(void)
{
    sota_image_header_t hdr;

    install_image(flash, 0, &img_a);
    fake_flash_ptr(flash, test_layout.slot[0].base + 0x100)[0] = 1;
    TEST_ASSERT_EQUAL(SOTA_E_HEADER, verify_slot(0, &hdr));
}

static void test_foreign_key_rejected(void)
{
    sota_image_header_t hdr;
    vector_t other = vector_load("image_b_otherkey");

    install_image(flash, 1, &other);
    TEST_ASSERT_EQUAL(SOTA_E_SIGNATURE, verify_slot(1, &hdr));
    vector_free(&other);
}

static void test_unknown_key_id_rejected(void)
{
    sota_image_header_t hdr;

    memcpy(&hdr, img_a.data, sizeof(hdr));
    hdr.key_id = 1; /* keyring only holds key 0 */
    TEST_ASSERT_EQUAL(SOTA_E_KEY,
                      sota_image_check_header(&hdr, &test_layout.slot[0], test_keyring()));
}

static void test_oversized_payload_rejected(void)
{
    sota_image_header_t hdr;

    memcpy(&hdr, img_a.data, sizeof(hdr));
    hdr.payload_size = test_layout.slot[0].size; /* header no longer fits */
    TEST_ASSERT_EQUAL(SOTA_E_SLOT,
                      sota_image_check_header(&hdr, &test_layout.slot[0], test_keyring()));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_image_verifies);
    RUN_TEST(test_erased_slot_has_no_magic);
    RUN_TEST(test_image_in_wrong_slot_rejected);
    RUN_TEST(test_flipped_payload_bit_rejected);
    RUN_TEST(test_flipped_header_bit_rejected);
    RUN_TEST(test_nonzero_reserved_rejected);
    RUN_TEST(test_foreign_key_rejected);
    RUN_TEST(test_unknown_key_id_rejected);
    RUN_TEST(test_oversized_payload_rejected);
    return UNITY_END();
}
