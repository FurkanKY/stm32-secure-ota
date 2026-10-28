# STM32 Secure OTA Bootloader

A from-scratch secure bootloader for STM32 with signed A/B firmware images, rollback
and anti-rollback protection, and firmware updates over UART and CAN.

> Work in progress. See the [roadmap](docs/ROADMAP.md) for what is done and what is next.

## Features (target)

- **Ed25519-signed images**, verified on every boot ([image format](docs/image-format.md))
- **A/B slots**, execute in place, the running image is never touched during an update
- **Trial boot with automatic rollback**: a new image must confirm itself, otherwise the
  watchdog and a boot counter bring the previous image back
- **Anti-rollback** via a monotonic security counter
- **Power-loss safe**: boot state lives in an append-only journal, the image header is
  written last
- Updates over **UART**, then **CAN**
- Host-tested C core, **Renode** simulation, GitHub Actions CI

## Repository layout

| Path      | Contents                                         |
|-----------|--------------------------------------------------|
| `boards/` | Flash layout per board (single source of truth)  |
| `docs/`   | Specs and design notes                           |
| `keys/`   | Public development key (never use in production) |
| `tools/`  | `otatool`, the Python host tool                  |

## Quick start (host tool)

```bash
python -m venv .venv
.venv/Scripts/pip install -e "tools[test]"   # Linux/macOS: .venv/bin/pip
pytest tools

otatool sign app_a.bin -o app_a.signed.bin --key keys/dev_ed25519.pem \
    --layout boards/stm32f407vg/layout.json --slot A --version 1.0.0
otatool inspect app_a.signed.bin --key keys/dev_ed25519.pem
```

## License

MIT
