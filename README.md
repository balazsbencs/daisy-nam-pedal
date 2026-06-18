# NAM Platform Pedal

A Neural Amp Modeler (NAM) guitar pedal built on the [Daisy Seed](https://electro-smith.com/products/daisy-seed) (STM32H750 Cortex-M7). Models and impulse responses are stored in QSPI flash and loaded at boot — no SD card required.

Signal chain: **IN → input gain → NAM model → IR (FIR) → output vol → OUT (L+R)**

---

## Project layout

```
main.cpp                   ← boot, main loop, UI dispatch
AudioEngine.cpp/h          ← audio callback and signal chain
Controls.cpp/h             ← encoder + footswitch debounce
IRLoader.cpp/h             ← IR (cabinet sim) FIR convolution
ModelManager.cpp/h         ← NAM model catalogue from QSPI
PresetManager.cpp/h        ← preset storage and apply
QspiStorage.cpp/h          ← read-only QSPI flash access
Ui.cpp/h                   ← ST7789 display UI (performance/browse/edit)
HardwareConfig.h           ← single source of truth for all pin assignments
data_format.h              ← on-flash data layout (header, entries, presets)
NamEmbeddedStubs.cpp       ← minimal embedded shims for NAM host-only APIs
display/                   ← ST7789 SPI driver and renderer
docs/                      ← hardware pinout and wiring

third_party/
├── libDaisy/              ← Daisy BSP + STM32 HAL (submodule)
└── DaisySP/               ← DSP utilities — FIR filter used for IR (submodule)

NeuralAmpModelerCore/      ← NAM DSP engine (submodule)
nam-binary-loader/         ← binary .namb model loader (submodule)
```

---

## Building

### Prerequisites

- `arm-none-eabi-gcc` toolchain 10.3+
- `make`

### First-time setup

Clone the repo and initialise all submodules (including their nested dependencies):

```sh
git clone <repo-url>
cd daisy-nam-pedal
git submodule update --init --recursive
```

Then build the two pre-compiled libraries:

```sh
make -C third_party/libDaisy
make -C third_party/DaisySP
```

### Build

```sh
make
```

Output artefacts land in `build/`:

| File | Description |
|------|-------------|
| `build/NamPlatform.elf` | ELF with debug symbols |
| `build/NamPlatform.hex` | Intel HEX for OpenOCD |
| `build/NamPlatform.bin` | Raw binary for DFU |

The firmware targets QSPI flash via the Daisy bootloader (`APP_TYPE = BOOT_QSPI`). The binary is ~600 KB; it exceeds the STM32H750's 128 KB internal flash, so BOOT_QSPI is mandatory.

### Flash firmware

With the Daisy in DFU mode (hold BOOT, press RESET):

```sh
dfu-util -a 0 -s 0x90040000:leave -D build/NamPlatform.bin
```

### Flash data partition (models + IRs)

Models and IRs are written to a separate QSPI partition at offset `0x90200000`. Use the tool in `tools/` — see `docs/HARDWARE.md` for details.

---

## Rebuilding from scratch

If the submodule libraries are already built, a full clean + rebuild is:

```sh
make clean && make
```

To rebuild the submodule libraries too:

```sh
make -C third_party/libDaisy clean && make -C third_party/libDaisy
make -C third_party/DaisySP clean && make -C third_party/DaisySP
make clean && make
```

---

## Hardware

See [docs/HARDWARE.md](docs/HARDWARE.md) for the full pin table and wiring diagram.

Key connections (Daisy Seed pins):

| Signal | Seed pin |
|--------|----------|
| FS1 (next preset) | D15 |
| FS2 (prev preset) | D16 |
| ENC1 A/B/click | D0 / D1 / D2 |
| Display SCK | D22 |
| Display MOSI | D18 |
| Display CS | D13 |
| Display DC | D14 |
| Display RST | D26 |
| Display BLK | D24 |

---

## Notes on the embedded build

NeuralAmpModelerCore's main branch targets desktop (uses `<filesystem>`, `<mutex>`, JSON). The embedded build excludes those paths via `#ifdef HOST_BUILD` guards added to the submodule, and compiles only the binary NAMB load path. The `NamEmbeddedStubs.cpp` file provides minimal shims for `create_dsp()` and `verify_config_version()`.

DaisySP's FIR filter runs with CMSIS-DSP hardware acceleration (`USE_ARM_DSP`). The two required CMSIS-DSP source files (`arm_fir_f32.c`, `arm_fir_init_f32.c`) are compiled directly from the libDaisy submodule — no separate library build step needed.
