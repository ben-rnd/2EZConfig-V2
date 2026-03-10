#pragma once
#include <cstdint>
#include <atomic>
struct BindingStore;

extern std::atomic<uint8_t>  s_djPortCache[7];     // index = port & 0x07 (covers 0x100-0x106)
extern std::atomic<uint16_t> s_dancerPortCache[4]; // index = (port - 0x300) >> 1

void initPortCache(const BindingStore& bs);
void updatePortCache(const BindingStore& bs);
