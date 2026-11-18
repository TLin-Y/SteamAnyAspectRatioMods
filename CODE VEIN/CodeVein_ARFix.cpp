#include "stdafx.h"
#include "helper.hpp"
#include <safetyhook.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <memory>

namespace
{
    static_assert(sizeof(void*) == 8, "Build for Windows x64.");
    constexpr char fixName[] = "CodeVein_ARFix";
    constexpr char constrainPattern[] =
        "F6 41 2C 01 4C 89 CB 0F 29 78 ?? 48 89 CF F3 0F 10 3D ?? ?? ?? ?? "
        "44 0F 29 48 ?? 45 0F 57 C9 74 ?? F3 0F 10 51 28";
    constexpr size_t constrainBranchOffset = 0x1F;
    constexpr char fovPattern[] =
        "F3 0F 10 51 18 F3 0F 5F 15 ?? ?? ?? ?? 41 8B 81 98 00 00 00 "
        "41 8B 89 9C 00 00 00 41 2B 81 90 00 00 00";
    constexpr size_t fovHookOffset = 0x0D;
    constexpr float degToRad = 3.14159265358979f / 180.0f;

    HMODULE ownModule = nullptr;
    std::shared_ptr<spdlog::logger> logFile;
    SafetyHookMid fovHook;
    uintptr_t imageBase = 0;
    size_t imageSize = 0;
    float fovMultiplier = 1.0f;
    bool logStats = false;
    bool borderless = false;
    std::atomic<uint64_t> calls{0};
    std::atomic<uint32_t> lastWidth{0}, lastHeight{0}, lastAxis{0};
    std::atomic<float> lastAspect{0}, lastFovIn{0}, lastFovOut{0};

    void Notify(const char* message)
    {
        OutputDebugStringA(message);
        MessageBoxA(nullptr, message, fixName, MB_OK | MB_ICONERROR);
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

    void AdjustFov(SafetyHookContext& ctx)
    {
        const auto* view = reinterpret_cast<const uint8_t*>(ctx.rdi);
        const auto* projection = reinterpret_cast<const int32_t*>(ctx.r9 + 0x90);
        if (view[0x30] != 0)
            return;
        const int32_t width = projection[2] - projection[0];
        const int32_t height = projection[3] - projection[1];
        if (width <= 0 || height <= 0)
            return;
        const uint8_t axis = *reinterpret_cast<const uint8_t*>(ctx.rdx);
        float cameraAspect = 0;
        std::memcpy(&cameraAspect, view + 0x28, sizeof(cameraAspect));
        const float screenAspect = static_cast<float>(width) / static_cast<float>(height);
        if (!(cameraAspect > 0.1f && cameraAspect < 10.0f))
            cameraAspect = screenAspect;

        float fovIn = 0;
        std::memcpy(&fovIn, &ctx.xmm2.f32[0], sizeof(fovIn));
        const float fov = (std::min)(fovIn * fovMultiplier, 170.0f);
        float tanHorizontal = std::tan(fov * 0.5f * degToRad);
        if (screenAspect > cameraAspect)
            tanHorizontal *= screenAspect / cameraAspect;
        const bool horizontalAxis = (width > height && axis == 2) || axis == 1;
        const float tanOut = horizontalAxis ? tanHorizontal : tanHorizontal / screenAspect;
        const float fovOut = (std::min)(2.0f * std::atan(tanOut) / degToRad, 175.0f);
        ctx.xmm2.f32[0] = fovOut;

        if (logStats)
        {
            calls.fetch_add(1, std::memory_order_relaxed);
            lastWidth = width;
            lastHeight = height;
            lastAxis = axis;
            lastAspect = cameraAspect;
            lastFovIn = fovIn;
            lastFovOut = fovOut;
        }
    }

    DWORD WINAPI StatsLoop(void*)
    {
        uint64_t previous = 0;
        for (;;)
        {
            Sleep(5000);
            const auto now = calls.load();
            logFile->info("Projection calls={} view={}x{} cameraAR={:.4f} axis={} fov {:.2f} -> {:.2f}",
                          now - previous, lastWidth.load(), lastHeight.load(), lastAspect.load(),
                          lastAxis.load(), lastFovIn.load(), lastFovOut.load());
            previous = now;
        }
    }

    BOOL CALLBACK FindWindowCallback(HWND window, LPARAM parameter)
    {
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        if (process != GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window, GW_OWNER))
            return TRUE;
        wchar_t className[128]{};
        const int classLength = GetClassNameW(window, className, static_cast<int>(std::size(className)));
        constexpr wchar_t gameSuffix[] = L"-CodeVein";
        constexpr int suffixLength = static_cast<int>(std::size(gameSuffix)) - 1;
        const bool gameClass = classLength > suffixLength &&
            std::wcscmp(className + classLength - suffixLength, gameSuffix) == 0;
        if (!gameClass && std::wcscmp(className, L"UnrealWindow") != 0)
            return TRUE;
        *reinterpret_cast<HWND*>(parameter) = window;
        return FALSE;
    }

