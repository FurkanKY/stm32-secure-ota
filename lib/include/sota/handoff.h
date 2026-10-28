/*
 * A small block at the very end of RAM that survives a jump and a software
 * reset (RAM is not cleared by a reset, and neither image's startup code
 * touches it). The bootloader tells the application how it was started; the
 * application can ask the bootloader to wait for an update.
 */
#ifndef SOTA_HANDOFF_H
#define SOTA_HANDOFF_H

#include <stdint.h>

#define SOTA_HANDOFF_MAGIC 0x46464F48u /* "HOFF" */
#define SOTA_REQUEST_NONE 0u
#define SOTA_REQUEST_UPDATE 0x54445055u /* "UPDT" */

typedef struct {
    uint32_t magic;
    uint32_t request;
    uint32_t request_check; /* ~request, so stale RAM after power-up never matches */
    uint32_t boot_slot;
    uint32_t boot_attempt;  /* 0 for a confirmed image, 1..N during a trial */
    uint32_t reset_cause;   /* BOARD_RESET_* seen by the bootloader */
} sota_handoff_t;

/* Placed by the linker script at the end of RAM. */
extern volatile sota_handoff_t _sota_handoff;

static inline void sota_handoff_request(uint32_t request)
{
    _sota_handoff.magic = SOTA_HANDOFF_MAGIC;
    _sota_handoff.request = request;
    _sota_handoff.request_check = ~request;
}

/* Returns the pending request and clears it. */
static inline uint32_t sota_handoff_take_request(void)
{
    uint32_t req = SOTA_REQUEST_NONE;

    if (_sota_handoff.magic == SOTA_HANDOFF_MAGIC &&
        _sota_handoff.request_check == ~_sota_handoff.request) {
        req = _sota_handoff.request;
    }
    _sota_handoff.request = SOTA_REQUEST_NONE;
    _sota_handoff.request_check = ~(uint32_t)SOTA_REQUEST_NONE;
    return req;
}

#endif /* SOTA_HANDOFF_H */
