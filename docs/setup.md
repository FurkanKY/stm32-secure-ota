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
.venv/Scripts/pip install -e "tools[test]"   # Linux/macOS: .venv/bin/pip
pytest tools
```

## Host unit tests (C)

```bash
cmake -S . -B build/host -G Ninja -DSOTA_HOST_TESTS=ON
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

On Linux, add `-DSOTA_SANITIZE=ON` to run the tests under AddressSanitizer and
UndefinedBehaviorSanitizer.

The C tests verify images in `test/vectors/`, which are signed by `otatool`.
Regenerate them after changing the image format:

```bash
python tools/gen_test_vectors.py
```
