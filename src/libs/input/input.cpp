#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
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
#include <array>
#include <optional>
#include <cstdint>

// ---------------------------------------------------------------------------
// Atomic 8-bit add — MinGW shim
// ---------------------------------------------------------------------------
#if defined(__GNUC__) && !defined(_InterlockedExchangeAdd8)
static inline char _InterlockedExchangeAdd8(volatile char* ptr, char val) {
    return __sync_fetch_and_add(ptr, val);
}
#endif

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static volatile bool g_running    = false;
static volatile bool g_listenMode = false;

static HANDLE g_msgThread = nullptr;
static HANDLE g_vttThread  = nullptr;
static HWND   g_inputHwnd  = nullptr;

static std::vector<DeviceInfo> g_devices;

static std::map<std::string, ButtonBinding>   g_buttonBindings;
static std::map<std::string, KeyboardBinding> g_keyBindings;
static std::map<std::string, AnalogBinding>   g_analogBindings;
static std::array<VTTBinding, 2>              g_vttBindings = {};

// Per-action logical state (written by msg thread + keyboard thread, read by UI)
static std::map<std::string, bool> g_buttonState;
static CRITICAL_SECTION            g_stateLock;

// Turntable positions
struct AnalogState { volatile uint8_t pos[2]; volatile uint8_t vttDelta[2]; };
static AnalogState g_analog = {};

// Capture mode
static std::optional<Input::ButtonCaptureResult> g_captureResult;
static CRITICAL_SECTION                          g_captureLock;

// Previous button state per device (for edge detection in listen mode)
// Key = device VID|PID<<16, value = flat button index set
static std::map<uint32_t, std::vector<bool>> g_prevButtonStates;

// ---------------------------------------------------------------------------
// enumerateRaw — returns raw HID device list for init() and enumerateDevices()
// ---------------------------------------------------------------------------

struct RawDevEntry {
    HANDLE raw_handle;
    std::string path;
    uint16_t vendor_id;
    uint16_t product_id;
};

static std::vector<RawDevEntry> enumerateRaw() {
    std::vector<RawDevEntry> out;
    UINT count = 0;
    GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST));
    if (count == 0) return out;
    std::vector<RAWINPUTDEVICELIST> list(count);
    if (GetRawInputDeviceList(list.data(), &count, sizeof(RAWINPUTDEVICELIST)) == (UINT)-1) return out;

    for (auto& e : list) {
        if (e.dwType != RIM_TYPEHID) continue;

        // Path
        UINT nameLen = 0;
        GetRawInputDeviceInfoA(e.hDevice, RIDI_DEVICENAME, nullptr, &nameLen);
        if (!nameLen) continue;
        std::string path(nameLen, '\0');
        GetRawInputDeviceInfoA(e.hDevice, RIDI_DEVICENAME, &path[0], &nameLen);
        if (!path.empty() && path.back() == '\0') path.pop_back();

        // VID/PID
        RID_DEVICE_INFO info = {};
        info.cbSize = sizeof(info);
        UINT sz = sizeof(info);
        if (GetRawInputDeviceInfoA(e.hDevice, RIDI_DEVICEINFO, &info, &sz) == (UINT)-1) continue;

        RawDevEntry re;
        re.raw_handle = e.hDevice;
        re.path       = path;
        re.vendor_id  = (uint16_t)info.hid.dwVendorId;
        re.product_id = (uint16_t)info.hid.dwProductId;
        out.push_back(re);
    }
    return out;
}

// ---------------------------------------------------------------------------
// buildDeviceInfo — build DeviceInfo from a raw entry (no CreateFile for data)
// ---------------------------------------------------------------------------

