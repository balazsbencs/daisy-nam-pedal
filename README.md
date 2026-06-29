# NAM Platform Pedal

An open-source Neural Amp Modeler guitar pedal built on the [Daisy Seed](https://daisy.audio/products/daisy-seed). It runs binary NAM captures, cabinet impulse responses, EQ, gate, compressor, delay, presets, and a tuner from onboard QSPI—no SD card required.

Signal chain: **input → gain → gate → compressor → NAM → IR → EQ → delay → volume → L/R output**

## Documentation

- [Features and project overview](https://balazsbencs.github.io/daisy-nam-pedal/)
- [Interactive user manual](https://balazsbencs.github.io/daisy-nam-pedal/manual.html)
- [Developer guide](https://balazsbencs.github.io/daisy-nam-pedal/developers.html)
- [Verified hardware pin map](https://balazsbencs.github.io/daisy-nam-pedal/hardware.html)

The website is the authoritative documentation. `HardwareConfig.h` remains the firmware source of truth for pin assignments, and `data_format.h` defines the QSPI storage ABI.

## Local build

Prerequisites: Arm GNU Toolchain, GNU Make, Git, and Python 3.

```sh
git clone --recurse-submodules https://github.com/balazsbencs/daisy-nam-pedal.git
cd daisy-nam-pedal
tools/apply_submodule_patches.sh
make -C third_party/libDaisy
make -C third_party/DaisySP
make
```

The application uses `BOOT_QSPI`; build artifacts are written under `build/`. See the [developer guide](https://balazsbencs.github.io/daisy-nam-pedal/developers.html) for firmware/data flashing, model limitations, memory layout, and validation.
