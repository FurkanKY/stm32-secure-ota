import struct
import zlib

import pytest

from otatool.proto import (
    CMD_BEGIN,
    CMD_DATA,
    CMD_FINISH,
    CMD_HELLO,
    CMD_RESET,
    RESPONSE,
    Client,
    DeviceError,
    FrameReader,
    ProtocolError,
    encode_frame,
)


def test_frame_layout():
    frame = encode_frame(CMD_DATA, 7, b"\x01\x02")
    assert frame[:2] == b"\xA5\x5A"
    assert frame[2:6] == bytes([CMD_DATA, 7, 2, 0])
    assert frame[6:8] == b"\x01\x02"
    assert struct.unpack("<I", frame[8:])[0] == zlib.crc32(frame[2:8])


def test_reader_skips_console_text_and_corrupt_frames():
    good = encode_frame(CMD_HELLO | RESPONSE, 3, b"\x00abc")
    bad = bytearray(encode_frame(CMD_HELLO | RESPONSE, 2, b"\x00xyz"))
    bad[-1] ^= 1
    stream = b"recovery: waiting\r\n\xA5" + bytes(bad) + good
    reader = FrameReader()
    frames = []
    for i in range(0, len(stream), 5):  # arrives in small pieces
        frames += reader.feed(stream[i:i + 5])
    assert frames == [(CMD_HELLO | RESPONSE, 3, b"\x00abc")]


class FakeDevice:
    """Answers like the bootloader, optionally dropping some responses."""

    def __init__(self, drop_first_data_response=False):
        self.reader = FrameReader()
        self.out = bytearray()
        self.received = bytearray()
        self.drop = drop_first_data_response
        self.writes = 0

    def write(self, data):
        for t, seq, payload in self.reader.feed(data):
            status, body = 0, b""
            if t == CMD_HELLO:
                body = struct.pack("<BHIBBBBIII", 1, 1024, 0x20000, 0, 1, 2, 0, 1, 0x01000000, 0)
            elif t == CMD_DATA:
                offset = struct.unpack_from("<I", payload)[0]
                if offset == len(self.received):
                    self.received += payload[4:]
                    self.writes += 1
                body = struct.pack("<I", len(self.received))
                if self.drop:
                    self.drop = False
                    continue
            elif t == CMD_BEGIN:
                status = 0 if len(payload) == 512 else -2
            elif t == CMD_FINISH:
                status = -7 if self.received.startswith(b"BAD") else 0
            self.out += encode_frame(t | RESPONSE, seq, struct.pack("<b", status) + body)

    def read(self, size, timeout):
        data, self.out = bytes(self.out[:size]), self.out[size:]
        return data


def test_hello_parsed():
    info = Client(FakeDevice()).hello()
    assert info.target == 1 and info.slot_states == (2, 0)
    assert info.slot_versions == (0x01000000, 0)


def test_update_survives_lost_response():
    dev = FakeDevice(drop_first_data_response=True)
    payload = bytes(range(256)) * 10
    Client(dev, timeout=0.05).update(bytes(512) + payload, chunk=1000)
    assert dev.received == payload
    assert dev.writes == 3  # the retransmitted chunk was not written twice


def test_device_error_raised():
    with pytest.raises(DeviceError, match="hash"):
        Client(FakeDevice(), timeout=0.05).update(bytes(512) + b"BAD" + bytes(100))


def test_silent_device_times_out():
    class Silent:
        def write(self, data):
            pass

        def read(self, size, timeout):
            return b""

    with pytest.raises(ProtocolError):
        Client(Silent(), timeout=0.01, retries=1).request(CMD_RESET)
