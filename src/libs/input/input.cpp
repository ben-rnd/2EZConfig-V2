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
#include <string>
#include <vector>
#include <map>
#include <array>
#include <optional>
#include <cstdint>
#include <cstdio>

// ---------------------------------------------------------------------------
// Atomic 8-bit add — MinGW shim
// ---------------------------------------------------------------------------
#if defined(__GNUC__) && !defined(_InterlockedExchangeAdd8)
static inline char _InterlockedExchangeAdd8(volatile char* ptr, char val) {
    return __sync_fetch_and_add(ptr, val);
}
#endif

// ---------------------------------------------------------------------------
// Helper: build the opaque device_id string from VID/PID/instance
// ---------------------------------------------------------------------------
static std::string makeDeviceId(uint16_t vendor_id, uint16_t product_id, uint8_t instance) {
    char buf[32];
    snprintf(buf, sizeof(buf), "VID_%04X&PID_%04X&Instance_%u",
             (unsigned)vendor_id, (unsigned)product_id, (unsigned)instance);
    return buf;
}

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static volatile bool g_running     = false;
static volatile bool g_captureMode = false;

static HANDLE g_msgThread = nullptr;
static HANDLE g_vttThread  = nullptr;
static HWND   g_inputHwnd  = nullptr;

static std::vector<DeviceInfo> g_devices;

// State lock: protects button/axis state reads from WM_INPUT handler and g_devices
static CRITICAL_SECTION g_stateLock;

// Capture result
static std::optional<Input::CaptureResult> g_captureResult;
static CRITICAL_SECTION                    g_captureLock;

// Previous button state per device for edge detection in capture mode.
// GUARD 2: keyed by full device_id string so identical VID/PID controllers
// (different instance) each have independent edge-detection state.
static std::map<std::string, std::vector<bool>> g_prevButtonStates;

// VTT bindings (configurable via setVttKeys)
static std::array<VTTBinding, 2> g_vttBindings = {};

// VTT accumulated delta [0..255] per port; volatile for lock-free read
struct VttState { volatile uint8_t pos[2]; };
static VttState g_vtt = {};

// ---------------------------------------------------------------------------
// enumerateRaw — returns raw HID device list
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
// buildDeviceInfo — build DeviceInfo from a raw entry
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
        // Normalize non-range entries to Range form for consistent iteration
        for (auto& bc : dev.button_caps) {
            if (!bc.IsRange) {
                bc.Range.UsageMin = bc.NotRange.Usage;
                bc.Range.UsageMax = bc.NotRange.Usage;
            }
        }
        // Allocate flat button_states with same traversal order as handleWmInput and getDevices
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
// WM_INPUT handler
// ---------------------------------------------------------------------------

