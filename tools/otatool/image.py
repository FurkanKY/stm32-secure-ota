"""Signed image format shared with the bootloader.

The byte layout is specified in docs/image-format.md; the C side mirrors it in
lib/image/image_format.h. Keep the two in sync.
"""

from __future__ import annotations

import hashlib
import struct
from dataclasses import dataclass, replace

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric.ed25519 import (
    Ed25519PrivateKey,
    Ed25519PublicKey,
)

MAGIC = 0x41544F53  # "SOTA" when read from memory
HEADER_VERSION = 1
HEADER_SIZE = 0x200
SIG_OFFSET = 0x1C0
SIG_SIZE = 64
HASH_SIZE = 64
PAYLOAD_ALIGN = 8
PAD_BYTE = 0xFF  # erased flash value, keeps padding cheap to program

# magic, header_version, header_size, payload_size, load_addr, version,
# security_counter, flags, key_id, 3 reserved bytes, payload_hash
_FIXED = struct.Struct("<IHHIIIIIB3x64s")
assert _FIXED.size == 0x60


class ImageError(ValueError):
    """Raised when an image is malformed or fails verification."""


def pack_version(text: str) -> int:
    """Turn "1.2.3" into the packed u32 used in the header."""
    parts = text.split(".")
    if len(parts) != 3 or not all(p.isdigit() for p in parts):
        raise ImageError(f"version must look like MAJOR.MINOR.PATCH, got {text!r}")
    major, minor, patch = (int(p) for p in parts)
    if major > 0xFF or minor > 0xFF or patch > 0xFFFF:
        raise ImageError(f"version {text} out of range (255.255.65535 max)")
    return (major << 24) | (minor << 16) | patch


def unpack_version(value: int) -> str:
    return f"{value >> 24}.{(value >> 16) & 0xFF}.{value & 0xFFFF}"


def pad_payload(payload: bytes) -> bytes:
    remainder = len(payload) % PAYLOAD_ALIGN
    if remainder:
        payload += bytes([PAD_BYTE]) * (PAYLOAD_ALIGN - remainder)
    return payload


@dataclass(frozen=True)
class ImageHeader:
    payload_size: int
    load_addr: int
    version: int
    security_counter: int
    payload_hash: bytes
    key_id: int = 0
    flags: int = 0
    magic: int = MAGIC
    header_version: int = HEADER_VERSION
    header_size: int = HEADER_SIZE
    signature: bytes = bytes(SIG_SIZE)

    def signed_region(self) -> bytes:
        """Header bytes covered by the signature (everything before it)."""
        fixed = _FIXED.pack(
            self.magic,
            self.header_version,
            self.header_size,
            self.payload_size,
            self.load_addr,
            self.version,
            self.security_counter,
            self.flags,
            self.key_id,
            self.payload_hash,
        )
        return fixed + bytes(SIG_OFFSET - len(fixed))

    def to_bytes(self) -> bytes:
        return self.signed_region() + self.signature

    @classmethod
    def from_bytes(cls, data: bytes) -> "ImageHeader":
        if len(data) < HEADER_SIZE:
            raise ImageError(f"image shorter than the {HEADER_SIZE}-byte header")
        (magic, header_version, header_size, payload_size, load_addr, version,
         security_counter, flags, key_id, payload_hash) = _FIXED.unpack_from(data)
        if magic != MAGIC:
            raise ImageError(f"bad magic 0x{magic:08X}")
        if header_version != HEADER_VERSION:
            raise ImageError(f"unsupported header version {header_version}")
        if header_size != HEADER_SIZE:
            raise ImageError(f"unexpected header size 0x{header_size:X}")
        if any(data[_FIXED.size:SIG_OFFSET]) or any(data[0x1D:0x20]):
            raise ImageError("reserved header bytes are not zero")
        return cls(
            payload_size=payload_size,
            load_addr=load_addr,
            version=version,
            security_counter=security_counter,
            payload_hash=payload_hash,
            key_id=key_id,
            flags=flags,
            signature=bytes(data[SIG_OFFSET:HEADER_SIZE]),
        )


def check_vector_table(payload: bytes, load_addr: int, ram_base: int, ram_size: int) -> None:
    """Catch the classic mistake of signing a binary linked for the wrong address.

    Word 0 is the initial stack pointer, word 1 the reset handler. Both must point
    where the bootloader is about to jump.
    """
    if len(payload) < 8:
        raise ImageError("payload too small to contain a vector table")
    sp, reset = struct.unpack_from("<II", payload)
    if not (ram_base < sp <= ram_base + ram_size) or sp % 8:
        raise ImageError(f"initial SP 0x{sp:08X} is not an 8-byte aligned RAM address")
    if not reset & 1:
        raise ImageError(f"reset handler 0x{reset:08X} lacks the Thumb bit")
    if not (load_addr <= (reset & ~1) < load_addr + len(payload)):
        raise ImageError(
            f"reset handler 0x{reset:08X} is outside the image at 0x{load_addr:08X}; "
            "was the binary linked for a different slot?"
        )


def sign_image(
    payload: bytes,
    key: Ed25519PrivateKey,
    *,
    load_addr: int,
    version: int,
    security_counter: int,
    key_id: int = 0,
    max_payload: int | None = None,
) -> bytes:
    """Return header + padded payload, ready to be written to a slot."""
    payload = pad_payload(payload)
    if max_payload is not None and len(payload) > max_payload:
        raise ImageError(f"payload is {len(payload)} bytes, slot holds {max_payload}")
    if not 0 <= security_counter <= 0xFFFFFFFF:
        raise ImageError("security counter must fit in 32 bits")
    if not 0 <= key_id <= 0xFF:
        raise ImageError("key id must fit in 8 bits")
    unsigned = ImageHeader(
        payload_size=len(payload),
        load_addr=load_addr,
        version=version,
        security_counter=security_counter,
        payload_hash=hashlib.sha512(payload).digest(),
        key_id=key_id,
    )
    signature = key.sign(unsigned.signed_region())
    return replace(unsigned, signature=signature).to_bytes() + payload


def verify_image(image: bytes, public_key: Ed25519PublicKey) -> ImageHeader:
    """Run the same checks as the bootloader, in the same order."""
    header = ImageHeader.from_bytes(image)
    try:
        public_key.verify(header.signature, header.signed_region())
    except InvalidSignature:
        raise ImageError("signature check failed") from None
    payload = image[HEADER_SIZE:]
    if len(payload) != header.payload_size:
        raise ImageError(
            f"payload is {len(payload)} bytes, header says {header.payload_size}"
        )
    if hashlib.sha512(payload).digest() != header.payload_hash:
        raise ImageError("payload hash mismatch")
    return header
