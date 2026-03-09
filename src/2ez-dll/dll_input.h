#pragma once
#include <cstdint>
struct BindingStore;

// Pre-computed port cache — VEH handler reads these directly (single volatile read).
// Background polling thread updates them at ~1ms intervals.
extern volatile uint8_t  s_djPortCache[7];     // index = port & 0x07 (covers 0x100-0x106)
extern volatile uint16_t s_dancerPortCache[4]; // index = (port - 0x300) >> 1

// Start background thread that polls input and pre-computes all port values.
void startInputPollingThread(const BindingStore& bs);
