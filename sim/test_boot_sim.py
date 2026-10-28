"""End-to-end boot scenarios on a simulated STM32F407 (Renode)."""

from otatool.bootstate import BAD, CONFIRMED, EMPTY, PENDING, BootState

from conftest import SLOT_A

A, B = 0, 1


def test_factory_image_boots_and_survives_reset(device):
    device.start()
    device.expect(r"sota-boot \d+\.\d+\.\d+")
    device.expect(r"slot A: v1\.0\.0 counter 1, \d+ bytes, verified in \d+ ms")
    device.expect("booting slot A\r\n")
    device.expect("app v1.0.0 in slot A")
    device.expect("app running")

    device.send("r")
    device.expect("sota-boot")
    device.expect("booting slot A\r\n")
    device.expect("app running")


def test_update_on_trial_confirms_itself(device):
    device.load_image("app_v2_b", "B")
    device.set_state(BootState(CONFIRMED, PENDING, active=A, security_floor=1))
    device.start()

    device.expect(r"booting slot B \(trial 1/3\)")
    device.expect("app v2.0.0 in slot B, security counter 1, trial boot 1")
    device.expect("image confirmed")
    device.expect("app running")

    # Next boot: B is the confirmed, active image; no trial any more.
    device.send("r")
    device.expect("booting slot B\r\n")
    device.expect("app v2.0.0 in slot B")


def test_release_that_never_confirms_is_rolled_back(device):
    device.load_image("app_bad_b", "B")
    device.set_state(BootState(CONFIRMED, PENDING, active=A, security_floor=1))
    device.start()

    for attempt in (1, 2, 3):
        device.expect(rf"booting slot B \(trial {attempt}/3\)")
        device.expect("not confirming")
        # The app stops kicking the watchdog, which resets the chip.
    device.expect("sota-boot", timeout=120)
    device.expect("booting slot A\r\n")
    device.expect("app v1.0.0 in slot A")
    device.expect("app running")


def test_security_counter_locks_out_old_image(device):
    # Both slots hold confirmed images, but a security fix raised the floor
    # to 2. The active slot A still has the vulnerable v1.0.0 (counter 1).
    device.load_image("app_v3_b", "B")
    device.set_state(BootState(CONFIRMED, CONFIRMED, active=A, security_floor=2))
    device.start()

    device.expect("booting slot B\r\n")
    device.expect("app v3.0.0 in slot B, security counter 2")


def test_tampered_image_is_not_started(device):
    device.corrupt(SLOT_A + 0x200 + 0x400)  # one byte of the factory payload
    device.start()

    device.expect(r"slot A: no valid image \(error 7\)")  # SOTA_E_HASH
    device.expect("recovery: no bootable image")


def test_unsigned_slot_is_ignored(blank_device):
    blank_device.start()
    blank_device.expect(r"slot B: no valid image \(error 3\)")  # SOTA_E_MAGIC
    blank_device.expect(r"slot A: no valid image \(error 3\)")
    blank_device.expect("recovery: no bootable image")


def test_application_can_request_update_mode(device):
    device.start()
    device.expect("app running")
    device.send("u")
    device.expect("rebooting into update mode")
    device.expect("recovery: update requested by the application")


def test_bad_slot_stays_bad(device):
    # A slot already marked bad is never retried, even with a valid image in it.
    device.load_image("app_v2_b", "B")
    device.set_state(BootState(CONFIRMED, BAD, active=A, security_floor=1))
    device.start()
    device.expect("booting slot A\r\n")


def test_image_not_installed_through_an_update_is_not_booted(device):
    # Once the journal exists, a valid image that simply appears in a slot
    # (say, written with a debugger) is not trusted until installed properly.
    device.load_image("app_v2_b", "B")
    device.set_state(BootState(EMPTY, EMPTY))
    device.start()
    device.expect("recovery: no bootable image")