    void MakeBorderless(HWND window)
    {
        constexpr LONG_PTR frame = WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
        constexpr LONG_PTR frameEx = WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE | WS_EX_STATICEDGE;
        const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
        if (style & WS_MAXIMIZE)
            return;
        const bool framed = (style & frame) != 0;
        RECT client{};
        if (!GetClientRect(window, &client) || client.right <= 0 || client.bottom <= 0)
            return;
        MONITORINFO info{sizeof(info)};
        if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info))
            return;
        const LONG monitorWidth = info.rcMonitor.right - info.rcMonitor.left;
        const LONG monitorHeight = info.rcMonitor.bottom - info.rcMonitor.top;
        const LONG x = info.rcMonitor.left + (std::max)(0L, (monitorWidth - client.right) / 2);
        const LONG y = info.rcMonitor.top + (std::max)(0L, (monitorHeight - client.bottom) / 2);
        RECT current{};
        if (!framed && GetWindowRect(window, &current) && current.left == x && current.top == y &&
            current.right - current.left == client.right && current.bottom - current.top == client.bottom)
            return;
        if (framed)
        {
            SetWindowLongPtrW(window, GWL_STYLE, style & ~frame);
            SetWindowLongPtrW(window, GWL_EXSTYLE, GetWindowLongPtrW(window, GWL_EXSTYLE) & ~frameEx);
        }
        SetWindowPos(window, nullptr, x, y, client.right, client.bottom,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        if (framed)
            logFile->info("Borderless window {}x{} at {},{}", client.right, client.bottom, x, y);
    }

    DWORD WINAPI BorderlessLoop(void*)
    {
        for (;;)
        {
            HWND window = nullptr;
            EnumWindows(FindWindowCallback, reinterpret_cast<LPARAM>(&window));
            if (window && !IsIconic(window))
                MakeBorderless(window);
            Sleep(1000);
        }
    }

    bool InstallFovHook(uintptr_t constrainSite)
    {
        const auto site = Find("FOV", fovPattern);
        if (!site)
            return false;
        const auto* branch = reinterpret_cast<const uint8_t*>(constrainSite + constrainBranchOffset);
        const auto target = constrainSite + constrainBranchOffset + 2 + static_cast<int8_t>(branch[1]);
        if (target != site)
        {
            logFile->error("FOV site is not the unconstrained branch");
            return false;
        }
        auto hook = SafetyHookMid::create(reinterpret_cast<void*>(site + fovHookOffset), AdjustFov);
        if (!hook)
        {
            logFile->error("FOV hook failed: {}", static_cast<unsigned>(hook.error().type));
            return false;
        }
        fovHook = std::move(*hook);
        logFile->info("FOV hook at game+0x{:X}", site + fovHookOffset - imageBase);
        return true;
    }

    DWORD WINAPI Setup(void*)
    {
        wchar_t executable[32768]{};
        const auto exeLength = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        if (!exeLength || exeLength >= std::size(executable) ||
            _wcsicmp(std::filesystem::path(executable).filename().c_str(),
                     L"CodeVein-Win64-Shipping.exe") != 0)
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
            logFile = spdlog::rotating_logger_mt(fixName, (folder / L"CodeVein_ARFix.log").string(),
                                                 1024 * 1024, 1, true);
            logFile->flush_on(spdlog::level::info);
        }
        catch (const spdlog::spdlog_ex& error)
        {
            Notify(error.what());
            return 0;
        }
        const auto ini = (folder / L"CodeVein_ARFix.ini").wstring();
        char value[64]{};
        const auto valueLength = GetPrivateProfileStringA("FOV", "Multiplier", "1.0", value,
                                                          static_cast<DWORD>(std::size(value)),
                                                          (folder / L"CodeVein_ARFix.ini").string().c_str());
        const auto parsed = std::from_chars(value, value + valueLength, fovMultiplier);
        if (parsed.ec != std::errc{} || !(fovMultiplier >= 0.5f && fovMultiplier <= 2.0f))
        {
            logFile->warn("FOV Multiplier must be 0.5-2.0; using 1.0");
            fovMultiplier = 1.0f;
        }
        logStats = GetPrivateProfileIntW(L"Debug", L"LogStats", 0, ini.c_str()) != 0;
        wchar_t flag[16]{};
        GetPrivateProfileStringW(L"Display", L"Borderless", L"false", flag, static_cast<DWORD>(std::size(flag)), ini.c_str());
        borderless = _wcsicmp(flag, L"true") == 0 || std::wcscmp(flag, L"1") == 0;
        logFile->info("{} {}.{}.{}: FOV Multiplier={:.3f}, Borderless={}", fixName, VERSION_MAJOR, VERSION_MINOR,
                      VERSION_PATCH, fovMultiplier, borderless);
        if (borderless)
            if (const HANDLE thread = CreateThread(nullptr, 0, BorderlessLoop, nullptr, 0, nullptr))
                CloseHandle(thread);

        imageBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(imageBase);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(imageBase + dos->e_lfanew);
        imageSize = nt->OptionalHeader.SizeOfImage;

        const auto constrainSite = Find("Constrain", constrainPattern);
        if (!constrainSite || !InstallFovHook(constrainSite) || !RemoveBars(constrainSite))
        {
            Notify("CodeVein_ARFix: unsupported game version. See CodeVein_ARFix.log.");
            return 0;
        }
        if (logStats)
            if (const HANDLE thread = CreateThread(nullptr, 0, StatsLoop, nullptr, 0, nullptr))
                CloseHandle(thread);
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
