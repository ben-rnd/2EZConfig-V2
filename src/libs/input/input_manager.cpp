// MinGW shim: _InterlockedExchangeAdd8 not declared in MinGW intrin.h
// Maps to identical lock-xadd instruction on x86/x86-64.
#if defined(__GNUC__) && !defined(_InterlockedExchangeAdd8)
static inline char _InterlockedExchangeAdd8(volatile char* ptr, char val) {
    return __sync_fetch_and_add(ptr, val);
}
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}
#include <setupapi.h>
#include <mmsystem.h>

#include "input_manager.h"

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <sstream>
#include <iomanip>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string wide_to_utf8(const wchar_t* src) {
    if (!src || src[0] == L'\0') return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, src, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, src, -1, &result[0], len, nullptr, nullptr);
    return result;
}

// Extract "VID_XXXX" and "PID_XXXX" from device path and format as "VID_XXXX:PID_XXXX".
static std::string vid_pid_from_path(const std::string& path) {
    std::string upper = path;
    for (auto& c : upper) c = (char)toupper((unsigned char)c);
    auto vid_pos = upper.find("VID_");
    auto pid_pos = upper.find("PID_");
    if (vid_pos == std::string::npos || pid_pos == std::string::npos) return "Unknown Device";
    std::string vid = path.substr(vid_pos, 8); // "VID_XXXX"
    std::string pid = path.substr(pid_pos, 8); // "PID_XXXX"
    return vid + ":" + pid;
}

static std::string axis_label(USAGE usage_page, USAGE usage) {
    if (usage_page == 0x01) {
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
        }
        std::ostringstream ss;
        ss << "Axis 0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << usage;
        return ss.str();
    }
    std::ostringstream ss;
    ss << "Page 0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << usage_page
       << " Usage 0x" << std::setw(2) << std::setfill('0') << usage;
    return ss.str();
}

// DPad direction names indexed 0-7 (0=Up, clockwise).
static const char* DPAD_NAMES[8] = {
    "DPad Up",
    "DPad Up-Right",
    "DPad Right",
    "DPad Down-Right",
    "DPad Down",
    "DPad Down-Left",
    "DPad Left",
    "DPad Up-Left"
};

// ---------------------------------------------------------------------------
// VTT binding
// ---------------------------------------------------------------------------

struct VttBinding {
    int plus_vk  = 0;
    int minus_vk = 0;
    int step     = 3;
};

// ---------------------------------------------------------------------------
// Impl struct
// ---------------------------------------------------------------------------

struct InputManagerImpl {
    // Device list (written once at startup, read from multiple threads under lock).
    std::vector<Device> devices;
    CRITICAL_SECTION    devices_lock;

    // Message-pump thread.
    HANDLE       pump_thread  = nullptr;
    HWND         hwnd         = nullptr;
    volatile bool running     = false;

    // VTT
    VttBinding   vtt_bindings[2];
    volatile char vtt_pos[2]  = {(char)128, (char)128};
    HANDLE       vtt_thread   = nullptr;

    // Capture mode.
    bool capture_mode = false;
    std::optional<CaptureResult> capture_result;
    CRITICAL_SECTION capture_lock;

    // Previous button states for edge detection (keyed by device path).
    std::map<std::string, std::vector<bool>> prev_button_states;

    // Output flush thread and its lock.
    CRITICAL_SECTION output_lock;
    HANDLE           output_thread = nullptr;

    InputManagerImpl() {
        InitializeCriticalSection(&devices_lock);
        InitializeCriticalSection(&capture_lock);
        InitializeCriticalSection(&output_lock);
    }
    ~InputManagerImpl() {
        DeleteCriticalSection(&devices_lock);
        DeleteCriticalSection(&capture_lock);
        DeleteCriticalSection(&output_lock);
    }
};

// ---------------------------------------------------------------------------
// Static Impl pointer for WndProc (one instance assumed)
// ---------------------------------------------------------------------------

static InputManagerImpl* g_impl = nullptr;

// ---------------------------------------------------------------------------
// Device enumeration
// ---------------------------------------------------------------------------

