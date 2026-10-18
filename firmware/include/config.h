#pragma once

/* How long a line must hold a level before a test step accepts it (ms). */
#define DEBOUNCE_MS 20

/* Pattern tests: how long each level must be held, and how short a change is
   ignored as a glitch (ms). */
#define PATTERN_HOLD_MS 2000
#define GLITCH_MS 150

/* Option ROM scan logger: the window BUSLOG_WINDOW_BASE..+BUSLOG_WINDOW_SIZE is
   split into BUSLOG_BLOCK_SIZE blocks, and the first read in each block is
   listed. A status line is printed every BUSLOG_STATUS_MS until the first one,
   and logging stops once the window has been quiet for BUSLOG_IDLE_STOP_MS. */
#define BUSLOG_WINDOW_BASE 0xC0000
#define BUSLOG_WINDOW_SIZE 0x30000
#define BUSLOG_BLOCK_SIZE 0x800
#define BUSLOG_POLL_MS 100
#define BUSLOG_STATUS_MS 2000
#define BUSLOG_IDLE_STOP_MS 5000

/* RESET DRV pulses shorter than this (us) are ignored as power-up noise. */
#define BUSLOG_MIN_RESET_US 1000

/* Option ROM server: where the image appears in ISA memory. The bus logs show
   this BIOS scans C8000-DFFFF in 2 KB steps for option ROMs (E0000-EFFFF are
   on-board ROM sockets it only checksums), and CheckIt reports C0000-DFFFF
   free. DD000 puts a 12 KB image right below the video ROM at E0000. */
#define BUSROM_BASE 0xDD000

/* Set BUSROM_DEBUG to 1 (or configure with cmake -DBUSROM_DEBUG=ON) for the
   ROM server's read logging: the first read in each BUSLOG_BLOCK_SIZE block of
   BUSROM_LOG_BASE..+BUSROM_LOG_SIZE, ROM read counts every BUSROM_STATUS_MS,
   and every pulse on RESET DRV. Without it the server prints its banner and a
   line each time the BIOS reads the ROM signature, and nothing else. */
#ifndef BUSROM_DEBUG
#define BUSROM_DEBUG 0
#endif
#define BUSROM_STATUS_MS 1000
#define BUSROM_LOG_BASE 0xC0000
#define BUSROM_LOG_SIZE 0x40000
