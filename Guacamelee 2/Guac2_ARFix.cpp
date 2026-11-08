#include "stdafx.h"
#include "version.h"
#include "helper.hpp"

#ifdef _DEBUG
    #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#else
    #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#endif

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <inipp/inipp.h>
#include <windows.h>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <safetyhook.hpp>
#include <utility>

namespace fs = std::filesystem;

namespace
{
    HMODULE exeModule = GetModuleHandle(nullptr);
    HMODULE thisModule = nullptr;

    constexpr char sConfigFile[] = "Guac2_ARFix.ini";
    fs::path FixPath;
    fs::path ExePath;

    int iTargetWidth = 1280;
    int iTargetHeight = 800;
    constexpr float fOriginalAR = 16.0f / 9.0f;
    float fTargetAR = 1.6f;
    float fVerticalScale = 1.0f;
    uint8_t* primaryAspectMode = nullptr;
    SafetyHookMid viewportHook;
    SafetyHookMid projectionHook;
    SafetyHookMid perspectiveHook;
    SafetyHookMid orthographicHook;
    std::atomic<bool> hooksReady{ false };
    std::atomic<bool> invalidProjectionLogged{ false };

    static_assert(sizeof(void*) == 8, "Guac2_ARFix must be built for Windows/x64.");

    template<typename T>
    T& Field(uintptr_t object, uintptr_t offset)
    {
        return *reinterpret_cast<T*>(object + offset);
    }

    void OverrideViewport(SafetyHookContext& ctx)
    {
        if (!hooksReady.load())
            return;

        Field<float>(ctx.rcx, 0xF764) = static_cast<float>(iTargetWidth);
        Field<float>(ctx.rcx, 0xF76C) = static_cast<float>(iTargetHeight);
        *primaryAspectMode = 0;
    }

    void OverrideProjectionAspect(SafetyHookContext& ctx)
    {
        if (!hooksReady.load())
            return;

        const int type = Field<int>(ctx.rcx, 0x31C);
        if (type == 0 || type == 1)
            Field<float>(ctx.rcx, 0x2FC) = fTargetAR;
    }

    void ExpandCameraHeight(SafetyHookContext& ctx, float& halfHeight, int expectedType)
    {
        if (!hooksReady.load() || Field<int>(ctx.rbx, 0x31C) != expectedType)
            return;

        if (!std::isfinite(halfHeight))
        {
            if (!invalidProjectionLogged.exchange(true))
                SPDLOG_ERROR("Non-finite camera half-height; leaving this projection unchanged.");
            return;
        }

        halfHeight *= fVerticalScale;
    }

    void ExpandPerspective(SafetyHookContext& ctx)
    {
        ExpandCameraHeight(ctx, ctx.xmm4.f32[0], 0);
    }

    void ExpandOrthographic(SafetyHookContext& ctx)
    {
        ExpandCameraHeight(ctx, ctx.xmm6.f32[0], 1);
    }

    void ExpandSteamOrthographic(SafetyHookContext& ctx)
    {
        ExpandCameraHeight(ctx, ctx.xmm5.f32[0], 1);
    }

    void InitializeLogging()
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
            std::ofstream file(ExePath / "Guac2_ARFix.log", std::ios::trunc);
            if (file.is_open()) file.close();

