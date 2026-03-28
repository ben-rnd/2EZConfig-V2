#pragma once
#include "target.h"
#include <stddef.h>

#define MEMUTILS_PAGE_SIZE 0x1000

#if TARGET_ARCH_64
#define HOTPATCH_ADDRESS_OFFSET 2
static unsigned char hotpatch_stub[] = {
    0x48,0xB8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // mov rax, [Abs Jump Address]
    0xFF,0xE0,                                         // jmp rax
    0xC3,                                              // ret
};
#else
#define HOTPATCH_ADDRESS_OFFSET 1
static unsigned char hotpatch_stub[] = {
        0xB8, 0x00, 0x00, 0x00, 0x00, // mov eax, [Abs Jump Address]
        0xFF,0xE0,                    // jmp eax
        0xC3                          // ret
};
#endif

struct HotPatchInfo {
    void* target_function_address;
    void* replacement_function_address;
    void* trampoline_address;
    size_t trampoline_size;
    unsigned char* target_original_bytes;
    size_t target_original_bytes_size;
};

#ifdef __cplusplus
extern "C" {
#endif

unsigned char memutils_get_proc(const char* lib_name, const char* func_name, void** func_address);
unsigned char memutils_alloc_exec(void** page_addr);
unsigned char memutils_free_exec(void* page_addr);

unsigned char memutils_patch_ret0(void* target_addr);
unsigned char memutils_patch_ret1(void* target_addr);
unsigned char memutils_patch(void* target_addr, void* data_ptr, size_t data_len, unsigned char is_write, unsigned char is_exec);
unsigned char memutils_hotpatch(void* target_function_address, void* replacement_function_address, size_t target_original_bytes_size, struct HotPatchInfo* ctx, void** ptrampoline_address);
unsigned char memutils_unhotpatch(struct HotPatchInfo* ctx);

// Convenience: hook an exported function by module/name.
int memutils_hotpatch_import(const char* module_name, const char* func_name, size_t target_original_bytes_size, void* replacement_function, void** original_function);

#ifdef __cplusplus
}
#endif
