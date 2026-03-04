#pragma once
#include <cstdint>

// Virtual turntable keys -> turntable position accumulator
// Used internally by input.cpp VTT thread; configurable via Input::setVttKeys()
struct VTTBinding {
    uint32_t plus_vk  = 0;
    uint32_t minus_vk = 0;
    uint8_t  step     = 3;
};
