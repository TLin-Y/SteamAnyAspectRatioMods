#include "stdafx.h"
#include "helper.hpp"
#include <safetyhook.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <atomic>
#include <charconv>
#include <cmath>
#include <memory>

namespace
{
    static_assert(sizeof(void*) == 8, "Build for Windows x64.");
    constexpr char fixName[] = "Palworld_ARFix";
    constexpr char projectionPattern[] =
        "F3 0F 10 43 30 F3 0F 5F 05 ?? ?? ?? ?? F3 0F 59 05 ?? ?? ?? ?? E8 ?? ?? ?? ?? "
        "F3 41 0F 5E C5 E8 ?? ?? ?? ?? F3 0F 10 4B 44";
    constexpr size_t projectionHookOffset = 0x24;
    constexpr char uiScalePattern[] =
        "F3 0F 59 86 C0 01 00 00 48 8B 5C 24 40 48 8B 74 24 48 F3 0F 5F 05";
    constexpr float referenceAspect = 16.0f / 9.0f;
    constexpr float degToRad = 3.14159265358979f / 180.0f;

    HMODULE ownModule = nullptr;
    std::shared_ptr<spdlog::logger> logFile;
    SafetyHookMid fovHook;
    SafetyHookMid uiScaleHook;
    float uiScale = 1.0f;
    uintptr_t imageBase = 0;
    size_t imageSize = 0;
    int targetWidth = 1280;
    int targetHeight = 800;
    float fovMultiplier = 1.0f;
    bool logStats = false;
    std::atomic<uint64_t> calls{0};
    std::atomic<int32_t> lastWidth{0}, lastHeight{0};
    std::atomic<bool> lastMaintainX{false};
    std::atomic<float> lastFovIn{0}, lastFovOut{0};

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

    void AdjustFov(SafetyHookContext& ctx)
    {
        const auto* rect = reinterpret_cast<const int32_t*>(ctx.rdi + 0x120);
        const int32_t width = rect[2] - rect[0];
        const int32_t height = rect[3] - rect[1];
        if (width <= 0 || height <= 0)
            return;
        const float halfIn = ctx.xmm0.f32[0];
        if (!(halfIn > 0.0f && halfIn < 1.57f))
            return;
        const bool maintainX = ctx.xmm11.f32[0] == 1.0f;
        float tanReference = std::tan(halfIn);
        if (!maintainX)
            tanReference *= referenceAspect;
        const float screenAspect = static_cast<float>(width) / static_cast<float>(height);
        const float fovReference = 2.0f * std::atan(tanReference);
        const float fov = (std::min)(fovReference * fovMultiplier, 170.0f * degToRad);
        const float tanHorizontal = std::tan(fov * 0.5f) * (std::max)(1.0f, screenAspect / referenceAspect);
        const float halfOut = std::atan(tanHorizontal);
        ctx.xmm0.f32[0] = halfOut;
        ctx.xmm11.f32[0] = 1.0f;
        ctx.xmm7.f32[0] = screenAspect;

        if (logStats)
        {
            calls.fetch_add(1, std::memory_order_relaxed);
            lastWidth = width;
            lastHeight = height;
            lastMaintainX = maintainX;
            lastFovIn = fovReference / degToRad;
            lastFovOut = 2.0f * halfOut / degToRad;
        }
    }

    void ScaleUi(SafetyHookContext& ctx)
    {
        ctx.xmm0.f32[0] *= uiScale;
    }

    bool InstallUiScaleHook()
    {
        const auto site = Find("UI scale", uiScalePattern);
        if (!site)
            return false;
        auto hook = SafetyHookMid::create(reinterpret_cast<void*>(site), ScaleUi);
        if (!hook)
        {
            logFile->error("UI scale hook failed: {}", static_cast<unsigned>(hook.error().type));
            return false;
        }
        uiScaleHook = std::move(*hook);
        logFile->info("UI scale hook at game+0x{:X}", site - imageBase);
        return true;
    }

    DWORD WINAPI StatsLoop(void*)
    {
        uint64_t previous = 0;
        for (;;)
        {
            Sleep(5000);
            const auto now = calls.load();
            logFile->info("Projection calls={} view={}x{} axis={} 16:9 hfov {:.2f} -> hfov {:.2f}",
                          now - previous, lastWidth.load(), lastHeight.load(),
                          lastMaintainX.load() ? "X" : "Y", lastFovIn.load(), lastFovOut.load());
            previous = now;
        }
    }

