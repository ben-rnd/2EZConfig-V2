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

// Port byte computation — called from VEH handler hot path.
uint8_t computeDJPortByte(uint16_t port, const BindingStore& bs, InputManager& mgr);
uint16_t computeDancerPortWord(uint16_t port, const BindingStore& bs, InputManager& mgr);
