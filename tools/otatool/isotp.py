"""ISO 15765-2 (ISO-TP) for the host, mirroring lib/src/isotp.c.

Bus-agnostic: give it a function that sends one 8-byte CAN frame and feed it
the frames received on the response id. Used by CanTransport with python-can.
"""

from __future__ import annotations

import time
from typing import Callable

PAD = 0xCC
REQUEST_ID = 0x7E0
RESPONSE_ID = 0x7E8
MAX_MESSAGE = 4095
TIMEOUT = 1.0  # N_Bs / N_Cr, seconds


class IsoTpError(RuntimeError):
    pass


def _pad(frame: bytes) -> bytes:
    return frame + bytes([PAD]) * (8 - len(frame))


def _stmin_seconds(raw: int) -> float:
    if raw <= 0x7F:
        return raw / 1000
    if 0xF1 <= raw <= 0xF9:
        return (raw - 0xF0) / 10000
    return 0.127


def segment(message: bytes) -> list[bytes]:
    """The frames of a message as sent with no flow control in between (the
    first frame and every consecutive frame). Handy for tests."""
    if len(message) <= 7:
        return [_pad(bytes([len(message)]) + message)]
    frames = [_pad(bytes([0x10 | len(message) >> 8, len(message) & 0xFF]) + message[:6])]
    sn = 1
    for off in range(6, len(message), 7):
        frames.append(_pad(bytes([0x20 | sn]) + message[off:off + 7]))
        sn = (sn + 1) & 0x0F
    return frames


class IsoTp:
    def __init__(self, send_frame: Callable[[bytes], None],
                 recv_frame: Callable[[float], bytes | None],
                 block_size: int = 0, stmin: int = 0):
        """send_frame(data) sends one frame on our transmit id; recv_frame(timeout)
        returns the data of the next frame on our receive id, or None."""
        self.send_frame = send_frame
        self.recv_frame = recv_frame
        self.block_size = block_size
        self.stmin = stmin

    def send(self, message: bytes) -> None:
        if not message or len(message) > MAX_MESSAGE:
            raise IsoTpError(f"cannot send {len(message)} bytes")
        frames = segment(message)
        self.send_frame(frames[0])
        if len(frames) == 1:
            return

        pending = frames[1:]
        while pending:
            block, separation = self._await_flow_control()
            count = block or len(pending)
            for i, frame in enumerate(pending[:count]):
                if i and separation:
                    time.sleep(separation)
                self.send_frame(frame)
            pending = pending[count:]
            if pending and separation:
                time.sleep(separation)  # STmin also spans blocks

    def _await_flow_control(self) -> tuple[int, float]:
        deadline = time.monotonic() + TIMEOUT
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise IsoTpError("no flow control from the device")
            frame = self.recv_frame(remaining)
            if not frame or frame[0] >> 4 != 0x3:
                continue
            status = frame[0] & 0x0F
            if status == 0:
                return frame[1], _stmin_seconds(frame[2])
            if status == 1:
                deadline = time.monotonic() + TIMEOUT  # wait
                continue
            raise IsoTpError("device reported overflow")

    def receive(self, timeout: float) -> bytes | None:
        """Next complete message, or None if nothing arrived in time."""
        deadline = time.monotonic() + timeout
        buf = bytearray()
        expected = 0
        sn = 0
        left = 0
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            frame = self.recv_frame(min(remaining, TIMEOUT) if expected else remaining)
            if frame is None:
                if expected:
                    return None  # N_Cr: sender went quiet mid-message
                continue
            pci = frame[0] >> 4
            if pci == 0x0:
                length = frame[0] & 0x0F
                if 1 <= length <= 7:
                    return bytes(frame[1:1 + length])
            elif pci == 0x1:
                expected = (frame[0] & 0x0F) << 8 | frame[1]
                buf = bytearray(frame[2:8])
                sn, left = 1, self.block_size
                self.send_frame(_pad(bytes([0x30, self.block_size, self.stmin])))
            elif pci == 0x2 and expected:
                if frame[0] & 0x0F != sn:
                    expected = 0  # lost a frame; drop the message
                    continue
                buf += frame[1:1 + min(7, expected - len(buf))]
                sn = (sn + 1) & 0x0F
                if len(buf) >= expected:
                    return bytes(buf)
                if self.block_size:
                    left -= 1
                    if left == 0:
                        left = self.block_size
                        self.send_frame(_pad(bytes([0x30, self.block_size, self.stmin])))
