"""Byte transports for the update protocol: a serial port or a TCP socket
(Renode exposes the simulated UART as a TCP server)."""

from __future__ import annotations

import socket
import time


class SerialTransport:
    def __init__(self, port: str, baud: int):
        import serial  # pyserial, only needed for real hardware

        self.port = serial.Serial(port, baud, timeout=0)

    def write(self, data: bytes) -> None:
        self.port.write(data)

    def read(self, size: int, timeout: float) -> bytes:
        deadline = time.monotonic() + timeout
        while True:
            data = self.port.read(size)
            if data or time.monotonic() >= deadline:
                return data
            time.sleep(0.002)

    def close(self) -> None:
        self.port.close()


class TcpTransport:
    def __init__(self, host: str, port: int):
        self.sock = socket.create_connection((host, port), timeout=10)

    def write(self, data: bytes) -> None:
        self.sock.sendall(data)

    def read(self, size: int, timeout: float) -> bytes:
        self.sock.settimeout(max(timeout, 0.001))
        try:
            return self.sock.recv(size)
        except (socket.timeout, TimeoutError):
            return b""

    def close(self) -> None:
        self.sock.close()


class CanTransport:
    """Update protocol over CAN with ISO-TP, through any python-can adapter.

    Each protocol frame the client writes becomes one ISO-TP message on the
    request id; ISO-TP messages on the response id come back as bytes. Writes
    that are not protocol frames (such as the 'u' poke meant for a UART
    console) are ignored: on CAN the HELLO itself wakes the application.
    """

    def __init__(self, interface: str, channel: str, bitrate: int = 500000):
        import can  # python-can, optional: pip install "otatool[can]"

        from .isotp import REQUEST_ID, RESPONSE_ID, IsoTp

        self.bus = can.Bus(interface=interface, channel=channel, bitrate=bitrate,
                           can_filters=[{"can_id": RESPONSE_ID, "can_mask": 0x7FF,
                                         "extended": False}])
        self._can = can
        self.request_id = REQUEST_ID
        self.response_id = RESPONSE_ID
        self.tp = IsoTp(self._send_frame, self._recv_frame)
        self.rx = bytearray()

    def _send_frame(self, data: bytes) -> None:
        self.bus.send(self._can.Message(arbitration_id=self.request_id, data=data,
                                        is_extended_id=False))

    def _recv_frame(self, timeout: float) -> bytes | None:
        deadline = time.monotonic() + timeout
        while True:
            msg = self.bus.recv(max(deadline - time.monotonic(), 0))
            if msg is None:
                return None
            if msg.arbitration_id == self.response_id and not msg.is_extended_id:
                return bytes(msg.data)

    def write(self, data: bytes) -> None:
        if data[:2] == b"\xA5\x5A":
            self.tp.send(data)

    def read(self, size: int, timeout: float) -> bytes:
        if not self.rx:
            self.rx += self.tp.receive(timeout) or b""
        data, self.rx = bytes(self.rx[:size]), self.rx[size:]
        return data

    def close(self) -> None:
        self.bus.shutdown()


def open_transport(spec: str, baud: int = 115200):
    """Where to find the device:
      COM3, /dev/ttyACM0       serial port (direct UART or a UART<->CAN gateway)
      tcp:HOST:PORT            TCP socket, e.g. Renode's simulated UART
      can:INTERFACE:CHANNEL    python-can adapter, e.g. can:pcan:PCAN_USBBUS1,
                               can:slcan:COM7, can:socketcan:can0 (500 kbit/s;
                               append @BITRATE to change)
    """
    if spec.startswith("tcp:"):
        host, _, port = spec[4:].rpartition(":")
        return TcpTransport(host or "127.0.0.1", int(port))
    if spec.startswith("can:"):
        rest, _, bitrate = spec[4:].partition("@")
        interface, _, channel = rest.partition(":")
        return CanTransport(interface, channel, int(bitrate) if bitrate else 500000)
    return SerialTransport(spec, baud)
