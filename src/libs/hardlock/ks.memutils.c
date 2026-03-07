#include <string.h>

#include "target.h"

#ifdef TARGET_OS_WINDOWS
#include <windows.h>

#elif TARGET_OS_LINUX ||TARGET_OS_OSX
#include <dlfcn.h>
#include <sys/mman.h>
// Stupid GNU Thing
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS 0x20
#endif
#endif

#include "ks.memutils.h"


unsigned char MemUtils__alloc(size_t amt, unsigned int is_exec, void** addr) {
    unsigned int access_mask = 0;
#ifdef TARGET_OS_WINDOWS
    if (is_exec) { access_mask = PAGE_EXECUTE_READWRITE; }
    else { access_mask = PAGE_READWRITE; }
    *addr = VirtualAlloc(NULL, PAGE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
#elif TARGET_OS_LINUX || TARGET_OS_OSX
    if (is_exec) { access_mask = PROT_EXEC | PROT_READ | PROT_WRITE; }
    else { access_mask = PROT_READ | PROT_WRITE; }
    *addr = mmap(NULL, amt, access_mask, MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
#endif
    return 1;
}

unsigned char MemUtils__free(void* page_addr, size_t num) {
#ifdef TARGET_OS_WINDOWS
    VirtualFree(page_addr, 0, MEM_RELEASE);
#elif TARGET_OS_LINUX || TARGET_OS_OSX
    munmap(page_addr, num);
#endif
    return 1;
}


unsigned char MemUtils__get_function_address(const char* lib_name, const char* func_name, void** func_address) {
    if (!func_address) { return 0; }
    *func_address = NULL;
#ifdef TARGET_OS_WINDOWS
    HMODULE hl = GetModuleHandleA(lib_name);
    if (!hl) {
        // Force load the library if it hasn't been loaded already.
        hl = LoadLibraryA(lib_name);
    }
    if(!hl){return 0;}
    *func_address = GetProcAddress(hl, func_name);

#elif TARGET_OS_LINUX || TARGET_OS_OSX
    void* lib_handle = dlopen(lib_name, RTLD_LOCAL | RTLD_LAZY);
    *func_address = dlsym(lib_handle, func_name);
#endif
    return 1;
}

unsigned char MemUtils__heap_alloc_exec_page(void** page_addr) {
    size_t page_sz = PAGE_SIZE;
    *page_addr = NULL;
    if (!MemUtils__alloc(PAGE_SIZE, 1, page_addr)) { return 0; }
    return 1;
}

unsigned char MemUtils__heap_clear_exec_page(void* page_addr) {
    memset(page_addr, 0x00, PAGE_SIZE);
    if (!MemUtils__free(page_addr, PAGE_SIZE)) { return 0; }
    return 1;
}

unsigned char MemUtils__set_permission(void* target_addr, size_t data_len, unsigned char enable_read, unsigned char enable_write, unsigned char enable_execute, unsigned int* old_prot) {
    // Default to PAGE_SIZE if no len.
    if (!data_len) { data_len = PAGE_SIZE; }
    unsigned int n_prot = 0;

#ifdef TARGET_OS_WINDOWS
    if (*old_prot) {
        n_prot = *old_prot;
    }
    else {
        if (enable_write && enable_execute) { n_prot = PAGE_EXECUTE_READWRITE; }
        else if (enable_write && enable_read && !enable_execute) { n_prot = PAGE_READWRITE; }
        else { n_prot = PAGE_READONLY; } // IDK
    }
    VirtualProtect(target_addr, data_len, n_prot, (PDWORD)old_prot);

#else
    // TODO: Get Old Protection Flags
    if (enable_write) { n_prot |= PROT_WRITE; }
    if (enable_read) { n_prot |= PROT_READ; }
    if (enable_execute) { n_prot |= PROT_EXEC; }
    mprotect((void*)(target_addr - ((size_t)target_addr % PAGE_SIZE)), data_len, n_prot);

#endif
    return 1;
}

// TODO: Set Failure conditions.
unsigned char MemUtils__patch_memory(void* target_addr, void* data_ptr, size_t data_len, unsigned char is_write, unsigned char is_exec) {
    unsigned int old_prot = 0;
    if (!is_write) {
        if (!MemUtils__set_permission(target_addr, data_len, 1, 1, is_exec, &old_prot)) { return 0; }
    }

    memcpy(target_addr, data_ptr, data_len);
    if (!is_write) {
        if (!MemUtils__set_permission(target_addr, data_len, 1, 0, is_exec, &old_prot)) { return 0; }
    }

    return 1;
}

unsigned char MemUtils__patch_ret0(void* target_addr) {
    unsigned char x86_ret_0[3] = { 0x31,0xC0, 0xC3 };
    return MemUtils__patch_memory(target_addr, x86_ret_0, sizeof(x86_ret_0), 0, 1);
}

unsigned char MemUtils__patch_ret1(void* target_addr) {
    unsigned char x86_ret_1[4] = { 0x31,0xC0, 0x40, 0xC3 };
    return MemUtils__patch_memory(target_addr, x86_ret_1, sizeof(x86_ret_1), 0, 1);
}

unsigned char MemUtils__unhotpatch_function(struct HotPatch_Info* ctx) {
    MemUtils__patch_memory(ctx->target_function_address, ctx->target_original_bytes, ctx->target_original_bytes_size, 0, 1);
    return 1;
}

unsigned char MemUtils__hotpatch_function(void* target_function_address, void* replacement_function_address, size_t target_original_bytes_size, struct HotPatch_Info* ctx, void** ptrampoline_address) {

    if (!ptrampoline_address || !target_function_address || !replacement_function_address) {return 0; }


    unsigned char hook_stub[sizeof(hotpatch_stub)] = { 0 };
    unsigned char trampoline_stub[sizeof(hotpatch_stub)] = { 0 };

    memcpy(hook_stub, hotpatch_stub, sizeof(hotpatch_stub));
    memcpy(trampoline_stub, hotpatch_stub, sizeof(hotpatch_stub));
    ctx->target_function_address = target_function_address;
    ctx->replacement_function_address = replacement_function_address;
    ctx->target_original_bytes_size = target_original_bytes_size;

    // We'll default the original bytes size to the size of our stub if it's not specified.
    if (!ctx->target_original_bytes_size) {
        ctx->target_original_bytes_size = sizeof(hotpatch_stub);
    }
    ctx->target_original_bytes = NULL;
    MemUtils__alloc(ctx->target_original_bytes_size, 0, (void**)&ctx->target_original_bytes);

    // Create our Hook Stub - Backup instructions we're about to modify.
    memcpy(hook_stub + HOTPATCH_ADDRESS_OFFSET, &ctx->replacement_function_address, sizeof(void*));
    memcpy(ctx->target_original_bytes, ctx->target_function_address, ctx->target_original_bytes_size);

    // Allocate an executable page for our trampoline.
    if (!MemUtils__heap_alloc_exec_page(ptrampoline_address)) { return 0; }
    ctx->trampoline_address = *ptrampoline_address;
    // Write our stolen instructions to the head of the trampoline.
    memcpy(ctx->trampoline_address, ctx->target_original_bytes, ctx->target_original_bytes_size);

    // Calculate what address we're coming back to (the original offset post jump stub).
    unsigned char* actual_return_address = ctx->target_function_address;
    actual_return_address += ctx->target_original_bytes_size;
    memcpy(trampoline_stub + HOTPATCH_ADDRESS_OFFSET, &actual_return_address, sizeof(void*));

    // Write this to our return trampoline.
    memcpy((unsigned char*)ctx->trampoline_address + ctx->target_original_bytes_size, trampoline_stub, sizeof(hotpatch_stub));

    ctx->trampoline_size = ctx->target_original_bytes_size + sizeof(hotpatch_stub);

    // Finally, write the hook stub.
    if (!MemUtils__patch_memory(ctx->target_function_address, hook_stub, sizeof(hotpatch_stub), 0, 1)) { return 0; }

    return 1;
}
