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
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <intrin.h>
#include <safetyhook.hpp>
#include <utility>


namespace fs = std::filesystem;

namespace
{
    HMODULE exeModule = GetModuleHandle(nullptr);
    HMODULE thisModule = nullptr;

    constexpr char sConfigFile[] = "Guac1_ARFix.ini";
    fs::path FixPath;
    fs::path ExePath;

    int iTargetWidth = 1280;
    int iTargetHeight = 800;
    constexpr float fOriginalAR = 16.0f / 9.0f;
    float fTargetAR = 1.6f;
    float fVerticalScale = 1.0f;
    uint8_t* aspectMode = nullptr;
    SafetyHookMid viewportHook;
    SafetyHookMid projectionHook;
    SafetyHookMid perspectiveHook;
    SafetyHookMid orthographicHook;
    std::atomic<bool> hooksReady{ false };
    std::atomic<bool> invalidProjectionLogged{ false };

    static_assert(sizeof(void*) == 4, "Guac1_ARFix must be built for Windows/x86.");

    struct ScopedFpuState
    {
        alignas(16) unsigned char state[512];

        ScopedFpuState()
        {
            _fxsave(state);
            __asm fninit
        }

        ~ScopedFpuState()
        {
            _fxrstor(state);
        }
    };

    template<typename T>
    T& Field(uintptr_t object, uintptr_t offset)
    {
        return *reinterpret_cast<T*>(object + offset);
    }

    void OverrideViewport(SafetyHookContext& ctx)
    {
        ScopedFpuState fpu;
        if (!hooksReady.load())
            return;

        Field<float>(ctx.ecx, 0x1660) = static_cast<float>(iTargetWidth);
        Field<float>(ctx.ecx, 0x1664) = static_cast<float>(iTargetHeight);
        *aspectMode = 0; // Native mode fits the full window, rather than a fixed-aspect rectangle.
    }

    void OverrideProjectionAspect(SafetyHookContext& ctx)
    {
        ScopedFpuState fpu;
        if (!hooksReady.load())
            return;

        const int type = Field<int>(ctx.ecx, 0x12C);
        if (type == 0 || type == 1)
            Field<float>(ctx.ecx, 0x134) = fTargetAR;
    }

    void ExpandCameraHeight(SafetyHookContext& ctx, uintptr_t localOffset, int expectedType)
    {
        if (!hooksReady.load() || Field<int>(ctx.esi, 0x12C) != expectedType)
            return;

        auto& halfHeight = *reinterpret_cast<float*>(ctx.ebp - localOffset);
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
        ScopedFpuState fpu;
        ExpandCameraHeight(ctx, 0xC, 0);
    }

    void ExpandOrthographic(SafetyHookContext& ctx)
    {
        ScopedFpuState fpu;
        ExpandCameraHeight(ctx, 0x8, 1);
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
            std::ofstream file(ExePath / "Guac1_ARFix.log", std::ios::trunc);
            if (file.is_open()) file.close();

            auto logger = std::make_shared<spdlog::logger>(
                "Guac1_ARFix",
                std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    (ExePath / "Guac1_ARFix.log").string(), 5 * 1024 * 1024, 1));
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::debug);
            spdlog::set_level(spdlog::level::debug);

            SPDLOG_INFO("Guacamelee! 1 (32-bit) Borderless & AR Fix Initialized.");
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
            SPDLOG_INFO("Config file not found. Created default Guac1_ARFix.ini");
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

    uint8_t* FindUnique(const char* name, const char* pattern)
    {
        auto matches = Memory::PatternScanAll(exeModule, pattern);
        if (matches.size() != 1) {
            SPDLOG_ERROR("{} signature has {} matches; expected one. No fix installed.", name, matches.size());
            return nullptr;
        }
        return matches.front();
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
        auto* getter = FindUnique("Aspect getter",
            "55 8B EC 51 0F B6 05 ?? ?? ?? ?? 83 F8 03 77 ?? FF 24 85");
        auto* viewport = FindUnique("Viewport",
            "55 8B EC 83 EC 14 56 8B F1 6A 00 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? D9 05");
        auto* projection = FindUnique("Projection",
            "55 8B EC 83 EC 0C 56 8B F1 8B 86 2C 01 00 00 85 C0 0F 84");
        auto* perspective = FindUnique("Perspective height",
            "D9 45 F4 D8 8E 38 01 00 00 D8 B6 40 01 00 00 D9 5D F4 D9 45 F4 D9 96 F0 00 00 00");
        auto* orthographic = FindUnique("Orthographic height",
            "D9 45 FC D8 4D F8 D8 B6 40 01 00 00 D9 E8 D9 C9 D9 5D F8 D9 45 F8 D9 96 F0 00 00 00");
        if (!getter || !viewport || !projection || !perspective || !orthographic)
            return false;

        uintptr_t modeAddress = 0;
        std::memcpy(&modeAddress, getter + 7, sizeof(modeAddress));
        const auto* base = reinterpret_cast<const uint8_t*>(exeModule);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        const uintptr_t imageBase = reinterpret_cast<uintptr_t>(base);
        if (modeAddress < imageBase || modeAddress - imageBase >= nt->OptionalHeader.SizeOfImage) {
            SPDLOG_ERROR("Aspect mode address is outside the executable. No fix installed.");
            return false;
        }
        aspectMode = reinterpret_cast<uint8_t*>(modeAddress);

        if (!InstallHook("Perspective height", perspectiveHook, perspective + 18, ExpandPerspective) ||
            !InstallHook("Orthographic height", orthographicHook, orthographic + 19, ExpandOrthographic) ||
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
        SPDLOG_INFO("Full viewport and Vert+ enabled. Type-2 UI camera bounds remain game-controlled.");
        return true;
    }

    // Window override thread for 32-bit target
    DWORD WINAPI WindowOverrideThread(void*)
    {
        HWND hwnd = nullptr;
        while (!hwnd) {
            // Note: Update title string to match Guacamelee 1 window title if necessary
            hwnd = FindWindowA(nullptr, "Guacamelee!");
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

            // Apply borderless style for 32-bit window
            LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
            style &= ~WS_CAPTION;
            style &= ~WS_THICKFRAME;
            style &= ~WS_MINIMIZEBOX;
            style &= ~WS_MAXIMIZEBOX;
            style |= WS_POPUP;
            SetWindowLongPtrW(hwnd, GWL_STYLE, style);

            SetWindowPos(hwnd, HWND_TOP, 0, 0, iTargetWidth, iTargetHeight,
                         SWP_SHOWWINDOW | SWP_FRAMECHANGED);

            SPDLOG_INFO("Forced 32-bit window style and size to {}x{}.", iTargetWidth, iTargetHeight);
        }

        return 0;
    }

    DWORD WINAPI SetupThread(void*)
    {
        InitializeLogging();
        LoadConfiguration();

        Sleep(1000);

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