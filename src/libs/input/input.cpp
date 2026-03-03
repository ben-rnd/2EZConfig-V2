#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>  // timeBeginPeriod / timeEndPeriod
extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}
#include "input.h"
#include "binding.h"
#include "device_info.h"
#include "settings.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <array>
#include <cstdint>

// ---------------------------------------------------------------------------
// Internal state (file-scope, not exported)
// ---------------------------------------------------------------------------

static volatile bool g_running = false;

static HANDLE g_pollThread = nullptr;
static HANDLE g_vttThread  = nullptr;

static std::vector<DeviceInfo> g_devices;

static std::map<std::string, ButtonBinding>    g_buttonBindings;
static std::map<std::string, KeyboardBinding>  g_keyBindings;
static std::map<std::string, AnalogBinding>    g_analogBindings;
static std::array<VTTBinding,        2>        g_vttBindings     = {};
static std::array<MouseWheelBinding, 2>        g_mouseWheelBindings = {};
static std::array<std::string,       2>        g_mouseWheelDevicePaths = {};

struct InputState {
    volatile uint8_t ttPos[2];    // combined turntable positions (Plan 03 writes)
    volatile uint8_t vttDelta[2]; // VTT/mouse accumulator (Plan 03 writes)
};
static InputState g_state = {};

// Per-action logical button state.  true = pressed.
// bool reads on x86 are effectively atomic for aligned single-byte values.
static std::map<std::string, volatile bool> g_buttonState;

// ---------------------------------------------------------------------------
// DeviceInfo::close
// ---------------------------------------------------------------------------

void DeviceInfo::close() {
    if (preparsedData) { HidD_FreePreparsedData(preparsedData); preparsedData = nullptr; }
    if (ov.hEvent)     { CloseHandle(ov.hEvent); ov.hEvent = nullptr; }
    if (handle != INVALID_HANDLE_VALUE) { CloseHandle(handle); handle = INVALID_HANDLE_VALUE; }
    readPending = false;
}

// ---------------------------------------------------------------------------
// openAndBuildDeviceInfo — internal helper
// ---------------------------------------------------------------------------

static DeviceInfo openAndBuildDeviceInfo(const std::string& path) {
    DeviceInfo dev;
    dev.path = path;

    dev.handle = CreateFileA(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        NULL
    );
    if (dev.handle == INVALID_HANDLE_VALUE) {
        return dev;
    }

    if (!HidD_GetPreparsedData(dev.handle, &dev.preparsedData)) {
        CloseHandle(dev.handle); dev.handle = INVALID_HANDLE_VALUE;
        return dev;
    }

    if (HidP_GetCaps(dev.preparsedData, &dev.caps) != HIDP_STATUS_SUCCESS) {
        HidD_FreePreparsedData(dev.preparsedData); dev.preparsedData = nullptr;
        CloseHandle(dev.handle); dev.handle = INVALID_HANDLE_VALUE;
        return dev;
    }

    // Button caps
    if (dev.caps.NumberInputButtonCaps > 0) {
        dev.buttonCaps.resize(dev.caps.NumberInputButtonCaps);
        USHORT btnCapsLen = dev.caps.NumberInputButtonCaps;
        HidP_GetButtonCaps(HidP_Input, dev.buttonCaps.data(), &btnCapsLen, dev.preparsedData);
        // Normalize IsRange=FALSE: promote NotRange.Usage to Range.UsageMin/Max
        for (auto& bc : dev.buttonCaps) {
            if (!bc.IsRange) {
                bc.Range.UsageMin = bc.NotRange.Usage;
                bc.Range.UsageMax = bc.NotRange.Usage;
            }
        }
    }

    // Value caps
    if (dev.caps.NumberInputValueCaps > 0) {
        dev.valueCaps.resize(dev.caps.NumberInputValueCaps);
        USHORT valCapsLen = dev.caps.NumberInputValueCaps;
        HidP_GetValueCaps(HidP_Input, dev.valueCaps.data(), &valCapsLen, dev.preparsedData);
    }

    // Allocate usages buffer: worst-case count = sum of all (UsageMax - UsageMin + 1)
    ULONG usagesBufSize = 0;
    for (auto& bc : dev.buttonCaps) {
        usagesBufSize += (ULONG)(bc.Range.UsageMax - bc.Range.UsageMin + 1);
    }
    if (usagesBufSize == 0) usagesBufSize = 1;
    dev.usagesBuf.resize(usagesBufSize);

    // Report buffer
    dev.reportBuf.resize(dev.caps.InputReportByteLength);

    // Overlapped event
    dev.ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    // Issue first async read
    ReadFile(dev.handle, dev.reportBuf.data(), dev.caps.InputReportByteLength, NULL, &dev.ov);
    dev.readPending = true;

    return dev;
}

