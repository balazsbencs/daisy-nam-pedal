#!/usr/bin/env bash
# flash_app.sh — build and flash the pedal FIRMWARE (not the model data).
#
# The app is too large for internal flash, so it boots execute-in-place from
# QSPI (APP_TYPE=BOOT_QSPI). That requires the Daisy QSPI bootloader to be
# present once; after that you only re-run the app flash step.
#
# Put the Daisy in DFU mode before each step:
#   hold BOOT, tap RESET, release BOOT  ->  device enumerates as 0483:df11
#
# Usage:
#   ./flash_app.sh           # build + flash app
#   ./flash_app.sh --boot    # also (re)install the QSPI bootloader first
set -euo pipefail

# Run from the project root (parent of tools/).
cd "$(dirname "$0")/.."

if [[ "${1:-}" == "--boot" ]]; then
  echo "Installing Daisy QSPI bootloader (one-time)..."
  echo "  -> make sure the board is in DFU mode, then it will reboot into the bootloader."
  make program-boot
  echo
  echo "Board is now in the bootloader. Put it back in DFU mode (hold BOOT, tap RESET)"
  echo "and re-run:  ./flash_app.sh"
  exit 0
fi

echo "Building firmware..."
make

echo "Flashing firmware to QSPI (app region @ 0x90040000) via DFU..."
make program-dfu

echo "Done. Firmware flashed. Upload models/IRs separately with ./flash_data.sh"