            auto logger = std::make_shared<spdlog::logger>(
                "Guac2_ARFix",
                std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    (ExePath / "Guac2_ARFix.log").string(), 5 * 1024 * 1024, 1));
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::debug);
            spdlog::set_level(spdlog::level::debug);

            SPDLOG_INFO("Guacamelee! 2 Borderless & AR Fix Initialized.");
        } catch (...) {}
    }

    void LoadConfiguration()
    {
        inipp::Ini<char> ini;
        std::ifstream iniFile(FixPath / sConfigFile);
        if (iniFile) {
            ini.parse(iniFile);
            ini.strip_trailing_comments();
            inipp::get_value(ini.sections["Resolution"], "Width", iTargetWidth);
            inipp::get_value(ini.sections["Resolution"], "Height", iTargetHeight);
        } else {
            std::ofstream defaultIni(FixPath / sConfigFile);
            if (defaultIni.is_open()) {
                defaultIni << "[Resolution]\n";
                defaultIni << "Width = 1280\n";
                defaultIni << "Height = 800\n";
                defaultIni.close();
            }
            SPDLOG_INFO("Config file not found. Created default Guac2_ARFix.ini");
        }

        if (iTargetWidth <= 0 || iTargetHeight <= 0) {
            SPDLOG_WARN("Invalid target resolution {}x{}. Using default 1280x800.",
                        iTargetWidth, iTargetHeight);
            iTargetWidth = 1280;
            iTargetHeight = 800;
        }
        fTargetAR = static_cast<float>(iTargetWidth) / static_cast<float>(iTargetHeight);
        fVerticalScale = std::max(1.0f, fOriginalAR / fTargetAR);

        SPDLOG_INFO("Config Loaded -> Target Resolution: {}x{}, Aspect Ratio: {:.5f}, vertical scale: {:.5f}",
                    iTargetWidth, iTargetHeight, fTargetAR, fVerticalScale);
    }

    struct ScanProfile
    {
        const char* name;
        std::array<const char*, 5> patterns;
        safetyhook::MidHookFn orthographicCallback;
    };

    constexpr std::array<const char*, 5> scanNames{
        "Primary aspect mode", "Viewport", "Projection", "Perspective height", "Orthographic height"
    };
    const std::array<ScanProfile, 2> scanProfiles{ {
        {
            "GOG",
            {
                "0F B6 0D ?? ?? ?? ?? 85 C9 74 ?? 83 E9 01 74 ?? 83 E9 01",
                "40 53 48 83 EC 70 0F 29 74 24 60 48 8B D9 0F 29 7C 24 50",
                "40 53 48 83 EC 40 48 8B D9 0F 29 74 24 30 8B 89 1C 03 00 00",
                "F3 0F 5E A3 08 03 00 00 F3 0F 11 A3 E0 02 00 00 0F 28 D4 F3 0F 59 CC",
                "0F 28 CE F3 0F 59 8B FC 02 00 00 33 C0 F3 0F 11 B3 E0 02 00 00"
            },
            ExpandOrthographic
        },
        {
            "Steam",
            {
                "0F B6 0D ?? ?? ?? ?? 85 C9 74 ?? FF C9 74 ?? FF C9 74 ?? FF C9",
                "40 53 48 83 EC 70 0F 29 74 24 60 0F 29 7C 24 50 48 8B D9",
                "40 53 48 83 EC 40 48 8B D9 8B 89 1C 03 00 00 0F 29 74 24 30",
                "F3 0F 5E A3 08 03 00 00 F3 0F 59 CC F3 0F 11 A3 E0 02 00 00 0F 28 D4",
                "F3 0F 11 AB E0 02 00 00 0F 28 CD 0F 28 D5 33 C0 41 0F 28 D8"
            },
            ExpandSteamOrthographic
        }
    } };

    bool FindScanProfile(std::array<uint8_t*, 5>& sites, const ScanProfile*& selected)
    {
        std::array<std::array<size_t, 5>, 2> counts{};
        const ULONGLONG started = GetTickCount64();
        bool waitingLogged = false;
        do {
            size_t completeProfiles = 0;
            for (size_t profileIndex = 0; profileIndex < scanProfiles.size(); ++profileIndex) {
                const auto& profile = scanProfiles[profileIndex];
                std::array<uint8_t*, 5> candidates{};
                bool complete = true;
                for (size_t site = 0; site < candidates.size(); ++site) {
                    auto matches = Memory::PatternScanAll(exeModule, profile.patterns[site]);
                    counts[profileIndex][site] = matches.size();
                    if (matches.size() > 1) {
                        SPDLOG_ERROR("{} {} signature is ambiguous ({} matches). No fix installed.",
                            profile.name, scanNames[site], matches.size());
                        return false;
                    }
                    if (matches.size() == 1)
                        candidates[site] = matches.front();
                    else
                        complete = false;
                }
                if (complete) {
                    ++completeProfiles;
                    sites = candidates;
                    selected = &profile;
                }
            }
            if (completeProfiles > 1) {
                SPDLOG_ERROR("Multiple complete executable scan profiles matched. No fix installed.");
                return false;
            }
            if (completeProfiles == 1) {
                SPDLOG_INFO("Selected {} executable scan profile after {} ms.",
                    selected->name, GetTickCount64() - started);
                return true;
            }
            if (!waitingLogged) {
                SPDLOG_INFO("Waiting for initialized executable code (up to 30 seconds); trying GOG and Steam profiles.");
                waitingLogged = true;
            }
            // Steam initializes its encoded code section after DLL loading. Retry without installing partial hooks.
            Sleep(250);
        } while (GetTickCount64() - started < 30000);

        for (size_t profile = 0; profile < scanProfiles.size(); ++profile)
            for (size_t site = 0; site < sites.size(); ++site)
                SPDLOG_ERROR("{} {} signature has {} matches; expected one.",
                    scanProfiles[profile].name, scanNames[site], counts[profile][site]);
        SPDLOG_ERROR("No supported executable profile became ready within 30 seconds. No fix installed.");
        return false;
    }

    bool InstallHook(const char* name, SafetyHookMid& hook, uint8_t* address,
                     safetyhook::MidHookFn callback)
    {
        auto result = SafetyHookMid::create(address, callback);
        if (!result) {
            SPDLOG_ERROR("Failed to install {} hook (error {}).", name, static_cast<int>(result.error().type));
            return false;
        }
        hook = std::move(*result);
        SPDLOG_INFO("{} hook installed at 0x{:X}.", name, reinterpret_cast<uintptr_t>(address));
        return true;
    }

    bool InstallAspectFix()
    {
        std::array<uint8_t*, 5> sites{};
        const ScanProfile* profile = nullptr;
        if (!FindScanProfile(sites, profile))
            return false;
        auto* mode = sites[0];
        auto* viewport = sites[1];
        auto* projection = sites[2];
        auto* perspective = sites[3];
        auto* orthographic = sites[4];

        int32_t displacement = 0;
        std::memcpy(&displacement, mode + 3, sizeof(displacement));
        const uintptr_t modeAddress = reinterpret_cast<uintptr_t>(mode + 7) + displacement;
        const auto* base = reinterpret_cast<const uint8_t*>(exeModule);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const uintptr_t imageBase = reinterpret_cast<uintptr_t>(base);
        if (modeAddress < imageBase || modeAddress - imageBase >= nt->OptionalHeader.SizeOfImage) {
            SPDLOG_ERROR("Primary aspect mode address is outside the executable. No fix installed.");
            return false;
        }
        primaryAspectMode = reinterpret_cast<uint8_t*>(modeAddress);

        if (!InstallHook("Perspective height", perspectiveHook, perspective + 8, ExpandPerspective) ||
            !InstallHook("Orthographic height", orthographicHook, orthographic, profile->orthographicCallback) ||
            !InstallHook("Projection aspect", projectionHook, projection, OverrideProjectionAspect) ||
            !InstallHook("Native viewport", viewportHook, viewport, OverrideViewport)) {
            viewportHook.reset();
            projectionHook.reset();
            orthographicHook.reset();
            perspectiveHook.reset();
            SPDLOG_ERROR("Aspect fix installation failed; all installed hooks removed.");
            return false;
        }

        hooksReady.store(true);
        SPDLOG_INFO("Full primary viewport and Vert+ enabled. Type-2 UI bounds and secondary viewport dimensions remain game-controlled.");
        return true;
    }

    DWORD WINAPI WindowOverrideThread(void*)
    {
        HWND hwnd = nullptr;
        while (!hwnd) {
            hwnd = FindWindowA(nullptr, "Guacamelee! 2");
            if (!hwnd) {
                HWND hIter = GetTopWindow(nullptr);
                while (hIter) {
                    DWORD pid = 0;
                    GetWindowThreadProcessId(hIter, &pid);
                    if (pid == GetCurrentProcessId() && IsWindowVisible(hIter)) {
                        hwnd = hIter;
                        break;
                    }
                    hIter = GetWindow(hIter, GW_HWNDNEXT);
                }
            }
            Sleep(200);
        }

        if (hwnd) {
            SPDLOG_INFO("Game window handle found: 0x{:X}", reinterpret_cast<uintptr_t>(hwnd));

            LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
            style &= ~WS_CAPTION;
            style &= ~WS_THICKFRAME;
            style &= ~WS_MINIMIZEBOX;
            style &= ~WS_MAXIMIZEBOX;
            style |= WS_POPUP; // Convert to borderless popup style
            SetWindowLongPtrW(hwnd, GWL_STYLE, style);

            SetWindowPos(hwnd, HWND_TOP, 0, 0, iTargetWidth, iTargetHeight,
                         SWP_SHOWWINDOW | SWP_FRAMECHANGED);

            SPDLOG_INFO("Successfully forced borderless window style and size to {}x{}.", iTargetWidth, iTargetHeight);
        }

        return 0;
    }

    DWORD WINAPI SetupThread(void*)
    {
        InitializeLogging();
        LoadConfiguration();

        Sleep(1000); // Wait for modules to settle

        if (!InstallAspectFix())
            return 0;

        if (HANDLE hWindowThread = CreateThread(nullptr, 0, WindowOverrideThread, nullptr, 0, nullptr)) {
            CloseHandle(hWindowThread);
        } else {
            SPDLOG_ERROR("Failed to create window override thread: {}.", GetLastError());
        }

        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        thisModule = hModule;
        if (HANDLE mainHandle = CreateThread(nullptr, 0, SetupThread, nullptr, 0, nullptr)) {
            CloseHandle(mainHandle);
        }
    }
    return TRUE;
}