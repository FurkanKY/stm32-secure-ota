# Roadmap

Milestones are done in order. Each one ends with something that runs and is tested.

## M1: Image format and signing tool ✅
- Signed image header spec ([image-format.md](image-format.md))
- `otatool` (Python): `keygen`, `pubkey`, `sign`, `inspect`
- Board flash layout as a single JSON source of truth
- pytest suite

## M2: Portable boot core, tested on the host ✅
- C mirror of the header with `_Static_assert` offset checks
- Image verification with Monocypher (Ed25519 + SHA-512)
- Power-loss-safe boot state journal (append-only records, CRC, ping-pong sectors)
- Boot decision state machine: trial boot, confirm, rollback, anti-rollback floor
- Transport-independent update engine (header first, payload, header written last)
- Fake flash driver that enforces erase/program rules and can cut power during any
  operation, leaving torn data behind
- Unit tests (Unity): every flash operation of a full update cycle is interrupted in
  turn and the device must still boot a valid image and finish the update on retry;
  the C side verifies images signed by `otatool`

## M3: Bootloader and demo app on STM32F407 (Renode)
- Bootloader: startup, flash driver, verify, jump to slot, IWDG, reset cause
- Demo app linked for slot A and slot B, `boot_confirm()` API
- Renode script that boots the bootloader and app

## M4: Update over UART
- Framed protocol (length + CRC32, ACK/NACK, resumable chunks)
- Bootloader-side receiver: header-first signature check, erase, program, header last
- `otatool update` over a serial port or Renode's TCP UART
- End-to-end Renode tests: good update, bad signature, rollback when the new app never
  confirms, power cut in the middle of an update

## M5: Continuous integration
- GitHub Actions: Python tests, host C tests, firmware build, Renode Robot tests
- Size report and test results as job artifacts

## M6: Update over CAN
- bxCAN driver and ISO-TP style segmentation
- Host side with python-can (CANable / PCAN)
- Renode CAN test if the model allows it

## M7: Real hardware
- Nucleo-F446RE layout and board support
- Measured numbers on hardware: bootloader size, verify time, update time

## M8: Documentation
- README with memory map and update flow diagrams, measurements, demo GIF
- Threat model and EU Cyber Resilience Act notes

## Later
- CAN FD on Nucleo-G474RE
- Download in the running application instead of in the bootloader
- Encrypted images, key rotation, read-out / write protection option bytes
