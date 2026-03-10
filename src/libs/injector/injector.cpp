#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>
#include "injector.h"

static BOOL SetPrivilege(HANDLE hToken, LPCTSTR lpszPrivilege, BOOL bEnable) {
    LUID luid;
    if (!LookupPrivilegeValue(NULL, lpszPrivilege, &luid))
        return FALSE;

    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = bEnable ? SE_PRIVILEGE_ENABLED : 0;

    AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, NULL);
    return (GetLastError() == ERROR_SUCCESS);
}

static int OsVersion() {
    OSVERSIONINFO osvi;
    ZeroMemory(&osvi, sizeof(OSVERSIONINFO));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    GetVersionEx(&osvi);
    return osvi.dwMajorVersion;
}

static void EnableDebugPrivilege() {
    // XP (major version 5) needs explicit debug privilege for process injection
    if (OsVersion() < 6) {
        HANDLE hToken;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken)) {
            SetPrivilege(hToken, SE_DEBUG_NAME, TRUE);
            CloseHandle(hToken);
            printf("[+] Debug privilege\n");
        }
    }
}

static DWORD GetProcId(const char* procName) {
    DWORD procId = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE)
        return 0;

    wchar_t wideName[MAX_PATH];
    mbstowcs(wideName, procName, MAX_PATH);

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);

    if (Process32First(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, wideName) == 0) {
                procId = pe.th32ProcessID;
                break;
            }
        } while (Process32Next(hSnap, &pe));
    }

    CloseHandle(hSnap);
    return procId;
}

static int InjectDll(HANDLE hProcess, const char* dllName) {
    FARPROC llAddr = GetProcAddress(GetModuleHandle(TEXT("kernel32.dll")), "LoadLibraryA");
    if (!llAddr){
        printf("[-] LoadLibraryA address not found");
        return 0;
    }

    size_t len = strlen(dllName) + 1;
    LPVOID dllAddr = VirtualAllocEx(hProcess, NULL, len, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!dllAddr){
        printf("[-] VirtualAllocEx failed");
        return 0;
    }

    if (!WriteProcessMemory(hProcess, dllAddr, dllName, len, NULL)) {
        VirtualFreeEx(hProcess, dllAddr, 0, MEM_RELEASE);
        printf("[-] WriteProcessMemory failed");
        return 0;
    }

    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)llAddr, dllAddr, 0, NULL);
    if (!hThread) {
        VirtualFreeEx(hProcess, dllAddr, 0, MEM_RELEASE);
        printf("[-] CreateRemoteThread failed\n");
        return 0;
    }

    printf("Injection successful [%s] | process pointer: %x \n", dllName, hThread);
    
    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
    return 1;
}

static int Inject(DWORD *pid){
    EnableDebugPrivilege();
    int result = 1;
    
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, *pid);
    if (!hProcess){
        printf("[-] OpenProcess failed\n");
        return 0;
    }

    result = InjectDll(hProcess, "2EZ.dll");
    if (!result){
        printf("[-] InjectDll failed\n");
        return 0;
    }

    CloseHandle(hProcess);
    return 1;
}

int Injector::LaunchAndInject(const char* exeName, const std::vector<std::string>& extraDlls) {
    char fullPath[MAX_PATH];
    if (!GetFullPathNameA(exeName, MAX_PATH, fullPath, NULL)) {
        printf("[-] GetFullPathNameA failed\n");
        return 0;
    }

    char gameDir[MAX_PATH];
    strncpy(gameDir, fullPath, MAX_PATH);
    char* lastSlash = strrchr(gameDir, '\\');
    if (lastSlash) *lastSlash = '\0';

    EnableDebugPrivilege();

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};

    if (!CreateProcessA(fullPath, NULL, NULL, NULL, FALSE,
                        CREATE_SUSPENDED, NULL, gameDir, &si, &pi)) {
        printf("[-] CreateProcessA failed (%lu)\n", GetLastError());
        return 0;
    }

    // Inject DLL while process is suspended — DllMain runs before any game code
    int result = InjectDll(pi.hProcess, "2EZ.dll");
    if (!result) {
        printf("[-] InjectDll failed\n");
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return 0;
    }

    for (const auto& dll : extraDlls) {
        if (!dll.empty())
            InjectDll(pi.hProcess, dll.c_str());
    }

    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 1;
}

int Injector::InjectRunningProcess(const char* processName) {
    DWORD pid = GetProcId(processName);
    if (!pid)
        return 0;

    return Inject(&pid);
}
