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
            case 0x39: return "Hat Switch";
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

// Human-readable label for HID output channels.
// Page 0x08 = LED, Page 0x0A = Ordinal (common for game light hardware).
static std::string output_label(USAGE usage_page, USAGE usage) {
    if (usage_page == 0x08) return "LED " + std::to_string(usage);
    if (usage_page == 0x0A) return "Output " + std::to_string(usage);
    if (usage_page == 0x01) return axis_label(usage_page, usage);
    std::ostringstream ss;
    ss << "Page 0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << usage_page
       << " Usage 0x" << std::setw(2) << std::setfill('0') << usage;
    return ss.str();
}

// ---------------------------------------------------------------------------
// Hat direction helpers (spice2x HAT_SWITCH_INCREMENT pattern)
// ---------------------------------------------------------------------------

static const float HAT_SWITCH_INCREMENT = 1.0f / 7.0f;

// Get the primary ButtonAnalogType for a hat float value.
// Returns NONE if hat_val is neutral (-1.0f).
static ButtonAnalogType getHatDirection(float hat_val) {
    if (hat_val < 0.0f) return ButtonAnalogType::NONE;
    // 8 directions: 0/7, 1/7, ..., 7/7
    // Find closest direction index.
    float pos = hat_val / HAT_SWITCH_INCREMENT;
    int dir = (int)(pos + 0.5f);
    if (dir < 0) dir = 0;
    if (dir > 7) dir = 7;
    // Map dir index to ButtonAnalogType: 0=UP, 1=UP_RIGHT, ..., 7=UP_LEFT
    return (ButtonAnalogType)(dir + 1);  // +1 because NONE=0, HS_UP=1
}

// Check if a hat float value matches a specific direction.
// Diagonals activate both adjacent cardinal directions.
// Uses +/- 0.5 * HAT_SWITCH_INCREMENT tolerance.
static bool isHatDirectionActive(float hat_val, ButtonAnalogType dir) {
    if (hat_val < 0.0f || dir == ButtonAnalogType::NONE) return false;
    int dir_idx = (int)dir - 1;  // 0=UP, 1=UP_RIGHT, ..., 7=UP_LEFT
    float dir_center = (float)dir_idx * HAT_SWITCH_INCREMENT;
    float tolerance = HAT_SWITCH_INCREMENT * 0.5f;

    // Check exact match with tolerance
    float diff = hat_val - dir_center;
    if (diff >= -tolerance && diff <= tolerance) return true;

    // Handle wrap-around (UP_LEFT at 7/7 is adjacent to UP at 0/7)
    float diff_wrap_pos = (hat_val + 1.0f + HAT_SWITCH_INCREMENT) - dir_center;
    float diff_wrap_neg = hat_val - (dir_center + 1.0f + HAT_SWITCH_INCREMENT);
    if (diff_wrap_pos >= -tolerance && diff_wrap_pos <= tolerance) return true;
    if (diff_wrap_neg >= -tolerance && diff_wrap_neg <= tolerance) return true;

    // For cardinal directions, check if a diagonal is active that includes this cardinal.
    // E.g., UP is active when UP_RIGHT (1/7) or UP_LEFT (7/7) is the hat value.
    // This is handled by the tolerance check above for adjacent directions.
    return false;
}

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
    // Previous hat value states for capture edge detection (keyed by device path).
    std::map<std::string, std::vector<float>> prev_hat_states;

    // Output event (auto-reset) — signals output thread on setLight().
    HANDLE output_event   = nullptr;
    // Output thread — wakes on event, processes pending devices.
    HANDLE output_thread  = nullptr;
    // Flush thread — periodic 500ms writes for all output-enabled devices.
    HANDLE flush_thread   = nullptr;

    InputManagerImpl() {
        InitializeCriticalSection(&devices_lock);
        InitializeCriticalSection(&capture_lock);
    }
    ~InputManagerImpl() {
        DeleteCriticalSection(&devices_lock);
        DeleteCriticalSection(&capture_lock);
    }
};

// ---------------------------------------------------------------------------
// Device output writing (Pattern 5 — spice2x device_write_output)
// ---------------------------------------------------------------------------

