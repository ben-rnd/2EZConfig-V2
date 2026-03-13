#pragma once
#include <string>
#include <sstream>
#include <iomanip>
#include <cstdint>

inline std::string toHexString(const void* ptr) {
    std::ostringstream ss;
    ss << std::hex << std::uppercase << reinterpret_cast<uintptr_t>(ptr);
    return ss.str();
}

inline std::string toHexString(uint32_t val) {
    std::ostringstream ss;
    ss << std::hex << std::uppercase << val;
    return ss.str();
}

inline std::string toHexString(uint16_t val) {
    std::ostringstream ss;
    ss << std::hex << std::uppercase << val;
    return ss.str();
}