static DeviceInfo buildDeviceInfo(const RawDevEntry& re, uint8_t instance) {
    DeviceInfo dev;
    dev.raw_handle = re.raw_handle;
    dev.vendor_id  = re.vendor_id;
    dev.product_id = re.product_id;
    dev.instance   = instance;
    dev.path       = re.path;

    // Preparsed data via RIDI_PREPARSEDDATA — no CreateFile needed
    UINT ppSize = 0;
    if (GetRawInputDeviceInfoA(re.raw_handle, RIDI_PREPARSEDDATA, nullptr, &ppSize) == (UINT)-1 || !ppSize)
        return dev;
    dev.preparsed = (PHIDP_PREPARSED_DATA)LocalAlloc(LMEM_FIXED, ppSize);
    if (!dev.preparsed) return dev;
    if (GetRawInputDeviceInfoA(re.raw_handle, RIDI_PREPARSEDDATA, dev.preparsed, &ppSize) == (UINT)-1) {
        LocalFree(dev.preparsed); dev.preparsed = nullptr; return dev;
    }

    if (HidP_GetCaps(dev.preparsed, &dev.caps) != HIDP_STATUS_SUCCESS) {
        dev.destroy(); return dev;
    }

    // Button caps
    if (dev.caps.NumberInputButtonCaps > 0) {
        dev.button_caps.resize(dev.caps.NumberInputButtonCaps);
        USHORT n = dev.caps.NumberInputButtonCaps;
        HidP_GetButtonCaps(HidP_Input, dev.button_caps.data(), &n, dev.preparsed);
        // Normalize non-range entries
        for (auto& bc : dev.button_caps) {
            if (!bc.IsRange) {
                bc.Range.UsageMin = bc.NotRange.Usage;
                bc.Range.UsageMax = bc.NotRange.Usage;
            }
        }
        // Allocate flat button_states
        int total = 0;
        for (auto& bc : dev.button_caps) total += (int)(bc.Range.UsageMax - bc.Range.UsageMin + 1);
        dev.button_states.assign((size_t)total, false);
    }

    // Value caps
    if (dev.caps.NumberInputValueCaps > 0) {
        dev.value_caps.resize(dev.caps.NumberInputValueCaps);
        USHORT n = dev.caps.NumberInputValueCaps;
        HidP_GetValueCaps(HidP_Input, dev.value_caps.data(), &n, dev.preparsed);
        dev.axis_states.assign(dev.value_caps.size(), 0.5f);
    }

    // Manufacturer + product strings: try CreateFile GENERIC_READ only; ignore failure
    HANDLE h = CreateFileA(re.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        wchar_t buf[256] = {};
        if (HidD_GetManufacturerString(h, buf, sizeof(buf))) {
            int len = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                std::string s(len, '\0');
                WideCharToMultiByte(CP_UTF8, 0, buf, -1, &s[0], len, nullptr, nullptr);
                if (!s.empty() && s.back() == '\0') s.pop_back();
                dev.manufacturer = s;
            }
        }
        wchar_t pbuf[256] = {};
        if (HidD_GetProductString(h, pbuf, sizeof(pbuf))) {
            int len = WideCharToMultiByte(CP_UTF8, 0, pbuf, -1, nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                std::string s(len, '\0');
                WideCharToMultiByte(CP_UTF8, 0, pbuf, -1, &s[0], len, nullptr, nullptr);
                if (!s.empty() && s.back() == '\0') s.pop_back();
                dev.product = s;
            }
        }
        CloseHandle(h);
    }

    return dev;
}

// ---------------------------------------------------------------------------
// updateActionStates — called from WM_INPUT handler after button/axis update
// ---------------------------------------------------------------------------

static void updateActionStates(DeviceInfo& dev) {
    // Buttons
    for (auto& kv : g_buttonBindings) {
        const ButtonBinding& b = kv.second;
        if (b.device.vendor_id  != dev.vendor_id)  continue;
        if (b.device.product_id != dev.product_id) continue;
        if (b.device.instance   != dev.instance)   continue;
        int idx = dev.findButtonIndex(b.usage_page, b.usage_id);
        bool pressed = (idx >= 0 && idx < (int)dev.button_states.size())
                       ? dev.button_states[(size_t)idx] : false;
        EnterCriticalSection(&g_stateLock);
        g_buttonState[kv.first] = pressed;
        LeaveCriticalSection(&g_stateLock);
    }

    // Axes
    for (auto& kv : g_analogBindings) {
        const AnalogBinding& ab = kv.second;
        if (ab.device.vendor_id  != dev.vendor_id)  continue;
        if (ab.device.product_id != dev.product_id) continue;
        if (ab.device.instance   != dev.instance)   continue;

        int port = -1;
        if (kv.first == "P1 Turntable") port = 0;
        else if (kv.first == "P2 Turntable") port = 1;
        if (port < 0) continue;

        int idx = dev.findAxisIndex(ab.usage_page, ab.usage_id);
        if (idx < 0 || idx >= (int)dev.axis_states.size()) continue;

        float fval = dev.axis_states[(size_t)idx];  // [0,1]
        if (ab.reverse) fval = 1.0f - fval;

        // Apply sensitivity (scale deviation from center)
        if (ab.sensitivity != 1.0f) {
            float dev_v = (fval - 0.5f) * ab.sensitivity;
            fval = 0.5f + dev_v;
            if (fval < 0.0f) fval = 0.0f;
            if (fval > 1.0f) fval = 1.0f;
        }

        uint8_t scaled = (uint8_t)(fval * 255.0f);

        // Dead zone: snap to center if within dead_zone of 128
        if (ab.dead_zone > 0) {
            int dist = (int)scaled - 128;
            if (dist < 0) dist = -dist;
            if (dist < (int)ab.dead_zone) scaled = 128;
        }

        g_analog.pos[(size_t)port] = scaled + g_analog.vttDelta[(size_t)port];
    }
}