// ---------------------------------------------------------------------------
// enumerateDevicesInternal — returns list of HID device paths
// ---------------------------------------------------------------------------

static std::vector<std::string> enumerateDevicesInternal() {
    std::vector<std::string> paths;

    UINT count = 0;
    GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST));
    if (count == 0) return paths;

    std::vector<RAWINPUTDEVICELIST> devList(count);
    if (GetRawInputDeviceList(devList.data(), &count, sizeof(RAWINPUTDEVICELIST)) == (UINT)-1)
        return paths;

    for (auto& entry : devList) {
        if (entry.dwType != RIM_TYPEHID) continue;

        UINT nameLen = 0;
        GetRawInputDeviceInfoA(entry.hDevice, RIDI_DEVICENAME, nullptr, &nameLen);
        if (nameLen == 0) continue;

        std::string name(nameLen, '\0');
        GetRawInputDeviceInfoA(entry.hDevice, RIDI_DEVICENAME, &name[0], &nameLen);
        // nameLen now includes the null terminator; strip it
        if (!name.empty() && name.back() == '\0')
            name.pop_back();

        paths.push_back(name);
    }

    return paths;
}

// ---------------------------------------------------------------------------
// Forward declarations for polling functions
// ---------------------------------------------------------------------------
static void pollAllDevices();
static void pollKeyboard();

// ---------------------------------------------------------------------------
// Thread functions — stubs/implementation
// ---------------------------------------------------------------------------

static DWORD WINAPI pollingThread(void*) {
    timeBeginPeriod(1);
    while (g_running) {
        pollAllDevices();
        pollKeyboard();
        Sleep(1);
    }
    timeEndPeriod(1);
    return 0;
}

static DWORD WINAPI vttThread(void*) {
    return 0;  // Plan 03 implements this
}

// ---------------------------------------------------------------------------
// Input::enumerateDevices — for Phase 2 UI
// ---------------------------------------------------------------------------

std::vector<Input::DeviceDesc> Input::enumerateDevices() {
    std::vector<DeviceDesc> result;
    std::vector<std::string> paths = enumerateDevicesInternal();

    for (const auto& path : paths) {
        DeviceDesc desc;
        desc.path = path;

        // Open a temporary synchronous handle just for string queries
        HANDLE h = CreateFileA(
            path.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            0,
            NULL
        );
        if (h == INVALID_HANDLE_VALUE) {
            result.push_back(desc);
            continue;
        }

        // Manufacturer string
        wchar_t wbuf[256] = {};
        if (HidD_GetManufacturerString(h, wbuf, sizeof(wbuf))) {
            int len = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                std::string s(len, '\0');
                WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, &s[0], len, nullptr, nullptr);
                if (!s.empty() && s.back() == '\0') s.pop_back();
                desc.manufacturer = s;
            }
        }

        // Product string
        wchar_t wprod[256] = {};
        if (HidD_GetProductString(h, wprod, sizeof(wprod))) {
            int len = WideCharToMultiByte(CP_UTF8, 0, wprod, -1, nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                std::string s(len, '\0');
                WideCharToMultiByte(CP_UTF8, 0, wprod, -1, &s[0], len, nullptr, nullptr);
                if (!s.empty() && s.back() == '\0') s.pop_back();
                desc.product = s;
            }
        }

        CloseHandle(h);
        result.push_back(desc);
    }

    return result;
}

