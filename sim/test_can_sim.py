"""Updates over CAN (ISO-TP), through a second simulated board acting as a
UART <-> CAN gateway: host -> UART -> gateway -> CAN -> device."""

import time

import pytest

from conftest import REPO

from otatool.cli import main
from otatool.proto import DeviceError

A, B = 0, 1


def test_update_over_can(can_bench):
    can_bench.start()
    can_bench.gateway_uart.expect("sota-gateway")
    can_bench.expect("app running")

    client, info = can_bench.enter_update_mode()
    can_bench.expect("update request on CAN")
    assert (info.active, info.target) == (A, B)
    assert info.boot_version >= 0x00030000

    image = can_bench.image("app_v2_b")
    start = time.monotonic()
    client.update(image)
    print(f"{len(image)} bytes over CAN in {time.monotonic() - start:.2f} s (host time)")
    can_bench.reboot(client)

    can_bench.expect(r"booting slot B \(trial 1/3\)")
    can_bench.expect("app v2.0.0 in slot B")
    can_bench.expect("image confirmed")


def test_forged_image_rejected_over_can(can_bench):
    can_bench.start()
    can_bench.expect("app running")
    client, _ = can_bench.enter_update_mode()

    image = bytearray(can_bench.image("app_v2_b"))
    image[0x14] = 0x7F  # security counter, not covered by a new signature
    with pytest.raises(DeviceError, match="signature"):
        client.update(bytes(image))

    can_bench.reboot(client)
    can_bench.expect("booting slot A\r\n")


def test_otatool_cli_through_the_gateway(build_dir, capsys):
    from renode import Renode

    renode = Renode(build_dir)
    try:
        renode.setup(factory="app_v1_a")
        renode.add_can_gateway(connect_uart=False)  # otatool owns the gateway UART
        renode.start()
        port = f"tcp:127.0.0.1:{renode.gateway_port}"
        layout = str(REPO / "boards" / "stm32f407vg" / "layout.json")
        images = [str(build_dir / "app" / f"{n}.signed.bin") for n in ("app_v3_a", "app_v2_b")]

        assert main(["update", "--port", port, "--layout", layout, *images]) == 0
        assert "app_v2_b.signed.bin for slot B" in capsys.readouterr().out
        renode.uart.expect("image confirmed", timeout=60)
    finally:
        renode.close()
