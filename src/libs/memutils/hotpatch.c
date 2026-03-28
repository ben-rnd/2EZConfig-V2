#include <windows.h>
#include "memutils.h"


int memutils_hotpatch_import(const char* module_name, const char* func_name, size_t target_original_bytes_size, void* replacement_function, void** original_function) {
    void* cf_address = NULL;
    void* throwaway_function_addr = NULL;
    if (!original_function) { original_function = &throwaway_function_addr; }
    struct HotPatchInfo ctx;
    ZeroMemory(&ctx, sizeof(struct HotPatchInfo));
    if (!memutils_get_proc(module_name, func_name, &cf_address)) { return FALSE; }
    if (!memutils_hotpatch(cf_address, replacement_function, target_original_bytes_size, &ctx, original_function)) { return FALSE; }
    return TRUE;
}