    BOOL CALLBACK ResizeWindowCallback(HWND window, LPARAM)
    {
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        if (process != GetCurrentProcessId() || GetParent(window) || !IsWindowVisible(window))
            return TRUE;
        LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
        style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
        SetWindowLongPtrW(window, GWL_STYLE, style);
        const int x = (std::max)(0, (GetSystemMetrics(SM_CXSCREEN) - targetWidth) / 2);
        const int y = (std::max)(0, (GetSystemMetrics(SM_CYSCREEN) - targetHeight) / 2);
        SetWindowPos(window, HWND_TOP, x, y, targetWidth, targetHeight, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
        return FALSE;
    }

    DWORD WINAPI WindowLoop(void*)
    {
        for (int i = 0; i < 10; ++i)
        {
            Sleep(1000);
            EnumWindows(ResizeWindowCallback, 0);
        }
        logFile->info("Window set to {}x{}", targetWidth, targetHeight);
        return 0;
    }

    bool InstallFovHook()
    {
        const auto site = Find("Projection", projectionPattern);
        if (!site)
            return false;
        auto hook = SafetyHookMid::create(reinterpret_cast<void*>(site + projectionHookOffset), AdjustFov);
        if (!hook)
        {
            logFile->error("FOV hook failed: {}", static_cast<unsigned>(hook.error().type));
            return false;
        }
        fovHook = std::move(*hook);
        logFile->info("FOV hook at game+0x{:X}", site + projectionHookOffset - imageBase);
        return true;
    }

    DWORD WINAPI Setup(void*)
    {
        wchar_t executable[32768]{};
        const auto exeLength = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        if (!exeLength || exeLength >= std::size(executable) ||
            _wcsicmp(std::filesystem::path(executable).filename().c_str(),
                     L"Palworld-Win64-Shipping.exe") != 0)
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
            logFile = spdlog::rotating_logger_mt(fixName, (folder / L"Palworld_ARFix.log").string(),
                                                 1024 * 1024, 1, true);
            logFile->flush_on(spdlog::level::info);
        }
        catch (const spdlog::spdlog_ex& error)
        {
            Notify(error.what());
            return 0;
        }
        const auto ini = (folder / L"Palworld_ARFix.ini").wstring();
        targetWidth = GetPrivateProfileIntW(L"Resolution", L"TargetWidth", 1280, ini.c_str());
        targetHeight = GetPrivateProfileIntW(L"Resolution", L"TargetHeight", 800, ini.c_str());
        if (targetWidth <= 0 || targetHeight <= 0)
        {
            logFile->warn("Invalid TargetWidth/TargetHeight; using 1280x800");
            targetWidth = 1280;
            targetHeight = 800;
        }
        char value[64]{};
        const auto valueLength = GetPrivateProfileStringA("FOV", "Multiplier", "1.0", value,
                                                          static_cast<DWORD>(std::size(value)),
                                                          (folder / L"Palworld_ARFix.ini").string().c_str());
        const auto parsed = std::from_chars(value, value + valueLength, fovMultiplier);
        if (parsed.ec != std::errc{} || !(fovMultiplier >= 0.5f && fovMultiplier <= 2.0f))
        {
            logFile->warn("FOV Multiplier must be 0.5-2.0; using 1.0");
            fovMultiplier = 1.0f;
        }
        const auto scaleLength = GetPrivateProfileStringA("UI", "Scale", "1.0", value,
                                                          static_cast<DWORD>(std::size(value)),
                                                          (folder / L"Palworld_ARFix.ini").string().c_str());
        const auto scaleParsed = std::from_chars(value, value + scaleLength, uiScale);
        if (scaleParsed.ec != std::errc{} || !(uiScale >= 0.5f && uiScale <= 2.0f))
        {
            logFile->warn("UI Scale must be 0.5-2.0; using 1.0");
            uiScale = 1.0f;
        }
        logStats = GetPrivateProfileIntW(L"Debug", L"LogStats", 0, ini.c_str()) != 0;
        logFile->info("{} {}.{}.{}: Resolution={}x{}, FOV Multiplier={:.3f}, UI Scale={:.3f}", fixName,
                      VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, targetWidth, targetHeight, fovMultiplier,
                      uiScale);
        if (const HANDLE thread = CreateThread(nullptr, 0, WindowLoop, nullptr, 0, nullptr))
            CloseHandle(thread);

        imageBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(imageBase);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(imageBase + dos->e_lfanew);
        imageSize = nt->OptionalHeader.SizeOfImage;

        if (!InstallFovHook())
        {
            Notify("Palworld_ARFix: unsupported game version. See Palworld_ARFix.log.");
            return 0;
        }
        if (uiScale != 1.0f && !InstallUiScaleHook())
            logFile->warn("UI Scale is not applied");
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
