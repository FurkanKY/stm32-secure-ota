/* Public keys trusted by the bootloader, generated at build time by
 * `otatool pubkey --c` from SOTA_SIGNING_KEY. Index = image key_id. */
#ifndef BOOT_KEYS_H
#define BOOT_KEYS_H

#include <stdint.h>

extern const uint8_t boot_pubkeys[][32];
extern const unsigned boot_pubkeys_count;

#endif /* BOOT_KEYS_H */
