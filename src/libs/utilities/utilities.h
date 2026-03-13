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

inline std::string toHexStringPadded(unsigned val, int width = 2) {
    std::ostringstream ss;
    ss << std::uppercase << std::hex << std::setw(width) << std::setfill('0') << val;
    return ss.str();
}

inline std::string truncate(const std::string& s, size_t maxLen) {
    if (s.size() <= maxLen) {
        return s;
    }
    return s.substr(0, maxLen);
}

inline std::string toUpperCase(const std::string& s) {
    std::string result = s;
    for (auto& c : result) {
        c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    }
    return result;
}

inline std::vector<uint8_t> parseBytes(const std::string& hexStr) {
    std::vector<uint8_t> result;
    std::istringstream ss(hexStr);
    std::string token;
    while (ss >> token) {
        result.push_back(static_cast<uint8_t>(std::stoul(token, nullptr, 16)));
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
    int len = WideCharToMultiByte(CP_UTF8, 0, src, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) {
        return {};
    }
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, src, -1, &result[0], len, nullptr, nullptr);
    return result;
}
#endif
