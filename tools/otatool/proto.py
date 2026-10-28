"""Host side of the update protocol (docs/update-protocol.md, lib/src/proto.c)."""

from __future__ import annotations

import struct
import time
import zlib
from dataclasses import dataclass
from typing import Callable, Protocol

from .image import HEADER_SIZE

SOF = b"\xA5\x5A"
HDR = struct.Struct("<BBH")  # type, seq, length (after the two SOF bytes)
MAX_CHUNK = 1024

CMD_HELLO, CMD_BEGIN, CMD_DATA, CMD_FINISH, CMD_RESET = range(1, 6)
RESPONSE = 0x80

# sota_err_t from lib/include/sota/common.h
ERRORS = {
    0: "ok",
    -1: "flash error",
    -2: "bad argument",
    -3: "no image header",
    -4: "inconsistent header",
    -5: "unknown key id",
    -6: "signature check failed",
    -7: "payload hash mismatch",
    -8: "image built for another slot or too large",
    -9: "rejected by anti-rollback (security counter too low)",
    -10: "no boot state",
    -11: "not allowed in the current state",
}
SLOT_STATES = {0: "empty", 1: "pending", 2: "confirmed", 3: "bad"}


class ProtocolError(RuntimeError):
    pass


class DeviceError(RuntimeError):
    def __init__(self, command: str, status: int):
        self.status = status
        super().__init__(f"{command}: {ERRORS.get(status, f'error {status}')}")


class Transport(Protocol):
    def write(self, data: bytes) -> None: ...
    def read(self, size: int, timeout: float) -> bytes: ...


def encode_frame(frame_type: int, seq: int, payload: bytes = b"") -> bytes:
    body = HDR.pack(frame_type, seq & 0xFF, len(payload)) + payload
    return SOF + body + struct.pack("<I", zlib.crc32(body))


class FrameReader:
    """Pulls frames out of a byte stream that may also carry console text."""

    def __init__(self) -> None:
        self.buf = bytearray()

    def feed(self, data: bytes) -> list[tuple[int, int, bytes]]:
        self.buf += data
        frames = []
        while True:
            start = self.buf.find(SOF)
            if start < 0:
                del self.buf[:-1]  # keep a trailing 0xA5 that may start a frame
                return frames
            del self.buf[:start]
            if len(self.buf) < 2 + HDR.size:
                return frames
            frame_type, seq, length = HDR.unpack_from(self.buf, 2)
            total = 2 + HDR.size + length + 4
            if length > MAX_CHUNK + 4:
                del self.buf[:1]
                continue
            if len(self.buf) < total:
                return frames
            body = bytes(self.buf[2:total - 4])
            (crc,) = struct.unpack_from("<I", self.buf, total - 4)
            if crc == zlib.crc32(body):
                frames.append((frame_type, seq, body[HDR.size:]))
                del self.buf[:total]
            else:
                del self.buf[:1]


@dataclass(frozen=True)
class DeviceInfo:
    proto_version: int
    max_chunk: int
    boot_version: int
    active: int
    target: int
    slot_states: tuple[int, int]
    security_floor: int
    slot_versions: tuple[int, int]

    @classmethod
    def parse(cls, data: bytes) -> "DeviceInfo":
        (proto_version, max_chunk, boot_version, active, target, sa, sb, floor,
         va, vb) = struct.unpack_from("<BHIBBBBIII", data)
        return cls(proto_version, max_chunk, boot_version, active, target, (sa, sb), floor,
                   (va, vb))


class Client:
    def __init__(self, transport: Transport, timeout: float = 2.0, retries: int = 3):
        self.transport = transport
        self.timeout = timeout
        self.retries = retries
        self.seq = 0
        self.reader = FrameReader()
        self.stray = bytearray()  # console text seen while waiting for frames

    def _await(self, frame_type: int, seq: int, timeout: float) -> bytes | None:
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            chunk = self.transport.read(4096, remaining)
            for t, s, payload in self.reader.feed(chunk):
                if t == frame_type | RESPONSE and s == seq:
                    return payload

    def request(self, cmd: int, payload: bytes = b"", timeout: float | None = None,
                retries: int | None = None) -> tuple[int, bytes]:
        self.seq = (self.seq + 1) & 0xFF
        frame = encode_frame(cmd, self.seq, payload)
        for _ in range(1 + (self.retries if retries is None else retries)):
            self.transport.write(frame)
            response = self._await(cmd, self.seq, timeout or self.timeout)
            if response is not None:
                status = struct.unpack_from("<b", response)[0]
                return status, response[1:]
        raise ProtocolError(f"no response to command {cmd}")

    def _check(self, name: str, status: int) -> None:
        if status != 0:
            raise DeviceError(name, status)

    def hello(self, timeout: float | None = None, retries: int | None = None) -> DeviceInfo:
        status, data = self.request(CMD_HELLO, timeout=timeout, retries=retries)
        self._check("hello", status)
        return DeviceInfo.parse(data)

    def wait_for_bootloader(self, total: float = 15.0, poke: bytes = b"") -> DeviceInfo:
        """Poll with HELLO until the bootloader answers (it may be rebooting).

        poke is sent before every attempt: b"u" asks a running application to
        reboot into update mode, and the bootloader treats it as line noise.
        Repeating it matters because the application reads single-character
        commands and a HELLO frame can contain bytes it acts on.
        """
        deadline = time.monotonic() + total
        while True:
            if poke:
                self.transport.write(poke)
            try:
                return self.hello(timeout=0.5, retries=0)
            except ProtocolError:
                if time.monotonic() > deadline:
                    raise ProtocolError("bootloader did not answer; is it in update mode?")

    def update(self, image: bytes, chunk: int = MAX_CHUNK,
               progress: Callable[[int, int], None] | None = None) -> None:
        header, payload = image[:HEADER_SIZE], image[HEADER_SIZE:]

        # BEGIN erases the slot, which takes seconds on real flash.
        status, _ = self.request(CMD_BEGIN, header, timeout=30.0, retries=1)
        self._check("begin", status)

        offset = 0
        while offset < len(payload):
            data = payload[offset:offset + chunk]
            status, rest = self.request(CMD_DATA, struct.pack("<I", offset) + data)
            next_offset = struct.unpack_from("<I", rest)[0]
            if status != 0 and next_offset == offset:
                self._check("data", status)
            offset = next_offset  # resume wherever the device says
            if progress:
                progress(offset, len(payload))

        status, _ = self.request(CMD_FINISH, timeout=30.0, retries=1)
        self._check("finish", status)

    def reset(self) -> None:
        status, _ = self.request(CMD_RESET)
        self._check("reset", status)
