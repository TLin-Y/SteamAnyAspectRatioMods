#include "stdafx.h"
#include "helper.hpp"
#include <safetyhook.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <cstring>
#include <cwchar>
#include <memory>

namespace
{
    static_assert(sizeof(void*) == 8, "Build for Windows x64.");
    constexpr char fixName[] = "DSClient_ARFix";
    constexpr char constrainPattern[] =
        "F6 41 54 01 49 8B D9 44 0F 29 48 ?? 44 0F B6 CA "
        "F2 44 0F 10 0D ?? ?? ?? ?? 48 8B F9 44 0F 29 50 ?? 45 0F 57 D2 74 ?? 49 8B 00";
    constexpr size_t constrainBranchOffset = 0x25;
    constexpr char axisPattern[] =
        "80 79 50 00 44 0F 29 A4 24 ?? ?? ?? ?? 74 05 44 0F B6 49 4C 0F 29 B4 24";
    constexpr size_t axisHookOffset = 0x14;
    constexpr char legacyFovPattern[] = "48 8B 05 ?? ?? ?? ?? 80 38 00 0F 85";
    constexpr size_t legacyFovSearch = 0x100;

    HMODULE ownModule = nullptr;
    std::shared_ptr<spdlog::logger> logFile;
    SafetyHookMid axisHook;
    uintptr_t imageBase = 0;
    size_t imageSize = 0;
    int targetWidth = 1280;
    int targetHeight = 800;
    bool keepHorizontalFov = true;
    const uint8_t* const* legacyFovFlag = nullptr;

    void Notify(const char* message)
    {
        OutputDebugStringA(message);
        MessageBoxA(nullptr, message, fixName, MB_OK | MB_ICONERROR);
    }

    bool Matches(uintptr_t address, const char* signature)
    {
        const auto pattern = Memory::PatternToBytes(signature);
        const auto* bytes = reinterpret_cast<const uint8_t*>(address);
        for (size_t i = 0; i < pattern.size(); ++i)
            if (pattern[i] >= 0 && bytes[i] != pattern[i])
                return false;
        return true;
    }

    uintptr_t FindUnique(const char* signature, bool& ambiguous)
    {
        ambiguous = false;
        const auto pattern = Memory::PatternToBytes(signature);
        uintptr_t found = 0;
        const auto end = imageBase + imageSize;
        for (auto cursor = imageBase; cursor < end;)
        {
            MEMORY_BASIC_INFORMATION region{};
            if (!VirtualQuery(reinterpret_cast<void*>(cursor), &region, sizeof(region)))
                return 0;
            const auto regionEnd = (std::min)(end,
                reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize);
            if (regionEnd <= cursor)
                return 0;
            const DWORD protection = region.Protect & 0xff;
            const bool executable = protection == PAGE_EXECUTE_READ ||
                protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
            if (region.State != MEM_COMMIT || !executable || (region.Protect & PAGE_GUARD))
            {
                cursor = regionEnd;
                continue;
            }
            const auto* bytes = reinterpret_cast<const uint8_t*>(cursor);
            const size_t size = regionEnd - cursor;
            for (size_t i = 0; i + pattern.size() <= size; ++i)
            {
                if (bytes[i] != pattern[0])
                    continue;
                size_t j = 1;
                while (j < pattern.size() && (pattern[j] < 0 || bytes[i + j] == pattern[j]))
                    ++j;
                if (j != pattern.size())
                    continue;
                if (found)
                {
                    ambiguous = true;
                    return 0;
                }
                found = cursor + i;
            }
            cursor = regionEnd;
        }
        return found;
    }

    uintptr_t Find(const char* name, const char* signature)
    {
        uintptr_t address = 0;
        bool ambiguous = false;
        for (int attempt = 0; attempt < 120 && !address && !ambiguous; ++attempt)
        {
            address = FindUnique(signature, ambiguous);
            if (!address && !ambiguous)
                Sleep(250);
        }
        if (address)
            logFile->info("{} signature: game+0x{:X}", name, address - imageBase);
        else
            logFile->error("{} signature {}", name, ambiguous ? "is ambiguous" : "not found");
        return address;
    }

    bool RemoveBars(uintptr_t site)
    {
        auto* branch = reinterpret_cast<uint8_t*>(site + constrainBranchOffset);
        DWORD oldProtect = 0;
        if (!VirtualProtect(branch, 1, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            logFile->error("VirtualProtect failed: {}", GetLastError());
            return false;
        }
        *branch = 0xEB;
        VirtualProtect(branch, 1, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), branch, 1);
        logFile->info("Aspect-ratio constraint disabled at game+0x{:X}", site + constrainBranchOffset - imageBase);
        return true;
    }

