// MinGW shim: _InterlockedExchangeAdd8 not declared in MinGW intrin.h
// Maps to identical lock-xadd instruction on x86/x86-64.
#if defined(__GNUC__) && !defined(_InterlockedExchangeAdd8)
static inline char _InterlockedExchangeAdd8(volatile char* ptr, char val) {
    return __sync_fetch_and_add(ptr, val);
}
#endif

#include <windows.h>
extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}
#include <mmsystem.h>

#include "input_manager.h"
#include "utilities.h"

#include <ole2.h>
#include <setupapi.h>

#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cstdint>
#include <cmath>

// Get display-friendly device description via SetupDi, matching by device interface path.
// Based on spice2x rawinput approach.
static std::string deviceDescFromPath(const std::string& rawPath) {
    // Parse GUID from end of path: \\?\HID#...#...#{guid}
    auto guidStart = rawPath.rfind('{');
    auto guidEnd = rawPath.rfind('}');
    if (guidStart == std::string::npos || guidEnd == std::string::npos || guidEnd <= guidStart) {
        return "";
    }
    std::string guidStr = rawPath.substr(guidStart, guidEnd - guidStart + 1);

    // Convert to wide string for IIDFromString.
    wchar_t wideGuid[64] = {};
    MultiByteToWideChar(CP_UTF8, 0, guidStr.c_str(), -1, wideGuid, 64);
    GUID guid;
    if (IIDFromString(wideGuid, &guid) != S_OK) {
        return "";
    }

    // Enumerate device interfaces of this class to find our device by path match.
    HDEVINFO devInfo = SetupDiGetClassDevsA(&guid, nullptr, nullptr, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
    if (devInfo == INVALID_HANDLE_VALUE) {
        return "";
    }

    std::string result;
    SP_DEVINFO_DATA devInfoData = {};
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    for (DWORD i1 = 0; SetupDiEnumDeviceInfo(devInfo, i1, &devInfoData); i1++) {
        SP_DEVICE_INTERFACE_DATA ifData = {};
        ifData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
        for (DWORD i2 = 0; SetupDiEnumDeviceInterfaces(devInfo, &devInfoData, &guid, i2, &ifData); i2++) {
            DWORD detailSize = 0;
            SetupDiGetDeviceInterfaceDetailA(devInfo, &ifData, nullptr, 0, &detailSize, nullptr);
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || detailSize == 0) {
                continue;
            }
            std::vector<BYTE> detailBuf(detailSize);
            auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_A*>(detailBuf.data());
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
            if (!SetupDiGetDeviceInterfaceDetailA(devInfo, &ifData, detail, detailSize, nullptr, nullptr)) {
                continue;
            }
            // Compare paths (case-insensitive). Apply the XP fix (second char → backslash).
            std::string ifPath(detail->DevicePath);
            if (ifPath.size() > 1) ifPath[1] = '\\';
            std::string comparePath = rawPath;
            if (comparePath.size() > 1) comparePath[1] = '\\';
            if (_stricmp(ifPath.c_str(), comparePath.c_str()) != 0) {
                continue;
            }
            // Found our device — read SPDRP_DEVICEDESC.
            DWORD descSize = 0;
            SetupDiGetDeviceRegistryPropertyW(devInfo, &devInfoData, SPDRP_DEVICEDESC, nullptr, nullptr, 0, &descSize);
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && descSize > 0) {
                std::vector<BYTE> descBuf(descSize);
                if (SetupDiGetDeviceRegistryPropertyW(devInfo, &devInfoData, SPDRP_DEVICEDESC, nullptr, descBuf.data(), descSize, nullptr)) {
                    result = wideToUtf8(reinterpret_cast<PWCHAR>(descBuf.data()));
                }
            }
            // Try to append HID product string (open with access 0 to avoid exclusive lock).
            HANDLE hFile = CreateFileA(rawPath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (hFile != INVALID_HANDLE_VALUE) {
                wchar_t prodBuf[126] = {};
                if (HidD_GetProductString(hFile, prodBuf, sizeof(prodBuf))) {
                    std::string prod = wideToUtf8(prodBuf);
                    if (!prod.empty() && prod != result) {
                        if (result.empty()) {
                            result = prod;
                        } else {
                            result += " - " + prod;
                        }
                    }
                }
                CloseHandle(hFile);
            }
            break;
        }
        if (!result.empty()) break;
    }
    SetupDiDestroyDeviceInfoList(devInfo);
    return result;
}

static std::string vidPidFromPath(const std::string& path) {
    std::string upper = toUpperCase(path);
    auto vidPos = upper.find("VID_");
    auto pidPos = upper.find("PID_");
    if (vidPos == std::string::npos || pidPos == std::string::npos) {
        return "Unknown Device";
    }
    std::string vid = path.substr(vidPos, 8); // "VID_XXXX"
    std::string pid = path.substr(pidPos, 8); // "PID_XXXX"
    return vid + ":" + pid;
}

static std::string axisLabel(USAGE usagePage, USAGE usage) {
    if (usagePage == 0x01) {
        switch (usage) {
            case 0x30: return "X Axis";
            case 0x31: return "Y Axis";
            case 0x32: return "Z Axis";
            case 0x33: return "Rx";
            case 0x34: return "Ry";
            case 0x35: return "Rz";
            case 0x36: return "Slider";
            case 0x37: return "Dial";
            case 0x38: return "Wheel";
            case 0x39: return "Hat Switch";
        }
        return "Axis 0x" + toHexStringPadded(usage);
    }
    return "Page 0x" + toHexStringPadded(usagePage) + " Usage 0x" + toHexStringPadded(usage);
}

// Page 0x08 = LED, Page 0x0A = Ordinal (common for game light hardware).
static std::string outputLabel(USAGE usagePage, USAGE usage) {
    if (usagePage == 0x08) {
        return "LED " + std::to_string(usage);
    }
    if (usagePage == 0x0A) {
        return "Output " + std::to_string(usage);
    }
    if (usagePage == 0x01) {
        return axisLabel(usagePage, usage);
    }
    return "Page 0x" + toHexStringPadded(usagePage) + " Usage 0x" + toHexStringPadded(usage);
}

static ButtonAnalogType getHatDirection(float hatVal) {
    if (hatVal < 0.0f) {
        return ButtonAnalogType::NONE;
    }
    float pos = hatVal / HAT_SWITCH_INCREMENT;
    int dir = static_cast<int>(pos + 0.5f);
    if (dir < 0) {
        dir = 0;
    }
    if (dir > 7) {
        dir = 7;
    }
    // Map dir index to ButtonAnalogType: 0=UP, 1=UP_RIGHT, ..., 7=UP_LEFT
    return static_cast<ButtonAnalogType>(dir + 1);  // +1 because NONE=0, HS_UP=1
}

