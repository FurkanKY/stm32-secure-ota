/*
 * RAM-backed flash that behaves like the real thing where it matters:
 *  - erase works on whole sectors (STM32F4 geometry by default),
 *  - programming needs aligned, previously erased bytes (as on ECC parts),
 *  - a power cut can be scheduled after N program/erase operations; the
 *    interrupted operation leaves torn data behind and every later access
 *    fails until fake_flash_power_on().
 */
#ifndef FAKE_FLASH_H
#define FAKE_FLASH_H

#include <stdbool.h>
#include <stdint.h>

#include "sota/common.h"

#define FAKE_FLASH_BASE 0x08000000u
#define FAKE_FLASH_SIZE (1024u * 1024u)

typedef struct {
    uint8_t mem[FAKE_FLASH_SIZE];
    sota_flash_t ops;
    long cut_after;      /* operations left before the cut, -1 = never */
    bool powered;
    bool violation;      /* a write hit non-erased bytes or was misaligned */
    unsigned writes;
    unsigned erases;
    uint32_t rng;
} fake_flash_t;

/* Allocates and erases a fresh device. */
fake_flash_t *fake_flash_new(uint32_t seed);
void fake_flash_free(fake_flash_t *f);

void fake_flash_cut_after(fake_flash_t *f, long ops);
void fake_flash_power_on(fake_flash_t *f);

/* Direct access for test setup, bypassing the program rules. */
void fake_flash_load(fake_flash_t *f, uint32_t addr, const void *data, uint32_t len);
uint8_t *fake_flash_ptr(fake_flash_t *f, uint32_t addr);

/* STM32F407 layout used throughout the tests (matches boards/stm32f407vg). */
extern const sota_layout_t test_layout;

#endif /* FAKE_FLASH_H */
