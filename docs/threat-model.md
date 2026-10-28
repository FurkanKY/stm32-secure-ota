# Threat model

What the bootloader defends against, how, and what it deliberately leaves to other
layers. "Tested" points to the test that shows the defence working.

## Assets

- **Integrity of the running firmware.** Only code signed by the vendor may run.
- **Availability of the device.** A bad update, a cut cable or a power loss must not
  leave it unable to boot.
- **Freshness.** Once a vulnerability is fixed, the vulnerable release must not come back.

## Trust boundaries and assumptions

- The bootloader (sectors 0-1) and its embedded public key are trusted. On a product
  they must be write-protected (see *Out of scope*).
- Everything that arrives over UART or CAN is untrusted, including the image header,
  every length field and every offset.
- The flash contents of the application slots are untrusted at boot: they may have been
  half-written, corrupted, or written by someone with a debugger.
- The signing key never leaves the vendor (HSM or KMS in a real deployment; the key in
  `keys/` is a public development key).

## Threats and mitigations

| # | Threat | Mitigation | Tested |
|---|--------|------------|--------|
| T1 | Attacker installs modified or foreign firmware | Ed25519 signature over the header, which holds the SHA-512 of the payload. Checked before installing **and** on every boot | `test_image.c`, `test_forged_image_rejected_before_anything_is_erased`, `test_foreign_key_rejected` |
| T2 | Forged header makes the device erase a slot (cheap denial of service) | The header signature is checked in BEGIN, before anything is erased | `test_forged_header_rejected_before_erase` |
| T3 | Downgrade to an older, vulnerable but genuinely signed release | Monotonic security counter in the signed header; the highest confirmed value is stored and lower images are refused at install and at boot | `test_anti_rollback`, `test_security_counter_locks_out_old_image`, `test_updates_alternate_slots_and_raise_the_floor` |
| T4 | Image linked for slot A placed in slot B (or a slot overflow) | `load_addr` is signed and must match the slot; size is bounded by the slot | `test_image_in_wrong_slot_rejected`, `test_oversized_payload_rejected` |
| T5 | Bit flips or tampering in flash after installation | Full verification on every boot; a confirmed slot that no longer verifies is marked bad and the other confirmed slot is used | `test_corrupted_active_falls_back_to_other_confirmed`, `test_tampered_image_is_not_started` |
| T6 | Signed but broken release (crashes, hangs, bricks the product) | Trial boot: the image must confirm itself; the watchdog runs from the jump, and after 3 unconfirmed attempts the previous image comes back | `test_unconfirmed_image_rolls_back`, `test_broken_release_is_rolled_back` |
| T7 | Power loss or reset during an update or a state change | Append-only, CRC-protected boot state journal over two pages; slot marked EMPTY before erase; header written last; the running slot is never the target | `test_powercut.c` (every flash operation of a full cycle interrupted in turn), `test_power_loss_during_update` |
| T8 | Corrupted or malicious protocol frames (lengths, offsets, garbage) | CRC32 per frame, bounded lengths, offsets must be contiguous, the engine checks every size against the signed header | `test_proto.c`, `test_isotp.c` |
| T9 | Unsigned image written directly into a slot (debugger, factory mistake) | Once a journal exists, only slots installed through the update engine are booted | `test_image_not_installed_through_an_update_is_not_booted` |
| T10 | Bricking through an update request that never completes | After an update request from the app, 30 s of silence returns to a normal boot | not automated (simulation time) |

## Out of scope, and what a product would add

These are outside a bootloader's control or need hardware configuration that varies by
product. They are listed so nobody mistakes the demo for a finished product.

- **Debug port and read-out protection.** With SWD open, an attacker can rewrite the
  bootloader and the key. A product sets RDP level 1 or 2 and write-protects sectors 0-3
  (bootloader and journal) through the option bytes.
- **Confidentiality.** Images are signed, not encrypted. Anyone who captures an update can
  read the firmware. Encryption (AES-CTR with a per-device or per-release key) would
  need key storage this part does not have.
- **Fault injection and side channels.** Voltage or clock glitching can skip a signature
  check on most microcontrollers. Countermeasures (redundant checks, random delays,
  flow-integrity counters) are not implemented. Monocypher's verification is constant
  time, but that only matters for secret data, which verification does not handle.
- **Key revocation.** `key_id` is in the header and the keyring supports several keys,
  but there is no signed mechanism yet to retire a compromised key.
- **Authenticated transport.** Anyone on the UART or CAN bus can start an update. They
  still cannot install anything unsigned, but they can occupy the bootloader. Per-session
  authentication would close that.
- **Rollback of the bootloader itself.** The bootloader is not field-updatable by design.

## Crypto choices

- **Ed25519** (RFC 8032) instead of ECDSA P-256: deterministic signatures (no nonce to get
  wrong on the signing side), small keys, simple constant-time code. Verification costs
  about 1.8 million instructions on Cortex-M4.
- **SHA-512** for the payload: Ed25519 needs it anyway, so the bootloader carries one hash.
  On a 32-bit core it is slower than SHA-256 (about 119 instructions per byte here). A
  part with a hash accelerator would switch the payload hash to SHA-256.
- **Monocypher 4.0.2**: small, audited, CC0/BSD licensed, no dynamic memory.
