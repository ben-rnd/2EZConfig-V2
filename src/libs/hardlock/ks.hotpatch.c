#include <windows.h>
#include "kitchen_sink.h"
#include "ks.memutils.h"


int HotPatch_patch(const char* module_name, const char* func_name, size_t target_original_bytes_size, void* replacement_function, void** original_function) {
    void* cf_address = NULL;
    void* throwaway_function_addr = NULL;
    if (!original_function) { original_function = &throwaway_function_addr; }
    struct HotPatch_Info ctx;
    ZeroMemory(&ctx, sizeof(struct HotPatch_Info));
    if (!MemUtils__get_function_address(module_name, func_name, &cf_address)) { return FALSE; }
    if (!MemUtils__hotpatch_function(cf_address, replacement_function, target_original_bytes_size, &ctx, original_function)) { return FALSE; }
    return TRUE;
}
