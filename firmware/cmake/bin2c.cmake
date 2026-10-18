# Turns a binary file into a C++ array in RAM.
# Usage: cmake -DINPUT=<file.bin> -DOUTPUT=<file.cpp> -P bin2c.cmake
# An option ROM (55 AA, then its length in 512 byte units) is cut to the length
# it declares, so padding past the end is not carried.
file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" hexLength)
math(EXPR size "${hexLength} / 2")
string(SUBSTRING "${hex}" 0 4 signature)
if(signature STREQUAL "55aa" AND size GREATER 2)
    string(SUBSTRING "${hex}" 4 2 blocks)
    math(EXPR declared "0x${blocks} * 512")
    if(declared GREATER 0 AND declared LESS size)
        set(size ${declared})
        math(EXPR hexLength "${size} * 2")
        string(SUBSTRING "${hex}" 0 ${hexLength} hex)
    endif()
endif()
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "((0x..,){16})" "\\1\n    " bytes "${bytes}")
get_filename_component(name "${INPUT}" NAME)
file(WRITE "${OUTPUT}"
"// Generated from ${name} by cmake/bin2c.cmake. Do not edit.\n\n"
"#include \"rom_image.h\"\n\n"
"// Not const, so it is copied to RAM at boot: flash is too slow to answer a\n"
"// bus cycle from.\n"
"uint8_t romImage[] = {\n    ${bytes}\n};\n\n"
"const uint32_t romImageSize = ${size};\n")
