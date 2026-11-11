#pragma once

#include <winsock2.h>
#include <windows.h>
#include <string>

inline std::wstring windowsClientHostname()
{
    wchar_t name[256];
    DWORD length = sizeof(name) / sizeof(name[0]);
    if (!GetComputerNameExW(ComputerNameDnsHostname, name, &length)) return {};
    return {name, length};
}
