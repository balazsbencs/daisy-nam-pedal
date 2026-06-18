#!/usr/bin/env bash
# flash_data.sh — upload a packed model/IR/preset image to the pedal's onboard
# QSPI data partition via the Daisy bootloader's DFU interface.
#
# The running firmware never writes QSPI (it executes from QSPI), so all data
# uploads go through DFU. Put the pedal in DFU mode first:
#   hold BOOT, tap RESET, release BOOT  ->  device enumerates as 0483:df11
#
# Usage:  ./flash_data.sh [data_image.bin]
#   defaults to ./data_image.bin
set -euo pipefail

IMAGE="${1:-data_image.bin}"

# Absolute target = QSPI base 0x90000000 + partition offset 0x00200000.
# Keep this in sync with NAM_DATA_PARTITION_* in ../data_format.h.
TARGET_ADDR="0x90200000"
DFU_ID="0483:df11"   # STM32 system bootloader (DfuSe)

if ! command -v dfu-util >/dev/null 2>&1; then
  echo "error: dfu-util not found. Install it:" >&2
  echo "  macOS:  brew install dfu-util" >&2
  echo "  Linux:  sudo apt install dfu-util" >&2
  exit 1
fi

if [[ ! -f "$IMAGE" ]]; then
  echo "error: image '$IMAGE' not found. Build one with:" >&2
  echo "  python3 build_data_image.py <src_dir> -o $IMAGE" >&2
  exit 1
fi

if ! dfu-util -l 2>/dev/null | grep -q "$DFU_ID"; then
  echo "error: no DFU device ($DFU_ID) found." >&2
  echo "Put the Daisy in DFU mode: hold BOOT, tap RESET, release BOOT — then retry." >&2
  exit 1
fi

echo "Flashing '$IMAGE' to QSPI data partition @ $TARGET_ADDR ..."
# -a 0 : the bootloader's flash alt-setting (covers QSPI memory-mapped range)
# :leave : exit DFU and run the application when done
dfu-util -a 0 -s "${TARGET_ADDR}:leave" -D "$IMAGE" -d ",${DFU_ID}"

echo "Done. The pedal should re-enumerate and boot with the new data."