static void handleWmInput(LPARAM lParam) {
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

        // Parse button states — GUARD 1: iterate button_caps in the same order as getDevices()
        // so flat_idx in button_labels[] matches button_states[] at the same index.
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

        // Capture mode: edge-detect newly pressed buttons
        // GUARD 1: Use flat index (btnOffset + bi) — same arithmetic as button_labels[] in getDevices()
        // GUARD 2: Key by full device_id string to avoid instance collision
        if (g_captureMode) {
            std::string key = makeDeviceId(dev.vendor_id, dev.product_id, dev.instance);
            auto& prev = g_prevButtonStates[key];
            if (prev.size() != dev.button_states.size())
                prev.assign(dev.button_states.size(), false);

            int btnOffset = 0;
            for (const auto& bc : dev.button_caps) {
                int count = (int)(bc.Range.UsageMax - bc.Range.UsageMin + 1);
                for (int bi = btnOffset; bi < btnOffset + count; bi++) {
                    if ((size_t)bi < dev.button_states.size() &&
                        dev.button_states[(size_t)bi] && !prev[(size_t)bi]) {
                        EnterCriticalSection(&g_captureLock);
                        if (!g_captureResult.has_value()) {
                            Input::CaptureResult r;
                            r.device_id   = makeDeviceId(dev.vendor_id, dev.product_id, dev.instance);
                            r.button_idx  = bi;  // flat index — matches button_labels[bi]
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
    WNDCLASSEXA wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = InputWndProc;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.lpszClassName = "EZ2ConfigInput";
    RegisterClassExA(&wc);

    g_inputHwnd = CreateWindowExA(0, "EZ2ConfigInput", nullptr, 0,
                                  0, 0, 0, 0,
                                  HWND_MESSAGE, nullptr,
                                  GetModuleHandleA(nullptr), nullptr);

    if (g_inputHwnd) {
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
                _InterlockedExchangeAdd8((volatile char*)&g_vtt.pos[port],  (char)b.step);
            if (GetAsyncKeyState((int)b.minus_vk) & 0x8000)
                _InterlockedExchangeAdd8((volatile char*)&g_vtt.pos[port], -(char)b.step);
        }
        Sleep(5);
    }
    timeEndPeriod(1);
    return 0;
}

// ---------------------------------------------------------------------------
// Public API — getDevices (for UI)
// ---------------------------------------------------------------------------

std::vector<Input::DeviceDesc> Input::getDevices() {
    auto rawList = enumerateRaw();
    std::map<uint32_t, uint8_t> instanceCount;
    std::vector<DeviceDesc> out;

    for (auto& re : rawList) {
        uint32_t key = (uint32_t)re.vendor_id | ((uint32_t)re.product_id << 16);
        uint8_t inst = instanceCount[key]++;

        DeviceInfo tmp = buildDeviceInfo(re, inst);
        if (!tmp.isValid()) { tmp.destroy(); continue; }

        DeviceDesc desc;
        desc.id   = makeDeviceId(tmp.vendor_id, tmp.product_id, tmp.instance);
        // Device name: prefer product string, fall back to "VID_XXXX:PID_XXXX"
        if (!tmp.product.empty()) {
            desc.name = tmp.product;
        } else if (!tmp.manufacturer.empty()) {
            desc.name = tmp.manufacturer;
        } else {
            char nbuf[24];
            snprintf(nbuf, sizeof(nbuf), "VID_%04X:PID_%04X",
                     (unsigned)tmp.vendor_id, (unsigned)tmp.product_id);
            desc.name = nbuf;
        }

        // GUARD 1: Build button_labels with the SAME flat-index iteration order used in
        // buildDeviceInfo (button_caps traversal) and handleWmInput (button_states update).
        // flat_idx == button_labels[flat_idx] index == button_states[flat_idx] index.
        {
            int flat_idx = 0;
            for (const auto& bc : tmp.button_caps) {
                int count = (int)(bc.Range.UsageMax - bc.Range.UsageMin + 1);
                for (int i = 0; i < count; i++) {
                    desc.button_labels.push_back("Button " + std::to_string(flat_idx + 1));
                    flat_idx++;
                }
            }
        }

        // Build axis_labels from value_caps
        for (const auto& vc : tmp.value_caps) {
            uint16_t page = vc.UsagePage;
            uint16_t id   = vc.IsRange ? vc.Range.UsageMin : vc.NotRange.Usage;
            std::string label;
            if (page == 0x01) {
                switch (id) {
                    case 0x30: label = "X Axis";     break;
                    case 0x31: label = "Y Axis";     break;
                    case 0x32: label = "Z Axis";     break;
                    case 0x33: label = "Rx";         break;
                    case 0x34: label = "Ry";         break;
                    case 0x35: label = "Rz";         break;
                    case 0x36: label = "Slider";     break;
                    case 0x37: label = "Dial";       break;
                    case 0x38: label = "Wheel";      break;
                    case 0x39: label = "Hat Switch"; break;
                    default:
                        label = "Axis 0x";
                        char hex[8];
                        snprintf(hex, sizeof(hex), "%X", (unsigned)id);
                        label += hex;
                        break;
                }
            } else {
                char lbuf[32];
                snprintf(lbuf, sizeof(lbuf), "Page 0x%X Usage 0x%X", (unsigned)page, (unsigned)id);
                label = lbuf;
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

void Input::init() {
    InitializeCriticalSection(&g_stateLock);
    InitializeCriticalSection(&g_captureLock);

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

    EnterCriticalSection(&g_stateLock);
    for (auto& dev : g_devices) dev.destroy();
    g_devices.clear();
    LeaveCriticalSection(&g_stateLock);

    g_prevButtonStates.clear();

    DeleteCriticalSection(&g_stateLock);
    DeleteCriticalSection(&g_captureLock);
}

// ---------------------------------------------------------------------------
// Public read API
// ---------------------------------------------------------------------------

bool Input::getButtonState(const std::string& device_id, int button_idx) {
    EnterCriticalSection(&g_stateLock);
    bool result = false;
    for (const auto& dev : g_devices) {
        if (makeDeviceId(dev.vendor_id, dev.product_id, dev.instance) == device_id) {
            if (button_idx >= 0 && button_idx < (int)dev.button_states.size())
                result = dev.button_states[(size_t)button_idx];
            break;
        }
    }
    LeaveCriticalSection(&g_stateLock);
    return result;
}

float Input::getAxisValue(const std::string& device_id, int axis_idx) {
    EnterCriticalSection(&g_stateLock);
    float result = 0.5f;  // center if not found
    for (const auto& dev : g_devices) {
        if (makeDeviceId(dev.vendor_id, dev.product_id, dev.instance) == device_id) {
            if (axis_idx >= 0 && axis_idx < (int)dev.axis_states.size())
                result = dev.axis_states[(size_t)axis_idx];
            break;
        }
    }
    LeaveCriticalSection(&g_stateLock);
    return result;
}

void Input::setVttKeys(int port, int plus_vk, int minus_vk, int step) {
    if (port < 0 || port >= 2) return;
    g_vttBindings[(size_t)port].plus_vk  = (uint32_t)plus_vk;
    g_vttBindings[(size_t)port].minus_vk = (uint32_t)minus_vk;
    g_vttBindings[(size_t)port].step     = (uint8_t)step;
}

uint8_t Input::getVttPosition(int port) {
    if (port < 0 || port >= 2) return 128;
    return g_vtt.pos[(size_t)port];
}

void Input::startCapture() {
    g_captureMode = true;
}

void Input::stopCapture() {
    g_captureMode = false;
    EnterCriticalSection(&g_captureLock);
    g_captureResult.reset();
    LeaveCriticalSection(&g_captureLock);
    g_prevButtonStates.clear();
}

std::optional<Input::CaptureResult> Input::pollCapture() {
    EnterCriticalSection(&g_captureLock);
    auto r = g_captureResult;
    g_captureResult.reset();
    LeaveCriticalSection(&g_captureLock);
    return r;
}
