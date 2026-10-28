/*
 * sota_flash_t for the STM32F4 embedded flash (RM0090 section 3).
 *
 * Code runs from the same flash bank it programs; the CPU simply stalls on
 * instruction fetches while an operation is in progress, which is fine for a
 * bootloader that has nothing else to do in the meantime.
 */
#include <string.h>

#include "board.h"
#include "stm32f4_regs.h"

#define FLASH_BASE_ADDR 0x08000000u
#define FLASH_SIZE      0x00100000u /* 1 MiB, sectors 0..11 */

/* Sector number and size for an offset into the 1 MiB single-bank array. */
static int sector_of(uint32_t off, uint32_t *size)
{
    if (off < 0x10000u) {
        *size = 0x4000u;
        return (int)(off / 0x4000u);
    }
    if (off < 0x20000u) {
        *size = 0x10000u;
        return 4;
    }
    *size = 0x20000u;
    return 5 + (int)((off - 0x20000u) / 0x20000u);
}

static int wait_idle(void)
{
    while (FLASH_SR & FLASH_SR_BSY) {
    }
    if (FLASH_SR & FLASH_SR_ERRORS) {
        FLASH_SR = FLASH_SR_ERRORS; /* write 1 to clear */
        return -1;
    }
    return 0;
}

static void unlock(void)
{
    if (FLASH_CR & FLASH_CR_LOCK) {
        FLASH_KEYR = FLASH_KEY1;
        FLASH_KEYR = FLASH_KEY2;
    }
    FLASH_SR = FLASH_SR_EOP | FLASH_SR_ERRORS;
}

static void lock(void)
{
    FLASH_CR = FLASH_CR_LOCK;
}

/* The ART data cache can hold bytes from before an erase. */
static void flush_data_cache(void)
{
    FLASH_ACR &= ~FLASH_ACR_DCEN;
    FLASH_ACR |= FLASH_ACR_DCRST;
    FLASH_ACR &= ~FLASH_ACR_DCRST;
    FLASH_ACR |= FLASH_ACR_DCEN;
}

static int in_range(uint32_t addr, uint32_t len)
{
    return addr >= FLASH_BASE_ADDR && len <= FLASH_SIZE &&
           addr - FLASH_BASE_ADDR <= FLASH_SIZE - len;
}

static int f4_read(const sota_flash_t *f, uint32_t addr, void *buf, uint32_t len)
{
    (void)f;
    if (!in_range(addr, len)) {
        return -1;
    }
    memcpy(buf, (const void *)addr, len);
    return 0;
}

static int f4_erase(const sota_flash_t *f, uint32_t addr, uint32_t len)
{
    uint32_t off = addr - FLASH_BASE_ADDR;
    uint32_t end = off + len;
    int rc = 0;

    (void)f;
    if (!in_range(addr, len)) {
        return -1;
    }
    unlock();
    while (off < end && rc == 0) {
        uint32_t size;
        int snb = sector_of(off, &size);

        if ((off & (size - 1u)) != 0 || end - off < size) {
            rc = -1;
            break;
        }
        rc = wait_idle();
        FLASH_CR = FLASH_CR_PSIZE_X32 | FLASH_CR_SER | ((uint32_t)snb << FLASH_CR_SNB_POS);
        FLASH_CR |= FLASH_CR_STRT;
        if (wait_idle() != 0) {
            rc = -1;
        }
        off += size;
    }
    lock();
    flush_data_cache();
    return rc;
}

static int f4_write(const sota_flash_t *f, uint32_t addr, const void *buf, uint32_t len)
{
    const uint8_t *src = buf;
    int rc = 0;

    (void)f;
    if (!in_range(addr, len) || (addr | len) % SOTA_FLASH_ALIGN != 0) {
        return -1;
    }
    unlock();
    FLASH_CR = FLASH_CR_PSIZE_X32 | FLASH_CR_PG;
    for (uint32_t i = 0; i < len && rc == 0; i += 4) {
        uint32_t word;

        memcpy(&word, src + i, sizeof(word)); /* source may be unaligned */
        *(volatile uint32_t *)(addr + i) = word;
        rc = wait_idle();
    }
    lock();
    if (rc == 0 && memcmp((const void *)addr, buf, len) != 0) {
        rc = -1;
    }
    return rc;
}

const sota_flash_t *board_flash(void)
{
    static const sota_flash_t flash = {
        .read = f4_read,
        .write = f4_write,
        .erase = f4_erase,
        .ctx = 0,
    };
    return &flash;
}