    void SelectAxis(SafetyHookContext& ctx)
    {
        if (static_cast<uint8_t>(ctx.r9) != 0)
            return;
        const auto* view = reinterpret_cast<const uint8_t*>(ctx.rdi);
        if (view[0x58] != 0 || !*legacyFovFlag || **legacyFovFlag != 0)
            return;
        float cameraAspect = 0;
        std::memcpy(&cameraAspect, view + 0x48, sizeof(cameraAspect));
        const auto width = static_cast<int32_t>(ctx.rax);
        const auto height = static_cast<int32_t>(ctx.rdx);
        if (!(cameraAspect > 0.0f) || width <= 0 || height <= 0)
            return;
        if (static_cast<float>(width) < static_cast<float>(height) * cameraAspect)
            ctx.r9 = 1;
    }

    bool InstallAxisHook(uintptr_t constrainSite)
    {
        const auto site = Find("Axis", axisPattern);
        if (!site)
            return false;
        uintptr_t legacy = 0;
        for (size_t offset = 0; offset < legacyFovSearch && !legacy; ++offset)
            if (Matches(site + offset, legacyFovPattern))
                legacy = site + offset;
        if (site < constrainSite || site - constrainSite > 0x200 || !legacy)
        {
            logFile->error("Axis contracts not verified");
            return false;
        }
        legacyFovFlag = reinterpret_cast<const uint8_t* const*>(
            Memory::GetRelativeAddr(reinterpret_cast<uint8_t*>(legacy + 3)));
        auto hook = SafetyHookMid::create(reinterpret_cast<void*>(site + axisHookOffset), SelectAxis);
        if (!hook)
        {
            logFile->error("Axis hook failed: {}", static_cast<unsigned>(hook.error().type));
            return false;
        }
        axisHook = std::move(*hook);
        logFile->info("Horizontal FOV kept on narrow screens: game+0x{:X}", site + axisHookOffset - imageBase);
        return true;
    }

    BOOL CALLBACK FindWindowCallback(HWND window, LPARAM parameter)
    {
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        if (process != GetCurrentProcessId() || !IsWindowVisible(window) ||
            GetWindow(window, GW_OWNER))
            return TRUE;
        wchar_t className[128]{};
        if (!GetClassNameW(window, className, static_cast<int>(std::size(className))) ||
            std::wcscmp(className, L"UnrealWindow") != 0)
            return TRUE;
        *reinterpret_cast<HWND*>(parameter) = window;
        return FALSE;
    }

