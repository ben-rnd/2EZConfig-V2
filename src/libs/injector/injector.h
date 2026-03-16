#pragma once
#include <windows.h>
#include <vector>
#include <string>

namespace Injector {
    int LaunchAndInject(const char* exeName, const std::vector<std::string>& extraDlls = {});
    int InjectRunningProcess(const char* processName, const char* dllPath = "2EZ.dll");
    DWORD FindProcess(const char* processName);
}