// Diagonals activate both adjacent cardinal directions.
// Uses +/- 0.5 * HAT_SWITCH_INCREMENT tolerance.
static bool isHatDirectionActive(float hatVal, ButtonAnalogType dir) {
    if (hatVal < 0.0f || dir == ButtonAnalogType::NONE) {
        return false;
    }
    int dirIdx = static_cast<int>(dir) - 1;  // 0=UP, 1=UP_RIGHT, ..., 7=UP_LEFT
    float dirCenter = static_cast<float>(dirIdx) * HAT_SWITCH_INCREMENT;
    float tolerance = HAT_SWITCH_INCREMENT * 0.5f;

    float diff = hatVal - dirCenter;
    if (diff >= -tolerance && diff <= tolerance) {
        return true;
    }

    // Handle wrap-around (UP_LEFT at 7/7 is adjacent to UP at 0/7)
    float diffWrapPos = (hatVal + 1.0f + HAT_SWITCH_INCREMENT) - dirCenter;
    float diffWrapNeg = hatVal - (dirCenter + 1.0f + HAT_SWITCH_INCREMENT);
    if (diffWrapPos >= -tolerance && diffWrapPos <= tolerance) {
        return true;
    }
    if (diffWrapNeg >= -tolerance && diffWrapNeg <= tolerance) {
        return true;
    }

    return false;
}

struct VttBinding {
    int plusVk = 0;
    int minusVk = 0;
    int step = 3;
};

struct MouseDevice {
    std::string path;
    std::string name;
    HANDLE rawHandle;
};

struct MouseBinding {
    HANDLE rawHandle = nullptr;
    int axis = -1;        // 0=X, 1=Y
    int sensitivity = 5;  // 1-20
};

struct InputManagerImpl {
    // Device list (written once at startup, read from multiple threads under lock).
    std::vector<Device> devices;
    CRITICAL_SECTION devicesLock;

    HANDLE pumpThread = nullptr;
    HWND hwnd = nullptr;
    volatile bool running = false;

    VttBinding vttBindings[2];
    volatile LONG vttPos[2] = {TT_CENTER_INTERNAL, TT_CENTER_INTERNAL};
    HANDLE vttThread = nullptr;

    std::vector<MouseDevice> mice;
    MouseBinding mouseBindings[2];
    volatile LONG mousePos[2] = {TT_CENTER_INTERNAL, TT_CENTER_INTERNAL};

    bool captureMode = false;
    CaptureResult captureResult;
    bool captureResultReady = false;
    CRITICAL_SECTION captureLock;

    std::map<std::string, std::vector<bool>> prevButtonStates;
    std::map<std::string, std::vector<float>> prevHatStates;

    HANDLE outputEvent = nullptr;
    HANDLE outputThread = nullptr;
    HANDLE flushThread = nullptr;

    // Optional callback fired after each WM_INPUT update (all locks released).
    void (*inputCallback)(void*) = nullptr;
    void* inputCallbackUserData = nullptr;

    InputManagerImpl() {
        InitializeCriticalSection(&devicesLock);
        InitializeCriticalSection(&captureLock);
    }
    ~InputManagerImpl() {
        DeleteCriticalSection(&devicesLock);
        DeleteCriticalSection(&captureLock);
    }
};

// Builds and writes one HID output report. Caller must hold dev.csOutput.
static void deviceWriteOutput(Device& dev) {
    if (!dev.hid || dev.hid->hidHandle == INVALID_HANDLE_VALUE) {
        return;
    }
    USHORT reportSize = dev.hid->caps.OutputReportByteLength;
    if (reportSize == 0) {
        return;
    }

    CHAR* outputReport = new CHAR[reportSize]();  // zero-initialized

    int stateOffset = 0;
    for (auto& buttonCaps : dev.hid->buttonOutputCapsList) {
        int btnCount = static_cast<int>(buttonCaps.Range.UsageMax - buttonCaps.Range.UsageMin + 1);
        if (btnCount <= 0) {
            continue;
        }

        std::vector<USAGE> onUsages, offUsages;
        for (int b = 0; b < btnCount; b++) {
            USAGE usg = buttonCaps.Range.UsageMin + static_cast<USAGE>(b);
            if (stateOffset + b < static_cast<int>(dev.hid->buttonOutputStates.size()) &&
                dev.hid->buttonOutputStates[stateOffset + b]) {
                onUsages.push_back(usg);
            } else {
                offUsages.push_back(usg);
            }
        }

        if (!onUsages.empty()) {
            ULONG usageCount = static_cast<ULONG>(onUsages.size());
            NTSTATUS st = HidP_SetButtons(HidP_Output, buttonCaps.UsagePage, buttonCaps.LinkCollection,
                                           onUsages.data(), &usageCount,
                                           dev.hid->preparsed, outputReport, reportSize);
            if (st == HIDP_STATUS_INCOMPATIBLE_REPORT_ID) {
                // Flush intermediate report via HidD_SetOutputReport, start fresh.
                HidD_SetOutputReport(dev.hid->hidHandle, outputReport, reportSize);
                delete[] outputReport;
                outputReport = new CHAR[reportSize]();
                usageCount = static_cast<ULONG>(onUsages.size());
                HidP_SetButtons(HidP_Output, buttonCaps.UsagePage, buttonCaps.LinkCollection,
                                onUsages.data(), &usageCount,
                                dev.hid->preparsed, outputReport, reportSize);
            }
        }

        if (!offUsages.empty()) {
            ULONG usageCount = static_cast<ULONG>(offUsages.size());
            NTSTATUS st = HidP_UnsetButtons(HidP_Output, buttonCaps.UsagePage, buttonCaps.LinkCollection,
                                             offUsages.data(), &usageCount,
                                             dev.hid->preparsed, outputReport, reportSize);
            if (st == HIDP_STATUS_INCOMPATIBLE_REPORT_ID) {
                HidD_SetOutputReport(dev.hid->hidHandle, outputReport, reportSize);
                delete[] outputReport;
                outputReport = new CHAR[reportSize]();
                usageCount = static_cast<ULONG>(offUsages.size());
                HidP_UnsetButtons(HidP_Output, buttonCaps.UsagePage, buttonCaps.LinkCollection,
                                  offUsages.data(), &usageCount,
                                  dev.hid->preparsed, outputReport, reportSize);
            }
        }

        stateOffset += btnCount;
    }

    for (size_t vi = 0; vi < dev.hid->valueOutputCapsList.size(); vi++) {
        auto& valueCaps = dev.hid->valueOutputCapsList[vi];
        if (vi >= dev.hid->valueOutputStates.size() || vi >= dev.hid->valueOutputUsages.size()) {
            break;
        }
        float norm = dev.hid->valueOutputStates[vi];
        LONG logicalMin = valueCaps.LogicalMin, logicalMax = valueCaps.LogicalMax;
        LONG logicalValue = (logicalMax > logicalMin)
            ? logicalMin + static_cast<LONG>(lroundf(static_cast<float>(logicalMax - logicalMin) * norm)) : logicalMin;
        if (logicalValue > logicalMax) {
            logicalValue = logicalMax;
        }
        if (logicalValue < logicalMin) {
            logicalValue = logicalMin;
        }
        ULONG usageValue = static_cast<ULONG>(logicalValue);
        NTSTATUS st = HidP_SetUsageValue(HidP_Output, valueCaps.UsagePage, valueCaps.LinkCollection,
                                          dev.hid->valueOutputUsages[vi], usageValue,
                                          dev.hid->preparsed, outputReport, reportSize);
        if (st == HIDP_STATUS_INCOMPATIBLE_REPORT_ID) {
            HidD_SetOutputReport(dev.hid->hidHandle, outputReport, reportSize);
            delete[] outputReport;
            outputReport = new CHAR[reportSize]();
            HidP_SetUsageValue(HidP_Output, valueCaps.UsagePage, valueCaps.LinkCollection,
                               dev.hid->valueOutputUsages[vi], usageValue,
                               dev.hid->preparsed, outputReport, reportSize);
        }
    }

    DWORD bytesWritten = 0;
    WriteFile(dev.hid->hidHandle, outputReport, reportSize, &bytesWritten, nullptr);
    delete[] outputReport;
}

