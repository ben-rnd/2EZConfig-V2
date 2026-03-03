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
#include <cstdint>

struct DeviceInfo {
    uint16_t vendor_id  = 0;
    uint16_t product_id = 0;
    uint8_t  instance   = 0;
    std::string manufacturer;
    std::string product;
    std::string path;  // device name from GetRawInputDeviceInfo(RIDI_DEVICENAME); kept for display

    // Kernel handle from GetRawInputDeviceList.
    // Used to match WM_INPUT's header.hDevice. NOT a file handle opened with CreateFile.
    HANDLE raw_handle = nullptr;

    HIDP_CAPS caps = {};
    PHIDP_PREPARSED_DATA preparsed = nullptr;  // from RIDI_PREPARSEDDATA; free with LocalFree
    std::vector<HIDP_BUTTON_CAPS> button_caps;
    std::vector<HIDP_VALUE_CAPS>  value_caps;

    // Live state — updated under g_stateLock by WM_INPUT handler, read by getButtonState/getAnalogValue.
    // button_states: flat bool array. button_caps[0] contributes (UsageMax-UsageMin+1) entries, etc.
    std::vector<bool>  button_states;
    // axis_states: one float [0.0, 1.0] per value_caps entry; 0.5 = center.
    std::vector<float> axis_states;

    // Find flat button index for (page, id). Returns -1 if not found.
    int findButtonIndex(uint16_t page, uint16_t id) const {
        int offset = 0;
        for (const auto& bc : button_caps) {
            USAGE umin = bc.IsRange ? bc.Range.UsageMin : bc.NotRange.Usage;
            USAGE umax = bc.IsRange ? bc.Range.UsageMax : bc.NotRange.Usage;
            if (bc.UsagePage == page && id >= umin && id <= umax)
                return offset + (int)(id - umin);
            offset += (int)(umax - umin + 1);
        }
        return -1;
    }

    // Find value cap index for (page, id). Returns -1 if not found.
    int findAxisIndex(uint16_t page, uint16_t id) const {
        for (int i = 0; i < (int)value_caps.size(); i++) {
            const auto& vc = value_caps[(size_t)i];
            USAGE u = vc.IsRange ? vc.Range.UsageMin : vc.NotRange.Usage;
            if (vc.UsagePage == page && u == id) return i;
        }
        return -1;
    }

    void destroy() {
        if (preparsed) { LocalFree(preparsed); preparsed = nullptr; }
    }

    bool isValid() const { return raw_handle != nullptr; }
};
