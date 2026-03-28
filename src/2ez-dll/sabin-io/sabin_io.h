#pragma once
#include <windows.h>

#include <cstdint>

class BindingStore;
class InputManager;

namespace SabinIO {

// Game structs
struct IOBuffer {
    uint8_t buffer[4096];
    uint32_t writePos;
    uint32_t readPos;
};

struct SerialIO {
    uint32_t _unknown0;
    HANDLE hComPort;
    char portName[0x200];
    uint32_t _unknown1;
    OVERLAPPED writeOverlapped;
    OVERLAPPED readOverlapped;
    uint32_t _unknown2;
    uint32_t _unknown3;
    IOBuffer ioBuffer;
    HWND hWnd;
};

// Public API — called from dll.cpp
void installHooks(BindingStore* bindings, InputManager* input);

}  // namespace SabinIO
