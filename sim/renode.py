"""Drive a headless Renode instance for the simulation tests.

Renode is started with its monitor on a TCP port; the console UART is
exposed on a second port. Tests talk to both with plain sockets, so the same
harness works on Windows and in Linux CI without Robot Framework.
"""

from __future__ import annotations

import os
import re
import shutil
import socket
import subprocess
import threading
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
# The monitor speaks telnet and ends lines with "\n\r" before the prompt.
PROMPT = re.compile(rb"[\r\n]\([\w-]+\) $")


def find_renode() -> str:
    candidates = [
        os.environ.get("RENODE"),
        shutil.which("renode"),
        r"C:\Program Files\Renode\bin\Renode.exe",
    ]
    for c in candidates:
        if c and Path(c).exists():
            return c
    raise FileNotFoundError("Renode not found; set RENODE to its executable")


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def connect(port: int, timeout: float = 30.0) -> socket.socket:
    deadline = time.monotonic() + timeout
    while True:
        try:
            return socket.create_connection(("127.0.0.1", port), timeout=1.0)
        except OSError:
            if time.monotonic() > deadline:
                raise
            time.sleep(0.2)


class Uart:
    """Collects console output in the background; expect() waits for text."""

    def __init__(self, sock: socket.socket):
        self.sock = sock
        self.buffer = bytearray()
        self.cursor = 0
        self.raw_cursor = 0
        self.lock = threading.Condition()
        self.thread = threading.Thread(target=self._reader, daemon=True)
        self.thread.start()

    def _reader(self) -> None:
        self.sock.settimeout(None)
        while True:
            try:
                data = self.sock.recv(4096)
            except OSError:
                return
            if not data:
                return
            with self.lock:
                self.buffer += data
                self.lock.notify_all()

    def expect(self, pattern: str, timeout: float = 30.0) -> re.Match:
        """Wait for pattern after the previous match and move past it."""
        regex = re.compile(pattern.encode())
        deadline = time.monotonic() + timeout
        with self.lock:
            while True:
                m = regex.search(self.buffer, self.cursor)
                if m:
                    self.cursor = m.end()
                    return m
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    tail = self.buffer[self.cursor:].decode(errors="replace")
                    raise TimeoutError(f"no {pattern!r} on UART; got:\n{tail}")
                self.lock.wait(remaining)

    def write(self, data: bytes) -> None:
        self.sock.sendall(data)

    # Raw byte access, so the update protocol client can use the same socket.
    # It has its own read position, independent of expect().

    def read(self, size: int, timeout: float) -> bytes:
        deadline = time.monotonic() + timeout
        with self.lock:
            while len(self.buffer) <= self.raw_cursor:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return b""
                self.lock.wait(remaining)
            data = bytes(self.buffer[self.raw_cursor:self.raw_cursor + size])
            self.raw_cursor += len(data)
            return data

    def skip_to_end(self) -> None:
        """Forget everything received so far, for both expect() and read()."""
        with self.lock:
            self.cursor = self.raw_cursor = len(self.buffer)

    def text(self) -> str:
        with self.lock:
            return self.buffer.decode(errors="replace")


class Renode:
    def __init__(self, build_dir: Path):
        self.build_dir = Path(build_dir).resolve()
        self.monitor_port = free_port()
        self.uart_port = free_port()
        # Renode wants a real console handle on Windows, so log to a file
        # rather than /dev/null. The log is useful when a test fails anyway.
        self.log_path = self.build_dir / f"renode-{self.monitor_port}.log"
        self.log = open(self.log_path, "wb")
        self.proc = subprocess.Popen(
            [find_renode(), "--disable-xwt", "--plain", "-P", str(self.monitor_port)],
            stdout=self.log,
            stderr=subprocess.STDOUT,
        )
        self.monitor = connect(self.monitor_port, timeout=120.0)
        self._read_prompt(60.0)
        self.uart: Uart | None = None

    def _read_prompt(self, timeout: float) -> str:
        self.monitor.settimeout(timeout)
        data = bytearray()
        while not PROMPT.search(data):
            chunk = self.monitor.recv(4096)
            if not chunk:
                raise ConnectionError("Renode monitor closed")
            data += chunk
        return data.decode(errors="replace")

    def command(self, line: str, timeout: float = 60.0) -> str:
        self.monitor.sendall(line.encode() + b"\n")
        out = self._read_prompt(timeout)
        if "error" in out.lower() or "unknown command" in out.lower():
            raise RuntimeError(f"Renode command failed: {line}\n{out}")
        return out

    def setup(self, factory: str | None = "app_v1_a", connect_uart: bool = True) -> None:
        """Create the machine with the bootloader and an optional factory image."""
        self.command(f"$build=@{self.build_dir.as_posix()}")
        if factory:
            self.command(f"$factory=@{(self.build_dir / 'app' / factory).as_posix()}.signed.bin")
        else:
            self.command(f"$factory=@{(self.build_dir / 'flash_erased.bin').as_posix()}")
        self.command(f"$platform=@{(REPO / 'renode' / 'stm32f407_sota.repl').as_posix()}")
        self.command(f"include @{(REPO / 'renode' / 'stm32f407.resc').as_posix()}")
        self.command(f'emulation CreateServerSocketTerminal {self.uart_port} "uart" false')
        self.command("connector Connect sysbus.usart2 uart")
        if connect_uart:
            self.uart = Uart(connect(self.uart_port))

    def add_can_gateway(self) -> Uart:
        """Put the device on a CAN bus with a gateway board; return the
        gateway's UART, which carries the update protocol to the bus."""
        self.gateway_port = free_port()
        self.command('emulation CreateCANHub "canHub"')
        self.command('mach set "sota"')
        self.command("connector Connect sysbus.can1 canHub")
        self.command(f"include @{(REPO / 'renode' / 'gateway.resc').as_posix()}")
        self.command(f'emulation CreateServerSocketTerminal {self.gateway_port} "gw_uart" false')
        self.command("connector Connect sysbus.usart2 gw_uart")
        self.command('mach set "sota"')
        return Uart(connect(self.gateway_port))

    def load(self, path: Path, address: int) -> None:
        self.command(f"sysbus LoadBinary @{Path(path).as_posix()} {address:#x}")

    def load_image(self, name: str, slot_base: int) -> None:
        self.load(self.build_dir / "app" / f"{name}.signed.bin", slot_base)

    def start(self) -> None:
        self.command("start")

    def close(self) -> None:
        try:
            self.monitor.sendall(b"quit\n")
        except OSError:
            pass
        try:
            self.proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            self.proc.kill()
        self.log.close()
