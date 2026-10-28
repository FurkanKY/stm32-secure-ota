#include "test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"

#ifndef VECTOR_DIR
#error "VECTOR_DIR must point at test/vectors"
#endif

vector_t vector_load(const char *name)
{
    char path[512];
    vector_t v = {0};
    FILE *fp;

    snprintf(path, sizeof(path), "%s/%s.bin", VECTOR_DIR, name);
    fp = fopen(path, "rb");
    if (fp == NULL) {
        TEST_FAIL_MESSAGE("missing test vector, run tools/gen_test_vectors.py");
    }
    fseek(fp, 0, SEEK_END);
    v.len = (uint32_t)ftell(fp);
    fseek(fp, 0, SEEK_SET);
    v.data = malloc(v.len);
    TEST_ASSERT_NOT_NULL(v.data);
    TEST_ASSERT_EQUAL_UINT32(v.len, fread(v.data, 1, v.len, fp));
    fclose(fp);
    return v;
}

void vector_free(vector_t *v)
{
    free(v->data);
    v->data = NULL;
    v->len = 0;
}

const sota_keyring_t *test_keyring(void)
{
    static uint8_t keys[1][SOTA_PUBKEY_SIZE];
    static sota_keyring_t ring = {(const uint8_t (*)[SOTA_PUBKEY_SIZE])keys, 1};
    static bool loaded;

    if (!loaded) {
        vector_t pub = vector_load("pubkey");
        TEST_ASSERT_EQUAL_UINT32(SOTA_PUBKEY_SIZE, pub.len);
        memcpy(keys[0], pub.data, SOTA_PUBKEY_SIZE);
        vector_free(&pub);
        loaded = true;
    }
    return &ring;
}

void install_image(fake_flash_t *f, unsigned slot, const vector_t *image)
{
    fake_flash_load(f, test_layout.slot[slot].base, image->data, image->len);
}

void probe_with_verify(void *ctx, unsigned slot, sota_slot_info_t *info)
{
    fake_flash_t *f = ctx;
    sota_image_header_t hdr;

    info->valid = sota_image_verify(&f->ops, &test_layout.slot[slot], test_keyring(),
                                    &hdr) == SOTA_OK;
    info->security_counter = info->valid ? hdr.security_counter : 0;
}

int run_bootloader(fake_flash_t *f, sota_bootstate_t *state_out)
{
    sota_journal_t journal;
    sota_bootstate_t stored;
    sota_decision_t d;
    sota_err_t err = sota_journal_open(&journal, &f->ops, &test_layout, &stored);

    if (err != SOTA_OK && err != SOTA_E_NO_STATE) {
        return -2;
    }
    sota_boot_decide(&stored, err == SOTA_OK, probe_with_verify, f, &d);
    if (d.changed && sota_journal_append(&journal, &d.state) != SOTA_OK) {
        return -2;
    }
    if (state_out != NULL) {
        *state_out = d.state;
    }
    return d.slot;
}