// Caller must hold dev.cs_output. Builds and writes one HID output report.
static void device_write_output(Device& dev) {
    if (!dev.hid || dev.hid->hid_handle == INVALID_HANDLE_VALUE) return;
    USHORT report_size = dev.hid->caps.OutputReportByteLength;
    if (report_size == 0) return;

    CHAR* report = new CHAR[report_size]();  // zero-initialized

    // Button outputs — build ON and OFF usage lists.
    int state_offset = 0;
    for (auto& bc : dev.hid->button_output_caps_list) {
        int btn_count = (int)(bc.Range.UsageMax - bc.Range.UsageMin + 1);
        if (btn_count <= 0) { continue; }

        std::vector<USAGE> on_usages, off_usages;
        for (int b = 0; b < btn_count; b++) {
            USAGE usg = bc.Range.UsageMin + (USAGE)b;
            if (state_offset + b < (int)dev.hid->button_output_states.size() &&
                dev.hid->button_output_states[state_offset + b]) {
                on_usages.push_back(usg);
            } else {
                off_usages.push_back(usg);
            }
        }

        // HidP_SetButtons for ON usages.
        if (!on_usages.empty()) {
            ULONG usage_count = (ULONG)on_usages.size();
            NTSTATUS st = HidP_SetButtons(HidP_Output, bc.UsagePage, bc.LinkCollection,
                                           on_usages.data(), &usage_count,
                                           dev.hid->preparsed, report, report_size);
            if (st == HIDP_STATUS_INCOMPATIBLE_REPORT_ID) {
                // Flush intermediate report via HidD_SetOutputReport, start fresh.
                HidD_SetOutputReport(dev.hid->hid_handle, report, report_size);
                delete[] report;
                report = new CHAR[report_size]();
                usage_count = (ULONG)on_usages.size();
                HidP_SetButtons(HidP_Output, bc.UsagePage, bc.LinkCollection,
                                on_usages.data(), &usage_count,
                                dev.hid->preparsed, report, report_size);
            }
        }

        // HidP_UnsetButtons for OFF usages — explicitly clear lights.
        if (!off_usages.empty()) {
            ULONG usage_count = (ULONG)off_usages.size();
            NTSTATUS st = HidP_UnsetButtons(HidP_Output, bc.UsagePage, bc.LinkCollection,
                                             off_usages.data(), &usage_count,
                                             dev.hid->preparsed, report, report_size);
            if (st == HIDP_STATUS_INCOMPATIBLE_REPORT_ID) {
                HidD_SetOutputReport(dev.hid->hid_handle, report, report_size);
                delete[] report;
                report = new CHAR[report_size]();
                usage_count = (ULONG)off_usages.size();
                HidP_UnsetButtons(HidP_Output, bc.UsagePage, bc.LinkCollection,
                                  off_usages.data(), &usage_count,
                                  dev.hid->preparsed, report, report_size);
            }
        }

        state_offset += btn_count;
    }

    // Value outputs — HidP_SetUsageValue per entry.
    for (size_t vi = 0; vi < dev.hid->value_output_caps_list.size(); vi++) {
        auto& vc = dev.hid->value_output_caps_list[vi];
        if (vi >= dev.hid->value_output_states.size() || vi >= dev.hid->value_output_usages.size()) break;
        float norm = dev.hid->value_output_states[vi];
        LONG lmin = vc.LogicalMin, lmax = vc.LogicalMax;
        LONG lval = (lmax > lmin)
            ? lmin + (LONG)lroundf((float)(lmax - lmin) * norm) : lmin;
        if (lval > lmax) lval = lmax;
        if (lval < lmin) lval = lmin;
        ULONG uval = (ULONG)lval;
        NTSTATUS st = HidP_SetUsageValue(HidP_Output, vc.UsagePage, vc.LinkCollection,
                                          dev.hid->value_output_usages[vi], uval,
                                          dev.hid->preparsed, report, report_size);
        if (st == HIDP_STATUS_INCOMPATIBLE_REPORT_ID) {
            HidD_SetOutputReport(dev.hid->hid_handle, report, report_size);
            delete[] report;
            report = new CHAR[report_size]();
            HidP_SetUsageValue(HidP_Output, vc.UsagePage, vc.LinkCollection,
                               dev.hid->value_output_usages[vi], uval,
                               dev.hid->preparsed, report, report_size);
        }
    }

    // Final report — WriteFile.
    DWORD written = 0;
    WriteFile(dev.hid->hid_handle, report, report_size, &written, nullptr);
    delete[] report;
}

