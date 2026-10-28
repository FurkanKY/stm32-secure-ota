/*
 * Boot decision and image confirmation.
 *
 * sota_boot_decide() is a pure function of the stored state and of what is
 * in the slots, which keeps the policy easy to test:
 *
 *   1. A pending slot gets up to SOTA_MAX_BOOT_ATTEMPTS trial boots. The
 *      attempt counter is persisted before the jump, so a hang or crash that
 *      ends in a watchdog reset still counts. Once the attempts are used up
 *      without a confirmation, the slot is marked bad (rollback).
 *   2. Otherwise boot the active confirmed slot, falling back to the other
 *      confirmed slot if the active one no longer verifies.
 *   3. Otherwise stay in the bootloader and wait for an update (recovery).
 *
 * A slot whose security counter is below the stored floor is never booted.
 */
#ifndef SOTA_BOOT_H
#define SOTA_BOOT_H

#include <stdbool.h>
#include <stdint.h>

#include "sota/bootstate.h"
#include "sota/common.h"

#ifndef SOTA_MAX_BOOT_ATTEMPTS
#define SOTA_MAX_BOOT_ATTEMPTS 3u
#endif

#define SOTA_RECOVERY (-1)

typedef struct {
    bool valid;
    uint32_t security_counter;
} sota_slot_info_t;

/* Verify the image in a slot. Called at most once per slot per decision and
 * only when needed, because full verification hashes the whole payload. */
typedef void (*sota_probe_fn)(void *ctx, unsigned slot, sota_slot_info_t *info);

typedef struct {
    int slot;               /* slot to boot, or SOTA_RECOVERY */
    bool changed;           /* state must be persisted before acting */
    sota_bootstate_t state; /* state to persist */
} sota_decision_t;

void sota_boot_decide(const sota_bootstate_t *stored, bool have_state,
                      sota_probe_fn probe, void *ctx, sota_decision_t *out);

/* Called by the application once it is healthy. security_counter is the
 * counter from the running image's header; it raises the anti-rollback floor. */
sota_err_t sota_boot_confirm(sota_journal_t *j, sota_bootstate_t *state,
                             unsigned slot, uint32_t security_counter);

#endif /* SOTA_BOOT_H */
