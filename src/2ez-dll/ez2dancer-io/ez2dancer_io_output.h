#pragma once
#include <cstdint>
struct BindingStore;

void initDancerOutputLogging(bool verbose);
void handleDancerOut(uint16_t port, uint16_t value);
void startDancerLightFlushThread(const BindingStore& bs);
