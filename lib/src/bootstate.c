#include "sota/bootstate.h"

#include <stddef.h>
#include <string.h>

#include "sota/crc32.h"

typedef struct {
    uint32_t magic;
    uint32_t seq;
    uint8_t slot[SOTA_NUM_SLOTS];
    uint8_t active;
    uint8_t attempts;
    uint32_t security_floor;
    uint32_t reserved[3]; /* zero, so a record can never read as erased */
    uint32_t crc;
} record_t;

_Static_assert(sizeof(record_t) == SOTA_RECORD_SIZE, "record size");
_Static_assert(SOTA_RECORD_SIZE % SOTA_FLASH_ALIGN == 0, "record alignment");

static int is_erased(const record_t *r)
{
    const uint8_t *p = (const uint8_t *)r;

    for (size_t i = 0; i < sizeof(*r); i++) {
        if (p[i] != 0xFFu) {
            return 0;
        }
    }
    return 1;
}

static int is_valid(const record_t *r)
{
    return r->magic == SOTA_RECORD_MAGIC &&
           r->crc == sota_crc32(0, r, offsetof(record_t, crc)) &&
           r->active < SOTA_NUM_SLOTS;
}

sota_err_t sota_journal_open(sota_journal_t *j, const sota_flash_t *flash,
                             const sota_layout_t *layout, sota_bootstate_t *state)
{
    uint32_t used_end[2] = {0, 0};
    record_t best = {0};
    int found = 0;

    j->flash = flash;
    j->layout = layout;
    j->seq = 0;
    j->page = 0;

    for (uint8_t page = 0; page < 2; page++) {
        const sota_region_t *reg = &layout->state_page[page];

        for (uint32_t off = 0; off + SOTA_RECORD_SIZE <= reg->size; off += SOTA_RECORD_SIZE) {
            record_t r;

            if (flash->read(flash, reg->base + off, &r, sizeof(r)) != 0) {
                return SOTA_E_FLASH;
            }
            if (is_erased(&r)) {
                continue;
            }
            used_end[page] = off + SOTA_RECORD_SIZE;
            if (is_valid(&r) && (!found || r.seq > best.seq)) {
                best = r;
                j->page = page;
                found = 1;
            }
        }
    }

    /* Append after the last non-erased block of the newest page, which also
     * skips a record that was torn by a power cut. */
    j->next_off = used_end[j->page];

    if (!found) {
        memset(state, 0, sizeof(*state));
        return SOTA_E_NO_STATE;
    }
    j->seq = best.seq;
    memcpy(state->slot, best.slot, sizeof(state->slot));
    state->active = best.active;
    state->attempts = best.attempts;
    state->security_floor = best.security_floor;
    return SOTA_OK;
}

sota_err_t sota_journal_append(sota_journal_t *j, const sota_bootstate_t *state)
{
    const sota_flash_t *flash = j->flash;
    record_t r;

    if (state->active >= SOTA_NUM_SLOTS) {
        return SOTA_E_ARG;
    }

    if (j->next_off + SOTA_RECORD_SIZE > j->layout->state_page[j->page].size) {
        uint8_t other = j->page ^ 1u;
        const sota_region_t *reg = &j->layout->state_page[other];

        if (flash->erase(flash, reg->base, reg->size) != 0) {
            return SOTA_E_FLASH;
        }
        j->page = other;
        j->next_off = 0;
    }

    memset(&r, 0, sizeof(r));
    r.magic = SOTA_RECORD_MAGIC;
    r.seq = j->seq + 1;
    memcpy(r.slot, state->slot, sizeof(r.slot));
    r.active = state->active;
    r.attempts = state->attempts;
    r.security_floor = state->security_floor;
    r.crc = sota_crc32(0, &r, offsetof(record_t, crc));

    /* Consume the location even if the write fails: it may hold a partial
     * record now and must not be programmed twice. */
    uint32_t addr = j->layout->state_page[j->page].base + j->next_off;
    j->next_off += SOTA_RECORD_SIZE;
    if (flash->write(flash, addr, &r, sizeof(r)) != 0) {
        return SOTA_E_FLASH;
    }
    j->seq = r.seq;
    return SOTA_OK;
}