// ---------------------------------------------------------------------------
// Input::init
// ---------------------------------------------------------------------------

void Input::init(SettingsManager& settings) {
    json& bindings = settings.globalSettings()["bindings"];

    for (auto it = bindings.begin(); it != bindings.end(); ++it) {
        const std::string& actionName = it.key();
        const json& b = it.value();

        if (!b.contains("type")) continue;
        std::string btype = b["type"].get<std::string>();

        if (btype == "hid_button") {
            ButtonBinding bb;
            bb.device_path = b.value("device_path", "");
            bb.usage_page  = b.value("usage_page",  (uint16_t)0);
            bb.usage_id    = b.value("usage_id",    (uint16_t)0);
            g_buttonBindings[actionName] = bb;
            g_buttonState[actionName] = false;
        }
        else if (btype == "keyboard") {
            KeyboardBinding kb;
            kb.vk_code = b.value("vk_code", (uint32_t)0);
            g_keyBindings[actionName] = kb;
            g_buttonState[actionName] = false;
        }
        else if (btype == "hid_axis") {
            AnalogBinding ab;
            ab.device_path = b.value("device_path", "");
            ab.usage_page  = b.value("usage_page",  (uint16_t)0);
            ab.usage_id    = b.value("usage_id",    (uint16_t)0);
            ab.reverse     = b.value("reverse",     false);
            ab.sensitivity = b.value("sensitivity", 1.0f);
            ab.dead_zone   = b.value("dead_zone",   (uint8_t)0);
            g_analogBindings[actionName] = ab;
        }
        else if (btype == "virtual_tt") {
            // Key convention: "{analog_name}_vtt" e.g. "P1 Turntable_vtt"
            // Strip "_vtt" suffix to find the port index
            std::string base = actionName;
            if (base.size() > 4 && base.substr(base.size() - 4) == "_vtt")
                base = base.substr(0, base.size() - 4);

            int port = -1;
            if (base == "P1 Turntable") port = 0;
            else if (base == "P2 Turntable") port = 1;
            if (port < 0) continue;

            VTTBinding vb;
            vb.plus_vk  = b.value("plus_vk",  (uint32_t)0);
            vb.minus_vk = b.value("minus_vk", (uint32_t)0);
            vb.step     = b.value("step",      (uint8_t)3);
            g_vttBindings[(size_t)port] = vb;
        }
        else if (btype == "mouse_wheel") {
            // Key convention: "{analog_name}_mouse" e.g. "P1 Turntable_mouse"
            std::string base = actionName;
            if (base.size() > 6 && base.substr(base.size() - 6) == "_mouse")
                base = base.substr(0, base.size() - 6);

            int port = -1;
            if (base == "P1 Turntable") port = 0;
            else if (base == "P2 Turntable") port = 1;
            if (port < 0) continue;

            MouseWheelBinding mwb;
            mwb.device_path = b.value("device_path", "");
            mwb.step        = b.value("step",         (uint8_t)3);
            g_mouseWheelBindings[(size_t)port]     = mwb;
            g_mouseWheelDevicePaths[(size_t)port]  = mwb.device_path;
        }
    }

    // Collect the set of device paths that must be opened
    std::set<std::string> neededPaths;
    for (auto& kv : g_buttonBindings)  neededPaths.insert(kv.second.device_path);
    for (auto& kv : g_analogBindings)  neededPaths.insert(kv.second.device_path);
    for (int i = 0; i < 2; i++) {
        if (!g_mouseWheelDevicePaths[(size_t)i].empty())
            neededPaths.insert(g_mouseWheelDevicePaths[(size_t)i]);
    }

    // Enumerate HID devices and open the ones we need
    std::vector<std::string> allPaths = enumerateDevicesInternal();
    for (const auto& path : allPaths) {
        if (neededPaths.count(path) == 0) continue;
        DeviceInfo dev = openAndBuildDeviceInfo(path);
        if (dev.isOpen()) {
            g_devices.push_back(std::move(dev));
        }
    }

    g_running = true;

    DWORD tid = 0;
    g_pollThread = CreateThread(nullptr, 0, pollingThread, nullptr, 0, &tid);
    g_vttThread  = CreateThread(nullptr, 0, vttThread,    nullptr, 0, &tid);
}