// ---------------------------------------------------------------------------
// WM_INPUT handler
// ---------------------------------------------------------------------------

static void handleWmInput(LPARAM lParam) {
    // Get size
    UINT dataSize = 0;
    if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &dataSize,
                        sizeof(RAWINPUTHEADER)) == (UINT)-1) return;
    if (!dataSize) return;

    std::vector<uint8_t> buf(dataSize);
    if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buf.data(), &dataSize,
                        sizeof(RAWINPUTHEADER)) != dataSize) return;

    RAWINPUT* raw = (RAWINPUT*)buf.data();
    if (raw->header.dwType != RIM_TYPEHID) return;

    HANDLE devHandle = raw->header.hDevice;
    PCHAR  reportBuf = (PCHAR)raw->data.hid.bRawData;
    DWORD  reportSz  = raw->data.hid.dwSizeHid;

    // Find matching DeviceInfo
    for (auto& dev : g_devices) {
        if (dev.raw_handle != devHandle) continue;
        if (!dev.preparsed) continue;

        // Parse button states
        int offset = 0;
        for (const auto& bc : dev.button_caps) {
            int count = (int)(bc.Range.UsageMax - bc.Range.UsageMin + 1);
            // Zero this cap's slice first
            for (int i = offset; i < offset + count && i < (int)dev.button_states.size(); i++)
                dev.button_states[(size_t)i] = false;

            std::vector<USAGE> usages((size_t)count);
            ULONG usageCount = (ULONG)count;
            NTSTATUS st = HidP_GetUsages(HidP_Input, bc.UsagePage, 0,
                                         usages.data(), &usageCount,
                                         dev.preparsed, reportBuf, reportSz);
            if (st == HIDP_STATUS_SUCCESS) {
                for (ULONG u = 0; u < usageCount; u++) {
                    int bi = offset + (int)(usages[u] - bc.Range.UsageMin);
                    if (bi >= 0 && bi < (int)dev.button_states.size())
                        dev.button_states[(size_t)bi] = true;
                }
            }
            offset += count;
        }

        // Parse axis states
        for (int i = 0; i < (int)dev.value_caps.size(); i++) {
            const auto& vc = dev.value_caps[(size_t)i];
            USAGE usage = vc.IsRange ? vc.Range.UsageMin : vc.NotRange.Usage;
            ULONG rawVal = 0;
            NTSTATUS st = HidP_GetUsageValue(HidP_Input, vc.UsagePage, 0, usage,
                                             &rawVal, dev.preparsed, reportBuf, reportSz);
            if (st == HIDP_STATUS_SUCCESS) {
                LONG logMin = vc.LogicalMin;
                LONG logMax = vc.LogicalMax;
                float fval = 0.5f;
                if (logMax != logMin) {
                    float f = (float)((LONG)rawVal - logMin) / (float)(logMax - logMin);
                    if (f < 0.0f) f = 0.0f;
                    if (f > 1.0f) f = 1.0f;
                    fval = f;
                }
                dev.axis_states[(size_t)i] = fval;
            }
        }

        // Update action states (button + analog)
        updateActionStates(dev);

        // Capture mode: edge-detect newly pressed buttons
        if (g_listenMode) {
            uint32_t key = ((uint32_t)dev.vendor_id) |
                           ((uint32_t)dev.product_id << 16);
            auto& prev = g_prevButtonStates[key];
            if (prev.size() != dev.button_states.size())
                prev.assign(dev.button_states.size(), false);

            int btnOffset = 0;
            for (const auto& bc : dev.button_caps) {
                int count = (int)(bc.Range.UsageMax - bc.Range.UsageMin + 1);
                for (int bi = btnOffset; bi < btnOffset + count; bi++) {
                    if ((size_t)bi < dev.button_states.size() &&
                        dev.button_states[(size_t)bi] && !prev[(size_t)bi]) {
                        // Newly pressed
                        EnterCriticalSection(&g_captureLock);
                        if (!g_captureResult.has_value()) {
                            Input::ButtonCaptureResult r;
                            r.vendor_id   = dev.vendor_id;
                            r.product_id  = dev.product_id;
                            r.instance    = dev.instance;
                            r.usage_page  = bc.UsagePage;
                            r.usage_id    = (uint16_t)(bc.Range.UsageMin + (bi - btnOffset));
                            r.device_name = dev.product.empty() ? dev.manufacturer : dev.product;
                            g_captureResult = r;
                        }
                        LeaveCriticalSection(&g_captureLock);
                    }
                }
                btnOffset += count;
            }
            prev = dev.button_states;
        }

        break;
    }
}

