# PicoTideFirmware

Firmware for the PicoTide card, an 8-bit ISA card built around a Raspberry Pi
Pico 2 (RP2350). The card watches the ISA address bus and memory strobes and
can drive the data bus, so the Pico can act as an option ROM.

There are similar designs out there, and more feature-complete Pico-ISA
designs too. This is the first ISA card I have designed and built, and it was
a fun way to solve a problem I had.

The project builds three separate firmwares:

| Firmware              | What it does                                                                       |
| --------------------- | ---------------------------------------------------------------------------------- |
| `PicoTideBenchTester` | Interactive bench tests for a newly built card                                     |
| `PicoTideBusLogger`   | Logs ISA memory reads in the machine, to find where the BIOS scans for option ROMs |
| `PicoTideOptionRom`   | Serves an option ROM image, such as the XTIDE Universal BIOS, from the card        |

All three log status over the USB serial connection.

## Building

You need the Pico SDK and an `arm-none-eabi` toolchain, plus your option ROM
image for `PicoTideOptionRom`. Put the image in the project root as
`option-rom.bin`, or point the CMake `ROM_IMAGE` variable at it, for example
`-DROM_IMAGE=~/ide_xtl_cfg_pad.bin`. A relative path is taken from the project
root. If the image isn't found, CMake stops with an error.

Then build:

```sh
cmake -S . -B build
cmake --build build
```

This builds:

- `build/PicoTideBenchTester.uf2`
- `build/PicoTideBusLogger.uf2`
- `build/PicoTideOptionRom.uf2`

The default board is `pico2`. To change it, copy `local.cmake.example` to
`local.cmake` and edit it. `local.cmake` is gitignored.

`cmake --build build --target format` runs clang-format over the sources.

## PicoTideBenchTester

Run this on the bench, with the card powered from a test harness instead of a
machine. It prints the power hookup and runs a power check, then shows a menu:

| Key | Test                                                                                                        |
| --- | ----------------------------------------------------------------------------------------------------------- |
| `1` | Address and RESET inputs (ISA A0-A19, RESET DRV to Pico). Automatic: hold each line at GND, +5 V, then GND  |
| `2` | Data inputs (ISA D0-D7 to Pico). Automatic, the same way                                                    |
| `3` | Gates: AEN, SMEMR# and SMEMW# to VALID_READ and VALID_WRITE, one truth-table step at a time                 |
| `4` | Data outputs (Pico to ISA D0-D7). Measure the edge pins                                                     |
| `5` | IO CH RDY output                                                                                            |
| `6` | Live input monitor                                                                                          |
| `p` | Card power check                                                                                            |
| `h` | Power hookup                                                                                                |

The power check is a heuristic. If the translators are missing 5 V on VCCB,
their Pico-side outputs float and follow the Pico's pull resistors.

## PicoTideBusLogger

Run this in the machine. It records ISA memory reads in C0000-EFFFF and lists
the first read in each 2 KB block. When logging stops, it prints the read
counts per block. A block with one or two reads is where a BIOS scan checked
for a `55 AA` signature.

- Plug the Pico into USB and open the terminal before switching the machine
  on. Logging starts when RESET DRV pulses.
- Key `r` starts logging immediately, which you need for a warm boot:
  Ctrl-Alt-Del does not pulse RESET DRV.
- Logging stops after 5 s with no reads in the window. A slow POST can leave
  longer gaps than that, so raise `BUSLOG_IDLE_STOP_MS` if a scan seems to be
  missing.

The window, block size and timings are set in `include/config.h`.

## PicoTideOptionRom

Serves an option ROM image at `BUSROM_BASE`, which defaults to `0xDD000`.
Core 1 polls VALID_READ from RAM. On a read inside the image, it points U3 at
the ISA bus, swaps U1 out for U3, drives the byte, and releases the bus when
SMEMR# rises.

By default it prints a banner, then one line each time something reads the
ROM's signature byte. Normally that is once per boot. A scan by a tool such as
CheckIt also prints a line.

For full logging, configure with `-DBUSROM_DEBUG=ON`. This adds:

- the first read in each 2 KB block of C0000-FFFFF
- ROM read counts, and how many of the ROM's bytes have been read
- every pulse on RESET DRV
- key `s`, which prints the reads per block, and key `r`, which clears the
  counts
