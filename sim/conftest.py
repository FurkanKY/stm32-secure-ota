from __future__ import annotations

import re
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(REPO / "tools"))

from otatool.bootstate import BootState, encode_record  # noqa: E402
from otatool.layout import load_layout  # noqa: E402
from otatool.proto import Client, DeviceInfo  # noqa: E402
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
        """Invert one byte of flash, so it is guaranteed to change."""
        out = self.renode.command(f"sysbus ReadByte {address:#x}")
        value = int(re.search(r"0x[0-9A-Fa-f]+", out).group(0), 16)
        self.renode.command(f"sysbus WriteByte {address:#x} {value ^ 0xFF:#x}")

    def start(self) -> None:
        self.renode.start()

    def image(self, name: str) -> bytes:
        return (self.renode.build_dir / "app" / f"{name}.signed.bin").read_bytes()

    def client(self) -> Client:
        """Protocol client on the console UART, starting from fresh output."""
        self.uart.skip_to_end()
        return Client(self.uart, timeout=5.0)

    def enter_update_mode(self) -> tuple[Client, DeviceInfo]:
        """Ask the running app to reboot into the bootloader's update mode."""
        client = self.client()
        return client, client.wait_for_bootloader(total=60.0, poke=b"u")

    def install(self, name: str) -> None:
        """Full update of a running device, ending with the reboot into the trial."""
        client, _ = self.enter_update_mode()
        client.update(self.image(name))
        self.reboot(client)

    def reboot(self, client: Client) -> None:
        client.reset()
        # Console text resumes right after the RESET response; the client may
        # already have pulled some of it into its frame buffer.
        self.uart.cursor = self.uart.raw_cursor - len(client.reader.buf)

    def power_cycle(self) -> None:
        self.renode.command("machine Reset")


@pytest.fixture
def device(build_dir, tmp_path):
    """Bootloader plus the v1.0.0 factory image in slot A, not yet started."""
    renode = Renode(build_dir)
    try:
        renode.setup(factory="app_v1_a")
        yield Device(renode, tmp_path)
    finally:
        renode.close()


class CanBench(Device):
    """The device on a CAN bus behind a UART<->CAN gateway board. Protocol
    traffic goes through the gateway; expect() still reads the device console."""

    def __init__(self, renode: Renode, tmp: Path, gateway_uart):
        super().__init__(renode, tmp)
        self.gateway_uart = gateway_uart

    def client(self) -> Client:
        self.gateway_uart.skip_to_end()
        return Client(self.gateway_uart, timeout=5.0)

    def enter_update_mode(self) -> tuple[Client, DeviceInfo]:
        # No poke needed: the app reboots into update mode when it sees the
        # HELLO frames on its CAN request id.
        client = self.client()
        return client, client.wait_for_bootloader(total=60.0)

    def reboot(self, client: Client) -> None:
        self.uart.skip_to_end()  # console text after the reset is all new
        client.reset()


@pytest.fixture
def can_bench(build_dir, tmp_path):
    renode = Renode(build_dir)
    try:
        renode.setup(factory="app_v1_a")
        bench = CanBench(renode, tmp_path, renode.add_can_gateway())
        yield bench
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
