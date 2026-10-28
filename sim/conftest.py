from __future__ import annotations

import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(REPO / "tools"))

from otatool.bootstate import BootState, encode_record  # noqa: E402
from otatool.layout import load_layout  # noqa: E402
from renode import Renode, find_renode  # noqa: E402

LAYOUT = load_layout(REPO / "boards" / "stm32f407vg" / "layout.json")
SLOT_A = LAYOUT.slot("A").base
SLOT_B = LAYOUT.slot("B").base


def pytest_addoption(parser):
    parser.addoption("--build", default=str(REPO / "build" / "fw"),
                     help="firmware build directory (cmake -B ... with the Arm toolchain)")


@pytest.fixture(scope="session")
def build_dir(request) -> Path:
    path = Path(request.config.getoption("--build")).resolve()
    if not (path / "boot" / "sota_boot.elf").exists():
        pytest.skip(f"no firmware build in {path}")
    try:
        find_renode()
    except FileNotFoundError as exc:
        pytest.skip(str(exc))
    return path


class Device:
    """A simulated board: Renode plus helpers to stage flash contents."""

    def __init__(self, renode: Renode, tmp: Path):
        self.renode = renode
        self.tmp = tmp

    @property
    def uart(self):
        return self.renode.uart

    def expect(self, pattern: str, timeout: float = 60.0):
        return self.renode.uart.expect(pattern, timeout)

    def send(self, text: str) -> None:
        self.renode.uart.write(text.encode())

    def load_image(self, name: str, slot: str) -> None:
        self.renode.load_image(name, LAYOUT.slot(slot).base)

    def set_state(self, state: BootState) -> None:
        """Write a single journal record, as if the device had got there itself."""
        path = self.tmp / "state.bin"
        path.write_bytes(encode_record(state))
        self.renode.load(path, LAYOUT.state_pages[0])

    def corrupt(self, address: int) -> None:
        self.renode.command(f"sysbus WriteByte {address:#x} 0x00")

    def start(self) -> None:
        self.renode.start()


@pytest.fixture
def device(build_dir, tmp_path):
    """Bootloader plus the v1.0.0 factory image in slot A, not yet started."""
    renode = Renode(build_dir)
    try:
        renode.setup(factory="app_v1_a")
        yield Device(renode, tmp_path)
    finally:
        renode.close()


@pytest.fixture
def blank_device(build_dir, tmp_path):
    """Bootloader only, both slots erased."""
    renode = Renode(build_dir)
    try:
        renode.setup(factory=None)
        yield Device(renode, tmp_path)
    finally:
        renode.close()
