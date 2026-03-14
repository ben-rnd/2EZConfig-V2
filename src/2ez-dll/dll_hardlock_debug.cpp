#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <string>
#include <windows.h>
#include "logger.h"
#include "utilities.h"

extern "C" {
    void DBG_print_buffer(unsigned char* data, size_t size) {
        if (!data || size == 0) {
            return;
        }
        std::string hex;
        hex.reserve(size * 3);
        for (size_t i = 0; i < size; i++) {
            if (i > 0) {
                hex += ' ';
            }
            char byte[4];
            snprintf(byte, sizeof(byte), "%02X", data[i]);
            hex += byte;
        }
        Logger::info("[Hardlock] " + hex);
    }

    void DBG_printfA(const char* fmt, ...) {
        char buffer[512] = {};
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer) - 1, fmt, args);
        va_end(args);
        Logger::info("[Hardlock] " + std::string(buffer));
    }

    void DBG_printfW(const wchar_t* format, ...) {
        wchar_t wideBuffer[512] = {};
        va_list args;
        va_start(args, format);
        _vsnwprintf(wideBuffer, (sizeof(wideBuffer) / sizeof(wchar_t)) - 1, format, args);
        va_end(args);
        Logger::info("[Hardlock] " + wideToUtf8(wideBuffer));
    }
} 