static void devices_reload(InputManagerImpl* impl) {
    // Free old preparsed data.
    for (auto& dev : impl->devices) {
        dev.destroy();
    }
    impl->devices.clear();
    impl->prev_button_states.clear();

    UINT device_count = 0;
    if (GetRawInputDeviceList(nullptr, &device_count, sizeof(RAWINPUTDEVICELIST)) == (UINT)-1)
        return;
    if (device_count == 0)
        return;

    std::vector<RAWINPUTDEVICELIST> device_list(device_count);
    if (GetRawInputDeviceList(device_list.data(), &device_count, sizeof(RAWINPUTDEVICELIST)) == (UINT)-1)
        return;

    for (UINT i = 0; i < device_count; i++) {
        const RAWINPUTDEVICELIST& entry = device_list[i];

        // Only HID devices (not keyboard, not mouse).
        if (entry.dwType != RIM_TYPEHID)
            continue;

        // Get device path.
        UINT path_len = 0;
        GetRawInputDeviceInfoA(entry.hDevice, RIDI_DEVICENAME, nullptr, &path_len);
        if (path_len == 0)
            continue;
        std::string path(path_len, '\0');
        if (GetRawInputDeviceInfoA(entry.hDevice, RIDI_DEVICENAME, &path[0], &path_len) == (UINT)-1)
            continue;
        // Strip trailing null if present.
        while (!path.empty() && path.back() == '\0')
            path.pop_back();
        if (path.empty())
            continue;

        // Get preparsed data.
        UINT preparsed_size = 0;
        GetRawInputDeviceInfoA(entry.hDevice, RIDI_PREPARSEDDATA, nullptr, &preparsed_size);
        if (preparsed_size == 0)
            continue;
        PHIDP_PREPARSED_DATA preparsed = (PHIDP_PREPARSED_DATA)LocalAlloc(LMEM_FIXED, preparsed_size);
        if (!preparsed)
            continue;
        if (GetRawInputDeviceInfoA(entry.hDevice, RIDI_PREPARSEDDATA, preparsed, &preparsed_size) == (UINT)-1) {
            LocalFree(preparsed);
            continue;
        }

        // Get HID caps.
        HIDP_CAPS caps = {};
        if (HidP_GetCaps(preparsed, &caps) != HIDP_STATUS_SUCCESS) {
            LocalFree(preparsed);
            continue;
        }

        Device dev;
        dev.path       = path;
        dev.raw_handle = entry.hDevice;
        dev.preparsed  = preparsed;
        dev.caps       = caps;

        // -----------------------------------------------------------------
        // Device name: try CreateFileA for product string.
        // -----------------------------------------------------------------
        {
            HANDLE hid_handle = CreateFileA(
                path.c_str(),
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                OPEN_EXISTING,
                0,
                nullptr);

            bool got_name = false;
            if (hid_handle != INVALID_HANDLE_VALUE) {
                wchar_t wbuf[256] = {};
                if (HidD_GetProductString(hid_handle, wbuf, sizeof(wbuf))) {
                    std::string s = wide_to_utf8(wbuf);
                    if (!s.empty()) {
                        dev.name  = s;
                        got_name  = true;
                    }
                }
                if (!got_name) {
                    wchar_t mbuf[256] = {};
                    if (HidD_GetManufacturerString(hid_handle, mbuf, sizeof(mbuf))) {
                        std::string s = wide_to_utf8(mbuf);
                        if (!s.empty()) {
                            dev.name = s;
                            got_name = true;
                        }
                    }
                }
                CloseHandle(hid_handle);
            }
            if (!got_name) {
                dev.name = vid_pid_from_path(path);
            }
        }

        // -----------------------------------------------------------------
        // Button caps (input).
        // -----------------------------------------------------------------
        {
            USHORT btn_cap_count = caps.NumberInputButtonCaps;
            std::vector<HIDP_BUTTON_CAPS> btn_cap_data(btn_cap_count);
            if (btn_cap_count > 0 &&
                HidP_GetButtonCaps(HidP_Input, btn_cap_data.data(), &btn_cap_count, preparsed) != HIDP_STATUS_SUCCESS)
            {
                LocalFree(preparsed);
                continue;
            }

            for (int cap_num = 0; cap_num < (int)btn_cap_count; cap_num++) {
                auto& bc = btn_cap_data[cap_num];

                // Normalize non-range caps to Range form.
                if (!bc.IsRange) {
                    bc.Range.UsageMin = bc.NotRange.Usage;
                    bc.Range.UsageMax = bc.NotRange.Usage;
                }

                int btn_count = (int)(bc.Range.UsageMax - bc.Range.UsageMin + 1);
                if (btn_count <= 0 || btn_count >= 0xffff)
                    continue;

                // Hat switch (Usage 0x39, Page 0x01) — promote to DPad buttons.
                if (bc.UsagePage == 0x01 && bc.Range.UsageMin == 0x39) {
                    // Skip adding to button_caps_list; record hat offset.
                    // Hat DPad directions will be read via value caps in WM_INPUT,
                    // but the hat is enumerated as a button cap here in some firmware.
                    // Record offset and add 8 DPad button entries.
                    int hat_offset = (int)dev.button_states.size();
                    dev.hat_cap_offsets.push_back(hat_offset);
                    for (int d = 0; d < 8; d++) {
                        dev.button_caps_names.push_back(DPAD_NAMES[d]);
                        dev.button_states.push_back(false);
                    }
                    // Store the button cap for hat handling in WM_INPUT.
                    // We re-use hat_caps_list (value caps also added below if present,
                    // but this handles pure button-cap hats).
                    // Convert to a HIDP_VALUE_CAPS-like entry for uniformity —
                    // we track these via a separate HIDP_BUTTON_CAPS list.
                    // (Hat value caps are handled in the value cap section below.)
                    continue;
                }

                // Regular button cap.
                dev.button_caps_list.push_back(bc);
                for (int b = 0; b < btn_count; b++) {
                    int button_number = (int)dev.button_states.size() + 1;
                    dev.button_caps_names.push_back("Button " + std::to_string(button_number));
                    dev.button_states.push_back(false);
                }
            }
        }

        // -----------------------------------------------------------------
        // Value caps (input).
        // -----------------------------------------------------------------
        {
            USHORT val_cap_count = caps.NumberInputValueCaps;
            std::vector<HIDP_VALUE_CAPS> val_cap_data(val_cap_count);
            if (val_cap_count > 0 &&
                HidP_GetValueCaps(HidP_Input, val_cap_data.data(), &val_cap_count, preparsed) != HIDP_STATUS_SUCCESS)
            {
                LocalFree(preparsed);
                continue;
            }

            for (int cap_num = 0; cap_num < (int)val_cap_count; cap_num++) {
                auto& vc = val_cap_data[cap_num];

                // Normalize non-range caps to Range form.
                if (!vc.IsRange) {
                    vc.Range.UsageMin = vc.NotRange.Usage;
                    vc.Range.UsageMax = vc.NotRange.Usage;
                }

                USAGE usage = vc.Range.UsageMin;

                // Hat switch value cap (page 0x01, usage 0x39) — promote to DPad buttons.
                if (vc.UsagePage == 0x01 && usage == 0x39) {
                    // Apply sign-extension fix to LogicalMin/Max (spice2x lines 597-604).
                    if (vc.BitSize > 0 && vc.BitSize <= (USHORT)(sizeof(vc.LogicalMin) * 8)) {
                        auto shift_size = sizeof(vc.LogicalMin) * 8 - vc.BitSize + 1;
                        auto mask = ((uint64_t)1 << vc.BitSize) - 1;
                        vc.LogicalMin &= (LONG)mask;
                        vc.LogicalMin <<= shift_size;
                        vc.LogicalMin >>= shift_size;
                        vc.LogicalMax &= (LONG)mask;
                    }

                    // Record hat offset and add DPad entries (if not already added from
                    // button caps above for this same hat).
                    // Check if we already have a hat_cap_offsets entry that would have been
                    // added from a button cap above. In practice, the hat shows up in either
                    // button caps OR value caps, not both. So we can safely add here.
                    int hat_offset = (int)dev.button_states.size();
                    dev.hat_cap_offsets.push_back(hat_offset);
                    dev.hat_caps_list.push_back(vc);
                    for (int d = 0; d < 8; d++) {
                        dev.button_caps_names.push_back(DPAD_NAMES[d]);
                        dev.button_states.push_back(false);
                    }
                    continue; // Do NOT add to value_caps_list.
                }

                // Regular value cap — apply sign-extension fix to LogicalMin/Max.
                if (vc.BitSize > 0 && vc.BitSize <= (USHORT)(sizeof(vc.LogicalMin) * 8)) {
                    auto shift_size = sizeof(vc.LogicalMin) * 8 - vc.BitSize + 1;
                    auto mask = ((uint64_t)1 << vc.BitSize) - 1;
                    vc.LogicalMin &= (LONG)mask;
                    vc.LogicalMin <<= shift_size;
                    vc.LogicalMin >>= shift_size;
                    vc.LogicalMax &= (LONG)mask;
                }

                dev.value_caps_list.push_back(vc);
                dev.value_caps_names.push_back(axis_label(vc.UsagePage, usage));
                dev.value_states.push_back(0.5f);
                dev.value_states_raw.push_back(0);
            }
        }

        // -----------------------------------------------------------------
        // Output button caps — store cap structs for flush thread.
        // -----------------------------------------------------------------
        {
            USHORT out_btn_count = caps.NumberOutputButtonCaps;
            std::vector<HIDP_BUTTON_CAPS> out_btn_data(out_btn_count);
            if (out_btn_count > 0 &&
                HidP_GetButtonCaps(HidP_Output, out_btn_data.data(), &out_btn_count, preparsed) == HIDP_STATUS_SUCCESS)
            {
                int out_btn_num = 1;
                for (int cap_num = 0; cap_num < (int)out_btn_count; cap_num++) {
                    auto& bc = out_btn_data[cap_num];
                    if (!bc.IsRange) {
                        bc.Range.UsageMin = bc.NotRange.Usage;
                        bc.Range.UsageMax = bc.NotRange.Usage;
                    }
                    int btn_count = (int)(bc.Range.UsageMax - bc.Range.UsageMin + 1);
                    if (btn_count <= 0 || btn_count >= 0xffff) continue;
                    // Store once per cap (not per button).
                    dev.button_output_caps_list.push_back(bc);
                    for (int b = 0; b < btn_count; b++) {
                        dev.button_output_caps_names.push_back("Button " + std::to_string(out_btn_num++));
                    }
                }
            }
        }

        // -----------------------------------------------------------------
        // Output value caps — store cap structs for flush thread.
        // -----------------------------------------------------------------
        {
            USHORT out_val_count = caps.NumberOutputValueCaps;
            std::vector<HIDP_VALUE_CAPS> out_val_data(out_val_count);
            if (out_val_count > 0 &&
                HidP_GetValueCaps(HidP_Output, out_val_data.data(), &out_val_count, preparsed) == HIDP_STATUS_SUCCESS)
            {
                for (int cap_num = 0; cap_num < (int)out_val_count; cap_num++) {
                    auto& vc = out_val_data[cap_num];
                    USAGE usage = vc.IsRange ? vc.Range.UsageMin : vc.NotRange.Usage;
                    dev.value_output_caps_list.push_back(vc);
                    dev.value_output_caps_names.push_back(axis_label(vc.UsagePage, usage));
                }
            }
        }

        // Size output state arrays and open persistent RW handle.
        dev.button_output_states.assign(dev.button_output_caps_names.size(), false);
        dev.value_output_states.assign(dev.value_output_caps_names.size(), 0.0f);
        dev.output_pending = false;

        // Open persistent GENERIC_READ|GENERIC_WRITE handle for HidD_SetOutputReport.
        // INVALID_HANDLE_VALUE if device denied write access (game controllers) — silently skipped.
        dev.hid_handle = CreateFileA(
            path.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr);
        // INVALID_HANDLE_VALUE is a silent non-output-capable device — no log, no crash.

        impl->devices.push_back(std::move(dev));
    }

    // Initialize prev_button_states for capture edge detection.
    for (const auto& dev : impl->devices) {
        impl->prev_button_states[dev.path] = std::vector<bool>(dev.button_states.size(), false);
    }
}

