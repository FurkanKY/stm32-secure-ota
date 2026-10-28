#include "sota/update.h"

#include <string.h>

#include "monocypher.h"

void sota_update_init(sota_update_t *u, const sota_flash_t *flash,
                      const sota_layout_t *layout, const sota_keyring_t *keys,
                      sota_journal_t *journal, sota_bootstate_t *state)
{
    memset(u, 0, sizeof(*u));
    u->flash = flash;
    u->layout = layout;
    u->keys = keys;
    u->journal = journal;
    u->state = state;
}

unsigned sota_update_target_slot(const sota_bootstate_t *state)
{
    return (state->active + 1u) % SOTA_NUM_SLOTS;
}

static sota_err_t set_slot_state(sota_update_t *u, uint8_t slot_state)
{
    sota_bootstate_t next = *u->state;

    next.slot[u->target] = slot_state;
    next.attempts = 0;
    sota_err_t err = sota_journal_append(u->journal, &next);
    if (err == SOTA_OK) {
        *u->state = next;
    }
    return err;
}

sota_err_t sota_update_begin(sota_update_t *u, const uint8_t header[SOTA_HEADER_SIZE],
                             unsigned *slot_out)
{
    const sota_region_t *slot = NULL;
    sota_err_t err;

    u->in_progress = 0;
    memcpy(&u->header, header, sizeof(u->header));

    for (unsigned i = 0; i < SOTA_NUM_SLOTS; i++) {
        if (u->layout->slot[i].base + SOTA_HEADER_SIZE == u->header.load_addr) {
            u->target = i;
            slot = &u->layout->slot[i];
        }
    }
    if (slot == NULL) {
        return SOTA_E_SLOT;
    }
    err = sota_image_check_header(&u->header, slot, u->keys);
    if (err != SOTA_OK) {
        return err;
    }
    if (u->header.security_counter < u->state->security_floor) {
        return SOTA_E_ROLLBACK;
    }
    /* Never overwrite the last known good image. */
    if (u->target == u->state->active &&
        u->state->slot[u->target] == SOTA_SLOT_CONFIRMED) {
        return SOTA_E_STATE;
    }

    /* Invalidate first, then erase: a cut in between leaves an EMPTY slot. */
    err = set_slot_state(u, SOTA_SLOT_EMPTY);
    if (err != SOTA_OK) {
        return err;
    }
    if (u->flash->erase(u->flash, slot->base, slot->size) != 0) {
        return SOTA_E_FLASH;
    }

    u->received = 0;
    u->programmed = 0;
    u->tail_len = 0;
    u->in_progress = 1;
    if (slot_out != NULL) {
        *slot_out = u->target;
    }
    return SOTA_OK;
}

static sota_err_t program(sota_update_t *u, const uint8_t *data, uint32_t len)
{
    if (u->flash->write(u->flash, u->header.load_addr + u->programmed, data, len) != 0) {
        u->in_progress = 0;
        return SOTA_E_FLASH;
    }
    u->programmed += len;
    return SOTA_OK;
}

sota_err_t sota_update_write(sota_update_t *u, uint32_t offset, const uint8_t *data,
                             uint32_t len)
{
    if (!u->in_progress) {
        return SOTA_E_STATE;
    }
    if (offset != u->received || len > u->header.payload_size - u->received) {
        return SOTA_E_ARG;
    }
    u->received += len;

    /* Top up a partial flash word left over from the previous chunk. */
    if (u->tail_len > 0) {
        uint32_t take = SOTA_FLASH_ALIGN - u->tail_len;

        if (take > len) {
            take = len;
        }
        memcpy(&u->tail[u->tail_len], data, take);
        u->tail_len += (uint8_t)take;
        data += take;
        len -= take;
        if (u->tail_len < SOTA_FLASH_ALIGN) {
            return SOTA_OK;
        }
        u->tail_len = 0;
        if (program(u, u->tail, SOTA_FLASH_ALIGN) != SOTA_OK) {
            return SOTA_E_FLASH;
        }
    }

    uint32_t whole = len & ~(SOTA_FLASH_ALIGN - 1u);
    if (whole > 0 && program(u, data, whole) != SOTA_OK) {
        return SOTA_E_FLASH;
    }

    memcpy(u->tail, data + whole, len - whole);
    u->tail_len = (uint8_t)(len - whole);
    return SOTA_OK;
}

sota_err_t sota_update_finish(sota_update_t *u)
{
    const sota_region_t *slot = &u->layout->slot[u->target];
    uint8_t hash[SOTA_HASH_SIZE];
    sota_image_header_t readback;
    sota_err_t err;

    if (!u->in_progress) {
        return SOTA_E_STATE;
    }
    if (u->programmed != u->header.payload_size) {
        return SOTA_E_ARG;
    }
    u->in_progress = 0;

    /* Check what actually landed in flash, not what we were sent. */
    err = sota_hash_flash(u->flash, u->header.load_addr, u->header.payload_size, hash);
    if (err != SOTA_OK) {
        return err;
    }
    if (crypto_verify64(hash, u->header.payload_hash) != 0) {
        return SOTA_E_HASH;
    }

    /* Header goes last; only now can the slot look like a valid image. */
    if (u->flash->write(u->flash, slot->base, &u->header, sizeof(u->header)) != 0 ||
        u->flash->read(u->flash, slot->base, &readback, sizeof(readback)) != 0) {
        return SOTA_E_FLASH;
    }
    if (memcmp(&readback, &u->header, sizeof(readback)) != 0) {
        return SOTA_E_FLASH;
    }

    return set_slot_state(u, SOTA_SLOT_PENDING);
}
