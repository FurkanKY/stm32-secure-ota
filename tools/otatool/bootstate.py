"""Boot state journal records, mirroring lib/src/bootstate.c.

Used by the simulation tests to put a device into a given state (for example
"slot B holds an update on trial") without going through an update first, and
to decode the journal read back from a device.
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass

RECORD_MAGIC = 0x52545342  # "BSTR"
RECORD_SIZE = 32

EMPTY, PENDING, CONFIRMED, BAD = range(4)
STATE_NAMES = {EMPTY: "empty", PENDING: "pending", CONFIRMED: "confirmed", BAD: "bad"}

# magic, seq, slot A, slot B, active, attempts, security_floor, 12 reserved bytes
_BODY = struct.Struct("<IIBBBBI12x")
assert _BODY.size + 4 == RECORD_SIZE


@dataclass(frozen=True)
class BootState:
    slot_a: int
    slot_b: int
    active: int = 0
    attempts: int = 0
    security_floor: int = 0


def encode_record(state: BootState, seq: int = 1) -> bytes:
    body = _BODY.pack(RECORD_MAGIC, seq, state.slot_a, state.slot_b, state.active,
                      state.attempts, state.security_floor)
    return body + struct.pack("<I", zlib.crc32(body))


def decode_journal(*pages: bytes) -> tuple[int, BootState] | None:
    """Newest valid record across the journal pages, as (seq, state)."""
    best = None
    for page in pages:
        for off in range(0, len(page) - RECORD_SIZE + 1, RECORD_SIZE):
            rec = page[off:off + RECORD_SIZE]
            magic, seq, a, b, active, attempts, floor = _BODY.unpack_from(rec)
            (crc,) = struct.unpack_from("<I", rec, _BODY.size)
            if magic != RECORD_MAGIC or crc != zlib.crc32(rec[:_BODY.size]):
                continue
            if best is None or seq > best[0]:
                best = (seq, BootState(a, b, active, attempts, floor))
    return best