static void devicesReload(InputManagerImpl* impl) {
    for (auto& dev : impl->devices) {
        dev.destroy();
    }
    impl->devices.clear();
    impl->prevButtonStates.clear();
    impl->prevHatStates.clear();

    UINT deviceCount = 0;
    if (GetRawInputDeviceList(nullptr, &deviceCount, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1)) {
        return;
    }
    if (deviceCount == 0) {
        return;
    }

    std::vector<RAWINPUTDEVICELIST> deviceList(deviceCount);
    if (GetRawInputDeviceList(deviceList.data(), &deviceCount, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1)) {
        return;
    }

    for (UINT i = 0; i < deviceCount; i++) {
        const RAWINPUTDEVICELIST& entry = deviceList[i];

        // Only HID devices (not keyboard, not mouse).
        if (entry.dwType != RIM_TYPEHID) {
            continue;
        }

        UINT pathLen = 0;
        GetRawInputDeviceInfoA(entry.hDevice, RIDI_DEVICENAME, nullptr, &pathLen);
        if (pathLen == 0) {
            continue;
        }
        std::string path(pathLen, '\0');
        if (GetRawInputDeviceInfoA(entry.hDevice, RIDI_DEVICENAME, &path[0], &pathLen) == static_cast<UINT>(-1)) {
            continue;
        }
        // Strip trailing null if present.
        while (!path.empty() && path.back() == '\0') {
            path.pop_back();
        }
        if (path.empty()) {
            continue;
        }

        UINT preparsedSize = 0;
        GetRawInputDeviceInfoA(entry.hDevice, RIDI_PREPARSEDDATA, nullptr, &preparsedSize);
        if (preparsedSize == 0) {
            continue;
        }
        PHIDP_PREPARSED_DATA preparsed = reinterpret_cast<PHIDP_PREPARSED_DATA>(LocalAlloc(LMEM_FIXED, preparsedSize));
        if (!preparsed) {
            continue;
        }
        if (GetRawInputDeviceInfoA(entry.hDevice, RIDI_PREPARSEDDATA, preparsed, &preparsedSize) == static_cast<UINT>(-1)) {
            LocalFree(preparsed);
            continue;
        }

        HIDP_CAPS caps = {};
        if (HidP_GetCaps(preparsed, &caps) != HIDP_STATUS_SUCCESS) {
            LocalFree(preparsed);
            continue;
        }

        // Skip vendor-specific top-level collections (usage page 0xFF**).
        if ((caps.UsagePage >> 8) == 0xFF) {
            LocalFree(preparsed);
            continue;
        }

        Device dev;
        dev.path       = path;
        dev.rawHandle = entry.hDevice;

        dev.hid = new DeviceHIDInfo();
        dev.hid->preparsed = preparsed;
        dev.hid->caps      = caps;

        // -----------------------------------------------------------------
        // Open persistent handle — GENERIC_READ|GENERIC_WRITE for output
        // sending (HidD_SetOutputReport) and HidD_GetIndexedString lookups.
        // -----------------------------------------------------------------
        dev.hid->hidHandle = CreateFileA(
            path.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr);

        {
            bool gotName = false;
            if (dev.hid->hidHandle != INVALID_HANDLE_VALUE) {
                wchar_t wideNameBuffer[256] = {};
                if (HidD_GetProductString(dev.hid->hidHandle, wideNameBuffer, sizeof(wideNameBuffer))) {
                    std::string utf8Name = wideToUtf8(wideNameBuffer);
                    if (!utf8Name.empty()) {
                        dev.name = utf8Name;
                        gotName = true;
                    }
                }
                if (!gotName) {
                    wchar_t wideManufacturerBuffer[256] = {};
                    if (HidD_GetManufacturerString(dev.hid->hidHandle, wideManufacturerBuffer, sizeof(wideManufacturerBuffer))) {
                        std::string utf8Name = wideToUtf8(wideManufacturerBuffer);
                        if (!utf8Name.empty()) {
                            dev.name = utf8Name;
                            gotName = true;
                        }
                    }
                }
            }
            if (!gotName) {
                dev.name = vidPidFromPath(path);
            }
        }

        {
            USHORT buttonCapsCount = caps.NumberInputButtonCaps;
            std::vector<HIDP_BUTTON_CAPS> buttonCapsData(buttonCapsCount);
            if (buttonCapsCount > 0 &&
                HidP_GetButtonCaps(HidP_Input, buttonCapsData.data(), &buttonCapsCount, preparsed) != HIDP_STATUS_SUCCESS)
            {
                dev.destroy();
                continue;
            }

            for (int capsIndex = 0; capsIndex < static_cast<int>(buttonCapsCount); capsIndex++) {
                auto& buttonCaps = buttonCapsData[capsIndex];

                // IsRange normalization — fill Range from NotRange
                // so all downstream code can assume Range.* is valid.
                if (!buttonCaps.IsRange) {
                    buttonCaps.Range.UsageMin = buttonCaps.NotRange.Usage;
                    buttonCaps.Range.UsageMax = buttonCaps.NotRange.Usage;
                }

                int btnCount = static_cast<int>(buttonCaps.Range.UsageMax - buttonCaps.Range.UsageMin + 1);
                if (btnCount <= 0 || btnCount >= 0xffff) {
                    continue;
                }

                // Hat switch in button caps — treat as regular button.
                // No DPad promotion. If hat appears as button cap, add as normal buttons.

                // Skip vendor-specific usage pages.
                if ((buttonCaps.UsagePage >> 8) == 0xFF) {
                    continue;
                }

                dev.hid->buttonCapsList.push_back(buttonCaps);
                for (int b = 0; b < btnCount; b++) {
                    int buttonNumber = static_cast<int>(dev.hid->buttonStates.size()) + 1;
                    dev.buttonCapsNames.push_back("Button " + std::to_string(buttonNumber));
                    dev.hid->buttonStates.push_back(false);
                }
            }
        }

        // Value caps (input). Hat switches included as float valueStates.
        {
            USHORT valueCapsCount = caps.NumberInputValueCaps;
            std::vector<HIDP_VALUE_CAPS> valueCapsData(valueCapsCount);
            if (valueCapsCount > 0 &&
                HidP_GetValueCaps(HidP_Input, valueCapsData.data(), &valueCapsCount, preparsed) != HIDP_STATUS_SUCCESS)
            {
                dev.destroy();
                continue;
            }

            for (int capsIndex = 0; capsIndex < static_cast<int>(valueCapsCount); capsIndex++) {
                auto& valueCaps = valueCapsData[capsIndex];

                if (!valueCaps.IsRange) {
                    valueCaps.Range.UsageMin = valueCaps.NotRange.Usage;
                    valueCaps.Range.UsageMax = valueCaps.NotRange.Usage;
                }

                USAGE usage = valueCaps.Range.UsageMin;

                // Sign-extension fix for LogicalMin/Max (sign-extension location 1 of 3).
                // Applied unconditionally to all value caps including hats.
                if (valueCaps.BitSize > 0 && valueCaps.BitSize <= static_cast<USHORT>(sizeof(valueCaps.LogicalMin) * 8)) {
                    auto shiftSize = sizeof(valueCaps.LogicalMin) * 8 - valueCaps.BitSize + 1;
                    auto mask = (static_cast<uint64_t>(1) << valueCaps.BitSize) - 1;
                    valueCaps.LogicalMin &= static_cast<LONG>(mask);
                    valueCaps.LogicalMin <<= shiftSize;
                    valueCaps.LogicalMin >>= shiftSize;
                    valueCaps.LogicalMax &= static_cast<LONG>(mask);
                }

                if ((valueCaps.UsagePage >> 8) == 0xFF) {
                    continue;
                }

                // All value caps (including hat switch 0x39) go into valueCapsList.
                // Hat switches store float in valueStates: -1.0f = neutral.
                dev.hid->valueCapsList.push_back(valueCaps);
                dev.valueCapsNames.push_back(axisLabel(valueCaps.UsagePage, usage));

                // Hat starts at -1.0f (neutral); regular axes start at 0.5f (center).
                bool isHat = (valueCaps.UsagePage == 0x01 && usage == 0x39);
                dev.hid->valueStates.push_back(isHat ? -1.0f : 0.5f);
                dev.hid->valueStatesRaw.push_back(0);
            }
        }

        {
            USHORT outputButtonCapsCount = caps.NumberOutputButtonCaps;
            std::vector<HIDP_BUTTON_CAPS> outputButtonCapsData(outputButtonCapsCount);
            if (outputButtonCapsCount > 0 &&
                HidP_GetButtonCaps(HidP_Output, outputButtonCapsData.data(), &outputButtonCapsCount, preparsed) == HIDP_STATUS_SUCCESS)
            {
                int outputButtonNumber = 1;
                for (int capsIndex = 0; capsIndex < static_cast<int>(outputButtonCapsCount); capsIndex++) {
                    auto& buttonCaps = outputButtonCapsData[capsIndex];
                    if (!buttonCaps.IsRange) {
                        buttonCaps.Range.UsageMin = buttonCaps.NotRange.Usage;
                        buttonCaps.Range.UsageMax = buttonCaps.NotRange.Usage;
                    }
                    int btnCount = static_cast<int>(buttonCaps.Range.UsageMax - buttonCaps.Range.UsageMin + 1);
                    if (btnCount <= 0 || btnCount >= 0xffff) {
                        continue;
                    }
                    if ((buttonCaps.UsagePage >> 8) == 0xFF) {
                        continue;
                    }
                    dev.hid->buttonOutputCapsList.push_back(buttonCaps);
                    for (int b = 0; b < btnCount; b++) {
                        USAGE usg = buttonCaps.Range.UsageMin + static_cast<USAGE>(b);
                        // Try device-provided string descriptor first.
                        ULONG stringIndex = 0;
                        if (buttonCaps.IsStringRange && buttonCaps.Range.StringMin != 0) {
                            stringIndex = static_cast<ULONG>(buttonCaps.Range.StringMin) + static_cast<ULONG>(b);
                        }
                        else if (!buttonCaps.IsRange && buttonCaps.NotRange.StringIndex != 0)
                            stringIndex = buttonCaps.NotRange.StringIndex;
                        wchar_t wideNameBuffer[256] = {};
                        if (stringIndex > 0 && dev.hid->hidHandle != INVALID_HANDLE_VALUE
                            && HidD_GetIndexedString(dev.hid->hidHandle, stringIndex, wideNameBuffer, sizeof(wideNameBuffer))
                            && wideNameBuffer[0] != L'\0') {
                            dev.buttonOutputCapsNames.push_back(wideToUtf8(wideNameBuffer));
                        } else {
                            dev.buttonOutputCapsNames.push_back(outputLabel(buttonCaps.UsagePage, usg));
                        }
                        outputButtonNumber++;
                    }
                }
            }
        }

        {
            USHORT outputValueCapsCount = caps.NumberOutputValueCaps;
            std::vector<HIDP_VALUE_CAPS> outputValueCapsData(outputValueCapsCount);
            if (outputValueCapsCount > 0 &&
                HidP_GetValueCaps(HidP_Output, outputValueCapsData.data(), &outputValueCapsCount, preparsed) == HIDP_STATUS_SUCCESS)
            {
                for (int capsIndex = 0; capsIndex < static_cast<int>(outputValueCapsCount); capsIndex++) {
                    auto& valueCaps = outputValueCapsData[capsIndex];

                    // Sign-extension fix for output LogicalMin/Max (sign-extension location 2 of 3).
                    if (valueCaps.BitSize > 0 && valueCaps.BitSize <= static_cast<USHORT>(sizeof(valueCaps.LogicalMin) * 8)) {
                        auto shiftSize = sizeof(valueCaps.LogicalMin) * 8 - valueCaps.BitSize + 1;
                        auto mask = (static_cast<uint64_t>(1) << valueCaps.BitSize) - 1;
                        valueCaps.LogicalMin &= static_cast<LONG>(mask);
                        valueCaps.LogicalMin <<= shiftSize;
                        valueCaps.LogicalMin >>= shiftSize;
                        valueCaps.LogicalMax &= static_cast<LONG>(mask);
                    }

                    if (!valueCaps.IsRange) {
                        valueCaps.Range.UsageMin = valueCaps.NotRange.Usage;
                        valueCaps.Range.UsageMax = valueCaps.NotRange.Usage;
                    }
                    int usageRangeCount = static_cast<int>(valueCaps.Range.UsageMax - valueCaps.Range.UsageMin + 1);
                    if (usageRangeCount <= 0 || usageRangeCount >= 0xffff) {
                        continue;
                    }
                    if ((valueCaps.UsagePage >> 8) == 0xFF) {
                        continue;
                    }
                    // Expand range: one entry per usage so each output is independently addressable.
                    for (int u = 0; u < usageRangeCount; u++) {
                        USAGE specific = valueCaps.Range.UsageMin + static_cast<USAGE>(u);
                        dev.hid->valueOutputCapsList.push_back(valueCaps);
                        dev.hid->valueOutputUsages.push_back(specific);
                        // Try device-provided string descriptor first.
                        ULONG stringIndex = 0;
                        if (valueCaps.IsStringRange && valueCaps.Range.StringMin != 0) {
                            stringIndex = static_cast<ULONG>(valueCaps.Range.StringMin) + static_cast<ULONG>(u);
                        }
                        else if (!valueCaps.IsRange && valueCaps.NotRange.StringIndex != 0)
                            stringIndex = valueCaps.NotRange.StringIndex;
                        wchar_t wideNameBuffer[256] = {};
                        if (stringIndex > 0 && dev.hid->hidHandle != INVALID_HANDLE_VALUE
                            && HidD_GetIndexedString(dev.hid->hidHandle, stringIndex, wideNameBuffer, sizeof(wideNameBuffer))
                            && wideNameBuffer[0] != L'\0') {
                            dev.valueOutputCapsNames.push_back(wideToUtf8(wideNameBuffer));
                        } else {
                            dev.valueOutputCapsNames.push_back(outputLabel(valueCaps.UsagePage, specific));
                        }
                    }
                }
            }
        }

        dev.hid->buttonOutputStates.assign(dev.buttonOutputCapsNames.size(), false);
        dev.hid->valueOutputStates.assign(dev.valueOutputCapsNames.size(), 0.0f);
        dev.outputPending = false;

        InitializeCriticalSection(&dev.csInput);
        InitializeCriticalSection(&dev.csOutput);

        // Output starts disabled — enabled on first setLight() call for this device.
        // This prevents sending all-zero reports to devices without light bindings.
        dev.outputEnabled = false;

        impl->devices.push_back(std::move(dev));
    }

    for (const auto& dev : impl->devices) {
        if (!dev.hid) {
            continue;
        }
        impl->prevButtonStates[dev.path] = std::vector<bool>(dev.hid->buttonStates.size(), false);
        // Build prevHatStates: one entry per value cap that is a hat switch.
        std::vector<float> hatSnapshot;
        for (size_t vi = 0; vi < dev.hid->valueCapsList.size(); vi++) {
            auto& valueCaps = dev.hid->valueCapsList[vi];
            if (valueCaps.UsagePage == 0x01 && valueCaps.Range.UsageMin == 0x39) {
                hatSnapshot.push_back(-1.0f);
            }
        }
        impl->prevHatStates[dev.path] = hatSnapshot;
    }

    // Enumerate mouse devices.
    impl->mice.clear();
    for (UINT i = 0; i < deviceCount; i++) {
        if (deviceList[i].dwType != RIM_TYPEMOUSE) {
            continue;
        }
        UINT pathLen = 0;
        GetRawInputDeviceInfoA(deviceList[i].hDevice, RIDI_DEVICENAME, nullptr, &pathLen);
        if (pathLen == 0) {
            continue;
        }
        std::string path(pathLen, '\0');
        if (GetRawInputDeviceInfoA(deviceList[i].hDevice, RIDI_DEVICENAME, &path[0], &pathLen) == static_cast<UINT>(-1)) {
            continue;
        }
        while (!path.empty() && path.back() == '\0') {
            path.pop_back();
        }
        if (path.empty()) {
            continue;
        }

        MouseDevice md;
        md.path = path;
        md.rawHandle = deviceList[i].hDevice;

        md.name = deviceDescFromPath(path);
        if (md.name.empty()) {
            md.name = vidPidFromPath(path);
        }

        impl->mice.push_back(std::move(md));
    }
}

