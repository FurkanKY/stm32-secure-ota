#include "sota/image.h"

#include <string.h>

#include "monocypher-ed25519.h"
#include "monocypher.h"

#define HASH_CHUNK 256u

static int all_zero(const uint8_t *p, size_t len)
{
    uint8_t acc = 0;

    while (len--) {
        acc |= *p++;
    }
    return acc == 0;
}

sota_err_t sota_image_check_header(const sota_image_header_t *hdr,
                                   const sota_region_t *slot,
                                   const sota_keyring_t *keys)
{
    if (hdr->magic != SOTA_IMAGE_MAGIC) {
        return SOTA_E_MAGIC;
    }
    if (hdr->header_version != SOTA_HEADER_VERSION || hdr->header_size != SOTA_HEADER_SIZE ||
        hdr->flags != 0 || !all_zero(hdr->reserved0, sizeof(hdr->reserved0)) ||
        !all_zero(hdr->reserved1, sizeof(hdr->reserved1))) {
        return SOTA_E_HEADER;
    }
    if (hdr->load_addr != slot->base + SOTA_HEADER_SIZE || hdr->payload_size == 0 ||
        hdr->payload_size > slot->size - SOTA_HEADER_SIZE ||
        hdr->payload_size % SOTA_FLASH_ALIGN != 0) {
        return SOTA_E_SLOT;
    }
    if (hdr->key_id >= keys->count) {
        return SOTA_E_KEY;
    }
    if (crypto_ed25519_check(hdr->signature, keys->keys[hdr->key_id], (const uint8_t *)hdr,
                             SOTA_SIG_OFFSET) != 0) {
        return SOTA_E_SIGNATURE;
    }
    return SOTA_OK;
}

sota_err_t sota_hash_flash(const sota_flash_t *flash, uint32_t addr, uint32_t len,
                           uint8_t out[SOTA_HASH_SIZE])
{
    crypto_sha512_ctx ctx;
    uint8_t buf[HASH_CHUNK];

    crypto_sha512_init(&ctx);
    while (len > 0) {
        uint32_t n = len < HASH_CHUNK ? len : HASH_CHUNK;

        if (flash->read(flash, addr, buf, n) != 0) {
            crypto_wipe(&ctx, sizeof(ctx));
            return SOTA_E_FLASH;
        }
        crypto_sha512_update(&ctx, buf, n);
        addr += n;
        len -= n;
    }
    crypto_sha512_final(&ctx, out);
    return SOTA_OK;
}

sota_err_t sota_image_verify(const sota_flash_t *flash, const sota_region_t *slot,
                             const sota_keyring_t *keys, sota_image_header_t *hdr)
{
    uint8_t hash[SOTA_HASH_SIZE];
    sota_err_t err;

    if (flash->read(flash, slot->base, hdr, sizeof(*hdr)) != 0) {
        return SOTA_E_FLASH;
    }
    err = sota_image_check_header(hdr, slot, keys);
    if (err != SOTA_OK) {
        return err;
    }
    err = sota_hash_flash(flash, hdr->load_addr, hdr->payload_size, hash);
    if (err != SOTA_OK) {
        return err;
    }
    return crypto_verify64(hash, hdr->payload_hash) == 0 ? SOTA_OK : SOTA_E_HASH;
}
