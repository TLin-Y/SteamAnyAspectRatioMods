#include "stdafx.h"
#include "version.h"
#include "helper.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <inipp/inipp.h>
#include <safetyhook.hpp>
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

namespace
{
    HMODULE exeModule = GetModuleHandle(nullptr);
    HMODULE thisModule = nullptr;

    constexpr char sConfigFile[] = "DW3RE_ARFix.ini";
    constexpr char fovPattern[] = "F3 0F 10 ?? 30 F3 0F 5F 05 ?? ?? ?? ?? F3 0F 59 05 ?? ?? ?? ??";
    constexpr size_t fovSiteCount = 3;
    constexpr size_t fovHookOffset = 5;
    constexpr size_t fovScaleOffset = 13;

    std::filesystem::path FixPath;
    std::filesystem::path ExePath;

    bool bForceWindow = true;
    int iTargetWidth = 1280;
    int iTargetHeight = 800;
    float fFovMultiplier = 1.0f;
    bool bLogStats = false;

    std::vector<SafetyHookMid> fovHooks;
    std::atomic<uint64_t> fovCalls{0};
    std::atomic<float> lastFovIn{0.0f};
    std::atomic<float> lastFovOut{0.0f};

    void Logging()
    {
        WCHAR dllPath[MAX_PATH] = {};
        GetModuleFileNameW(thisModule, dllPath, MAX_PATH);
        FixPath = dllPath;
        FixPath = FixPath.remove_filename();

        WCHAR gameExePath[MAX_PATH] = {};
        GetModuleFileNameW(exeModule, gameExePath, MAX_PATH);
        ExePath = gameExePath;
        ExePath = ExePath.remove_filename();

        try {
            std::ofstream file(ExePath / "DW3RE_ARFix.log", std::ios::trunc);
            if (file.is_open()) file.close();

            auto logger = std::make_shared<spdlog::logger>(
                "DW3RE_Fix",
                std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    (ExePath / "DW3RE_ARFix.log").string(), 10 * 1024 * 1024, 1));
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::info);
            spdlog::set_level(spdlog::level::info);