// ---------------------------------------------------------------------------
// Device enumeration
// ---------------------------------------------------------------------------

static void devices_reload(InputManagerImpl* impl) {
    // Free old devices.
    for (auto& dev : impl->devices) {
        dev.destroy();
    }
    impl->devices.clear();
    impl->prev_button_states.clear();
    impl->prev_hat_states.clear();

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

        // Skip vendor-specific top-level collections (usage page 0xFF**).
        if ((caps.UsagePage >> 8) == 0xFF) {
            LocalFree(preparsed);
            continue;
        }

        Device dev;
        dev.path       = path;
        dev.raw_handle = entry.hDevice;

        // Allocate DeviceHIDInfo sub-struct.
        dev.hid = new DeviceHIDInfo();
        dev.hid->preparsed = preparsed;
        dev.hid->caps      = caps;

        // -----------------------------------------------------------------
        // Open persistent handle — GENERIC_READ|GENERIC_WRITE for output
        // sending (HidD_SetOutputReport) and HidD_GetIndexedString lookups.
        // -----------------------------------------------------------------
        dev.hid->hid_handle = CreateFileA(
            path.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr);

        // -----------------------------------------------------------------
        // Device name from product/manufacturer string.
        // -----------------------------------------------------------------
        {
            bool got_name = false;
            if (dev.hid->hid_handle != INVALID_HANDLE_VALUE) {
                wchar_t wbuf[256] = {};
                if (HidD_GetProductString(dev.hid->hid_handle, wbuf, sizeof(wbuf))) {
                    std::string s = wide_to_utf8(wbuf);
                    if (!s.empty()) { dev.name = s; got_name = true; }
                }
                if (!got_name) {
                    wchar_t mbuf[256] = {};
                    if (HidD_GetManufacturerString(dev.hid->hid_handle, mbuf, sizeof(mbuf))) {
                        std::string s = wide_to_utf8(mbuf);
                        if (!s.empty()) { dev.name = s; got_name = true; }
                    }
                }
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
                dev.destroy();
                continue;
            }

            for (int cap_num = 0; cap_num < (int)btn_cap_count; cap_num++) {
                auto& bc = btn_cap_data[cap_num];

                // IsRange normalization (spice2x pattern) — fill Range from NotRange
                // so all downstream code can assume Range.* is valid.
                if (!bc.IsRange) {
                    bc.Range.UsageMin = bc.NotRange.Usage;
                    bc.Range.UsageMax = bc.NotRange.Usage;
                }

                int btn_count = (int)(bc.Range.UsageMax - bc.Range.UsageMin + 1);
                if (btn_count <= 0 || btn_count >= 0xffff)
                    continue;

                // Hat switch in button caps — treat as regular button (match spice2x).
                // No DPad promotion. If hat appears as button cap, add as normal buttons.

                // Skip vendor-specific usage pages.
                if ((bc.UsagePage >> 8) == 0xFF)
                    continue;

                // Regular button cap.
                dev.hid->button_caps_list.push_back(bc);
                for (int b = 0; b < btn_count; b++) {
                    int button_number = (int)dev.hid->button_states.size() + 1;
                    dev.button_caps_names.push_back("Button " + std::to_string(button_number));
                    dev.hid->button_states.push_back(false);
                }
            }
        }

        // -----------------------------------------------------------------
        // Value caps (input). Hat switches included as float value_states.
        // -----------------------------------------------------------------
        {
            USHORT val_cap_count = caps.NumberInputValueCaps;
            std::vector<HIDP_VALUE_CAPS> val_cap_data(val_cap_count);
            if (val_cap_count > 0 &&
                HidP_GetValueCaps(HidP_Input, val_cap_data.data(), &val_cap_count, preparsed) != HIDP_STATUS_SUCCESS)
            {
                dev.destroy();
                continue;
            }

            for (int cap_num = 0; cap_num < (int)val_cap_count; cap_num++) {
                auto& vc = val_cap_data[cap_num];

                // IsRange normalization.
                if (!vc.IsRange) {
                    vc.Range.UsageMin = vc.NotRange.Usage;
                    vc.Range.UsageMax = vc.NotRange.Usage;
                }

                USAGE usage = vc.Range.UsageMin;

                // Sign-extension fix for LogicalMin/Max (sign-extension location 1 of 3).
                // Applied unconditionally to all value caps including hats.
                if (vc.BitSize > 0 && vc.BitSize <= (USHORT)(sizeof(vc.LogicalMin) * 8)) {
                    auto shift_size = sizeof(vc.LogicalMin) * 8 - vc.BitSize + 1;
                    auto mask = ((uint64_t)1 << vc.BitSize) - 1;
                    vc.LogicalMin &= (LONG)mask;
                    vc.LogicalMin <<= shift_size;
                    vc.LogicalMin >>= shift_size;
                    vc.LogicalMax &= (LONG)mask;
                }

                // Skip vendor-specific usage pages.
                if ((vc.UsagePage >> 8) == 0xFF)
                    continue;

                // All value caps (including hat switch 0x39) go into value_caps_list.
                // Hat switches store float in value_states: -1.0f = neutral.
                dev.hid->value_caps_list.push_back(vc);
                dev.value_caps_names.push_back(axis_label(vc.UsagePage, usage));

                // Hat starts at -1.0f (neutral); regular axes start at 0.5f (center).
                bool is_hat = (vc.UsagePage == 0x01 && usage == 0x39);
                dev.hid->value_states.push_back(is_hat ? -1.0f : 0.5f);
                dev.hid->value_states_raw.push_back(0);
            }
        }

        // -----------------------------------------------------------------
        // Output button caps — store cap structs for output thread.
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
                    // Skip vendor-specific usage pages.
                    if ((bc.UsagePage >> 8) == 0xFF) continue;
                    // Store once per cap (not per button).
                    dev.hid->button_output_caps_list.push_back(bc);
                    for (int b = 0; b < btn_count; b++) {
                        USAGE usg = bc.Range.UsageMin + (USAGE)b;
                        // Try device-provided string descriptor first.
                        ULONG str_idx = 0;
                        if (bc.IsStringRange && bc.Range.StringMin != 0)
                            str_idx = (ULONG)bc.Range.StringMin + (ULONG)b;
                        else if (!bc.IsRange && bc.NotRange.StringIndex != 0)
                            str_idx = bc.NotRange.StringIndex;
                        wchar_t wbuf[256] = {};
                        if (str_idx > 0 && dev.hid->hid_handle != INVALID_HANDLE_VALUE
                            && HidD_GetIndexedString(dev.hid->hid_handle, str_idx, wbuf, sizeof(wbuf))
                            && wbuf[0] != L'\0') {
                            dev.button_output_caps_names.push_back(wide_to_utf8(wbuf));
                        } else {
                            dev.button_output_caps_names.push_back(output_label(bc.UsagePage, usg));
                        }
                        out_btn_num++;
                    }
                }
            }
        }

        // -----------------------------------------------------------------
        // Output value caps — store cap structs for output thread.
        // -----------------------------------------------------------------
        {
            USHORT out_val_count = caps.NumberOutputValueCaps;
            std::vector<HIDP_VALUE_CAPS> out_val_data(out_val_count);
            if (out_val_count > 0 &&
                HidP_GetValueCaps(HidP_Output, out_val_data.data(), &out_val_count, preparsed) == HIDP_STATUS_SUCCESS)
            {
                for (int cap_num = 0; cap_num < (int)out_val_count; cap_num++) {
                    auto& vc = out_val_data[cap_num];

                    // Sign-extension fix for output LogicalMin/Max (sign-extension location 2 of 3).
                    if (vc.BitSize > 0 && vc.BitSize <= (USHORT)(sizeof(vc.LogicalMin) * 8)) {
                        auto shift_size = sizeof(vc.LogicalMin) * 8 - vc.BitSize + 1;
                        auto mask = ((uint64_t)1 << vc.BitSize) - 1;
                        vc.LogicalMin &= (LONG)mask;
                        vc.LogicalMin <<= shift_size;
                        vc.LogicalMin >>= shift_size;
                        vc.LogicalMax &= (LONG)mask;
                    }

                    if (!vc.IsRange) {
                        vc.Range.UsageMin = vc.NotRange.Usage;
                        vc.Range.UsageMax = vc.NotRange.Usage;
                    }
                    int range_count = (int)(vc.Range.UsageMax - vc.Range.UsageMin + 1);
                    if (range_count <= 0 || range_count >= 0xffff) continue;
                    // Skip vendor-specific usage pages.
                    if ((vc.UsagePage >> 8) == 0xFF) continue;
                    // Expand range: one entry per usage so each output is independently addressable.
                    for (int u = 0; u < range_count; u++) {
                        USAGE specific = vc.Range.UsageMin + (USAGE)u;
                        dev.hid->value_output_caps_list.push_back(vc);
                        dev.hid->value_output_usages.push_back(specific);
                        // Try device-provided string descriptor first.
                        ULONG str_idx = 0;
                        if (vc.IsStringRange && vc.Range.StringMin != 0)
                            str_idx = (ULONG)vc.Range.StringMin + (ULONG)u;
                        else if (!vc.IsRange && vc.NotRange.StringIndex != 0)
                            str_idx = vc.NotRange.StringIndex;
                        wchar_t wbuf[256] = {};
                        if (str_idx > 0 && dev.hid->hid_handle != INVALID_HANDLE_VALUE
                            && HidD_GetIndexedString(dev.hid->hid_handle, str_idx, wbuf, sizeof(wbuf))
                            && wbuf[0] != L'\0') {
                            dev.value_output_caps_names.push_back(wide_to_utf8(wbuf));
                        } else {
                            dev.value_output_caps_names.push_back(output_label(vc.UsagePage, specific));
                        }
                    }
                }
            }
        }

        // Size output state arrays.
        dev.hid->button_output_states.assign(dev.button_output_caps_names.size(), false);
        dev.hid->value_output_states.assign(dev.value_output_caps_names.size(), 0.0f);
        dev.output_pending = false;

        // Initialize per-device critical sections.
        InitializeCriticalSection(&dev.cs_input);
        InitializeCriticalSection(&dev.cs_output);

        // Set output_enabled: device must have a valid write handle and non-zero output report length.
        dev.output_enabled = (dev.hid->hid_handle != INVALID_HANDLE_VALUE &&
                              dev.hid->caps.OutputReportByteLength > 0);

        impl->devices.push_back(std::move(dev));
    }

    // Initialize prev_button_states for capture edge detection.
    for (const auto& dev : impl->devices) {
        if (!dev.hid) continue;
        impl->prev_button_states[dev.path] = std::vector<bool>(dev.hid->button_states.size(), false);
        // Build prev_hat_states: one entry per value cap that is a hat switch.
        std::vector<float> hat_snapshot;
        for (size_t vi = 0; vi < dev.hid->value_caps_list.size(); vi++) {
            auto& vc = dev.hid->value_caps_list[vi];
            if (vc.UsagePage == 0x01 && vc.Range.UsageMin == 0x39) {
                hat_snapshot.push_back(-1.0f);
            }
        }
        impl->prev_hat_states[dev.path] = hat_snapshot;
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

    // Find matching device by raw handle under devices_lock (brief).
    EnterCriticalSection(&impl->devices_lock);
    Device* dev = nullptr;
    for (auto& d : impl->devices) {
        if (d.raw_handle == raw->header.hDevice) {
            dev = &d;
            break;
        }
    }
    if (!dev || !dev->hid) {
        LeaveCriticalSection(&impl->devices_lock);
        return;
    }
    // Release devices_lock now — we have a stable pointer; use per-device lock.
    LeaveCriticalSection(&impl->devices_lock);

    const BYTE*  report_buf  = hid_data.bRawData;
    const UINT   report_size = hid_data.dwSizeHid;
    PHIDP_PREPARSED_DATA preparsed = dev->hid->preparsed;

    // Acquire per-device input lock for state mutation.
    EnterCriticalSection(&dev->cs_input);

    // -----------------------------------------------------------------
    // Button states (regular buttons).
    // -----------------------------------------------------------------
    {
        int state_offset = 0;
        for (size_t cap_i = 0; cap_i < dev->hid->button_caps_list.size(); cap_i++) {
            auto& bc = dev->hid->button_caps_list[cap_i];
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
                dev->hid->button_states[state_offset + b] = false;
            }
            if (query_ok) {
                for (ULONG u = 0; u < usages_len; u++) {
                    int idx = (int)(usages[u] - bc.Range.UsageMin);
                    if (idx >= 0 && idx < btn_count) {
                        dev->hid->button_states[state_offset + idx] = true;
                    }
                }
            }

            state_offset += btn_count;
        }
    }

    // -----------------------------------------------------------------
    // Value states (axes + hat switches as float).
    // -----------------------------------------------------------------
    for (size_t cap_i = 0; cap_i < dev->hid->value_caps_list.size(); cap_i++) {
        auto& vc = dev->hid->value_caps_list[cap_i];

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

        bool is_hat = (vc.UsagePage == 0x01 && vc.Range.UsageMin == 0x39);

        if (is_hat) {
            // Hat switch — store as float. -1.0f = neutral.
            // Sign extension already applied at enumeration to LogicalMin/Max.
            LONG raw_val = (LONG)raw_ulong;

            // Sign-extension fix at WM_INPUT — only when LogicalMin < 0 (sign-extension location 3 of 3).
            if (vc.LogicalMin < 0 && vc.BitSize > 0 &&
                    vc.BitSize <= (USHORT)(sizeof(vc.LogicalMin) * 8)) {
                auto shift_size = sizeof(vc.LogicalMin) * 8 - vc.BitSize + 1;
                raw_val <<= shift_size;
                raw_val >>= shift_size;
            }

            LONG lmin = vc.LogicalMin, lmax = vc.LogicalMax;
            float hat_val = -1.0f;  // neutral
            if (lmin <= raw_val && raw_val <= lmax && lmax > lmin) {
                hat_val = (float)(raw_val - lmin) / (float)(lmax - lmin);
            }
            dev->hid->value_states[cap_i] = hat_val;
        } else {
            // Regular axis.
            LONG raw_val = (LONG)raw_ulong;

            // Sign-extension fix (spice2x line 1791) — only when LogicalMin < 0
            // (sign-extension location 3 of 3, shared with hat above).
            if (vc.LogicalMin < 0 &&
                    vc.BitSize > 0 &&
                    vc.BitSize <= (USHORT)(sizeof(vc.LogicalMin) * 8)) {
                auto shift_size = sizeof(vc.LogicalMin) * 8 - vc.BitSize + 1;
                raw_val <<= shift_size;
                raw_val >>= shift_size;
            }

            dev->hid->value_states_raw[cap_i] = raw_val;

            // Auto-calibration: expand range on the fly.
            if (raw_val < vc.LogicalMin) vc.LogicalMin = raw_val;
            if (raw_val > vc.LogicalMax) vc.LogicalMax = raw_val;

            LONG lmin = vc.LogicalMin;
            LONG lmax = vc.LogicalMax;
            float normalized = (lmax > lmin)
                ? (float)(raw_val - lmin) / (float)(lmax - lmin)
                : 0.5f;
            if (normalized < 0.0f) normalized = 0.0f;
            if (normalized > 1.0f) normalized = 1.0f;

            dev->hid->value_states[cap_i] = normalized;
        }
    }

    LeaveCriticalSection(&dev->cs_input);

    // -----------------------------------------------------------------
    // Capture mode edge detection.
    // -----------------------------------------------------------------
    {
        EnterCriticalSection(&impl->capture_lock);
        bool capturing = impl->capture_mode;
        bool already_captured = impl->capture_result.has_value();
        LeaveCriticalSection(&impl->capture_lock);

        if (capturing && !already_captured) {
            // Button edge detection.
            auto& prev = impl->prev_button_states[dev->path];

            EnterCriticalSection(&dev->cs_input);
            size_t btn_size = dev->hid->button_states.size();

            // Ensure prev vector is the right size.
            if (prev.size() != btn_size) {
                prev.assign(btn_size, false);
            }

            for (int i = 0; i < (int)btn_size; i++) {
                if (dev->hid->button_states[i] && !prev[i]) {
                    // Edge: button just pressed.
                    CaptureResult cr;
                    cr.path        = dev->path;
                    cr.button_idx  = i;
                    cr.device_name = dev->name;
                    cr.analog_type = ButtonAnalogType::NONE;

                    EnterCriticalSection(&impl->capture_lock);
                    if (!impl->capture_result.has_value()) {
                        impl->capture_result = cr;
                    }
                    LeaveCriticalSection(&impl->capture_lock);
                    break;
                }
            }

            // Copy current button states for next edge detection.
            prev.assign(dev->hid->button_states.begin(), dev->hid->button_states.end());

            // Hat direction change detection.
            EnterCriticalSection(&impl->capture_lock);
            already_captured = impl->capture_result.has_value();
            LeaveCriticalSection(&impl->capture_lock);

            if (!already_captured) {
                auto& prev_hats = impl->prev_hat_states[dev->path];
                int hat_idx = 0;
                for (size_t vi = 0; vi < dev->hid->value_caps_list.size(); vi++) {
                    auto& vc = dev->hid->value_caps_list[vi];
                    if (vc.UsagePage != 0x01 || vc.Range.UsageMin != 0x39) continue;

                    float cur_val = dev->hid->value_states[vi];
                    float prev_val = (hat_idx < (int)prev_hats.size()) ? prev_hats[hat_idx] : -1.0f;

                    // Detect direction change: was neutral and now has direction,
                    // or direction changed from snapshot.
                    if (cur_val >= 0.0f && (prev_val < 0.0f || std::fabs(cur_val - prev_val) > HAT_SWITCH_INCREMENT * 0.5f)) {
                        ButtonAnalogType dir = getHatDirection(cur_val);
                        if (dir != ButtonAnalogType::NONE) {
                            CaptureResult cr;
                            cr.path        = dev->path;
                            cr.button_idx  = (int)vi;  // axis_idx into value_states
                            cr.device_name = dev->name;
                            cr.analog_type = dir;

                            EnterCriticalSection(&impl->capture_lock);
                            if (!impl->capture_result.has_value()) {
                                impl->capture_result = cr;
                            }
                            LeaveCriticalSection(&impl->capture_lock);
                            break;
                        }
                    }

                    // Update snapshot.
                    if (hat_idx < (int)prev_hats.size()) {
                        prev_hats[hat_idx] = cur_val;
                    }
                    hat_idx++;
                }
            }

            LeaveCriticalSection(&dev->cs_input);
        }
    }
}

