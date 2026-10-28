/*
 * Shared types for the portable boot core. Nothing in lib/ touches hardware
 * directly: flash access goes through sota_flash_t, so the same code runs in
 * the bootloader, in the application and in host unit tests.
 */
#ifndef SOTA_COMMON_H
#define SOTA_COMMON_H

#include <stdbool.h>
#include <stdint.h>

#define SOTA_NUM_SLOTS 2u

/* Smallest unit the flash can program. 8 covers STM32F4 (any size works) and
 * the ECC parts (G4/L4/U5) that program 64-bit double words. */
#define SOTA_FLASH_ALIGN 8u

typedef enum {
    SOTA_OK = 0,
    SOTA_E_FLASH = -1,      /* driver reported an error */
    SOTA_E_ARG = -2,        /* bad argument or misaligned access */
    SOTA_E_MAGIC = -3,      /* no image header */
    SOTA_E_HEADER = -4,     /* header fields inconsistent */
    SOTA_E_KEY = -5,        /* unknown key id */
    SOTA_E_SIGNATURE = -6,  /* Ed25519 check failed */
    SOTA_E_HASH = -7,       /* payload does not match the signed hash */
    SOTA_E_SLOT = -8,       /* image linked for another slot or too large */
    SOTA_E_ROLLBACK = -9,   /* security counter below the stored floor */
    SOTA_E_NO_STATE = -10,  /* boot state journal is empty */
    SOTA_E_STATE = -11,     /* operation not allowed in the current state */
} sota_err_t;

typedef struct {
    uint32_t base;
    uint32_t size;
} sota_region_t;

typedef struct {
    sota_region_t slot[SOTA_NUM_SLOTS];
    /* Two erase units used as a ping-pong journal for the boot state. */
    sota_region_t state_page[2];
} sota_layout_t;

typedef struct sota_flash sota_flash_t;

struct sota_flash {
    /* All functions return 0 on success and a negative value on failure.
     * write: addr and len are multiples of SOTA_FLASH_ALIGN and the target
     *        bytes are erased.
     * erase: [addr, addr + len) covers whole erase units. */
    int (*read)(const sota_flash_t *f, uint32_t addr, void *buf, uint32_t len);
    int (*write)(const sota_flash_t *f, uint32_t addr, const void *buf, uint32_t len);
    int (*erase)(const sota_flash_t *f, uint32_t addr, uint32_t len);
    void *ctx;
};

#endif /* SOTA_COMMON_H */
