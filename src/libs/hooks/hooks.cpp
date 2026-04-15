#include "hooks.h"

#include <MinHook.h>

#include <sstream>
#include <string>

#include "logger.h"

static std::string narrow(const wchar_t* s) {
    if (!s) return {};
    std::string out;
    for (; *s; ++s) out.push_back((char)(*s < 0x80 ? *s : '?'));
    return out;
}

extern "C" int hooks_init(void) {
    MH_STATUS st = MH_Initialize();
    if (st != MH_OK) {
        std::ostringstream oss;
        oss << "[hooks] MH_Initialize failed: " << (int)st;
        Logger::error(oss.str());
        return 0;
    }
    Logger::info("[hooks] MinHook initialized");
    return 1;
}

extern "C" void hooks_shutdown(void) {
    MH_Uninitialize();
}

extern "C" int hook_create(void* target, void* detour, void** original) {
    MH_STATUS st = MH_CreateHook(target, detour, original);
    if (st != MH_OK) {
        std::ostringstream oss;
        oss << "[hooks] MH_CreateHook(" << target << ") failed: " << (int)st;
        Logger::error(oss.str());
        return 0;
    }
    st = MH_EnableHook(target);
    if (st != MH_OK) {
        std::ostringstream oss;
        oss << "[hooks] MH_EnableHook(" << target << ") failed: " << (int)st;
        Logger::error(oss.str());
        return 0;
    }
    return 1;
}

extern "C" int hook_create_api(const wchar_t* module_name, const char* proc_name,
                               void* detour, void** original) {
    MH_STATUS st = MH_CreateHookApi(module_name, proc_name, detour, original);
    if (st != MH_OK) {
        std::ostringstream oss;
        oss << "[hooks] MH_CreateHookApi(" << narrow(module_name) << "!" << proc_name
            << ") failed: " << (int)st;
        Logger::error(oss.str());
        return 0;
    }
    HMODULE mod = GetModuleHandleW(module_name);
    void* target = mod ? (void*)GetProcAddress(mod, proc_name) : nullptr;
    if (!target) {
        std::ostringstream oss;
        oss << "[hooks] resolve " << narrow(module_name) << "!" << proc_name
            << " failed post-create";
        Logger::error(oss.str());
        return 0;
    }
    st = MH_EnableHook(target);
    if (st != MH_OK) {
        std::ostringstream oss;
        oss << "[hooks] MH_EnableHook(" << narrow(module_name) << "!" << proc_name
            << ") failed: " << (int)st;
        Logger::error(oss.str());
        return 0;
    }
    return 1;
}

extern "C" int hook_enable_all(void) {
    MH_STATUS st = MH_EnableHook(MH_ALL_HOOKS);
    if (st != MH_OK) {
        std::ostringstream oss;
        oss << "[hooks] MH_EnableHook(ALL) failed: " << (int)st;
        Logger::error(oss.str());
        return 0;
    }
    return 1;
}
