#include <windows.h>
#include <string.h>
#include <winternl.h>

//#include "nativecore/debug.h"
//#include "nativecore/mem.h"
#include "io.hardlock.emulator.h"
#include "io.hardlock.hooks.h"
#include "hooks.h"

typedef NTSTATUS __stdcall tNtCreateFile(PHANDLE FileHandle, DWORD DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength);
typedef NTSTATUS __stdcall tNtDeviceIoControlFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, ULONG IoControlCode, PVOID InputBuffer, ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength);
static tNtCreateFile* ntdll_NtCreateFile = 0;
static tNtDeviceIoControlFile* ntdll_NtDeviceIoControlFile = 0;

PEMULATED_HARDLOCK EHardLock = NULL;

#define FAKE_HARDLOCK_HANDLE (HANDLE)0x1337



NTSTATUS NTAPI x_NtCreateFile(PHANDLE FileHandle, DWORD DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength) {
    if (ObjectAttributes && ObjectAttributes->ObjectName && ObjectAttributes->ObjectName->Buffer) {
        DBG_printfW(L"[io.hardlock]: NtCreateFile :%s",ObjectAttributes->ObjectName->Buffer);
        if (wcsstr(ObjectAttributes->ObjectName->Buffer, L"EnteDev")) {
            *FileHandle = FAKE_HARDLOCK_HANDLE;
            return 0;
        }
    }

    return ntdll_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
}


NTSTATUS NTAPI x_NtDeviceIoControlFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, ULONG IoControlCode, PVOID InputBuffer, ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength) {
    if (FileHandle != FAKE_HARDLOCK_HANDLE) {
        return ntdll_NtDeviceIoControlFile(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, IoControlCode, InputBuffer, InputBufferLength, OutputBuffer, OutputBufferLength);
    }

    ProcessHardlockIoctlWindows(IoControlCode, InputBuffer, InputBufferLength, OutputBuffer, OutputBufferLength);
    return TRUE;
}



int InitHooks() {
    hook_create_api(L"ntdll.dll", "NtCreateFile",          (void*)x_NtCreateFile,          (void**)&ntdll_NtCreateFile);
    hook_create_api(L"ntdll.dll", "NtDeviceIoControlFile", (void*)x_NtDeviceIoControlFile, (void**)&ntdll_NtDeviceIoControlFile);
    return TRUE;
}
