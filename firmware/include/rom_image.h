#pragma once

#include <stdint.h>

// The option ROM image, generated at build time from ROM_IMAGE (see
// CMakeLists.txt).
extern uint8_t romImage[];
extern const uint32_t romImageSize;
