#pragma once

// Interactive bench tests. Each one returns the card to BusMode::Address.

void printHookup();
bool powerCheck();

void ledInit();

// ISA -> Pico: on each line in turn the user holds GND, +5 V, then GND, and
// the test moves on by itself once the pattern is seen.
void addressInputTest();
void dataInputTest();

// Steps through the NOR truth tables, one prompt at a time.
void gateTest();

// Pico -> ISA: the Pico drives a pattern and the user measures the edge pins.
void dataOutputTest();
void ioChrdyTest();

void inputMonitor();

// Blocks until a key arrives.
int waitKey();
