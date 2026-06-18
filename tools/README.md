# NAM Pedal — model/IR upload tools

These tools put neural amp captures (`.namb`), cabinet impulse responses (`.wav`),
and presets onto the pedal's **onboard QSPI flash** — no SD card. You pack a folder
of files into a single image, then flash it over USB DFU.

## Why DFU (and not a live upload)

The firmware is too large for the STM32H750's 128 KB internal flash (~565 KB of code),
so it boots **execute-in-place from QSPI** (`APP_TYPE=BOOT_QSPI`). A program running from
QSPI can't safely erase/reprogram that same chip, so **all data writes go through the
bootloader's DFU**. The running firmware only ever *reads* the data partition, memory-mapped
at `0x90200000`. A live USB-CDC upload path (no DFU dance) is planned later — see the
project plan's "approach B".

## Layout of the data partition

Defined canonically in [`../data_format.h`](../data_format.h):

```
0x90200000  NamDataHeader   magic 'NAMD', version, entry count
            NamDataEntry[]  type, name[31], offset, length, samplerate   (48 B each)
            ... blobs, each 4 KiB-aligned ...
0x907FFFFF  end (6 MiB partition)
```

- **model** entries: raw `.namb` bytes, fed to `nam::get_dsp_namb()`.
- **IR** entries: mono `float32` little-endian taps (`tap_count = length / 4`).
- **preset** entries: a `NamPreset` record referencing a model + IR by name.

## Prerequisites

- `python3` (standard library only — no numpy needed)
- `dfu-util` — `brew install dfu-util` (macOS) / `sudo apt install dfu-util` (Linux)

## Workflow

1. **Collect your files** in one folder, e.g. `mydata/`:

   ```
   mydata/
     fender_deluxe.namb
     vox_ac30.namb
     greenback_412.wav
     marshall_cab.wav
     presets.json          # optional
   ```

   `presets.json` is a list of presets; `model`/`ir` reference file stems
   (filename without extension), and `ir` may be `""` to bypass the cab:

   ```json
   [
     {"name": "Clean Deluxe", "model": "fender_deluxe", "ir": "greenback_412",
      "input_gain": 1.0, "output_volume": 0.8, "bypass": false},
     {"name": "Crunch Vox", "model": "vox_ac30", "ir": "marshall_cab",
      "input_gain": 1.2, "output_volume": 0.7}
   ]
   ```

2. **Pack** the image (IRs are downmixed to mono and trimmed to 512 taps by default):

   ```bash
   python3 build_data_image.py mydata -o data_image.bin
   # adjust IR length:  --max-taps 1024   (0 = no trimming)
   ```

3. **Verify** before flashing (optional but handy):

   ```bash
   python3 inspect_data_image.py data_image.bin
   ```

4. **Flash.** Put the pedal in DFU mode — *hold BOOT, tap RESET, release BOOT* — then:

   ```bash
   ./flash_data.sh data_image.bin
   ```

The pedal re-enumerates and boots with the new models/IRs. Power-cycling keeps them
(they're in flash) — that's the SD-reliability win.

## Flashing the firmware itself

Separate from the data image. One-time bootloader install, then the app:

```bash
./flash_app.sh --boot     # once: install the QSPI bootloader (board reboots)
# put board back in DFU mode, then:
./flash_app.sh            # build + flash the firmware
```

## Notes / current limitations

- **Presets are read-only for now.** Saving presets *on the device* requires writing
  QSPI at runtime (the planned "approach B" RAM-resident writer). Until then, author
  presets in `presets.json` and re-flash. Runtime edits live in RAM and are lost on
  power-off.
- **`0x90200000` must fall inside the bootloader's DFU range.** It maps QSPI as one
  region from `0x90000000` and the app already writes at `0x90040000`, so a higher
  offset should work — but verify on your bootloader build the first time. If a build
  restricts the range, relocate `NAM_DATA_PARTITION_OFFSET` in `data_format.h` and the
  `TARGET_ADDR` in `flash_data.sh` to match (keep them in sync).
- Keep `data_format.h` and the constants at the top of `build_data_image.py` /
  `inspect_data_image.py` in sync if you change the format.
