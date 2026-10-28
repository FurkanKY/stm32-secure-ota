import struct
from pathlib import Path

import pytest
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

from otatool.layout import load_layout

REPO = Path(__file__).resolve().parents[2]
LAYOUT_PATH = REPO / "boards" / "stm32f407vg" / "layout.json"


@pytest.fixture
def layout():
    return load_layout(LAYOUT_PATH)


@pytest.fixture
def layout_path():
    return LAYOUT_PATH


@pytest.fixture
def key():
    return Ed25519PrivateKey.generate()


def fake_firmware(load_addr: int, size: int = 1000, sp: int = 0x20020000) -> bytes:
    """A payload whose vector table looks like a real app linked at load_addr."""
    body = bytes((i * 7) & 0xFF for i in range(size - 8))
    return struct.pack("<II", sp, (load_addr + 0x1C1) | 1) + body
