/*
 * Power-loss-safe boot state.
 *
 * The state is a small record appended to a journal that spans two erase
 * units ("pages"). A record is never modified in place: every change appends
 * a new record with a higher sequence number and a CRC. After a reset the
 * valid record with the highest sequence number wins, so a write torn by a
 * power cut simply leaves the previous state in charge.
 *
 * When the current page is full, the other page is erased and the new record
 * goes to its start. Every record in the old page is older than every record
 * in the new one, so a cut during that erase cannot resurrect a stale state.
 */
#ifndef SOTA_BOOTSTATE_H
#define SOTA_BOOTSTATE_H

#include <stdint.h>

#include "sota/common.h"

typedef enum {
    SOTA_SLOT_EMPTY = 0,      /* nothing bootable, or being written */
    SOTA_SLOT_PENDING = 1,    /* new image on trial, not yet confirmed */
    SOTA_SLOT_CONFIRMED = 2,  /* image confirmed itself at least once */
    SOTA_SLOT_BAD = 3,        /* failed verification or never confirmed */
} sota_slot_state_t;

typedef struct {
    uint8_t slot[SOTA_NUM_SLOTS]; /* sota_slot_state_t */
    uint8_t active;               /* preferred confirmed slot */
    uint8_t attempts;             /* boots tried for the pending slot */
    uint32_t security_floor;      /* lowest security counter still allowed */
} sota_bootstate_t;

#define SOTA_RECORD_SIZE 32u
#define SOTA_RECORD_MAGIC 0x52545342u /* "BSTR" */

typedef struct {
    const sota_flash_t *flash;
    const sota_layout_t *layout;
    uint32_t seq;      /* sequence number of the newest valid record */
    uint8_t page;      /* page holding the newest record */
    uint32_t next_off; /* offset in that page for the next record */
} sota_journal_t;

/* Scan both pages. Returns SOTA_E_NO_STATE on a blank journal; *state is then
 * zeroed and the journal is still ready for appending. */
sota_err_t sota_journal_open(sota_journal_t *j, const sota_flash_t *flash,
                             const sota_layout_t *layout, sota_bootstate_t *state);

sota_err_t sota_journal_append(sota_journal_t *j, const sota_bootstate_t *state);

#endif /* SOTA_BOOTSTATE_H */
