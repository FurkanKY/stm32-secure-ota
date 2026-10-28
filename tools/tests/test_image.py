import hashlib
import struct

import pytest
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

from otatool.image import (
    HEADER_SIZE,
    MAGIC,
    SIG_OFFSET,
    ImageError,
    ImageHeader,
    check_vector_table,
    pack_version,
    sign_image,
    unpack_version,
    verify_image,
)

from conftest import fake_firmware

LOAD = 0x08020200


def make(key, payload=None, **kw):
    params = dict(load_addr=LOAD, version=pack_version("1.2.3"), security_counter=4)
    params.update(kw)
    return sign_image(payload if payload is not None else fake_firmware(LOAD), key, **params)


def test_round_trip(key):
    image = make(key)
    header = verify_image(image, key.public_key())
    assert header.load_addr == LOAD
    assert unpack_version(header.version) == "1.2.3"
    assert header.security_counter == 4
    assert header.payload_size == len(image) - HEADER_SIZE


def test_header_layout_matches_spec(key):
    """Byte offsets are part of the bootloader ABI; pin them down."""
    image = make(key, key_id=3)
    magic, hver, hsize, psize, load, ver, ctr, flags = struct.unpack_from("<IHHIIIII", image)
    assert (magic, hver, hsize) == (MAGIC, 1, 0x200)
    assert image[:4] == b"SOTA"
    assert (load, ver, ctr, flags) == (LOAD, 0x01020003, 4, 0)
    assert image[0x1C] == 3
    assert image[0x20:0x60] == hashlib.sha512(image[HEADER_SIZE:]).digest()
    assert not any(image[0x60:SIG_OFFSET])
    assert len(image[SIG_OFFSET:HEADER_SIZE]) == 64


def test_payload_padded_to_double_word(key):
    image = make(key, fake_firmware(LOAD, size=1001))
    assert (len(image) - HEADER_SIZE) % 8 == 0
    assert image[-7:] == b"\xff" * 7
    verify_image(image, key.public_key())


@pytest.mark.parametrize("offset", [0x10, 0x14, 0x20, 0x100, SIG_OFFSET + 5])
def test_header_tamper_detected(key, offset):
    image = bytearray(make(key))
    image[offset] ^= 0x01
    with pytest.raises(ImageError):
        verify_image(bytes(image), key.public_key())


def test_payload_tamper_detected(key):
    image = bytearray(make(key))
    image[HEADER_SIZE + 100] ^= 0x80
    with pytest.raises(ImageError, match="hash"):
        verify_image(bytes(image), key.public_key())


def test_truncated_payload_detected(key):
    with pytest.raises(ImageError, match="payload is"):
        verify_image(make(key)[:-8], key.public_key())


def test_wrong_key_rejected(key):
    with pytest.raises(ImageError, match="signature"):
        verify_image(make(key), Ed25519PrivateKey.generate().public_key())


def test_bad_magic_rejected(key):
    image = b"XXXX" + make(key)[4:]
    with pytest.raises(ImageError, match="magic"):
        ImageHeader.from_bytes(image)


def test_payload_too_big_for_slot(key):
    with pytest.raises(ImageError, match="slot holds"):
        make(key, fake_firmware(LOAD, size=4096), max_payload=2048)


@pytest.mark.parametrize("text,value", [("0.0.0", 0), ("1.2.3", 0x01020003),
                                        ("255.255.65535", 0xFFFFFFFF)])
def test_version_packing(text, value):
    assert pack_version(text) == value
    assert unpack_version(value) == text


@pytest.mark.parametrize("text", ["1.2", "1.2.3.4", "v1.2.3", "256.0.0", "0.0.65536"])
def test_version_rejects_bad_input(text):
    with pytest.raises(ImageError):
        pack_version(text)


def test_version_order_matches_integer_order():
    versions = ["0.9.9", "1.0.0", "1.0.10", "1.1.0", "2.0.0"]
    assert sorted(versions, key=pack_version) == versions


class TestVectorTable:
    def test_accepts_matching_binary(self):
        check_vector_table(fake_firmware(LOAD), LOAD, 0x20000000, 0x20000)

    def test_rejects_binary_linked_for_other_slot(self):
        with pytest.raises(ImageError, match="different slot"):
            check_vector_table(fake_firmware(0x08080200), LOAD, 0x20000000, 0x20000)

    def test_rejects_stack_outside_ram(self):
        with pytest.raises(ImageError, match="SP"):
            check_vector_table(fake_firmware(LOAD, sp=0x08000000), LOAD, 0x20000000, 0x20000)

    def test_rejects_missing_thumb_bit(self):
        payload = struct.pack("<II", 0x20020000, LOAD + 0x100) + bytes(100)
        with pytest.raises(ImageError, match="Thumb"):
            check_vector_table(payload, LOAD, 0x20000000, 0x20000)
