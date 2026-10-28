#include "sota/boot.h"

#include <string.h>

typedef struct {
    sota_probe_fn probe;
    void *ctx;
    uint8_t probed[SOTA_NUM_SLOTS];
    sota_slot_info_t info[SOTA_NUM_SLOTS];
} probe_cache_t;

static const sota_slot_info_t *slot_info(probe_cache_t *c, unsigned slot)
{
    if (!c->probed[slot]) {
        memset(&c->info[slot], 0, sizeof(c->info[slot]));
        c->probe(c->ctx, slot, &c->info[slot]);
        c->probed[slot] = 1;
    }
    return &c->info[slot];
}

static int bootable(probe_cache_t *c, unsigned slot, uint32_t floor)
{
    const sota_slot_info_t *info = slot_info(c, slot);

    return info->valid && info->security_counter >= floor;
}

/* First boot after flashing at the factory: there is no journal yet, so every
 * slot that holds a valid image counts as confirmed and slot A is preferred. */
static void initial_state(probe_cache_t *c, sota_bootstate_t *s)
{
    memset(s, 0, sizeof(*s));
    for (unsigned i = SOTA_NUM_SLOTS; i-- > 0;) {
        if (bootable(c, i, 0)) {
            s->slot[i] = SOTA_SLOT_CONFIRMED;
            s->active = (uint8_t)i;
        }
    }
    if (s->slot[s->active] == SOTA_SLOT_CONFIRMED) {
        s->security_floor = slot_info(c, s->active)->security_counter;
    }
}

void sota_boot_decide(const sota_bootstate_t *stored, bool have_state,
                      sota_probe_fn probe, void *ctx, sota_decision_t *out)
{
    probe_cache_t cache = {.probe = probe, .ctx = ctx};
    sota_bootstate_t *s = &out->state;

    out->slot = SOTA_RECOVERY;
    out->changed = !have_state;
    if (have_state) {
        *s = *stored;
    } else {
        initial_state(&cache, s);
    }

    /* 1. Trial boot of a freshly installed image. */
    for (unsigned i = 0; i < SOTA_NUM_SLOTS; i++) {
        if (s->slot[i] != SOTA_SLOT_PENDING) {
            continue;
        }
        if (bootable(&cache, i, s->security_floor) && s->attempts < SOTA_MAX_BOOT_ATTEMPTS) {
            s->attempts++;
            out->changed = true;
            out->slot = (int)i;
            return;
        }
        /* Invalid, rolled back by the counter, or never confirmed: give up. */
        s->slot[i] = SOTA_SLOT_BAD;
        s->attempts = 0;
        out->changed = true;
    }

    /* 2. Last known good image, preferring the active slot. */
    for (unsigned n = 0; n < SOTA_NUM_SLOTS; n++) {
        unsigned i = (s->active + n) % SOTA_NUM_SLOTS;

        if (s->slot[i] != SOTA_SLOT_CONFIRMED) {
            continue;
        }
        if (bootable(&cache, i, s->security_floor)) {
            if (s->active != i) {
                s->active = (uint8_t)i;
                out->changed = true;
            }
            out->slot = (int)i;
            return;
        }
        s->slot[i] = SOTA_SLOT_BAD;
        out->changed = true;
    }

    /* 3. Nothing bootable: out->slot stays SOTA_RECOVERY. */
}

sota_err_t sota_boot_confirm(sota_journal_t *j, sota_bootstate_t *state,
                             unsigned slot, uint32_t security_counter)
{
    sota_bootstate_t next;

    if (slot >= SOTA_NUM_SLOTS) {
        return SOTA_E_ARG;
    }
    if (state->slot[slot] == SOTA_SLOT_CONFIRMED && state->active == slot) {
        return SOTA_OK; /* already confirmed, avoid wearing the journal */
    }
    if (state->slot[slot] != SOTA_SLOT_PENDING) {
        return SOTA_E_STATE;
    }

    next = *state;
    next.slot[slot] = SOTA_SLOT_CONFIRMED;
    next.active = (uint8_t)slot;
    next.attempts = 0;
    if (security_counter > next.security_floor) {
        next.security_floor = security_counter;
    }

    sota_err_t err = sota_journal_append(j, &next);
    if (err == SOTA_OK) {
        *state = next;
    }
    return err;
}
