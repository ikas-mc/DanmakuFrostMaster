#pragma once
#include <string>
#include <windows.h>

namespace DanmakuFrostMasterXx
{
    class DanmakuLogger
    {
    public:
        static void Log(const std::wstring& message) noexcept
        {
            std::wstring output = L"[DanmakuFrostMasterXx] " + message + L"\n";
            ::OutputDebugStringW(output.c_str());
        }

        static void Log(const wchar_t* message) noexcept
        {
            std::wstring output = std::wstring(L"[DanmakuFrostMasterXx] ") + message + L"\n";
            ::OutputDebugStringW(output.c_str());
        }

        static void Log(const char* message) noexcept
        {
            Log(winrt::to_hstring(message).data());
        }
    };
}