// ---------------------------------------------------------------------------
// WM_INPUT handler
// ---------------------------------------------------------------------------

static void handle_wm_input(InputManagerImpl* impl, HRAWINPUT hri) {
    UINT data_size = 0;
    if (GetRawInputData(hri, RID_INPUT, nullptr, &data_size, sizeof(RAWINPUTHEADER)) == (UINT)-1)
        return;
    if (data_size == 0)
        return;

    std::vector<BYTE> buf(data_size);
    if (GetRawInputData(hri, RID_INPUT, buf.data(), &data_size, sizeof(RAWINPUTHEADER)) == (UINT)-1)
        return;

    const RAWINPUT* raw = reinterpret_cast<const RAWINPUT*>(buf.data());
    if (raw->header.dwType != RIM_TYPEHID)
        return;

    const RAWHID& hid_data = raw->data.hid;

    EnterCriticalSection(&impl->devices_lock);

    // Find matching device by raw handle.
    Device* dev = nullptr;
    for (auto& d : impl->devices) {
        if (d.raw_handle == raw->header.hDevice) {
            dev = &d;
            break;
        }
    }
    if (!dev || !dev->preparsed) {
        LeaveCriticalSection(&impl->devices_lock);
        return;
    }

    const BYTE*  report_buf  = hid_data.bRawData;
    const UINT   report_size = hid_data.dwSizeHid;
    PHIDP_PREPARSED_DATA preparsed = dev->preparsed;

    // -----------------------------------------------------------------
    // Button states (regular buttons — not hat).
    // -----------------------------------------------------------------
    {
        int state_offset = 0;
        for (size_t cap_i = 0; cap_i < dev->button_caps_list.size(); cap_i++) {
            auto& bc = dev->button_caps_list[cap_i];
            int btn_count = (int)(bc.Range.UsageMax - bc.Range.UsageMin + 1);
            if (btn_count <= 0) {
                continue;
            }

            ULONG usages_len = (ULONG)btn_count;
            std::vector<USAGE> usages(btn_count);
            bool query_ok = (HidP_GetUsages(
                HidP_Input,
                bc.UsagePage,
                bc.LinkCollection,
                usages.data(),
                &usages_len,
                preparsed,
                (PCHAR)report_buf,
                report_size) == HIDP_STATUS_SUCCESS);

            // Zero the slice for this cap, then set pressed ones.
            for (int b = 0; b < btn_count; b++) {
                dev->button_states[state_offset + b] = false;
            }
            if (query_ok) {
                for (ULONG u = 0; u < usages_len; u++) {
                    int idx = (int)(usages[u] - bc.Range.UsageMin);
                    if (idx >= 0 && idx < btn_count) {
                        dev->button_states[state_offset + idx] = true;
                    }
                }
            }

            state_offset += btn_count;
        }
    }

    // -----------------------------------------------------------------
    // Hat switch states (via value caps).
    // -----------------------------------------------------------------
    for (size_t hat_i = 0; hat_i < dev->hat_caps_list.size(); hat_i++) {
        auto& hc = dev->hat_caps_list[hat_i];
        int hat_offset = dev->hat_cap_offsets[hat_i];

        ULONG raw_val = 0;
        NTSTATUS status = HidP_GetUsageValue(
            HidP_Input,
            hc.UsagePage,
            hc.LinkCollection,
            hc.Range.UsageMin,  // 0x39 (hat)
            &raw_val,
            preparsed,
            (PCHAR)report_buf,
            report_size);

        // All 8 directions default to false.
        for (int d = 0; d < 8; d++) {
            dev->button_states[hat_offset + d] = false;
        }

        if (status == HIDP_STATUS_SUCCESS) {
            LONG val = (LONG)raw_val;
            // Apply sign-extension fix.
            if (hc.LogicalMin < 0 && hc.BitSize > 0 && hc.BitSize <= (USHORT)(sizeof(hc.LogicalMin) * 8)) {
                auto shift_size = sizeof(hc.LogicalMin) * 8 - hc.BitSize + 1;
                val <<= shift_size;
                val >>= shift_size;
            }

            LONG lmin = hc.LogicalMin;
            LONG lmax = hc.LogicalMax;
            if (lmin <= val && val <= lmax && lmax > lmin) {
                // Map value to direction index 0-7.
                int dir = (int)((val - lmin) * 8 / (lmax - lmin + 1));
                if (dir >= 0 && dir < 8) {
                    dev->button_states[hat_offset + dir] = true;
                }
            }
            // If out-of-range: neutral — all false (already set).
        }
    }

    // -----------------------------------------------------------------
    // Axis states.
    // -----------------------------------------------------------------
    for (size_t cap_i = 0; cap_i < dev->value_caps_list.size(); cap_i++) {
        auto& vc = dev->value_caps_list[cap_i];

        ULONG raw_ulong = 0;
        NTSTATUS status = HidP_GetUsageValue(
            HidP_Input,
            vc.UsagePage,
            vc.LinkCollection,
            vc.Range.UsageMin,
            &raw_ulong,
            preparsed,
            (PCHAR)report_buf,
            report_size);

        if (status != HIDP_STATUS_SUCCESS)
            continue;

        LONG raw_val = (LONG)raw_ulong;

        // Sign-extension fix (spice2x lines 1791-1797).
        if (vc.LogicalMin < 0 && vc.BitSize > 0 && vc.BitSize <= (USHORT)(sizeof(vc.LogicalMin) * 8)) {
            auto shift_size = sizeof(vc.LogicalMin) * 8 - vc.BitSize + 1;
            raw_val <<= shift_size;
            raw_val >>= shift_size;
        }

        dev->value_states_raw[cap_i] = raw_val;

        LONG lmin = vc.LogicalMin;
        LONG lmax = vc.LogicalMax;
        float normalized;

        if (vc.IsAbsolute) {
            if (lmax > lmin) {
                normalized = (float)(raw_val - lmin) / (float)(lmax - lmin);
            } else {
                normalized = 0.5f;
            }
            // Clamp to [0, 1].
            if (normalized < 0.0f) normalized = 0.0f;
            if (normalized > 1.0f) normalized = 1.0f;
        } else {
            // Relative axis: accumulate and wrap.
            float range = (lmax > lmin) ? (float)(lmax - lmin) : 1.0f;
            float delta = (float)raw_val / range;
            float cur = dev->value_states[cap_i];
            cur += delta;
            cur -= floorf(cur); // wrap to [0, 1)
            normalized = cur;
        }

        dev->value_states[cap_i] = normalized;
    }

    // -----------------------------------------------------------------
    // Capture mode edge detection.
    // -----------------------------------------------------------------
    {
        EnterCriticalSection(&impl->capture_lock);
        bool capturing = impl->capture_mode;
        bool already_captured = impl->capture_result.has_value();
        LeaveCriticalSection(&impl->capture_lock);

        if (capturing && !already_captured) {
            auto& prev = impl->prev_button_states[dev->path];

            // Ensure prev vector is the right size.
            if (prev.size() != dev->button_states.size()) {
                prev.assign(dev->button_states.size(), false);
            }

            for (int i = 0; i < (int)dev->button_states.size(); i++) {
                if (dev->button_states[i] && !prev[i]) {
                    // Edge: button just pressed.
                    CaptureResult cr;
                    cr.path        = dev->path;
                    cr.button_idx  = i;
                    cr.device_name = dev->name;

                    EnterCriticalSection(&impl->capture_lock);
                    if (!impl->capture_result.has_value()) {
                        impl->capture_result = cr;
                    }
                    LeaveCriticalSection(&impl->capture_lock);
                    break;
                }
            }

            prev = dev->button_states;
        }
    }

    LeaveCriticalSection(&impl->devices_lock);
}

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------

