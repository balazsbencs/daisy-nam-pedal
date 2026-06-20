# Default Model and IR — Design Spec

**Date:** 2026-06-20

## Goal

Bundle `marshall.nam` and `ir.wav` as committed defaults so that any clone of the
repo can produce a flashable QSPI data image with a single command.

## Inputs

| File | Format | Notes |
|------|--------|-------|
| `marshall.nam` | JSON, SlimmableContainer v0.7.0 | Two WaveNet submodels; slim=0.0 selects the smaller one |
| `ir.wav` | RIFF PCM 16-bit, mono, 48 kHz | Already in the format `build_data_image.py` expects |

## What Gets Created

```
data/
  marshall.namb      # binary form of marshall.nam, slim=0.0 (smaller WaveNet)
  ir.wav             # copy of root ir.wav — no conversion needed
  presets.json       # one default preset wiring model + IR
```

`marshall.namb` is a compiled asset generated once via `nam2namb --slim 0.0` and
committed to the repo. Users do not need a C++ toolchain to reproduce the data image.

## `presets.json` content

```json
[
  {
    "name": "Marshall",
    "model": "marshall",
    "ir": "ir",
    "input_gain": 1.0,
    "output_volume": 0.8,
    "bypass": false
  }
]
```

## Makefile target

```makefile
data-image:
	python3 tools/build_data_image.py data/ -o data_image.bin
```

Produces `data_image.bin` ready for `./tools/flash_data.sh data_image.bin`.

## What is NOT done

- `marshall.nam` and `ir.wav` remain in the repo root as originals; they are not
  removed.
- The `nam2namb` build artifacts are not committed.
- No changes to firmware source code.

## Conversion step (one-time, during implementation)

1. `cmake -B nam-binary-loader/build -S nam-binary-loader/` (host build)
2. `cmake --build nam-binary-loader/build --target nam2namb`
3. `./nam-binary-loader/build/nam2namb --slim 0.0 marshall.nam data/marshall.namb`
