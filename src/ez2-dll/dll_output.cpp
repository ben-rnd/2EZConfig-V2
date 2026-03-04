#include "dll_output.h"
#include "bindings.h"
#include "input_manager.h"

// Stub implementations — filled in by Plan 03-02
void handleDJOut(uint16_t port, uint8_t value, const BindingStore& bs, InputManager& mgr) {
    (void)port; (void)value; (void)bs; (void)mgr;
}

void handleDancerOut(uint16_t port, uint8_t value, const BindingStore& bs, InputManager& mgr) {
    (void)port; (void)value; (void)bs; (void)mgr;
}
