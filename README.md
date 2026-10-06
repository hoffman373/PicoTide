# PicoTide

An ISA card that connects a Raspberry Pi Pico W to an 8-bit ISA slot, so the Pico can see and drive the bus of a PC/XT-class machine.

## Overview

The ISA bus runs at 5 V and the Pico's GPIO is 3.3 V, so every signal between them passes through a level translator.

- **MCU:** Raspberry Pi Pico W
- **Level translation:** SN74LVC8T245 8-bit dual-supply transceivers (3.3 V Pico side, 5 V ISA side)
- **Glue logic:** 74LVC1G02 / 74LVC1G04 / 74LVC1G06 single gates
- **Bus connector:** 8-bit ISA edge connector (`Bus_ISA_8bit`)

### Pin mapping

| Pico GPIO | ISA signal | Net label |
|---|---|---|
| GPIO0-7 | A0-A7 and D0-D7, multiplexed | `P_A0_D0` ... `P_A7_D7` |
| GPIO8-15 | A8-A15 | `P_A8` ... `P_A15` |
| GPIO16-19 | A16-A19 | `P_A16` ... `P_A19` |
| GPIO21 | RESET | `RESET` |

Address and data share GPIO0-7 and are told apart by timing. Each of those GPIOs goes through one translator to the matching address line and through another to the matching data line.

`IO_CHRDY` (ISA IO CH RDY) is driven through a 74LVC1G06 open-drain output so the Pico can insert wait states without driving the line high.

Net labels use a `P_` prefix for the Pico side of a translator. The matching ISA-side labels carry the bare ISA name.

## Opening the project

1. Install [KiCad](https://www.kicad.org/) 10 or newer. The files use the KiCad 10 format and will not open in older versions.
2. Clone the repository:
   ```
   git clone https://github.com/hoffman373/PicoTide.git
   ```
3. Open `PicoTide.kicad_pro`.

Custom footprints live in `lib/footprints/` and are registered through the project's `fp-lib-table` using `${KIPRJMOD}`, so no library setup is needed. Everything else uses stock KiCad symbols and footprints.

## Notes

- **Bus timing:** an ISA cycle is much faster than a Pico can respond in software, so expect to use the Pico's PIO and wait states (`IO_CHRDY`).
