#pragma once
#include <string>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <vector>
#include <cctype>

#ifdef _WIN32
#include <windows.h>
#endif

inline std::string toHexString(const void* ptr) {
    std::ostringstream stream;
    stream << std::hex << std::uppercase << reinterpret_cast<uintptr_t>(ptr);
    return stream.str();
}

inline std::string toHexString(uint32_t val) {
    std::ostringstream stream;
    stream << std::hex << std::uppercase << val;
    return stream.str();
}

inline std::string toHexString(uint16_t val) {
    std::ostringstream stream;
    stream << std::hex << std::uppercase << val;
    return stream.str();
}

inline std::string toBinaryString(uint8_t val) {
    char buf[9];
    for (int i = 7; i >= 0; --i) {
        buf[7 - i] = (val & (1 << i)) ? '1' : '0';
    }
    buf[8] = '\0';
    return buf;
}

inline std::string toBinaryString(uint16_t val) {
    char buf[18];
    for (int i = 15; i >= 8; --i) {
        buf[15 - i] = (val & (1 << i)) ? '1' : '0';
    }
    buf[8] = '_';
    for (int i = 7; i >= 0; --i) {
        buf[16 - i] = (val & (1 << i)) ? '1' : '0';
    }
    buf[17] = '\0';
    return buf;
}

inline std::string toHexStringPadded(unsigned val, int width = 2) {
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setw(width) << std::setfill('0') << val;
    return stream.str();
}

inline std::string truncate(const std::string& text, size_t maxLength) {
    if (text.size() <= maxLength) {
        return text;
    }
    return text.substr(0, maxLength);
}

inline std::string toUpperCase(const std::string& text) {
    std::string result = text;
    for (auto& character : result) {
        character = static_cast<char>(toupper(static_cast<unsigned char>(character)));
    }
    return result;
}

inline std::vector<uint8_t> parseBytes(const std::string& hexStr) {
    std::vector<uint8_t> result;
    std::istringstream stream(hexStr);
    std::string hexByte;
    while (stream >> hexByte) {
        result.push_back(static_cast<uint8_t>(std::stoul(hexByte, nullptr, 16)));
    }
    return result;
}

inline uint32_t parseHexOffset(const std::string& hexStr) {
    return static_cast<uint32_t>(std::stoul(hexStr, nullptr, 16));
}

inline constexpr float HAT_SWITCH_INCREMENT = 1.0f / 7.0f;

#ifdef _WIN32
inline std::string wideToUtf8(const wchar_t* src) {
    if (!src || src[0] == L'\0') {
        return {};
    }
    int bufferLength = WideCharToMultiByte(CP_UTF8, 0, src, -1, nullptr, 0, nullptr, nullptr);
    if (bufferLength <= 1) {
        return {};
    }
    std::string result(bufferLength - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, src, -1, &result[0], bufferLength, nullptr, nullptr);
    return result;
}
#endif
