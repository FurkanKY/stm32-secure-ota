/*
 * Transport-independent update engine.
 *
 * A transport (UART, CAN, ...) feeds it the header first and then the payload
 * in order. The sequence keeps the device bootable at every step:
 *
 *   begin   header signature checked, target slot marked EMPTY in the
 *           journal, then erased. The active slot is never the target.
 *   write   payload programmed after the header area, strictly in order.
 *   finish  payload hashed back from flash, header written last, image
 *           verified once more, slot marked PENDING for a trial boot.
 */
#ifndef SOTA_UPDATE_H
#define SOTA_UPDATE_H

#include <stdint.h>

#include "sota/bootstate.h"
#include "sota/common.h"
#include "sota/image.h"

typedef struct {
    const sota_flash_t *flash;
    const sota_layout_t *layout;
    const sota_keyring_t *keys;
    sota_journal_t *journal;
    sota_bootstate_t *state;

    sota_image_header_t header;
    unsigned target;
    uint32_t received;     /* payload bytes accepted so far */
    uint32_t programmed;   /* payload bytes already in flash */
    uint8_t tail[SOTA_FLASH_ALIGN]; /* bytes waiting for a full flash word */
    uint8_t tail_len;
    uint8_t in_progress;
} sota_update_t;

void sota_update_init(sota_update_t *u, const sota_flash_t *flash,
                      const sota_layout_t *layout, const sota_keyring_t *keys,
                      sota_journal_t *journal, sota_bootstate_t *state);

/* The image picks its slot through load_addr; slot_out reports it. */
sota_err_t sota_update_begin(sota_update_t *u, const uint8_t header[SOTA_HEADER_SIZE],
                             unsigned *slot_out);

sota_err_t sota_update_write(sota_update_t *u, uint32_t offset, const uint8_t *data,
                             uint32_t len);

sota_err_t sota_update_finish(sota_update_t *u);

/* Slot a host should build for: the one that is not active. */
unsigned sota_update_target_slot(const sota_bootstate_t *state);

#endif /* SOTA_UPDATE_H */