// ---------------------------------------------------------------------------
// Window proc + message pump
// ---------------------------------------------------------------------------

static LRESULT CALLBACK InputWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INPUT) {
        handleWmInput(lp);
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static DWORD WINAPI msgPumpThread(void*) {
    // Register window class
    WNDCLASSEXA wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = InputWndProc;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.lpszClassName = "EZ2ConfigInput";
    RegisterClassExA(&wc);

    // Create message-only window
    g_inputHwnd = CreateWindowExA(0, "EZ2ConfigInput", nullptr, 0,
                                  0, 0, 0, 0,
                                  HWND_MESSAGE, nullptr,
                                  GetModuleHandleA(nullptr), nullptr);

    if (g_inputHwnd) {
        // Register for all Generic Desktop HID devices (joystick, gamepad, etc.)
        // RIDEV_PAGEONLY with usUsage=0 catches all usages in page 0x01.
        // RIDEV_INPUTSINK receives input even when the window is not focused.
        RAWINPUTDEVICE rid = {};
        rid.usUsagePage = 0x01;
        rid.usUsage     = 0x00;
        rid.dwFlags     = RIDEV_PAGEONLY | RIDEV_INPUTSINK;
        rid.hwndTarget  = g_inputHwnd;
        RegisterRawInputDevices(&rid, 1, sizeof(rid));
    }

    MSG msg;
    while (g_running && GetMessageA(&msg, g_inputHwnd, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    if (g_inputHwnd) {
        DestroyWindow(g_inputHwnd);
        g_inputHwnd = nullptr;
    }
    UnregisterClassA("EZ2ConfigInput", GetModuleHandleA(nullptr));
    return 0;
}

// ---------------------------------------------------------------------------
// VTT thread
// ---------------------------------------------------------------------------

static DWORD WINAPI vttThread(void*) {
    timeBeginPeriod(1);
    while (g_running) {
        for (int port = 0; port < 2; port++) {
            const VTTBinding& b = g_vttBindings[(size_t)port];
            if (b.plus_vk == 0 && b.minus_vk == 0) continue;
            if (GetAsyncKeyState((int)b.plus_vk)  & 0x8000)
                _InterlockedExchangeAdd8((volatile char*)&g_analog.vttDelta[port],  (char)b.step);
            if (GetAsyncKeyState((int)b.minus_vk) & 0x8000)
                _InterlockedExchangeAdd8((volatile char*)&g_analog.vttDelta[port], -(char)b.step);
        }
        // Keyboard bindings
        for (auto& kv : g_keyBindings) {
            bool pressed = (GetAsyncKeyState((int)kv.second.vk_code) & 0x8000) != 0;
            EnterCriticalSection(&g_stateLock);
            g_buttonState[kv.first] = pressed;
            LeaveCriticalSection(&g_stateLock);
        }
        Sleep(5);
    }
    timeEndPeriod(1);
    return 0;
}

// ---------------------------------------------------------------------------
// Public API — enumerateDevices (for UI)
// ---------------------------------------------------------------------------

std::vector<Input::DeviceDesc> Input::enumerateDevices() {
    auto rawList = enumerateRaw();
    std::map<uint32_t, uint8_t> instanceCount;
    std::vector<DeviceDesc> out;

    for (auto& re : rawList) {
        uint32_t key = (uint32_t)re.vendor_id | ((uint32_t)re.product_id << 16);
        uint8_t inst = instanceCount[key]++;

        DeviceInfo tmp = buildDeviceInfo(re, inst);
        if (!tmp.isValid()) { tmp.destroy(); continue; }

        DeviceDesc desc;
        desc.vendor_id    = tmp.vendor_id;
        desc.product_id   = tmp.product_id;
        desc.instance     = tmp.instance;
        desc.manufacturer = tmp.manufacturer;
        desc.product      = tmp.product;
        desc.path         = tmp.path;

        // Pre-build axis labels from value_caps (no CreateFile needed)
        for (const auto& vc : tmp.value_caps) {
            uint16_t page = vc.UsagePage;
            uint16_t id   = vc.IsRange ? vc.Range.UsageMin : vc.NotRange.Usage;
            desc.axis_usages.push_back({page, id});

            // Use HID usage name if available
            std::string label;
            if (page == 0x01) {
                switch (id) {
                    case 0x30: label = "X Axis"; break;
                    case 0x31: label = "Y Axis"; break;
                    case 0x32: label = "Z Axis"; break;
                    case 0x33: label = "Rx"; break;
                    case 0x34: label = "Ry"; break;
                    case 0x35: label = "Rz"; break;
                    case 0x36: label = "Slider"; break;
                    case 0x37: label = "Dial"; break;
                    case 0x38: label = "Wheel"; break;
                    case 0x39: label = "Hat Switch"; break;
                    default:   label = "Axis 0x" + std::to_string(id); break;
                }
            } else {
                label = "Page 0x" + std::to_string(page) + " Usage 0x" + std::to_string(id);
            }
            desc.axis_labels.push_back(label);
        }

        tmp.destroy();
        out.push_back(std::move(desc));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Input::init
// ---------------------------------------------------------------------------

void Input::init(SettingsManager& settings) {
    InitializeCriticalSection(&g_stateLock);
    InitializeCriticalSection(&g_captureLock);

    // Parse bindings
    try {
        if (settings.globalSettings().contains("button_bindings")) {
            for (auto it = settings.globalSettings()["button_bindings"].begin();
                 it != settings.globalSettings()["button_bindings"].end(); ++it) {
                const std::string& action = it.key();
                const json& b = it.value();
                if (!b.contains("type")) continue;
                std::string btype = b["type"].get<std::string>();
                if (btype == "HidButton") {
                    ButtonBinding bb;
                    bb.device.vendor_id  = (uint16_t)b.value("vendor_id",  0);
                    bb.device.product_id = (uint16_t)b.value("product_id", 0);
                    bb.device.instance   = (uint8_t)b.value("instance",    0);
                    bb.usage_page        = (uint16_t)b.value("usage_page", 0);
                    bb.usage_id          = (uint16_t)b.value("usage_id",   0);
                    g_buttonBindings[action] = bb;
                    EnterCriticalSection(&g_stateLock);
                    g_buttonState[action] = false;
                    LeaveCriticalSection(&g_stateLock);
                } else if (btype == "Keyboard") {
                    KeyboardBinding kb;
                    kb.vk_code = b.value("vk_code", (uint32_t)0);
                    g_keyBindings[action] = kb;
                    EnterCriticalSection(&g_stateLock);
                    g_buttonState[action] = false;
                    LeaveCriticalSection(&g_stateLock);
                }
            }
        }

        if (settings.globalSettings().contains("analog_bindings")) {
            for (auto it = settings.globalSettings()["analog_bindings"].begin();
                 it != settings.globalSettings()["analog_bindings"].end(); ++it) {
                const std::string& action = it.key();
                const json& a = it.value();
                int port = -1;
                if (action == "P1 Turntable") port = 0;
                else if (action == "P2 Turntable") port = 1;

                if (a.contains("axis")) {
                    const json& ax = a["axis"];
                    AnalogBinding ab;
                    ab.device.vendor_id  = (uint16_t)ax.value("vendor_id",  0);
                    ab.device.product_id = (uint16_t)ax.value("product_id", 0);
                    ab.device.instance   = (uint8_t)ax.value("instance",    0);
                    ab.usage_page        = (uint16_t)ax.value("usage_page", 0);
                    ab.usage_id          = (uint16_t)ax.value("usage_id",   0);
                    ab.reverse           = ax.value("reverse",     false);
                    ab.sensitivity       = ax.value("sensitivity", 1.0f);
                    ab.dead_zone         = (uint8_t)ax.value("dead_zone",   0);
                    g_analogBindings[action] = ab;
                }
                if (a.contains("vtt") && port >= 0) {
                    const json& vt = a["vtt"];
                    VTTBinding vb;
                    vb.plus_vk  = vt.value("plus_vk",  (uint32_t)0);
                    vb.minus_vk = vt.value("minus_vk", (uint32_t)0);
                    vb.step     = (uint8_t)vt.value("step", 3);
                    g_vttBindings[(size_t)port] = vb;
                }
            }
        }
    } catch (...) {}

    // Enumerate and build all HID devices (no CreateFile for data reading)
    auto rawList = enumerateRaw();
    std::map<uint32_t, uint8_t> instanceCount;
    for (auto& re : rawList) {
        uint32_t key = (uint32_t)re.vendor_id | ((uint32_t)re.product_id << 16);
        uint8_t inst = instanceCount[key]++;
        DeviceInfo dev = buildDeviceInfo(re, inst);
        if (dev.isValid()) g_devices.push_back(std::move(dev));
        else dev.destroy();
    }

    g_running = true;

    DWORD tid = 0;
    g_msgThread = CreateThread(nullptr, 0, msgPumpThread, nullptr, 0, &tid);
    g_vttThread = CreateThread(nullptr, 0, vttThread,    nullptr, 0, &tid);
}

// ---------------------------------------------------------------------------
// Input::shutdown
// ---------------------------------------------------------------------------

void Input::shutdown() {
    g_running = false;

    // Stop message pump
    if (g_inputHwnd)
        PostMessageA(g_inputHwnd, WM_QUIT, 0, 0);
    if (g_msgThread) {
        WaitForSingleObject(g_msgThread, 2000);
        CloseHandle(g_msgThread);
        g_msgThread = nullptr;
    }

    if (g_vttThread) {
        WaitForSingleObject(g_vttThread, 500);
        CloseHandle(g_vttThread);
        g_vttThread = nullptr;
    }

    for (auto& dev : g_devices) dev.destroy();
    g_devices.clear();
    g_buttonBindings.clear();
    g_keyBindings.clear();
    g_analogBindings.clear();

    EnterCriticalSection(&g_stateLock);
    g_buttonState.clear();
    LeaveCriticalSection(&g_stateLock);
    g_prevButtonStates.clear();

    DeleteCriticalSection(&g_stateLock);
    DeleteCriticalSection(&g_captureLock);
}

// ---------------------------------------------------------------------------
// Public read API
// ---------------------------------------------------------------------------

bool Input::getButtonState(const std::string& gameAction) {
    EnterCriticalSection(&g_stateLock);
    auto it = g_buttonState.find(gameAction);
    bool r = (it != g_buttonState.end()) ? it->second : false;
    LeaveCriticalSection(&g_stateLock);
    return r;
}

uint8_t Input::getAnalogValue(const std::string& gameAction) {
    int port = -1;
    if (gameAction == "P1 Turntable") port = 0;
    else if (gameAction == "P2 Turntable") port = 1;
    if (port < 0) return 128;
    return g_analog.pos[(size_t)port];
}

void Input::setListenMode(bool enabled) {
    g_listenMode = enabled;
    if (!enabled) {
        EnterCriticalSection(&g_captureLock);
        g_captureResult.reset();
        LeaveCriticalSection(&g_captureLock);
        g_prevButtonStates.clear();
    }
}

std::optional<Input::ButtonCaptureResult> Input::pollNextButtonPress() {
    EnterCriticalSection(&g_captureLock);
    auto r = g_captureResult;
    g_captureResult.reset();
    LeaveCriticalSection(&g_captureLock);
    return r;
}
