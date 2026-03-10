#pragma once
#include <cstdint>
#include <atomic>
struct BindingStore;

// Pre-computed port cache — VEH handler reads these directly.
// Event-driven: updated immediately after each WM_INPUT event.
extern std::atomic<uint8_t>  s_djPortCache[7];     // index = port & 0x07 (covers 0x100-0x106)
extern std::atomic<uint16_t> s_dancerPortCache[4]; // index = (port - 0x300) >> 1

// Call once after bindings load. Builds bound-device path list, populates
// the cache, and registers the WM_INPUT callback on bs.mgr.
void initPortCache(const BindingStore& bs);

// Recompute all port values from a fresh device snapshot.
// Called automatically via callback; also exposed for the initial call in initPortCache.
void updatePortCache(const BindingStore& bs);