            spdlog::info("DW3RE_ARFix {}.{}.{} loaded", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
        } catch (...) {}
    }

    void Configuration()
    {
        inipp::Ini<char> ini;
        std::ifstream iniFile(FixPath / sConfigFile);
        if (!iniFile) {
            spdlog::warn("Config file not found! Using defaults.");
            return;
        }
        ini.parse(iniFile);
        ini.strip_trailing_comments();
        inipp::get_value(ini.sections["Resolution"], "Enable", bForceWindow);
        inipp::get_value(ini.sections["Resolution"], "Width", iTargetWidth);
        inipp::get_value(ini.sections["Resolution"], "Height", iTargetHeight);
        inipp::get_value(ini.sections["FOV"], "Multiplier", fFovMultiplier);
        inipp::get_value(ini.sections["Debug"], "LogStats", bLogStats);

        if (!(fFovMultiplier >= 0.5f && fFovMultiplier <= 2.0f)) {
            spdlog::warn("FOV Multiplier must be 0.5-2.0; using 1.0");
            fFovMultiplier = 1.0f;
        }
        spdlog::info("Config: window {} {}x{}, FOV Multiplier {:.3f}",
                     bForceWindow, iTargetWidth, iTargetHeight, fFovMultiplier);
    }

    std::vector<uintptr_t> FindInCode(const char* signature)
    {
        std::vector<uintptr_t> results;
        const auto pattern = Memory::PatternToBytes(signature);
        const auto base = reinterpret_cast<uint8_t*>(exeModule);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const auto* section = IMAGE_FIRST_SECTION(nt);

        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
            if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
            const uint8_t* bytes = base + section->VirtualAddress;
            const size_t size = section->Misc.VirtualSize;
            if (size < pattern.size()) continue;
            for (size_t offset = 0; offset + pattern.size() <= size; ++offset) {
                if (bytes[offset] != pattern[0]) continue;
                size_t j = 1;
                while (j < pattern.size() && (pattern[j] < 0 || bytes[offset + j] == pattern[j])) ++j;
                if (j == pattern.size()) results.push_back(reinterpret_cast<uintptr_t>(bytes + offset));
            }
        }
        return results;
    }

    float ReadRipFloat(uintptr_t instruction, size_t length)
    {
        const int32_t displacement = *reinterpret_cast<const int32_t*>(instruction + length - 4);
        return *reinterpret_cast<const float*>(instruction + length + displacement);
    }

    void ScaleFov(SafetyHookContext& ctx)
    {
        const float fovIn = ctx.xmm0.f32[0];
        const float fovOut = (std::min)(fovIn * fFovMultiplier, 170.0f);
        ctx.xmm0.f32[0] = fovOut;
        if (bLogStats) {
            fovCalls.fetch_add(1, std::memory_order_relaxed);
            lastFovIn = fovIn;
            lastFovOut = fovOut;
        }
    }

    DWORD WINAPI StatsLoop(void*)
    {
        uint64_t previous = 0;
        for (;;) {
            Sleep(5000);
            const uint64_t now = fovCalls.load();
            spdlog::info("FOV calls={} fov {:.2f} -> {:.2f}", now - previous, lastFovIn.load(), lastFovOut.load());
            previous = now;
        }
    }

    void InstallFovHooks()
    {
        if (fFovMultiplier == 1.0f) {
            spdlog::info("FOV Multiplier is 1.0; FOV left unchanged");
            return;
        }

        std::vector<uintptr_t> sites;
        for (int attempt = 0; attempt < 60; ++attempt) {
            sites = FindInCode(fovPattern);
            if (!sites.empty()) break;
            Sleep(250);
        }
        if (sites.size() != fovSiteCount) {
            spdlog::error("FOV signature found {} times, expected {}; FOV left unchanged", sites.size(), fovSiteCount);
            return;
        }

        for (const auto site : sites) {
            const float clamp = ReadRipFloat(site + fovHookOffset, 8);
            const float scale = ReadRipFloat(site + fovScaleOffset, 8);
            if (std::abs(clamp - 0.001f) > 1e-6f || std::abs(scale - 0.0087266462f) > 1e-6f) {
                spdlog::error("FOV site game+0x{:X} has constants {} / {}; FOV left unchanged",
                              site - reinterpret_cast<uintptr_t>(exeModule), clamp, scale);
                fovHooks.clear();
                return;
            }
            auto hook = SafetyHookMid::create(reinterpret_cast<void*>(site + fovHookOffset), ScaleFov);
            if (!hook) {
                spdlog::error("FOV hook failed at game+0x{:X}", site - reinterpret_cast<uintptr_t>(exeModule));
                fovHooks.clear();
                return;
            }
            fovHooks.push_back(std::move(*hook));
            spdlog::info("FOV hook at game+0x{:X}", site + fovHookOffset - reinterpret_cast<uintptr_t>(exeModule));
        }

        if (bLogStats) {
            if (HANDLE thread = CreateThread(nullptr, 0, StatsLoop, nullptr, 0, nullptr)) CloseHandle(thread);
        }
    }

    BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
        DWORD processId = 0;
        GetWindowThreadProcessId(hwnd, &processId);

        if (processId == GetCurrentProcessId()) {
            HWND parent = GetParent(hwnd);
            if (parent == NULL) {
                LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
                style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
                SetWindowLongPtr(hwnd, GWL_STYLE, style);

                int screenWidth = GetSystemMetrics(SM_CXSCREEN);
                int screenHeight = GetSystemMetrics(SM_CYSCREEN);
                int posX = (screenWidth - iTargetWidth) / 2;
                int posY = (screenHeight - iTargetHeight) / 2;
                if (posX < 0) posX = 0;
                if (posY < 0) posY = 0;

                SetWindowPos(hwnd, HWND_TOP, posX, posY, iTargetWidth, iTargetHeight,
                             SWP_SHOWWINDOW | SWP_FRAMECHANGED);

                spdlog::info("Window forcefully resized to {}x{} (Borderless)", iTargetWidth, iTargetHeight);
                return FALSE;
            }
        }
        return TRUE;
    }

    DWORD WINAPI WindowOverrideThread(void*) {
        if (!bForceWindow) return 0;
        for (int i = 0; i < 20; ++i) {
            Sleep(1000);
            EnumWindows(EnumWindowsCallback, 0);
        }
        return 0;
    }

    DWORD WINAPI MainThread(void*)
    {
        Logging();
        Configuration();

        if (HANDLE winHandle = CreateThread(nullptr, 0, WindowOverrideThread, nullptr, 0, nullptr)) {
            CloseHandle(winHandle);
        }

        InstallFovHooks();
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        thisModule = hModule;
        if (HANDLE mainHandle = CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr)) {
            CloseHandle(mainHandle);
        }
    }
    return TRUE;
}
