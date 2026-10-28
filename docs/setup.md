# Development setup

## Windows (winget)

```powershell
winget install -e --id Arm.GnuArmEmbeddedToolchain   # arm-none-eabi-gcc 14.2
winget install -e --id Kitware.CMake
winget install -e --id Ninja-build.Ninja
winget install -e --id BrechtSanders.WinLibs.POSIX.UCRT   # host gcc for unit tests
winget install -e --id Renode.Renode
```

Open a new terminal afterwards so `PATH` picks up the tools.

## Ubuntu (what CI uses)

```bash
sudo apt install gcc-arm-none-eabi cmake ninja-build gcc python3-venv
# Renode: https://github.com/renode/renode/releases (portable Linux package)
```

## Python tool

```bash
python -m venv .venv
.venv/Scripts/pip install -e "tools[test,can]"   # Linux/macOS: .venv/bin/pip
pytest tools
```

The `can` extra installs python-can, needed only for `--port can:...` (USB-CAN
adapters).

## Host unit tests (C)

```bash
cmake -S . -B build/host -G Ninja -DSOTA_HOST_TESTS=ON
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

On Linux, add `-DSOTA_SANITIZE=ON` to run the tests under AddressSanitizer and
UndefinedBehaviorSanitizer.

## Firmware

```bash
cmake -S . -B build/fw -G Ninja "-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake"
cmake --build build/fw
```

(Keep the quotes in PowerShell, which otherwise splits the argument at the dot.)

This builds `build/fw/boot/sota_boot.elf` and the demo apps in `build/fw/app/`,
each signed with `keys/dev_ed25519.pem` (`*.signed.bin`). Use
`-DSOTA_SIGNING_KEY=...` to sign with another key; the bootloader embeds the
matching public key.

## Simulation (Renode)

```bash
python sim/smoke.py            # boot the factory image and print the console
pytest sim                     # boot, trial, rollback and security scenarios
```

To poke around interactively, start Renode and run in its monitor:

```
include @renode/stm32f407.resc
showAnalyzer sysbus.usart2
start
```

`renode/stm32f407_sota.repl` adds the flash alias at address 0 that the real chip
has; without it the simulated core cannot find the bootloader again after a
reset.

## Test vectors

The C tests verify images in `test/vectors/`, which are signed by `otatool`.
Regenerate them after changing the image format:

```bash
python tools/gen_test_vectors.py
```