// ---------------------------------------------------------------------------
// Input::shutdown
// ---------------------------------------------------------------------------

void Input::shutdown() {
    g_running = false;

    if (g_pollThread) {
        WaitForSingleObject(g_pollThread, 500);
        CloseHandle(g_pollThread);
        g_pollThread = nullptr;
    }
    if (g_vttThread) {
        WaitForSingleObject(g_vttThread, 500);
        CloseHandle(g_vttThread);
        g_vttThread = nullptr;
    }

    for (auto& dev : g_devices) {
        dev.close();
    }

    g_devices.clear();
    g_buttonBindings.clear();
    g_keyBindings.clear();
    g_analogBindings.clear();
    g_buttonState.clear();
    g_mouseWheelDevicePaths = {};
}

// ---------------------------------------------------------------------------
// pollAllDevices — called every 1ms from pollingThread
// ---------------------------------------------------------------------------

static void pollAllDevices() {
    for (auto& dev : g_devices) {
        if (!dev.isOpen()) continue;

        DWORD bytes = 0;
        BOOL ok = GetOverlappedResult(dev.handle, &dev.ov, &bytes, FALSE);

        if (ok && bytes > 0) {
            // New report arrived — parse button states for all bindings on this device
            for (auto& kv : g_buttonBindings) {
                const std::string& actionName = kv.first;
                const ButtonBinding& binding  = kv.second;

                if (binding.device_path != dev.path) continue;

                ULONG usageCount = (ULONG)dev.usagesBuf.size();
                NTSTATUS status = HidP_GetUsages(
                    HidP_Input,
                    binding.usage_page,
                    0,
                    dev.usagesBuf.data(),
                    &usageCount,
                    dev.preparsedData,
                    (PCHAR)dev.reportBuf.data(),
                    bytes
                );

                bool pressed = false;
                if (status == HIDP_STATUS_SUCCESS) {
                    for (ULONG i = 0; i < usageCount; i++) {
                        if (dev.usagesBuf[i] == binding.usage_id) {
                            pressed = true;
                            break;
                        }
                    }
                }
                g_buttonState[actionName] = pressed;
            }

            // Re-arm read for next cycle
            ResetEvent(dev.ov.hEvent);
            ReadFile(dev.handle, dev.reportBuf.data(), dev.caps.InputReportByteLength, NULL, &dev.ov);
        }
        else if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_INCOMPLETE) {
                // No new data yet — retain cached state, continue
            }
            else {
                // Device disconnected or other error — close it
                dev.close();
            }
        }
    }
}

// ---------------------------------------------------------------------------
// pollKeyboard — called from pollingThread every 1ms
// ---------------------------------------------------------------------------

static void pollKeyboard() {
    for (auto& kv : g_keyBindings) {
        const std::string& actionName     = kv.first;
        const KeyboardBinding& binding    = kv.second;
        SHORT ks = GetAsyncKeyState((int)binding.vk_code);
        g_buttonState[actionName] = ((ks & 0x8000) != 0);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool Input::getButtonState(const std::string& gameAction) {
    auto it = g_buttonState.find(gameAction);
    if (it != g_buttonState.end()) return it->second;
    return false;  // unbound action = not pressed
}

uint8_t Input::getAnalogValue(const std::string& /*gameAction*/) {
    return 128;  // center; Plan 03 implements real value
}
