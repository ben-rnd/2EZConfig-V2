#pragma once
#include <cstdint>
struct BindingStore;

void handleDJOut(uint16_t port, uint8_t value, const BindingStore& bs);
void handleDancerOut(uint16_t port, uint8_t value, const BindingStore& bs);
void startLightFlushThread(const BindingStore& bs);
