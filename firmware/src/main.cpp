#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/stdio_usb.h"

#include "card.h"
#include "tests.h"

namespace {

void printMenu() {
    printf(
        "\n=== PicoTide card tester ===\n"
        "  1  Address and RESET inputs  (ISA A0-A19, RESET DRV -> Pico, automatic)\n"
        "  2  Data inputs               (ISA D0-D7 -> Pico, automatic)\n"
        "  3  Gates                     (AEN, SMEMR#, SMEMW# -> VALID_READ/WRITE)\n"
        "  4  Data outputs              (Pico -> ISA D0-D7)\n"
        "  5  IO CH RDY output          (Pico -> ISA A10)\n"
        "  6  Live input monitor\n"
        "  p  Card power check\n"
        "  h  Power hookup\n"
        "> ");
}

}  // namespace

int main() {
    stdio_init_all();
    cardInit();
    ledInit();

    // Nothing to do until a terminal is attached.
    while (!stdio_usb_connected()) sleep_ms(100);
    sleep_ms(500);

    printHookup();
    powerCheck();
    printMenu();

    while (true) {
        int c = waitKey();
        switch (c) {
            case '1':
                addressInputTest();
                break;
            case '2':
                dataInputTest();
                break;
            case '3':
                gateTest();
                break;
            case '4':
                dataOutputTest();
                break;
            case '5':
                ioChrdyTest();
                break;
            case '6':
                inputMonitor();
                break;
            case 'p':
                powerCheck();
                break;
            case 'h':
                printHookup();
                break;
            default:
                break;
        }
        printMenu();
    }
}
