# NamPlatform — Hardware Reference

Neural Amp Modeler guitar pedal built on the **Daisy Seed** (STM32H750, 480 MHz).  
Single source of truth for pin assignments: [`HardwareConfig.h`](../HardwareConfig.h).

---

## Daisy Seed pin numbering

Daisy uses a `seed::D0`–`seed::D30` numbering that maps to STM32 GPIO ports.  
Full pinout: <https://electro-smith.com/daisy/daisy>

---

## Pin assignments

### ST7789 colour display (240×320, SPI1)

| Function        | Daisy pin | STM32 pin | Notes                          |
|-----------------|-----------|-----------|--------------------------------|
| SCK             | D22       | PA5       | SPI1_SCK, alternate function   |
| MOSI / SDA      | D18       | PA7       | SPI1_MOSI, alternate function  |
| CS              | D13       | PB6       | GPIO output, active-low        |
| DC              | D14       | PB7       | GPIO output (data / command)   |
| RES             | D26       | PD11      | GPIO output, active-low reset  |
| BLK / Backlight | D24       | PA1       | GPIO output, drive HIGH for ON |

### Footswitches (momentary, active-low, internal pull-up)

| Function         | Daisy pin | STM32 pin | Notes              |
|------------------|-----------|-----------|--------------------|
| FS1 — next preset / apply edit  | D15  | PC0  | active-low, pull-up |
| FS2 — prev preset / cancel edit | D16  | PA3  | active-low, pull-up |

### Rotary encoder 1 (primary navigation / edit)

| Function    | Daisy pin | STM32 pin | Notes                    |
|-------------|-----------|-----------|--------------------------|
| ENC1_A      | D0         | PB12      | quadrature A phase       |
| ENC1_B      | D1         | PC11      | quadrature B phase       |
| ENC1_CLICK  | D2         | PC10      | push-button, active-low  |

### Rotary encoder 2 (optional — disabled in firmware until wired)

Set `hw::ENC2_PRESENT = true` in `HardwareConfig.h` to enable.

| Function    | Daisy pin | STM32 pin | Notes                    |
|-------------|-----------|-----------|--------------------------|
| ENC2_A      | D7         | PG10      | quadrature A phase       |
| ENC2_B      | D8         | PG11      | quadrature B phase       |
| ENC2_CLICK  | D9         | PB4       | push-button, active-low  |

---

## Fixed / non-configurable resources

| Resource              | Notes                                                                 |
|-----------------------|-----------------------------------------------------------------------|
| Audio codec (I2S/SAI) | Fixed to Daisy Seed on-board WM8731; 48 kHz, 48-sample blocks        |
| QSPI flash (IS25LP064A) | Fixed to dedicated QSPI pins; data partition at `0x90200000` (6 MiB) |
| USB (DFU)             | Fixed; used for firmware and data-image flashing                     |

---

## Constraints and notes

- **SPI1 pin lock:** SCK (D22/PA5) and MOSI (D18/PA7) are hardware-fixed to the STM32's SPI1 alternate function. Do not assign other peripherals to these pins.
- **Active-low signals:** footswitch and display CS/RES lines are active-low. The firmware uses the Daisy SDK's internal pull-up for footswitches — no external resistors needed.
- **Encoder pull-ups:** the Daisy SDK `Encoder` class enables internal pull-ups on all three encoder pins.
- **D26 is display RES:** do not reuse for FS2 or any other peripheral.
- **Encoder 2 disable:** if encoder 2 is not populated, leave `ENC2_PRESENT = false`; the firmware will ignore those pins.
