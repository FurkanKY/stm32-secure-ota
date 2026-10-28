/*
 * Signed image header, mirrored from docs/image-format.md and
 * tools/otatool/image.py. The struct is read straight from flash, which works
 * because both Cortex-M and the x86 hosts running the tests are little-endian.
 */
#ifndef SOTA_IMAGE_H
#define SOTA_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#include "sota/common.h"

#define SOTA_IMAGE_MAGIC     0x41544F53u /* "SOTA" */
#define SOTA_HEADER_VERSION  1u
#define SOTA_HEADER_SIZE     0x200u
#define SOTA_SIG_OFFSET      0x1C0u
#define SOTA_HASH_SIZE       64u
#define SOTA_SIG_SIZE        64u
#define SOTA_PUBKEY_SIZE     32u

typedef struct {
    uint32_t magic;
    uint16_t header_version;
    uint16_t header_size;
    uint32_t payload_size;
    uint32_t load_addr;
    uint32_t version;
    uint32_t security_counter;
    uint32_t flags;
    uint8_t key_id;
    uint8_t reserved0[3];
    uint8_t payload_hash[SOTA_HASH_SIZE];
    uint8_t reserved1[SOTA_SIG_OFFSET - 0x60];
    uint8_t signature[SOTA_SIG_SIZE];
} sota_image_header_t;

_Static_assert(sizeof(sota_image_header_t) == SOTA_HEADER_SIZE, "header size");
_Static_assert(offsetof(sota_image_header_t, payload_size) == 0x08, "payload_size");
_Static_assert(offsetof(sota_image_header_t, load_addr) == 0x0C, "load_addr");
_Static_assert(offsetof(sota_image_header_t, version) == 0x10, "version");
_Static_assert(offsetof(sota_image_header_t, security_counter) == 0x14, "counter");
_Static_assert(offsetof(sota_image_header_t, key_id) == 0x1C, "key_id");
_Static_assert(offsetof(sota_image_header_t, payload_hash) == 0x20, "payload_hash");
_Static_assert(offsetof(sota_image_header_t, signature) == SOTA_SIG_OFFSET, "signature");

typedef struct {
    const uint8_t (*keys)[SOTA_PUBKEY_SIZE];
    unsigned count;
} sota_keyring_t;

/* Check header structure, slot placement and signature. Does not touch the
 * payload, so it is cheap enough to run before erasing anything. */
sota_err_t sota_image_check_header(const sota_image_header_t *hdr,
                                   const sota_region_t *slot,
                                   const sota_keyring_t *keys);

/* Full verification of the image stored in a slot: header checks followed by
 * the payload hash. On success *hdr holds the header. */
sota_err_t sota_image_verify(const sota_flash_t *flash, const sota_region_t *slot,
                             const sota_keyring_t *keys, sota_image_header_t *hdr);

/* SHA-512 over a flash range, read in small chunks. */
sota_err_t sota_hash_flash(const sota_flash_t *flash, uint32_t addr, uint32_t len,
                           uint8_t out[SOTA_HASH_SIZE]);

#endif /* SOTA_IMAGE_H */
