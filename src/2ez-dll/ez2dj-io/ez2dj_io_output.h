#pragma once
#include <cstdint>
struct BindingStore;

void initDJOutputLogging(bool verbose);
void handleDJOut(uint16_t port, uint8_t value);
void startDJLightFlushThread(const BindingStore& bs);
