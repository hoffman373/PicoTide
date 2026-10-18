# PicoTide

An ISA card that connects a Raspberry Pi Pico to an 8-bit ISA slot, so the Pico can see and drive the bus of a PC/XT-class machine. With the firmware in [`firmware/`](firmware/), the card serves an option ROM, such as the XTIDE Universal BIOS, to the machine.

## Overview

The ISA bus runs at 5 V and the Pico's GPIO is 3.3 V, so every signal between them passes through a level translator.

- **MCU:** Raspberry Pi Pico. The footprint is the Pico W's and takes any Pico-footprint board; the firmware targets the Pico 2 (RP2350).
- **Level translation:** SN74LVC8T245 8-bit dual-supply transceivers (3.3 V Pico side, 5 V ISA side)
- **Glue logic:** 74LVC1G02 / 74LVC1G04 / 74LVC1G06 single gates. The NOR gates combine SMEMR# and SMEMW# with AEN into `VALID_READ` and `VALID_WRITE`.
- **Bus connector:** 8-bit ISA edge connector (`Bus_ISA_8bit`)

### Pin mapping

| Pico GPIO | ISA signal | Net label |
|---|---|---|
| GPIO0-7 | A0-A7 and D0-D7, multiplexed | `P_A0_D0` ... `P_A7_D7` |
| GPIO8-15 | A8-A15 | `P_A8` ... `P_A15` |
| GPIO16-19 | A16-A19 | `P_A16` ... `P_A19` |
| GPIO20 | IO CH RDY (high pulls it low) | `P_IO_CHRDY` |
| GPIO21 | RESET DRV | `P_RESET` |
| GPIO22 | Address/data select | `P_ADR_DATA` |
| GPIO26 | Data translator direction | `P_DATA_DIR` |
| GPIO27 | NOR(SMEMW#, AEN) | `VALID_WRITE` |
| GPIO28 | NOR(SMEMR#, AEN) | `VALID_READ` |

Address and data share GPIO0-7. Each of those GPIOs goes through one translator to the matching address line and through another to the matching data line. `P_ADR_DATA` picks which translator owns them: high enables the address translator, low enables the data translator, and `P_DATA_DIR` then sets whether data flows from the Pico to the bus or the other way.

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

## Firmware

The [`firmware/`](firmware/) directory builds three firmwares for the card: a bench tester for a newly built card, a bus logger that shows where a BIOS scans for option ROMs, and an option ROM server. See [`firmware/README.md`](firmware/README.md) for building and using them.

## Notes

- **Bus timing:** at 4.77 MHz, a Pico 2 polling the bus from RAM on its second core answers memory reads in software with time to spare, without PIO or wait states. The option ROM server has booted an XT from the XTIDE Universal BIOS this way. `IO_CHRDY` is there for faster machines or slower responses.
