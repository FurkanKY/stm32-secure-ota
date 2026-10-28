# STM32 Secure OTA Bootloader

[![CI](https://github.com/FurkanKY/stm32-secure-ota/actions/workflows/ci.yml/badge.svg)](https://github.com/FurkanKY/stm32-secure-ota/actions/workflows/ci.yml)

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
- Updates over **UART** with a resumable, CRC-checked protocol ([spec](docs/update-protocol.md)); **CAN** next
- Host-tested C core, **Renode** simulation, GitHub Actions CI

## Repository layout

| Path           | Contents                                                  |
|----------------|-----------------------------------------------------------|
| `app/`         | Demo application, built per slot and in test variants     |
| `boards/`      | Flash layout per board (single source of truth)           |
| `boot/`        | The bootloader                                            |
| `cmake/`       | Arm toolchain file, layout code generation                |
| `docs/`        | Specs and design notes                                    |
| `keys/`        | Public development key (never use in production)          |
| `lib/`         | Portable boot core: verify, boot state, policy, update    |
| `port/`        | Board support (bare-metal STM32F4, no vendor HAL)         |
| `renode/`      | Simulation platform and script                            |
| `sim/`         | Renode end-to-end tests (pytest)                          |
| `test/`        | Host unit tests, fake flash with power-cut injection      |
| `third_party/` | Monocypher (Ed25519, SHA-512), Unity (test framework)     |
| `tools/`       | `otatool`, the Python host tool                           |

## Quick start

Tool installation is described in [docs/setup.md](docs/setup.md).

```bash
# Python host tool
python -m venv .venv
.venv/Scripts/pip install -e "tools[test]"   # Linux/macOS: .venv/bin/pip
pytest tools

# C boot core on the host
cmake -S . -B build/host -G Ninja -DSOTA_HOST_TESTS=ON
cmake --build build/host
ctest --test-dir build/host --output-on-failure

# Firmware (bootloader + signed demo apps) and the Renode simulation
cmake -S . -B build/fw -G Ninja "-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake"
cmake --build build/fw
pytest sim

# Update a running board (or the simulation: --port tcp:127.0.0.1:PORT)
otatool update --port COM5 --layout boards/stm32f407vg/layout.json \
    build/fw/app/app_v3_a.signed.bin build/fw/app/app_v2_b.signed.bin

# Sign an application binary for slot A
otatool sign app_a.bin -o app_a.signed.bin --key keys/dev_ed25519.pem \
    --layout boards/stm32f407vg/layout.json --slot A --version 1.0.0
otatool inspect app_a.signed.bin --key keys/dev_ed25519.pem
```

## License

MIT