static LRESULT CALLBACK EZ2InputWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_INPUT) {
        if (g_impl) {
            handle_wm_input(g_impl, reinterpret_cast<HRAWINPUT>(lParam));
        }
        DefWindowProcA(hwnd, msg, wParam, lParam);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Message pump thread
// ---------------------------------------------------------------------------

static DWORD WINAPI msg_pump_thread(LPVOID param) {
    InputManagerImpl* impl = reinterpret_cast<InputManagerImpl*>(param);

    // Register window class.
    WNDCLASSEXA wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXA);
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.lpfnWndProc   = EZ2InputWndProc;
    wc.lpszClassName = "EZ2InputMgr";
    RegisterClassExA(&wc);

    // Create message-only window.
    HWND hwnd = CreateWindowExA(
        0,
        "EZ2InputMgr",
        nullptr,
        0, 0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        wc.hInstance,
        nullptr);
    impl->hwnd = hwnd;

    if (!hwnd) {
        return 1;
    }

    // Register for all HID input via page-only filter.
    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = 0x01;
    rid.usUsage     = 0x00;
    rid.dwFlags     = RIDEV_PAGEONLY | RIDEV_INPUTSINK;
    rid.hwndTarget  = hwnd;
    RegisterRawInputDevices(&rid, 1, sizeof(RAWINPUTDEVICE));

    // Message loop.
    MSG msg;
    while (impl->running && GetMessageA(&msg, hwnd, 0, 0) > 0) {
        DispatchMessageA(&msg);
    }

    DestroyWindow(hwnd);
    impl->hwnd = nullptr;
    return 0;
}

