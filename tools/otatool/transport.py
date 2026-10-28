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


def open_transport(spec: str, baud: int = 115200):
    """'tcp:HOST:PORT' or a serial port name such as COM3 or /dev/ttyACM0."""
    if spec.startswith("tcp:"):
        host, _, port = spec[4:].rpartition(":")
        return TcpTransport(host or "127.0.0.1", int(port))
    return SerialTransport(spec, baud)
