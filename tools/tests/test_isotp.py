import threading

import pytest

from otatool.isotp import IsoTp, segment
from otatool.proto import Client

from test_proto import FakeDevice


def test_wire_format_matches_the_c_implementation():
    # Same expectations as test_wire_format_of_a_segmented_message in test/test_isotp.c
    msg = bytes((i * 31 + 7) & 0xFF for i in range(20))
    frames = segment(msg)
    assert frames[0] == bytes([0x10, 20]) + msg[:6]
    assert frames[1] == bytes([0x21]) + msg[6:13]
    assert frames[2] == bytes([0x22]) + msg[13:20]
    assert segment(b"\x01\x02\x03\x04\x05") == [bytes([5, 1, 2, 3, 4, 5, 0xCC, 0xCC])]


def test_sequence_number_wraps():
    frames = segment(bytes(6 + 7 * 17))
    assert [f[0] for f in frames[15:18]] == [0x2F, 0x20, 0x21]


class Wire:
    """Two IsoTp endpoints joined by queues, standing in for a CAN bus."""

    def __init__(self):
        import queue

        self.a_to_b = queue.Queue()
        self.b_to_a = queue.Queue()

    @staticmethod
    def _recv(q):
        import queue

        def recv(timeout):
            try:
                return q.get(timeout=timeout)
            except queue.Empty:
                return None
        return recv

    def endpoints(self, block_size_b=3):
        a = IsoTp(self.a_to_b.put, self._recv(self.b_to_a))
        b = IsoTp(self.b_to_a.put, self._recv(self.a_to_b), block_size=block_size_b)
        return a, b


def test_long_message_with_block_size():
    host, device = Wire().endpoints(block_size_b=3)
    msg = bytes(range(256)) * 4
    result = {}
    t = threading.Thread(target=lambda: result.setdefault("msg", device.receive(5)))
    t.start()
    host.send(msg)
    t.join()
    assert result["msg"] == msg


def test_client_over_python_can_virtual_bus():
    can = pytest.importorskip("can")
    from otatool.isotp import REQUEST_ID, RESPONSE_ID
    from otatool.transport import CanTransport

    channel = "otatool-test"
    device_bus = can.Bus(interface="virtual", channel=channel)
    fake = FakeDevice()
    stop = threading.Event()

    def recv(timeout):
        msg = device_bus.recv(timeout)
        while msg is not None and msg.arbitration_id != REQUEST_ID:
            msg = device_bus.recv(timeout)
        return bytes(msg.data) if msg else None

    def send(data):
        device_bus.send(can.Message(arbitration_id=RESPONSE_ID, data=data,
                                    is_extended_id=False))

    device_tp = IsoTp(send, recv, block_size=3)

    def serve():
        while not stop.is_set():
            request = device_tp.receive(0.1)
            if request:
                fake.write(request)
                if fake.out:
                    device_tp.send(bytes(fake.out))
                    fake.out.clear()

    t = threading.Thread(target=serve, daemon=True)
    t.start()
    transport = CanTransport("virtual", channel)
    try:
        client = Client(transport, timeout=2.0)
        assert client.hello().target == 1
        payload = bytes(range(256)) * 6
        client.update(bytes(512) + payload)
        assert fake.received == payload
    finally:
        stop.set()
        t.join()
        transport.close()
        device_bus.shutdown()
