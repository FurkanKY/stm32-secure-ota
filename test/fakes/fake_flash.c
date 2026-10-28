#include "fake_flash.h"

#include <stdlib.h>
#include <string.h>

const sota_layout_t test_layout = {
    .slot = {
        {0x08020000u, 0x60000u},
        {0x08080000u, 0x60000u},
    },
    .state_page = {
        {0x08008000u, 0x4000u},
        {0x0800C000u, 0x4000u},
    },
};

/* STM32F407: 4 x 16 KiB, 1 x 64 KiB, 7 x 128 KiB */
static uint32_t sector_size_at(uint32_t off)
{
    if (off < 0x10000u) {
        return 0x4000u;
    }
    if (off < 0x20000u) {
        return 0x10000u;
    }
    return 0x20000u;
}

static uint32_t next_rand(fake_flash_t *f)
{
    /* xorshift32, deterministic per seed so failures can be replayed */
    f->rng ^= f->rng << 13;
    f->rng ^= f->rng >> 17;
    f->rng ^= f->rng << 5;
    return f->rng;
}

static bool in_range(uint32_t addr, uint32_t len)
{
    return addr >= FAKE_FLASH_BASE && len <= FAKE_FLASH_SIZE &&
           addr - FAKE_FLASH_BASE <= FAKE_FLASH_SIZE - len;
}

/* Returns true if this operation is the one that gets cut. */
static bool power_cut_now(fake_flash_t *f)
{
    if (f->cut_after < 0) {
        return false;
    }
    if (f->cut_after == 0) {
        f->powered = false;
        f->cut_after = -1;
        return true;
    }
    f->cut_after--;
    return false;
}

static int ff_read(const sota_flash_t *ops, uint32_t addr, void *buf, uint32_t len)
{
    fake_flash_t *f = ops->ctx;

    if (!f->powered || !in_range(addr, len)) {
        return -1;
    }
    memcpy(buf, &f->mem[addr - FAKE_FLASH_BASE], len);
    return 0;
}

static int ff_write(const sota_flash_t *ops, uint32_t addr, const void *buf, uint32_t len)
{
    fake_flash_t *f = ops->ctx;
    uint8_t *dst;

    if (!f->powered || !in_range(addr, len)) {
        return -1;
    }
    if (addr % SOTA_FLASH_ALIGN || len % SOTA_FLASH_ALIGN) {
        f->violation = true;
        return -1;
    }
    dst = &f->mem[addr - FAKE_FLASH_BASE];
    for (uint32_t i = 0; i < len; i++) {
        if (dst[i] != 0xFFu) {
            f->violation = true;
            return -1;
        }
    }
    f->writes++;
    if (power_cut_now(f)) {
        /* Some words made it, the one being programmed holds garbage. */
        uint32_t words = len / SOTA_FLASH_ALIGN;
        uint32_t done = next_rand(f) % words;

        memcpy(dst, buf, done * SOTA_FLASH_ALIGN);
        for (uint32_t i = 0; i < SOTA_FLASH_ALIGN; i++) {
            dst[done * SOTA_FLASH_ALIGN + i] &= (uint8_t)next_rand(f);
        }
        return -1;
    }
    memcpy(dst, buf, len);
    return 0;
}

static int ff_erase(const sota_flash_t *ops, uint32_t addr, uint32_t len)
{
    fake_flash_t *f = ops->ctx;
    uint32_t off = addr - FAKE_FLASH_BASE;
    uint32_t end = off + len;

    if (!f->powered || !in_range(addr, len)) {
        return -1;
    }
    while (off < end) {
        uint32_t size = sector_size_at(off);

        if (off % size != 0 || end - off < size) {
            f->violation = true;
            return -1;
        }
        f->erases++;
        if (power_cut_now(f)) {
            /* Half-erased sector: a random mix of erased and old bytes. */
            for (uint32_t i = 0; i < size; i++) {
                if (next_rand(f) & 1u) {
                    f->mem[off + i] = 0xFFu;
                }
            }
            return -1;
        }
        memset(&f->mem[off], 0xFF, size);
        off += size;
    }
    return 0;
}

fake_flash_t *fake_flash_new(uint32_t seed)
{
    fake_flash_t *f = malloc(sizeof(*f));

    if (f == NULL) {
        abort();
    }
    memset(f->mem, 0xFF, sizeof(f->mem));
    f->ops.read = ff_read;
    f->ops.write = ff_write;
    f->ops.erase = ff_erase;
    f->ops.ctx = f;
    f->cut_after = -1;
    f->powered = true;
    f->violation = false;
    f->writes = 0;
    f->erases = 0;
    f->rng = seed ? seed : 0x12345678u;
    return f;
}

void fake_flash_free(fake_flash_t *f)
{
    free(f);
}

void fake_flash_cut_after(fake_flash_t *f, long ops)
{
    f->cut_after = ops;
}

void fake_flash_power_on(fake_flash_t *f)
{
    f->powered = true;
    f->cut_after = -1;
}

void fake_flash_load(fake_flash_t *f, uint32_t addr, const void *data, uint32_t len)
{
    memcpy(&f->mem[addr - FAKE_FLASH_BASE], data, len);
}

uint8_t *fake_flash_ptr(fake_flash_t *f, uint32_t addr)
{
    return &f->mem[addr - FAKE_FLASH_BASE];
}
