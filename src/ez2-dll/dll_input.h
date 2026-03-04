#pragma once
#include <cstdint>
class InputManager;
struct BindingStore;

// Button/analog name arrays for BindingStore::load() — duplicated from strings.h
// to avoid pulling md5.h / Windows.h data dependencies into the DLL.
extern const char* const s_ioButtonNames[];
extern const int IO_BUTTON_COUNT;          // 20
extern const char* const s_dancerButtonNames[];
extern const int DANCER_BUTTON_COUNT;      // 16
extern const char* const s_lightNames[];
extern const int LIGHT_NAME_COUNT;         // 23

// Pre-computed port cache — VEH handler reads these directly (single volatile read).
// Background polling thread updates them at ~1ms intervals.
extern volatile uint8_t  s_djPortCache[7];     // index = port & 0x07 (covers 0x100-0x106)
extern volatile uint16_t s_dancerPortCache[4]; // index = (port - 0x300) >> 1

// Start background thread that polls input and pre-computes all port values.
void startInputPollingThread(const BindingStore& bs, InputManager& mgr);
