# Cooperative Display Transfer Design

## Goal

Keep ALPS EC11 encoders responsive while updating the 240x320 ST7789 display,
without changing audio processing or encoder decoding.

## Root Cause

`St7789Driver::PushFrame` currently transmits an entire 153,600-byte frame
synchronously. At 50 MHz SPI this blocks the main loop for approximately 25 ms.
Audio continues because its interrupt preempts the transfer, but controls are
polled only before and after the full frame, causing EC11 transitions to be
missed during redraws. `Ui` comments describing the path as DMA are incorrect.

## Architecture

Replace the monolithic frame transmission with a cooperative transfer state
machine. `PushFrame` sets the display window, issues RAMWR, stores the frame
pointer and byte count, and returns with chip select asserted. `Service` sends
one 480-byte display row per call. `IsBusy` reports whether rows remain.

`Ui::Update` calls `driver_.Service()` on every main-loop iteration before its
dirty and frame-rate checks. While a transfer is active it returns without
rendering into the framebuffer. This keeps the single framebuffer immutable
during transfer and prevents tearing. Encoder and switch processing runs once
between every row transfer.

When the last row completes, `Service` deasserts chip select and clears the
transfer state. Null buffers, zero lengths, and attempts to start a second
frame while busy are ignored without disturbing the active transfer.

## Timing

Each service call blocks only for one 480-byte row, approximately 0.08 ms at
50 MHz, rather than a complete frame. Total wire time remains approximately
25 ms. ENC1 remains governed by libDaisy's 1 kHz debounce rate; ENC2-5 are
polled at least once per row.

## Verification

Host-test the transfer state independently from hardware SPI: starting a frame
sets busy, each service call advances one chunk, the final call clears busy,
and a busy transfer cannot be replaced. Existing encoder and UI tests must
remain passing.

Build and flash continuous NAM plus the complete 512-tap IR with the display
enabled. Rotate all encoders rapidly during repeated redraws for at least 60
seconds. Acceptance requires responsive controls, coherent frames, clean
audio, and `cpu_peak` below 0.900 ms without overload reports.
