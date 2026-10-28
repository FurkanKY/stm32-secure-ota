"""Generate the signed images that the C unit tests verify.

The C tests check images produced by otatool, so a format change on either
side shows up as a test failure. Output is deterministic (Ed25519 signatures
are), so CI regenerates the files and fails if they differ from the committed
ones.

    python tools/gen_test_vectors.py
"""

from __future__ import annotations

import struct
from pathlib import Path

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

from otatool import keys
from otatool.bootstate import CONFIRMED, PENDING, BootState, encode_record
from otatool.image import pack_version, sign_image
from otatool.layout import load_layout

REPO = Path(__file__).resolve().parents[1]
OUT = REPO / "test" / "vectors"


def payload(load_addr: int, size: int, seed: int) -> bytes:
    """Deterministic fake firmware with a plausible vector table."""
    body = bytearray()
    x = seed
    while len(body) < size - 8:
        x = (x * 1103515245 + 12345) & 0x7FFFFFFF
        body += struct.pack("<I", x)
    return struct.pack("<II", 0x20020000, (load_addr + 0x101) | 1) + bytes(body[: size - 8])


def main() -> None:
    layout = load_layout(REPO / "boards" / "stm32f407vg" / "layout.json")
    dev_key = keys.load_private(REPO / "keys" / "dev_ed25519.pem")
    other_key = Ed25519PrivateKey.from_private_bytes(bytes([0x42]) * 32)

    images = {
        # name: (slot, version, security counter, payload size, key)
        "image_a_v1": ("A", "1.0.0", 1, 6000, dev_key),
        "image_b_v2": ("B", "2.0.0", 2, 9001, dev_key),
        "image_a_v3": ("A", "3.0.0", 2, 7000, dev_key),
        "image_b_v0": ("B", "0.9.0", 0, 5000, dev_key),
        "image_b_otherkey": ("B", "2.0.0", 2, 5000, other_key),
    }

    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "pubkey.bin").write_bytes(keys.raw_public(dev_key.public_key()))
    for name, (slot, version, counter, size, key) in images.items():
        load = layout.load_addr(slot)
        image = sign_image(
            payload(load, size, seed=len(name) * 7919 + size),
            key,
            load_addr=load,
            version=pack_version(version),
            security_counter=counter,
            max_payload=layout.max_payload(slot),
        )
        (OUT / f"{name}.bin").write_bytes(image)
        print(f"{name}.bin  slot {slot}  v{version}  ctr {counter}  {len(image)} bytes")

    # A journal record encoded in Python; the C journal must read it back.
    record = encode_record(
        BootState(slot_a=CONFIRMED, slot_b=PENDING, active=0, attempts=2, security_floor=7),
        seq=5,
    )
    (OUT / "journal_record.bin").write_bytes(record)
    print("journal_record.bin")


if __name__ == "__main__":
    main()
