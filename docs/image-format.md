# Signed image format (v1)

Every application image that the bootloader accepts is a **512-byte header followed by
the payload**. The payload is a raw binary whose first word is the application's
vector table. The header occupies the first 512 bytes of a slot, so the vector table
lands at `slot_base + 0x200`, which satisfies the Cortex-M4 `VTOR` alignment rule for
every STM32F4/G4 part (vector tables of up to 128 entries need 512-byte alignment).

All multi-byte fields are little-endian.

| Offset  | Size | Field              | Notes                                                        |
|---------|------|--------------------|--------------------------------------------------------------|
| `0x000` | 4    | `magic`            | `0x41544F53` (`"SOTA"` in memory)                            |
| `0x004` | 2    | `header_version`   | `1`                                                          |
| `0x006` | 2    | `header_size`      | `0x200`                                                      |
| `0x008` | 4    | `payload_size`     | Bytes after the header, padded to a multiple of 8            |
| `0x00C` | 4    | `load_addr`        | Absolute address of the vector table (`slot_base + 0x200`)   |
| `0x010` | 4    | `version`          | `major << 24 \| minor << 16 \| patch`                        |
| `0x014` | 4    | `security_counter` | Monotonic anti-rollback counter                              |
| `0x018` | 4    | `flags`            | Reserved, must be `0`                                        |
| `0x01C` | 1    | `key_id`           | Index of the public key that signed the image                |
| `0x01D` | 3    | reserved           | Must be `0`                                                  |
| `0x020` | 64   | `payload_hash`     | SHA-512 of the payload (`payload_size` bytes)                |
| `0x060` | 352  | reserved           | Must be `0`, room for future fields                          |
| `0x1C0` | 64   | `signature`        | Ed25519 signature over header bytes `0x000..0x1BF`            |

## Why it looks like this

- **One signature, one hash.** The signature covers the header and the header covers
  the payload through `payload_hash`. The bootloader checks the signature first
  (cheap, 448 bytes) and only then hashes the payload. A forged header is rejected
  before any flash is erased.
- **SHA-512 rather than SHA-256.** Ed25519 already needs SHA-512, so the bootloader
  carries a single hash implementation.
- **Reserved bytes are zero and signed.** Future fields can be added under a new
  `header_version` without an old bootloader misreading them.
- **`load_addr` is explicit.** Images are execute-in-place and linked for one slot.
  The bootloader refuses an image whose `load_addr` does not match the slot it sits in,
  so an image built for slot A can never be started from slot B.
- **`version` vs `security_counter`.** `version` is for humans and the update protocol.
  `security_counter` only increases when a release fixes a vulnerability. The bootloader
  stores the highest confirmed counter and rejects anything lower (anti-rollback),
  while still allowing a downgrade to an older release that shares the same counter.
- **Payload padded to 8 bytes.** STM32G4/H7/L4 flash programs 64-bit double words, so
  the format is ready for parts with ECC flash.

## Write order during an update

The header is written to flash **last**. Until the final step the slot has no valid
magic, so a power cut at any point during an update leaves an image that can never be
mistaken for a bootable one.

## Tools

```
otatool sign    --key keys/dev_ed25519.pem --layout boards/stm32f407vg/layout.json \
                --slot A --version 1.2.0 --security-counter 1 app_a.bin -o app_a.signed.bin
otatool inspect app_a.signed.bin --key keys/dev_ed25519.pem
```