    bool SetBorderless(HWND window)
    {
        SetLastError(ERROR_SUCCESS);
        LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
        if (!style && GetLastError() != ERROR_SUCCESS)
        {
            logFile->error("GetWindowLongPtr failed: {}", GetLastError());
            return false;
        }
        style &= ~(static_cast<LONG_PTR>(WS_CAPTION) | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
        SetLastError(ERROR_SUCCESS);
        if (!SetWindowLongPtrW(window, GWL_STYLE, style) && GetLastError() != ERROR_SUCCESS)
        {
            logFile->error("SetWindowLongPtr failed: {}", GetLastError());
            return false;
        }
        SetLastError(ERROR_SUCCESS);
        LONG_PTR extended = GetWindowLongPtrW(window, GWL_EXSTYLE);
        if (!extended && GetLastError() != ERROR_SUCCESS)
        {
            logFile->error("GetWindowLongPtr extended style failed: {}", GetLastError());
            return false;
        }
        extended &= ~(static_cast<LONG_PTR>(WS_EX_CLIENTEDGE) | WS_EX_WINDOWEDGE | WS_EX_STATICEDGE);
        SetLastError(ERROR_SUCCESS);
        if (!SetWindowLongPtrW(window, GWL_EXSTYLE, extended) && GetLastError() != ERROR_SUCCESS)
        {
            logFile->error("SetWindowLongPtr extended style failed: {}", GetLastError());
            return false;
        }
        MONITORINFO info{sizeof(info)};
        if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info))
        {
            logFile->error("GetMonitorInfo failed: {}", GetLastError());
            return false;
        }
        const int x = info.rcMonitor.left + ((info.rcMonitor.right - info.rcMonitor.left) - targetWidth) / 2;
        const int y = info.rcMonitor.top + ((info.rcMonitor.bottom - info.rcMonitor.top) - targetHeight) / 2;
        if (!SetWindowPos(window, nullptr, x, y, targetWidth, targetHeight,
                          SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED))
        {
            logFile->error("SetWindowPos failed: {}", GetLastError());
            return false;
        }
        return true;
    }

    void WindowLoop()
    {
        HWND window = nullptr;
        for (int i = 0; i < 120 && !window; ++i)
        {
            EnumWindows(FindWindowCallback, reinterpret_cast<LPARAM>(&window));
            if (!window)
                Sleep(1000);
        }
        if (!window)
        {
            logFile->error("No visible UnrealWindow found within 120 seconds");
            return;
        }
        if (!SetBorderless(window))
            return;
        bool mismatchLogged = false;
        bool initialLogged = false;
        while (IsWindow(window))
        {
            Sleep(1000);
            if (!IsWindowVisible(window) || IsIconic(window))
                continue;
            RECT client{};
            if (!GetClientRect(window, &client))
                return;
            const auto width = client.right - client.left;
            const auto height = client.bottom - client.top;
            if (width == targetWidth && height == targetHeight)
            {
                if (!initialLogged || mismatchLogged)
                    logFile->info("Window client={}x{}", width, height);
                initialLogged = true;
                mismatchLogged = false;
                continue;
            }
            if (!mismatchLogged)
            {
                logFile->warn("Window client={}x{}; restoring {}x{}", width, height, targetWidth, targetHeight);
                mismatchLogged = true;
            }
            if (!SetBorderless(window))
                return;
        }
    }

    DWORD WINAPI Setup(void*)
    {
        wchar_t executable[32768]{};
        const auto exeLength = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        if (!exeLength || exeLength >= std::size(executable) ||
            _wcsicmp(std::filesystem::path(executable).filename().c_str(),
                     L"DSClient-Win64-Shipping.exe") != 0)
            return 0;
        wchar_t modulePath[32768]{};
        const auto length = GetModuleFileNameW(ownModule, modulePath, static_cast<DWORD>(std::size(modulePath)));
        if (!length || length >= std::size(modulePath))
        {
            Notify("Cannot locate the fix module.");
            return 0;
        }
        const auto folder = std::filesystem::path(modulePath).parent_path();
        try
        {
            logFile = spdlog::rotating_logger_mt(fixName, (folder / L"DSClient_ARFix.log").string(),
                                                 1024 * 1024, 1, true);
            logFile->flush_on(spdlog::level::info);
        }
        catch (const spdlog::spdlog_ex& error)
        {
            Notify(error.what());
            return 0;
        }
        const auto ini = folder / L"DSClient_ARFix.ini";
        targetWidth = static_cast<int>(GetPrivateProfileIntW(L"Resolution", L"Width", 1280, ini.c_str()));
        targetHeight = static_cast<int>(GetPrivateProfileIntW(L"Resolution", L"Height", 800, ini.c_str()));
        keepHorizontalFov = GetPrivateProfileIntW(L"FOV", L"KeepHorizontalFOV", 1, ini.c_str()) != 0;
        const bool validSize = targetWidth >= 320 && targetWidth <= 16384 &&
                               targetHeight >= 240 && targetHeight <= 16384;
        logFile->info("{} {}.{}.{}: {}x{}, KeepHorizontalFOV={}", fixName, VERSION_MAJOR, VERSION_MINOR,
                      VERSION_PATCH, targetWidth, targetHeight, keepHorizontalFov);
        if (!validSize)
            logFile->error("Width must be 320-16384 and Height 240-16384; window override disabled");

        imageBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(imageBase);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(imageBase + dos->e_lfanew);
        imageSize = nt->OptionalHeader.SizeOfImage;

        const auto constrainSite = Find("Constrain", constrainPattern);
        if (!constrainSite || !RemoveBars(constrainSite))
            Notify("DSClient_ARFix: unsupported game version. See DSClient_ARFix.log.");
        else if (keepHorizontalFov && !InstallAxisHook(constrainSite))
            Notify("DSClient_ARFix: FOV hook failed. See DSClient_ARFix.log.");
        if (validSize)
            WindowLoop();
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        ownModule = module;
        DisableThreadLibraryCalls(module);
        const HANDLE thread = CreateThread(nullptr, 0, Setup, nullptr, 0, nullptr);
        if (!thread)
            return FALSE;
        CloseHandle(thread);
    }
    return TRUE;
}
