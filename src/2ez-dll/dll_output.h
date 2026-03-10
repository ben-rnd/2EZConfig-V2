#pragma once
#include <cstdint>
struct BindingStore;

void handleDJOut(uint16_t port, uint8_t value);
void handleDancerOut(uint16_t port, uint8_t value);
void startLightFlushThread(const BindingStore& bs);
