# Patch GLFW 3.3.10 for Windows XP compatibility.
# Replaces GetModuleHandleExW (Vista+) with runtime detection + fallback.
# Idempotent: skips if already patched.

file(READ src/win32_init.c content)

# Skip if already patched
string(FIND "${content}" "XP compat: GetModuleHandleExW" already_patched)
if(NOT already_patched EQUAL -1)
    message(STATUS "GLFW XP compat patch: already applied, skipping")
    return()
endif()

string(REPLACE
    "    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |\n                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,\n                            (const WCHAR*) &_glfw,\n                            (HMODULE*) &_glfw.win32.instance))\n    {\n        _glfwInputErrorWin32(GLFW_PLATFORM_ERROR,\n                             \"Win32: Failed to retrieve own module handle\");\n        return GLFW_FALSE;\n    }"

    "    // XP compat: GetModuleHandleExW is Vista+. Resolve dynamically,\n    // fall back to GetModuleHandleW(NULL) on XP.\n    {\n        typedef BOOL (WINAPI *PFN_GetModuleHandleExW)(DWORD, LPCWSTR, HMODULE*);\n        PFN_GetModuleHandleExW pfn = (PFN_GetModuleHandleExW)\n            GetProcAddress(GetModuleHandleW(L\"kernel32.dll\"), \"GetModuleHandleExW\");\n        if (pfn)\n            pfn(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |\n                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,\n                (const WCHAR*) &_glfw,\n                (HMODULE*) &_glfw.win32.instance);\n        else\n            _glfw.win32.instance = GetModuleHandleW(NULL);\n    }\n    if (!_glfw.win32.instance)\n    {\n        _glfwInputErrorWin32(GLFW_PLATFORM_ERROR,\n                             \"Win32: Failed to retrieve own module handle\");\n        return GLFW_FALSE;\n    }"

    content "${content}"
)

file(WRITE src/win32_init.c "${content}")
message(STATUS "GLFW XP compat patch: applied successfully")