static void handleWmInput(InputManagerImpl* impl, HRAWINPUT hri) {
    UINT dataSize = 0;
    if (GetRawInputData(hri, RID_INPUT, nullptr, &dataSize, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) {
        return;
    }
    if (dataSize == 0) {
        return;
    }

    std::vector<BYTE> buf(dataSize);
    if (GetRawInputData(hri, RID_INPUT, buf.data(), &dataSize, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) {
        return;
    }

    const RAWINPUT* raw = reinterpret_cast<const RAWINPUT*>(buf.data());

    // Handle mouse raw input for turntable bindings.
    if (raw->header.dwType == RIM_TYPEMOUSE) {
        const RAWMOUSE& mouse = raw->data.mouse;
        if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
            bool anyActive = false;
            for (int port = 0; port < 2; port++) {
                if (impl->mouseBindings[port].axis < 0) continue;
                if (impl->mouseBindings[port].rawHandle != raw->header.hDevice) continue;
                LONG delta = (impl->mouseBindings[port].axis == 0) ? mouse.lLastX : mouse.lLastY;
                int scaled = (delta * impl->mouseBindings[port].sensitivity) / 5;
                if (scaled != 0) {
                    InterlockedExchangeAdd(&impl->mousePos[port], static_cast<LONG>(scaled));
                    anyActive = true;
                }
            }
            if (anyActive && impl->inputCallback) {
                impl->inputCallback(impl->inputCallbackUserData);
            }
        }
        return;
    }

    if (raw->header.dwType != RIM_TYPEHID) {
        return;
    }

    const RAWHID& hidData = raw->data.hid;

    // Find matching device by raw handle under devicesLock (brief).
    EnterCriticalSection(&impl->devicesLock);
    Device* dev = nullptr;
    for (auto& d : impl->devices) {
        if (d.rawHandle == raw->header.hDevice) {
            dev = &d;
            break;
        }
    }
    if (!dev || !dev->hid) {
        LeaveCriticalSection(&impl->devicesLock);
        return;
    }
    // Release devicesLock now — we have a stable pointer; use per-device lock.
    LeaveCriticalSection(&impl->devicesLock);

    const BYTE*  reportBuf  = hidData.bRawData;
    const UINT   reportSize = hidData.dwSizeHid;
    PHIDP_PREPARSED_DATA preparsed = dev->hid->preparsed;

    // Acquire per-device input lock for state mutation.
    EnterCriticalSection(&dev->csInput);

    {
        int stateOffset = 0;
        for (size_t capI = 0; capI < dev->hid->buttonCapsList.size(); capI++) {
            auto& buttonCaps = dev->hid->buttonCapsList[capI];
            int btnCount = static_cast<int>(buttonCaps.Range.UsageMax - buttonCaps.Range.UsageMin + 1);
            if (btnCount <= 0) {
                continue;
            }

            ULONG usagesLen = static_cast<ULONG>(btnCount);
            std::vector<USAGE> usages(btnCount);
            bool queryOk = (HidP_GetUsages(
                HidP_Input,
                buttonCaps.UsagePage,
                buttonCaps.LinkCollection,
                usages.data(),
                &usagesLen,
                preparsed,
                reinterpret_cast<PCHAR>(const_cast<BYTE*>(reportBuf)),
                reportSize) == HIDP_STATUS_SUCCESS);

            for (int b = 0; b < btnCount; b++) {
                dev->hid->buttonStates[stateOffset + b] = false;
            }
            if (queryOk) {
                for (ULONG u = 0; u < usagesLen; u++) {
                    int idx = static_cast<int>(usages[u] - buttonCaps.Range.UsageMin);
                    if (idx >= 0 && idx < btnCount) {
                        dev->hid->buttonStates[stateOffset + idx] = true;
                    }
                }
            }

            stateOffset += btnCount;
        }
    }

    // Value states (axes + hat switches as float).
    for (size_t capI = 0; capI < dev->hid->valueCapsList.size(); capI++) {
        auto& valueCaps = dev->hid->valueCapsList[capI];

        ULONG rawUlong = 0;
        NTSTATUS status = HidP_GetUsageValue(
            HidP_Input,
            valueCaps.UsagePage,
            valueCaps.LinkCollection,
            valueCaps.Range.UsageMin,
            &rawUlong,
            preparsed,
            reinterpret_cast<PCHAR>(const_cast<BYTE*>(reportBuf)),
            reportSize);

        if (status != HIDP_STATUS_SUCCESS) {
            continue;
        }

        bool isHat = (valueCaps.UsagePage == 0x01 && valueCaps.Range.UsageMin == 0x39);

        if (isHat) {
            // Hat switch — store as float. -1.0f = neutral.
            // Sign extension already applied at enumeration to LogicalMin/Max.
            LONG rawVal = static_cast<LONG>(rawUlong);

            // Sign-extension fix at WM_INPUT — only when LogicalMin < 0 (sign-extension location 3 of 3).
            if (valueCaps.LogicalMin < 0 && valueCaps.BitSize > 0 &&
                    valueCaps.BitSize <= static_cast<USHORT>(sizeof(valueCaps.LogicalMin) * 8)) {
                auto shiftSize = sizeof(valueCaps.LogicalMin) * 8 - valueCaps.BitSize + 1;
                rawVal <<= shiftSize;
                rawVal >>= shiftSize;
            }

            LONG lmin = valueCaps.LogicalMin, lmax = valueCaps.LogicalMax;
            float hatVal = -1.0f;  // neutral
            if (lmin <= rawVal && rawVal <= lmax && lmax > lmin) {
                hatVal = static_cast<float>(rawVal - lmin) / static_cast<float>(lmax - lmin);
            }
            dev->hid->valueStates[capI] = hatVal;
        } else {
            LONG rawVal = static_cast<LONG>(rawUlong);

            // Sign-extension fix — only when LogicalMin < 0
            // (sign-extension location 3 of 3, shared with hat above).
            if (valueCaps.LogicalMin < 0 &&
                    valueCaps.BitSize > 0 &&
                    valueCaps.BitSize <= static_cast<USHORT>(sizeof(valueCaps.LogicalMin) * 8)) {
                auto shiftSize = sizeof(valueCaps.LogicalMin) * 8 - valueCaps.BitSize + 1;
                rawVal <<= shiftSize;
                rawVal >>= shiftSize;
            }

            dev->hid->valueStatesRaw[capI] = rawVal;

            // Auto-calibration: expand range on the fly.
            if (rawVal < valueCaps.LogicalMin) {
                valueCaps.LogicalMin = rawVal;
            }
            if (rawVal > valueCaps.LogicalMax) {
                valueCaps.LogicalMax = rawVal;
            }

            LONG lmin = valueCaps.LogicalMin;
            LONG lmax = valueCaps.LogicalMax;
            float normalized = (lmax > lmin)
                ? static_cast<float>(rawVal - lmin) / static_cast<float>(lmax - lmin)
                : 0.5f;
            if (normalized < 0.0f) {
                normalized = 0.0f;
            }
            if (normalized > 1.0f) {
                normalized = 1.0f;
            }

            dev->hid->valueStates[capI] = normalized;
        }
    }

    LeaveCriticalSection(&dev->csInput);

    // Capture mode edge detection.
    {
        EnterCriticalSection(&impl->captureLock);
        bool capturing = impl->captureMode;
        bool alreadyCaptured = impl->captureResultReady;
        LeaveCriticalSection(&impl->captureLock);

        if (capturing && !alreadyCaptured) {
            auto& prev = impl->prevButtonStates[dev->path];

            EnterCriticalSection(&dev->csInput);
            size_t btnSize = dev->hid->buttonStates.size();

            // Ensure prev vector is the right size.
            if (prev.size() != btnSize) {
                prev.assign(btnSize, false);
            }

            for (int i = 0; i < static_cast<int>(btnSize); i++) {
                if (dev->hid->buttonStates[i] && !prev[i]) {
                    CaptureResult cr;
                    cr.path        = dev->path;
                    cr.buttonIdx  = i;
                    cr.deviceName = dev->name;
                    cr.analogType = ButtonAnalogType::NONE;

                    EnterCriticalSection(&impl->captureLock);
                    if (!impl->captureResultReady) {
                        impl->captureResult = cr;
                        impl->captureResultReady = true;
                    }
                    LeaveCriticalSection(&impl->captureLock);
                    break;
                }
            }

            // Copy current button states for next edge detection.
            prev.assign(dev->hid->buttonStates.begin(), dev->hid->buttonStates.end());

            // Hat direction change detection.
            EnterCriticalSection(&impl->captureLock);
            alreadyCaptured = impl->captureResultReady;
            LeaveCriticalSection(&impl->captureLock);

            if (!alreadyCaptured) {
                auto& prevHats = impl->prevHatStates[dev->path];
                int hatIdx = 0;
                for (size_t vi = 0; vi < dev->hid->valueCapsList.size(); vi++) {
                    auto& valueCaps = dev->hid->valueCapsList[vi];
                    if (valueCaps.UsagePage != 0x01 || valueCaps.Range.UsageMin != 0x39) {
                        continue;
                    }

                    float curVal = dev->hid->valueStates[vi];
                    float prevVal = (hatIdx < static_cast<int>(prevHats.size())) ? prevHats[hatIdx] : -1.0f;

                    // Detect direction change: was neutral and now has direction,
                    // or direction changed from snapshot.
                    if (curVal >= 0.0f && (prevVal < 0.0f || std::fabs(curVal - prevVal) > HAT_SWITCH_INCREMENT * 0.5f)) {
                        ButtonAnalogType dir = getHatDirection(curVal);
                        if (dir != ButtonAnalogType::NONE) {
                            CaptureResult cr;
                            cr.path        = dev->path;
                            cr.buttonIdx  = static_cast<int>(vi);  // axisIdx into valueStates
                            cr.deviceName = dev->name;
                            cr.analogType = dir;

                            EnterCriticalSection(&impl->captureLock);
                            if (!impl->captureResultReady) {
                                impl->captureResult = cr;
                                impl->captureResultReady = true;
                            }
                            LeaveCriticalSection(&impl->captureLock);
                            break;
                        }
                    }

                    if (hatIdx < static_cast<int>(prevHats.size())) {
                        prevHats[hatIdx] = curVal;
                    }
                    hatIdx++;
                }
            }

            LeaveCriticalSection(&dev->csInput);
        }
    }

    if (impl->inputCallback) {
        impl->inputCallback(impl->inputCallbackUserData);
    }
}

static LRESULT CALLBACK EZ2InputWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }

    auto* impl = reinterpret_cast<InputManagerImpl*>(
        GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    if (!impl) {
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }

    if (msg == WM_INPUT) {
        handleWmInput(impl, reinterpret_cast<HRAWINPUT>(lParam));
        DefWindowProcA(hwnd, msg, wParam, lParam);
        return 0;
    }
    if (msg == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static DWORD WINAPI msgPumpThread(LPVOID param) {
    InputManagerImpl* impl = reinterpret_cast<InputManagerImpl*>(param);

    // Set time-critical priority for input pump thread.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    WNDCLASSEXA wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXA);
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.lpfnWndProc   = EZ2InputWndProc;
    wc.lpszClassName = "EZ2InputMgr";
    RegisterClassExA(&wc);

    // Create message-only window — pass impl via lpCreateParams.
    HWND hwnd = CreateWindowExA(
        0,
        "EZ2InputMgr",
        nullptr,
        0, 0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        wc.hInstance,
        impl);  // lpCreateParams = impl for GWLP_USERDATA
    impl->hwnd = hwnd;

    if (!hwnd) {
        return 1;
    }

    // Register for joystick, gamepad, and multi-axis controller HID input.
    // Avoid RIDEV_PAGEONLY (usage page 0x01, usage 0x00) because it captures
    // keyboard and mouse raw input too, which breaks overlays like DDraw Compat
    // that rely on receiving keyboard raw input (e.g. Shift+F11).
    RAWINPUTDEVICE rids[] = {
        { 0x01, 0x04, RIDEV_INPUTSINK, hwnd },  // Joystick
        { 0x01, 0x05, RIDEV_INPUTSINK, hwnd },  // Gamepad
        { 0x01, 0x08, RIDEV_INPUTSINK, hwnd },  // Multi-axis Controller
        { 0x01, 0x02, RIDEV_INPUTSINK, hwnd },  // Mouse
    };
    RegisterRawInputDevices(rids, 4, sizeof(RAWINPUTDEVICE));

    MSG msg;
    while (impl->running && GetMessageA(&msg, hwnd, 0, 0) > 0) {
        DispatchMessageA(&msg);
    }

    DestroyWindow(hwnd);
    impl->hwnd = nullptr;
    return 0;
}

static DWORD WINAPI vttThreadFunc(LPVOID param) {
    InputManagerImpl* impl = reinterpret_cast<InputManagerImpl*>(param);
    timeBeginPeriod(1);

    while (impl->running) {
        for (int port = 0; port < 2; port++) {
            int plusVk  = impl->vttBindings[port].plusVk;
            int minusVk = impl->vttBindings[port].minusVk;
            int step     = impl->vttBindings[port].step;

            if (plusVk != 0 && (GetAsyncKeyState(plusVk) & 0x8000)) {
                InterlockedExchangeAdd(&impl->vttPos[port], static_cast<LONG>(step));
            }
            if (minusVk != 0 && (GetAsyncKeyState(minusVk) & 0x8000)) {
                InterlockedExchangeAdd(&impl->vttPos[port], static_cast<LONG>(-step));
            }
        }
        Sleep(5);
    }

    timeEndPeriod(1);
    return 0;
}

static DWORD WINAPI outputThreadFunc(LPVOID param) {
    InputManagerImpl* impl = reinterpret_cast<InputManagerImpl*>(param);
    while (impl->running) {
        WaitForSingleObject(impl->outputEvent, INFINITE);
        if (!impl->running) {
            break;
        }

        // Do-while: re-check after processing to catch updates that arrived during writes.
        bool anyPending;
        do {
            anyPending = false;
            for (auto& dev : impl->devices) {
                EnterCriticalSection(&dev.csOutput);
                bool pending = dev.outputPending && dev.outputEnabled;
                dev.outputPending = false;
                if (pending) {
                    // Hold csOutput through entire write (report build + WriteFile).
                    // HID reports are small, fast I/O — holding the lock is intentional.
                    deviceWriteOutput(dev);
                }
                LeaveCriticalSection(&dev.csOutput);
                if (pending) {
                    anyPending = true;
                }
            }
            // Check if any device got a new update during our write pass.
            if (!anyPending) {
                for (auto& dev : impl->devices) {
                    EnterCriticalSection(&dev.csOutput);
                    if (dev.outputPending && dev.outputEnabled) {
                        anyPending = true;
                    }
                    LeaveCriticalSection(&dev.csOutput);
                    if (anyPending) {
                        break;
                    }
                }
            }
        } while (anyPending && impl->running);
    }
    return 0;
}

static DWORD WINAPI flushThreadFunc(LPVOID param) {
    InputManagerImpl* impl = reinterpret_cast<InputManagerImpl*>(param);
    while (impl->running) {
        Sleep(495);
        if (!impl->running) {
            break;
        }
        for (auto& dev : impl->devices) {
            EnterCriticalSection(&dev.csOutput);
            bool enabled = dev.outputEnabled;
            if (enabled) {
                deviceWriteOutput(dev);
            }
            LeaveCriticalSection(&dev.csOutput);
        }
    }
    return 0;
}

InputManager::InputManager() {
    impl = new InputManagerImpl();

    impl->outputEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    devicesReload(impl);

    impl->running = true;
    impl->pumpThread = CreateThread(
        nullptr, 0, msgPumpThread, impl, 0, nullptr);

    // Wait for HWND to be created (max ~500ms).
    for (int i = 0; i < 500 && !impl->hwnd; i++) {
        Sleep(1);
    }

    impl->vttPos[0] = TT_CENTER_INTERNAL;
    impl->vttPos[1] = TT_CENTER_INTERNAL;
    impl->vttThread = CreateThread(
        nullptr, 0, vttThreadFunc, impl, 0, nullptr);

    // Start output thread (event-based) and flush thread (periodic 500ms).
    impl->outputThread = CreateThread(nullptr, 0, outputThreadFunc, impl, 0, nullptr);
    impl->flushThread  = CreateThread(nullptr, 0, flushThreadFunc, impl, 0, nullptr);
}

InputManager::~InputManager() {
    impl->running = false;

    // Wake output thread so it can exit.
    if (impl->outputEvent) {
        SetEvent(impl->outputEvent);
    }

    // Stop pump thread — WM_CLOSE triggers DestroyWindow inside the thread.
    if (impl->hwnd) {
        PostMessageA(impl->hwnd, WM_CLOSE, 0, 0);
    }
    if (impl->pumpThread) {
        WaitForSingleObject(impl->pumpThread, 2000);
        CloseHandle(impl->pumpThread);
        impl->pumpThread = nullptr;
    }

    if (impl->vttThread) {
        WaitForSingleObject(impl->vttThread, 500);
        CloseHandle(impl->vttThread);
        impl->vttThread = nullptr;
    }

    if (impl->outputThread) {
        WaitForSingleObject(impl->outputThread, 2000);
        CloseHandle(impl->outputThread);
        impl->outputThread = nullptr;
    }

    if (impl->flushThread) {
        WaitForSingleObject(impl->flushThread, 2000);
        CloseHandle(impl->flushThread);
        impl->flushThread = nullptr;
    }

    if (impl->outputEvent) {
        CloseHandle(impl->outputEvent);
        impl->outputEvent = nullptr;
    }

    // Free device data (destroy() calls DeleteCriticalSection for both csInput, csOutput).
    for (auto& dev : impl->devices) {
        dev.destroy();
    }
    impl->devices.clear();

    delete impl;
    impl = nullptr;
}

std::vector<Device> InputManager::getDevices() const {
    EnterCriticalSection(&impl->devicesLock);
    std::vector<Device> snapshot = impl->devices;
    LeaveCriticalSection(&impl->devicesLock);
    return snapshot;
}

bool InputManager::snapshotDevice(const std::string& path, DeviceSnapshot& out) const {
    EnterCriticalSection(&impl->devicesLock);
    for (auto& d : impl->devices) {
        if (d.path == path && d.hid) {
            EnterCriticalSection(&d.csInput);
            out.buttons = d.hid->buttonStates;
            out.values  = d.hid->valueStates;
            LeaveCriticalSection(&d.csInput);
            LeaveCriticalSection(&impl->devicesLock);
            return true;
        }
    }
    LeaveCriticalSection(&impl->devicesLock);
    return false;
}

void InputManager::setInputCallback(void(*fn)(void*), void* ud) {
    impl->inputCallback            = fn;
    impl->inputCallbackUserData   = ud;
}

bool InputManager::getButtonState(const std::string& path, int buttonIdx) const {
    EnterCriticalSection(&impl->devicesLock);
    for (auto& dev : impl->devices) {
        if (dev.path == path) {
            LeaveCriticalSection(&impl->devicesLock);
            if (!dev.hid) {
                return false;
            }
            EnterCriticalSection(&dev.csInput);
            bool state = false;
            if (buttonIdx >= 0 && buttonIdx < static_cast<int>(dev.hid->buttonStates.size())) {
                state = dev.hid->buttonStates[buttonIdx];
            }
            LeaveCriticalSection(&dev.csInput);
            return state;
        }
    }
    LeaveCriticalSection(&impl->devicesLock);
    return false;
}

float InputManager::getAxisValue(const std::string& path, int axisIdx) const {
    EnterCriticalSection(&impl->devicesLock);
    for (auto& dev : impl->devices) {
        if (dev.path == path) {
            LeaveCriticalSection(&impl->devicesLock);
            if (!dev.hid) {
                return 0.5f;
            }
            EnterCriticalSection(&dev.csInput);
            float val = 0.5f;
            if (axisIdx >= 0 && axisIdx < static_cast<int>(dev.hid->valueStates.size())) {
                val = dev.hid->valueStates[axisIdx];
            }
            LeaveCriticalSection(&dev.csInput);
            return val;
        }
    }
    LeaveCriticalSection(&impl->devicesLock);
    return 0.5f;
}

void InputManager::setVttKeys(int port, int plusVk, int minusVk, int step) {
    if (port < 0 || port > 1) {
        return;
    }
    impl->vttBindings[port].plusVk  = plusVk;
    impl->vttBindings[port].minusVk = minusVk;
    impl->vttBindings[port].step     = step > 0 ? step : 3;
}

uint8_t InputManager::getVttPosition(int port) const {
    if (port < 0 || port > 1) {
        return TT_CENTER;
    }
    return static_cast<uint8_t>((impl->vttPos[port] / TT_INTERNAL_MULTIPLIER) & 0xFF);
}

std::vector<MouseDeviceInfo> InputManager::getMouseDevices() const {
    std::vector<MouseDeviceInfo> result;
    for (auto& m : impl->mice) {
        result.push_back({m.path, m.name});
    }
    return result;
}

void InputManager::setMouseBinding(int port, const std::string& devicePath, int axis, int sensitivity) {
    if (port < 0 || port > 1) return;
    impl->mouseBindings[port].axis = axis;
    impl->mouseBindings[port].sensitivity = (sensitivity > 0) ? sensitivity : 5;
    impl->mouseBindings[port].rawHandle = nullptr;
    for (auto& m : impl->mice) {
        if (m.path == devicePath) {
            impl->mouseBindings[port].rawHandle = m.rawHandle;
            break;
        }
    }
}

uint8_t InputManager::getMousePosition(int port) const {
    if (port < 0 || port > 1) return TT_CENTER;
    return static_cast<uint8_t>((impl->mousePos[port] / TT_INTERNAL_MULTIPLIER) & 0xFF);
}

void InputManager::startCapture() {
    EnterCriticalSection(&impl->captureLock);
    impl->captureMode         = true;
    impl->captureResultReady = false;
    LeaveCriticalSection(&impl->captureLock);

    // Reset prev states so any current press is treated as a fresh edge.
    EnterCriticalSection(&impl->devicesLock);
    for (auto& kv : impl->prevButtonStates) {
        std::fill(kv.second.begin(), kv.second.end(), false);
    }
    // Reset hat snapshots to neutral.
    for (auto& kv : impl->prevHatStates) {
        std::fill(kv.second.begin(), kv.second.end(), -1.0f);
    }
    LeaveCriticalSection(&impl->devicesLock);
}

void InputManager::stopCapture() {
    EnterCriticalSection(&impl->captureLock);
    impl->captureMode         = false;
    impl->captureResultReady = false;
    LeaveCriticalSection(&impl->captureLock);
}

bool InputManager::pollCapture(CaptureResult& out) {
    EnterCriticalSection(&impl->captureLock);
    bool ready = impl->captureResultReady;
    if (ready) {
        out = impl->captureResult;
        impl->captureResultReady = false;
    }
    LeaveCriticalSection(&impl->captureLock);
    return ready;
}

void InputManager::setLight(const std::string& path, int outputIdx, float value) {
    EnterCriticalSection(&impl->devicesLock);
    for (auto& dev : impl->devices) {
        if (dev.path != path) {
            continue;
        }
        LeaveCriticalSection(&impl->devicesLock);

        if (!dev.hid || dev.hid->hidHandle == INVALID_HANDLE_VALUE ||
            dev.hid->caps.OutputReportByteLength == 0) {
            return;
        }

        EnterCriticalSection(&dev.csOutput);
        dev.outputEnabled = true;
        int btnCount = static_cast<int>(dev.hid->buttonOutputStates.size());
        if (outputIdx < btnCount) {
            dev.hid->buttonOutputStates[outputIdx] = (value > 0.5f);
        } else {
            int valIdx = outputIdx - btnCount;
            if (valIdx < static_cast<int>(dev.hid->valueOutputStates.size())) {
                dev.hid->valueOutputStates[valIdx] = value;
            }
        }
        dev.outputPending = true;
        LeaveCriticalSection(&dev.csOutput);

        SetEvent(impl->outputEvent);
        return;
    }
    LeaveCriticalSection(&impl->devicesLock);
}

void InputManager::disableOutput(const std::string& path) {
    EnterCriticalSection(&impl->devicesLock);
    for (auto& dev : impl->devices) {
        if (dev.path != path) {
            continue;
        }
        LeaveCriticalSection(&impl->devicesLock);
        EnterCriticalSection(&dev.csOutput);
        dev.outputEnabled = false;
        LeaveCriticalSection(&dev.csOutput);
        return;
    }
    LeaveCriticalSection(&impl->devicesLock);
}
