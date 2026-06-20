# Default Model and IR Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Commit `marshall.namb` + `ir.wav` + `presets.json` into `data/` and add a `make data-image` target so any clone produces a flashable QSPI image with one command.

**Architecture:** Convert `marshall.nam` (SlimmableContainer JSON) to the binary `.namb` format using the `nam2namb` host tool (already in `nam-binary-loader/`), copy `ir.wav` unchanged, write a preset JSON, then wire everything into the Makefile. The `data/` folder becomes the source directory for `tools/build_data_image.py`.

**Tech Stack:** CMake (host build of `nam2namb`), Python 3 (stdlib only), GNU Make.

## Global Constraints

- Slim factor: `0.0` (selects the smaller WaveNet submodel — submodel[0], max_value=0.5)
- `nam2namb` build artifacts must NOT be committed
- `data_image.bin` must NOT be committed (add to `.gitignore` if absent)
- IR max-taps: `512` (build_data_image.py default — do not override)
- Preset name: `"Marshall"`, model stem: `"marshall"`, IR stem: `"ir"`

---

### Task 1: Populate data/ folder (namb + wav + preset)

**Files:**
- Create: `data/marshall.namb` (binary, generated; committed)
- Create: `data/ir.wav` (copy of root `ir.wav`)
- Create: `data/presets.json`

**Interfaces:**
- Produces: `data/` folder consumed by Task 2's `make data-image` target

- [ ] **Step 1: Build nam2namb**

```bash
cmake -B nam-binary-loader/build -S nam-binary-loader/ -DCMAKE_BUILD_TYPE=Release
cmake --build nam-binary-loader/build --target nam2namb
```

Expected: `nam-binary-loader/build/nam2namb` exists and is executable.

```bash
ls -lh nam-binary-loader/build/nam2namb
```

- [ ] **Step 2: Create data/ and convert marshall.nam**

```bash
mkdir -p data
./nam-binary-loader/build/nam2namb --slim 0.0 marshall.nam data/marshall.namb
```

Expected output (stderr):
```
SlimmableContainer: slim=0 -> submodel[0] (max_value=0.5)
```
Expected output (stdout):
```
marshall.nam -> data/marshall.namb
  JSON: 299054 bytes
  NAMB: <N> bytes
  Reduction: <R>%
```

- [ ] **Step 3: Verify namb magic bytes**

```bash
xxd data/marshall.namb | head -1
```

Expected: first 4 bytes are `4e 41 4d 42` (`NAMB`):
```
00000000: 4e41 4d42 ...
```

- [ ] **Step 4: Copy ir.wav**

```bash
cp ir.wav data/ir.wav
```

Verify:
```bash
file data/ir.wav
```
Expected: `RIFF (little-endian) data, WAVE audio, Microsoft PCM, 16 bit, mono 48000 Hz`

- [ ] **Step 5: Create data/presets.json**

Create the file with exactly this content:

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

- [ ] **Step 6: Commit data/ folder**

```bash
git add data/marshall.namb data/ir.wav data/presets.json
git commit -m "feat: add default marshall model, IR, and preset to data/"
```

---

### Task 2: Add make data-image target and verify end-to-end

**Files:**
- Modify: `Makefile` (append a `data-image` phony target after the libDaisy include)
- Optionally modify: `.gitignore` (ensure `data_image.bin` is ignored)

**Interfaces:**
- Consumes: `data/` folder from Task 1

- [ ] **Step 1: Check .gitignore for data_image.bin**

```bash
grep data_image.bin .gitignore 2>/dev/null || echo "NOT FOUND"
```

If `NOT FOUND`, add it:
```bash
echo "data_image.bin" >> .gitignore
git add .gitignore
```

- [ ] **Step 2: Add data-image target to Makefile**

Append to the end of `Makefile`:

```makefile

# ---------------------------------------------------------------------------
# Data image — pack default models, IRs, and presets into a QSPI flash image.
# Flash it with:  ./tools/flash_data.sh data_image.bin
# ---------------------------------------------------------------------------
.PHONY: data-image
data-image:
	python3 tools/build_data_image.py data/ -o data_image.bin
```

- [ ] **Step 3: Run make data-image**

```bash
make data-image
```

Expected output (last lines):
```
wrote data_image.bin: 3 entries, <N> bytes (<K> KiB, <P>% of 6 MiB partition)
flash it with:  ./flash_data.sh data_image.bin
```

Confirm the file exists:
```bash
ls -lh data_image.bin
```

- [ ] **Step 4: Inspect and verify data_image.bin contents**

```bash
python3 tools/inspect_data_image.py data_image.bin
```

Expected output should show 3 entries — one model, one IR, one preset:
```
magic OK, version 1, 3 entries
[0] model   marshall  offset=...  length=...
[1] IR      ir        offset=...  length=...  rate=48000
[2] preset  Marshall  model=marshall  ir=ir  gain=1.00  vol=0.80  bypass=0
```

- [ ] **Step 5: Commit Makefile (and .gitignore if changed)**

```bash
git add Makefile
git commit -m "feat: add make data-image target for default QSPI data image"
```