// ---------------------------------------------------------------------------
// WndProc — GWLP_USERDATA pattern (replaces g_impl global)
// ---------------------------------------------------------------------------

static LRESULT CALLBACK EZ2InputWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }

    auto* impl = reinterpret_cast<InputManagerImpl*>(
        GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    if (!impl) return DefWindowProcA(hwnd, msg, wParam, lParam);

    if (msg == WM_INPUT) {
        handle_wm_input(impl, reinterpret_cast<HRAWINPUT>(lParam));
        DefWindowProcA(hwnd, msg, wParam, lParam);
        return 0;
    }
    if (msg == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Message pump thread
// ---------------------------------------------------------------------------

static DWORD WINAPI msg_pump_thread(LPVOID param) {
    InputManagerImpl* impl = reinterpret_cast<InputManagerImpl*>(param);

    // Set time-critical priority for input pump thread (match spice2x).
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    // Register window class.
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
// Output thread — event-based (Pattern 4 — spice2x do-while)
// ---------------------------------------------------------------------------

static DWORD WINAPI output_thread_func(LPVOID param) {
    InputManagerImpl* impl = (InputManagerImpl*)param;
    while (impl->running) {
        WaitForSingleObject(impl->output_event, INFINITE);
        if (!impl->running) break;

        // Do-while: re-check after processing to catch updates that arrived during writes.
        bool any_pending;
        do {
            any_pending = false;
            for (auto& dev : impl->devices) {
                EnterCriticalSection(&dev.cs_output);
                bool pending = dev.output_pending && dev.output_enabled;
                dev.output_pending = false;
                if (pending) {
                    // Hold cs_output through entire write (report build + WriteFile).
                    // Intentional: HID reports are small, fast I/O. Matches spice2x.
                    device_write_output(dev);
                }
                LeaveCriticalSection(&dev.cs_output);
                if (pending) any_pending = true;
            }
            // Check if any device got a new update during our write pass.
            if (!any_pending) {
                for (auto& dev : impl->devices) {
                    EnterCriticalSection(&dev.cs_output);
                    if (dev.output_pending && dev.output_enabled) any_pending = true;
                    LeaveCriticalSection(&dev.cs_output);
                    if (any_pending) break;
                }
            }
        } while (any_pending && impl->running);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Flush thread — periodic 500ms writes for DAO IIDX boards (Pattern 4)
// ---------------------------------------------------------------------------

static DWORD WINAPI flush_thread_func(LPVOID param) {
    InputManagerImpl* impl = (InputManagerImpl*)param;
    while (impl->running) {
        Sleep(495);
        if (!impl->running) break;
        for (auto& dev : impl->devices) {
            EnterCriticalSection(&dev.cs_output);
            bool enabled = dev.output_enabled;
            if (enabled) {
                device_write_output(dev);
            }
            LeaveCriticalSection(&dev.cs_output);
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// InputManager constructor / destructor
// ---------------------------------------------------------------------------

InputManager::InputManager() {
    impl = new InputManagerImpl();

    // Create output event (auto-reset).
    impl->output_event = CreateEvent(NULL, FALSE, FALSE, NULL);

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

    // Start output thread (event-based) and flush thread (periodic 500ms).
    impl->output_thread = CreateThread(nullptr, 0, output_thread_func, impl, 0, nullptr);
    impl->flush_thread  = CreateThread(nullptr, 0, flush_thread_func, impl, 0, nullptr);
}

InputManager::~InputManager() {
    impl->running = false;

    // Wake output thread so it can exit.
    if (impl->output_event) {
        SetEvent(impl->output_event);
    }

    // Stop pump thread — WM_CLOSE triggers DestroyWindow inside the thread.
    if (impl->hwnd) {
        PostMessageA(impl->hwnd, WM_CLOSE, 0, 0);
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

    // Stop output thread.
    if (impl->output_thread) {
        WaitForSingleObject(impl->output_thread, 2000);
        CloseHandle(impl->output_thread);
        impl->output_thread = nullptr;
    }

    // Stop flush thread.
    if (impl->flush_thread) {
        WaitForSingleObject(impl->flush_thread, 2000);
        CloseHandle(impl->flush_thread);
        impl->flush_thread = nullptr;
    }

    // Close output event.
    if (impl->output_event) {
        CloseHandle(impl->output_event);
        impl->output_event = nullptr;
    }

    // Free device data (destroy() calls DeleteCriticalSection for both cs_input, cs_output).
    for (auto& dev : impl->devices) {
        dev.destroy();
    }
    impl->devices.clear();

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
    for (auto& dev : impl->devices) {
        if (dev.path == path) {
            LeaveCriticalSection(&impl->devices_lock);
            if (!dev.hid) return false;
            EnterCriticalSection(&dev.cs_input);
            bool state = false;
            if (button_idx >= 0 && button_idx < (int)dev.hid->button_states.size()) {
                state = dev.hid->button_states[button_idx];
            }
            LeaveCriticalSection(&dev.cs_input);
            return state;
        }
    }
    LeaveCriticalSection(&impl->devices_lock);
    return false;
}

float InputManager::getAxisValue(const std::string& path, int axis_idx) const {
    EnterCriticalSection(&impl->devices_lock);
    for (auto& dev : impl->devices) {
        if (dev.path == path) {
            LeaveCriticalSection(&impl->devices_lock);
            if (!dev.hid) return 0.5f;
            EnterCriticalSection(&dev.cs_input);
            float val = 0.5f;
            if (axis_idx >= 0 && axis_idx < (int)dev.hid->value_states.size()) {
                val = dev.hid->value_states[axis_idx];
            }
            LeaveCriticalSection(&dev.cs_input);
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
    // Reset hat snapshots to neutral.
    for (auto& kv : impl->prev_hat_states) {
        std::fill(kv.second.begin(), kv.second.end(), -1.0f);
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
        LeaveCriticalSection(&impl->devices_lock);

        if (!dev.hid || dev.hid->hid_handle == INVALID_HANDLE_VALUE) return;

        EnterCriticalSection(&dev.cs_output);
        int btn_count = (int)dev.hid->button_output_states.size();
        if (output_idx < btn_count) {
            dev.hid->button_output_states[output_idx] = (value > 0.5f);
        } else {
            int val_idx = output_idx - btn_count;
            if (val_idx < (int)dev.hid->value_output_states.size()) {
                dev.hid->value_output_states[val_idx] = value;
            }
        }
        dev.output_pending = true;
        LeaveCriticalSection(&dev.cs_output);

        // Signal output thread.
        SetEvent(impl->output_event);
        return;
    }
    LeaveCriticalSection(&impl->devices_lock);
}