// ---------------------------------------------------------------------------
// VTT thread
// ---------------------------------------------------------------------------

static DWORD WINAPI vtt_thread(LPVOID param) {
    InputManagerImpl* impl = reinterpret_cast<InputManagerImpl*>(param);
    timeBeginPeriod(1);

    while (impl->running) {
        for (int port = 0; port < 2; port++) {
            int plus_vk  = impl->vtt_bindings[port].plus_vk;
            int minus_vk = impl->vtt_bindings[port].minus_vk;
            int step     = impl->vtt_bindings[port].step;

            if (plus_vk != 0 && (GetAsyncKeyState(plus_vk) & 0x8000)) {
                _InterlockedExchangeAdd8(&impl->vtt_pos[port], (char)step);
            }
            if (minus_vk != 0 && (GetAsyncKeyState(minus_vk) & 0x8000)) {
                _InterlockedExchangeAdd8(&impl->vtt_pos[port], (char)(-step));
            }
        }
        Sleep(5);
    }

    timeEndPeriod(1);
    return 0;
}

// ---------------------------------------------------------------------------
// Output flush thread
// ---------------------------------------------------------------------------

static DWORD WINAPI output_flush_thread(LPVOID param) {
    InputManagerImpl* impl = (InputManagerImpl*)param;
    while (impl->running) {
        Sleep(1);  // XP-safe polling; 1ms = imperceptible for LED output
        EnterCriticalSection(&impl->devices_lock);
        for (auto& dev : impl->devices) {
            if (!dev.output_pending) continue;
            if (dev.hid_handle == INVALID_HANDLE_VALUE) { dev.output_pending = false; continue; }
            USHORT report_size = dev.caps.OutputReportByteLength;
            if (report_size == 0) { dev.output_pending = false; continue; }

            std::vector<BYTE> report(report_size, 0);

            // Button outputs: gather on-usages per cap, call HidP_SetButtons.
            int state_offset = 0;
            for (auto& bc : dev.button_output_caps_list) {
                if (!bc.IsRange) { bc.Range.UsageMin = bc.NotRange.Usage; bc.Range.UsageMax = bc.NotRange.Usage; }
                int btn_count = (int)(bc.Range.UsageMax - bc.Range.UsageMin + 1);
                std::vector<USAGE> on_usages;
                for (int b = 0; b < btn_count; b++) {
                    if (state_offset + b < (int)dev.button_output_states.size() &&
                        dev.button_output_states[state_offset + b]) {
                        on_usages.push_back(bc.Range.UsageMin + (USAGE)b);
                    }
                }
                if (!on_usages.empty()) {
                    ULONG usage_count = (ULONG)on_usages.size();
                    HidP_SetButtons(HidP_Output, bc.UsagePage, bc.LinkCollection,
                                    on_usages.data(), &usage_count,
                                    dev.preparsed, (PCHAR)report.data(), report_size);
                }
                state_offset += btn_count;
            }

            // Value outputs: HidP_SetUsageValue per cap.
            for (size_t vi = 0; vi < dev.value_output_caps_list.size(); vi++) {
                auto& vc = dev.value_output_caps_list[vi];
                if (vi >= dev.value_output_states.size()) break;
                float norm = dev.value_output_states[vi];
                LONG lmin = vc.LogicalMin, lmax = vc.LogicalMax;
                ULONG uval = (lmax > lmin) ? (ULONG)((long)(norm * (float)(lmax - lmin)) + lmin) : 0;
                USAGE usage = vc.IsRange ? vc.Range.UsageMin : vc.NotRange.Usage;
                HidP_SetUsageValue(HidP_Output, vc.UsagePage, vc.LinkCollection,
                                   usage, uval,
                                   dev.preparsed, (PCHAR)report.data(), report_size);
            }

            HidD_SetOutputReport(dev.hid_handle, report.data(), report_size);
            dev.output_pending = false;
        }
        LeaveCriticalSection(&impl->devices_lock);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// InputManager constructor / destructor
// ---------------------------------------------------------------------------

InputManager::InputManager() {
    impl = new InputManagerImpl();
    g_impl = impl;

    // Enumerate devices.
    devices_reload(impl);

    // Start message pump thread.
    impl->running = true;
    impl->pump_thread = CreateThread(
        nullptr, 0, msg_pump_thread, impl, 0, nullptr);

    // Wait for HWND to be created (max ~500ms).
    for (int i = 0; i < 500 && !impl->hwnd; i++) {
        Sleep(1);
    }

    // Start VTT thread.
    impl->vtt_pos[0] = (char)128;
    impl->vtt_pos[1] = (char)128;
    impl->vtt_thread = CreateThread(
        nullptr, 0, vtt_thread, impl, 0, nullptr);

    // Start output flush thread.
    impl->output_thread = CreateThread(nullptr, 0, output_flush_thread, impl, 0, nullptr);
}

InputManager::~InputManager() {
    impl->running = false;

    // Stop pump thread.
    if (impl->hwnd) {
        PostMessageA(impl->hwnd, WM_QUIT, 0, 0);
    }
    if (impl->pump_thread) {
        WaitForSingleObject(impl->pump_thread, 2000);
        CloseHandle(impl->pump_thread);
        impl->pump_thread = nullptr;
    }

    // Stop VTT thread.
    if (impl->vtt_thread) {
        WaitForSingleObject(impl->vtt_thread, 500);
        CloseHandle(impl->vtt_thread);
        impl->vtt_thread = nullptr;
    }

    // Stop output flush thread.
    if (impl->output_thread) {
        WaitForSingleObject(impl->output_thread, 2000);
        CloseHandle(impl->output_thread);
        impl->output_thread = nullptr;
    }

    // Free device preparsed data and close hid_handles.
    for (auto& dev : impl->devices) {
        dev.destroy();
    }
    impl->devices.clear();

    g_impl = nullptr;
    delete impl;
    impl = nullptr;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::vector<Device> InputManager::getDevices() const {
    EnterCriticalSection(&impl->devices_lock);
    std::vector<Device> snapshot = impl->devices;
    LeaveCriticalSection(&impl->devices_lock);
    return snapshot;
}

bool InputManager::getButtonState(const std::string& path, int button_idx) const {
    EnterCriticalSection(&impl->devices_lock);
    for (const auto& dev : impl->devices) {
        if (dev.path == path) {
            bool state = false;
            if (button_idx >= 0 && button_idx < (int)dev.button_states.size()) {
                state = dev.button_states[button_idx];
            }
            LeaveCriticalSection(&impl->devices_lock);
            return state;
        }
    }
    LeaveCriticalSection(&impl->devices_lock);
    return false;
}

float InputManager::getAxisValue(const std::string& path, int axis_idx) const {
    EnterCriticalSection(&impl->devices_lock);
    for (const auto& dev : impl->devices) {
        if (dev.path == path) {
            float val = 0.5f;
            if (axis_idx >= 0 && axis_idx < (int)dev.value_states.size()) {
                val = dev.value_states[axis_idx];
            }
            LeaveCriticalSection(&impl->devices_lock);
            return val;
        }
    }
    LeaveCriticalSection(&impl->devices_lock);
    return 0.5f;
}

void InputManager::setVttKeys(int port, int plus_vk, int minus_vk, int step) {
    if (port < 0 || port > 1) return;
    impl->vtt_bindings[port].plus_vk  = plus_vk;
    impl->vtt_bindings[port].minus_vk = minus_vk;
    impl->vtt_bindings[port].step     = step > 0 ? step : 3;
}

uint8_t InputManager::getVttPosition(int port) const {
    if (port < 0 || port > 1) return 128;
    return (uint8_t)(unsigned char)impl->vtt_pos[port];
}

void InputManager::startCapture() {
    EnterCriticalSection(&impl->capture_lock);
    impl->capture_mode   = true;
    impl->capture_result = std::nullopt;
    LeaveCriticalSection(&impl->capture_lock);

    // Reset prev states so any current press is treated as a fresh edge.
    EnterCriticalSection(&impl->devices_lock);
    for (auto& kv : impl->prev_button_states) {
        std::fill(kv.second.begin(), kv.second.end(), false);
    }
    LeaveCriticalSection(&impl->devices_lock);
}

void InputManager::stopCapture() {
    EnterCriticalSection(&impl->capture_lock);
    impl->capture_mode   = false;
    impl->capture_result = std::nullopt;
    LeaveCriticalSection(&impl->capture_lock);
}

std::optional<CaptureResult> InputManager::pollCapture() {
    EnterCriticalSection(&impl->capture_lock);
    auto result = impl->capture_result;
    impl->capture_result = std::nullopt;
    LeaveCriticalSection(&impl->capture_lock);
    return result;
}

void InputManager::setLight(const std::string& path, int output_idx, float value) {
    EnterCriticalSection(&impl->devices_lock);
    for (auto& dev : impl->devices) {
        if (dev.path != path) continue;
        if (dev.hid_handle == INVALID_HANDLE_VALUE) break;
        int btn_count = (int)dev.button_output_states.size();
        if (output_idx < btn_count) {
            dev.button_output_states[output_idx] = (value > 0.5f);
        } else {
            int val_idx = output_idx - btn_count;
            if (val_idx < (int)dev.value_output_states.size()) {
                dev.value_output_states[val_idx] = value;
            }
        }
        dev.output_pending = true;
        break;
    }
    LeaveCriticalSection(&impl->devices_lock);
}
