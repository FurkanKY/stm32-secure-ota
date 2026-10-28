"""Firmware updates over the UART protocol, end to end in Renode."""

import pytest

from otatool.proto import DeviceError

A, B = 0, 1
CONFIRMED, EMPTY = 2, 0


def boot_to_app(device):
    device.start()
    device.expect("app v1.0.0 in slot A")
    device.expect("app running")


def test_update_trial_confirm(device):
    boot_to_app(device)
    client, info = device.enter_update_mode()
    assert (info.active, info.target) == (A, B)
    assert info.slot_versions == (0x01000000, 0)

    client.update(device.image("app_v2_b"))
    device.reboot(client)

    device.expect(r"booting slot B \(trial 1/3\)")
    device.expect("image confirmed")
    device.expect("app running")

    client, info = device.enter_update_mode()
    assert info.active == B and info.slot_states == (CONFIRMED, CONFIRMED)
    assert info.slot_versions == (0x01000000, 0x02000000)


def test_updates_alternate_slots_and_raise_the_floor(device):
    boot_to_app(device)
    device.install("app_v2_b")
    device.expect("image confirmed")
    device.expect("app running")

    # B is active now, so the next update goes to A: a security fix (counter 2).
    device.install("app_v3_a")
    device.expect(r"booting slot A \(trial 1/3\)")
    device.expect("app v3.0.1 in slot A, security counter 2")
    device.expect("image confirmed")
    device.expect("app running")

    # The old v2.0.0 (counter 1) can no longer be installed.
    client, info = device.enter_update_mode()
    assert info.security_floor == 2 and info.target == B
    with pytest.raises(DeviceError, match="anti-rollback") as err:
        client.update(device.image("app_v2_b"))
    assert err.value.status == -9


def test_forged_image_rejected_before_anything_is_erased(device):
    boot_to_app(device)
    client, _ = device.enter_update_mode()
    image = bytearray(device.image("app_v2_b"))
    image[0x10] ^= 0x01  # bump the version without re-signing

    with pytest.raises(DeviceError, match="signature") as err:
        client.update(bytes(image))
    assert err.value.status == -6
    assert client.hello().slot_states == (CONFIRMED, EMPTY)

    device.reboot(client)
    device.expect("booting slot A\r\n")


def test_corrupted_payload_rejected_at_finish(device):
    boot_to_app(device)
    client, _ = device.enter_update_mode()
    image = bytearray(device.image("app_v2_b"))
    image[0x200 + 1000] ^= 0xFF

    with pytest.raises(DeviceError, match="hash"):
        client.update(bytes(image))
    device.reboot(client)
    device.expect("slot A: v1.0.0")
    device.expect("booting slot A\r\n")


def test_broken_release_is_rolled_back(device):
    boot_to_app(device)
    device.install("app_bad_b")
    for attempt in (1, 2, 3):
        device.expect(rf"booting slot B \(trial {attempt}/3\)")
        device.expect("not confirming")
    device.expect("booting slot A\r\n", timeout=120)
    device.expect("app v1.0.0 in slot A")


def test_power_loss_during_update(device):
    boot_to_app(device)
    client, _ = device.enter_update_mode()

    class PowerCut(Exception):
        pass

    def cut_after_two_chunks(done, total):
        if done >= 2048:
            raise PowerCut

    with pytest.raises(PowerCut):
        client.update(device.image("app_v2_b"), progress=cut_after_two_chunks)
    device.uart.skip_to_end()
    device.power_cycle()

    # Half-written slot B is ignored, the factory image keeps running ...
    device.expect("booting slot A\r\n")
    device.expect("app running")
    # ... and the update simply goes through on the next attempt.
    device.install("app_v2_b")
    device.expect(r"booting slot B \(trial 1/3\)")
    device.expect("image confirmed")


def test_blank_device_accepts_first_image(blank_device):
    blank_device.start()
    blank_device.expect("recovery: no bootable image")
    client = blank_device.client()
    info = client.wait_for_bootloader()
    assert info.slot_versions == (0, 0) and info.target == B

    client.update(blank_device.image("app_v2_b"))
    blank_device.reboot(client)
    blank_device.expect(r"booting slot B \(trial 1/3\)")
    blank_device.expect("image confirmed")
