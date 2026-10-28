# Update protocol (v1)

The bootloader speaks this protocol in recovery mode. Recovery mode starts when no
slot holds a bootable image, or when the application asks for it (it writes a request
into a RAM block that survives the reset, see `lib/include/sota/handoff.h`). After an
application request, 30 s without a valid frame sends the device back to a normal
boot, so a stray request cannot strand it.

The protocol only moves bytes. Every security decision (signature, slot, anti-rollback,
hash) is made by the update engine in `lib/src/update.c`, the same code the host unit
tests and the power-cut sweep exercise.

## Frames

```
+------+------+------+-----+-----------+-----------------+-----------+
| 0xA5 | 0x5A | type | seq | length:16 | payload[length] | crc32:32  |
+------+------+------+-----+-----------+-----------------+-----------+
```

- Little-endian. `crc32` is CRC-32/ISO-HDLC (zlib) over `type`, `seq`, `length` and
  the payload.
- The host sends requests; the device answers each with a response whose `type` has
  bit 7 set and whose `seq` matches. The first payload byte of every response is a
  status code (`sota_err_t`, 0 = OK).
- A frame with a bad CRC or an impossible length is dropped without an answer, and
  the receiver resynchronises on the next `A5 5A`. The host retransmits after a
  timeout. Console text on the same UART is skipped the same way.

## Commands

| Type   | Request payload          | Response payload (after status)                         |
|--------|--------------------------|---------------------------------------------------------|
| `0x01` HELLO  | none              | protocol version u8, max chunk u16, bootloader version u32, active slot u8, target slot u8, slot states u8[2], security floor u32, slot versions u32[2] |
| `0x02` BEGIN  | 512-byte image header | slot u8                                             |
| `0x03` DATA   | offset u32, data (≤ 1024 bytes) | next expected offset u32                     |
| `0x04` FINISH | none              | none                                                    |
| `0x05` RESET  | none              | none; the device resets after sending it               |

Slot states: 0 empty, 1 pending, 2 confirmed, 3 bad. Versions are packed
`major << 24 | minor << 16 | patch`, 0 for an empty slot.

## Sequence

```
host                                   bootloader
 | -- 'u' (to the running application) --> reboots into recovery mode
 | -- HELLO -----------------------------> |  target slot = the one not active
 | -- BEGIN(header for target slot) -----> |  check signature, slot, counter;
 |                                         |  mark slot EMPTY, erase it
 | -- DATA(0, ...) ----------------------> |  program, reply next offset
 | -- DATA(1024, ...) -------------------> |
 |    ...                                  |
 | -- FINISH ----------------------------> |  hash flash, write header last,
 |                                         |  mark slot PENDING
 | -- RESET -----------------------------> |  trial boot of the new image
```

- **Lost responses.** If a DATA response is lost, the host resends the same chunk.
  The device recognises a chunk it already has and acknowledges it again without
  writing.
- **Resuming.** Every DATA response carries the next offset the device expects; the
  host always continues from there.
- **Power loss.** The target slot is marked EMPTY before it is erased and the header
  is written last, so an interrupted update leaves the running image in charge. The
  host just starts again with HELLO.

## Over CAN (ISO-TP)

The same frames travel over CAN, one protocol frame per ISO 15765-2 message
(`lib/src/isotp.c`, `tools/otatool/isotp.py`):

| | |
|---|---|
| Bit rate | 500 kbit/s, classic CAN, 11-bit identifiers |
| Host → device | `0x7E0` |
| Device → host | `0x7E8` |
| Addressing | normal, frames padded to 8 bytes with `0xCC` |
| Flow control | the receiver asks for block size 3, STmin 0 |

Block size 3 matches the 3-frame receive FIFO of bxCAN: the sender waits for a flow
control frame after every three consecutive frames, so a polled receiver cannot be
overrun however fast frames arrive. The unit tests include a case where a sender that
ignores this overruns a 3-deep FIFO.

On CAN the application has no console to receive the `u` request, so it reboots into
update mode as soon as it sees any frame on `0x7E0`: the host's first HELLO.

Two ways to reach the bus from a PC:

- **USB-CAN adapter** (PCAN, CANable/slcan, SocketCAN):
  `otatool update --port can:pcan:PCAN_USBBUS1 ...` (needs `pip install "otatool[can]"`)
- **UART ↔ CAN gateway**: `gateway/` is firmware for a second STM32 that forwards
  protocol frames between its UART and the bus, so `otatool` works unchanged over a
  serial port. The Renode tests run exactly this two-board setup.

```
PC ──USB-UART──▶ gateway STM32 ──CAN 500k──▶ target STM32
                 (gateway/)                  (boot/ + app/)
```

## Host tool

```bash
otatool info   --port COM5
otatool update --port COM5 --layout boards/stm32f407vg/layout.json \
               build/fw/app/app_v3_a.signed.bin build/fw/app/app_v2_b.signed.bin
```

With `--layout`, `otatool` picks the image linked for the slot the device reports as
its target. `--port tcp:127.0.0.1:PORT` talks to the UART of a Renode simulation.
