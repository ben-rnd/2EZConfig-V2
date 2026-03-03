#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}
#include <vector>
#include <string>

struct DeviceInfo {
    std::string              path;
    HANDLE                   handle     = INVALID_HANDLE_VALUE;
    OVERLAPPED               ov         = {};
    std::vector<uint8_t>     reportBuf;
    bool                     readPending = false;

    PHIDP_PREPARSED_DATA     preparsedData = nullptr;
    HIDP_CAPS                caps          = {};

    std::vector<HIDP_BUTTON_CAPS> buttonCaps;
    std::vector<HIDP_VALUE_CAPS>  valueCaps;

    bool isOpen() const { return handle != INVALID_HANDLE_VALUE; }

    // Close handle, free preparsed data, reset fields
    void close();
};
