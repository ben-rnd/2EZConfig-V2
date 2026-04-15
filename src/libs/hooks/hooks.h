#pragma once

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the MinHook backend. Call once on DLL attach before any hook_create*.
// Returns nonzero on success.
int hooks_init(void);

// Uninitialize the MinHook backend. Call once on DLL detach.
void hooks_shutdown(void);

// Install an inline hook at an absolute target address.
// `original` receives a pointer to the trampoline that calls the original function.
// Hook is enabled immediately. Returns nonzero on success.
int hook_create(void* target, void* detour, void** original);

// Install an inline hook on an exported function of a loaded module.
// Hook is enabled immediately. Returns nonzero on success.
int hook_create_api(const wchar_t* module_name, const char* proc_name,
                    void* detour, void** original);

// Enable every hook created so far. Useful if hooks were created disabled
// (not our current pattern, but kept for completeness).
int hook_enable_all(void);

#ifdef __cplusplus
}
#endif
