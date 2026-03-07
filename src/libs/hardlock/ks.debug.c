#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include "kitchen_sink.h"




void DBG_print_buffer(unsigned char* data, size_t size) {
#ifdef DEBUG_MODE
    size_t ansi_sz = (size * 2) + 1;
    char* a_buffer = NULL;
    a_buffer = (char*)calloc(1, ansi_sz);
    if (!a_buffer) { return; }
    for (unsigned int i = 0; i < size; i++) {
        snprintf(a_buffer + (i * 2), ansi_sz, "%02x", data[i]);
    }
    OutputDebugStringA(a_buffer);
    free(a_buffer);
#endif
}


void DBG_printfA(const char* fmt, ...) {
#ifdef DEBUG_MODE
    char s[512] = { 0x00 };
    va_list args;
    va_start(args, fmt);
    vsnprintf(s, 512 - 1, fmt, args);
    va_end(args);
    s[512 - 1] = 0x00;
    OutputDebugStringA(s);
#endif
}


// Debug Print Functions for Windows
void DBG_printfW(const wchar_t* format, ...) {
#ifdef DEBUG_MODE
    wchar_t* s = NULL;
    va_list args;

    va_start(args, format);

    int buffer_size = vswprintf(NULL, 0, format, args);

    if (buffer_size < 1) { return; }
    buffer_size++;
    buffer_size *= 2;
    s = (wchar_t*)calloc(1, buffer_size);
    buffer_size = vswprintf(s, buffer_size, format, args);

    va_end(args);
    OutputDebugStringW(s);
    free(s);
#endif
}
