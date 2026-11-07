#include "stdafx.h"
#include "version.h"

#if !defined(_M_IX86)
#error RaymanLegends_ARFix must be built for Windows x86 (32-bit), not x64.
#endif

#ifdef _DEBUG
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#else
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#endif

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <inipp/inipp.h>
#include <safetyhook.hpp>
#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
    HMODULE exeModule = GetModuleHandleW(nullptr);
    HMODULE thisModule = nullptr;
    fs::path FixPath;
    constexpr char sConfigFile[] = "RaymanLegends_ARFix.ini";
    constexpr char sLogFile[] = "RaymanLegends_ARFix.log";

    int iTargetWidth = 1280;
    int iTargetHeight = 800;
    bool bAspectFix = true;
    bool bHUDFix = true;
    float fHardcodedAR = 1.6f;
    float fMenuAspect = 16.0f / 9.0f;
    float pushValue = 1090.0f;

    // Direct RVA offsets for the target game version
    constexpr std::uint32_t offsetAspect1 = 0xE96C9;
    constexpr std::uint32_t offsetAspect2 = 0x7E091;
    constexpr std::uint32_t offsetHUD = 0x11FE83;

    safetyhook::InlineHook aspect1Hook;
    safetyhook::InlineHook aspect2Hook;
    safetyhook::InlineHook HUDHook;
    std::uint8_t* aspect1Return = nullptr;
    std::uint8_t* aspect2Return = nullptr;
    std::uint8_t* HUDReturn = nullptr;

    __declspec(naked) void pokeAspect1CodeCave()
    {
        __asm {
            fstp dword ptr [esi + 0C0h]
            push eax
            mov eax, dword ptr [fHardcodedAR]
            mov dword ptr [esi + 0C0h], eax
            pop eax
            jmp dword ptr [aspect1Return]
        }
    }

    __declspec(naked) void pokeAspect2CodeCave()
    {
        __asm {
            xor edi, edi
            cmp dword ptr [esi + 10h], edi
            push eax
            mov eax, dword ptr [fMenuAspect]
            mov dword ptr [esi + 4], eax
            pop eax
            jmp dword ptr [aspect2Return]
        }
    }

    __declspec(naked) void pokeHUDCodeCave()
    {
        __asm {
            pushfd
            push eax
            mov eax, dword ptr [esi + 54h]
            cmp eax, 044884000h
            jg noShift
            sub esp, 16
            movdqu xmmword ptr [esp], xmm0
            movss xmm0, dword ptr [esi + 54h]
            addss xmm0, dword ptr [pushValue]
            movss dword ptr [esi + 54h], xmm0
            movdqu xmm0, xmmword ptr [esp]
            add esp, 16
        noShift:
            pop eax
            popfd
            fld dword ptr [esi + 54h]
            fld dword ptr [ebp - 4]
            jmp dword ptr [HUDReturn]
        }
    }

    bool InitializeLogging()
    {
        WCHAR dllPath[MAX_PATH] = {};
        if (GetModuleFileNameW(thisModule, dllPath, MAX_PATH) == 0) return false;
        FixPath = fs::path(dllPath).parent_path();

        // Truncate log file on every startup
        std::ofstream clearLog(FixPath / sLogFile, std::ios::trunc);
        if (clearLog.is_open()) clearLog.close();

        try {
            auto logger = std::make_shared<spdlog::logger>(
                "RaymanLegends_ARFix",
                std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    (FixPath / sLogFile).string(), 5 * 1024 * 1024, 1));
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::info);
            SPDLOG_INFO("Rayman Legends x86 minimal fix initialized.");
            return true;
        } catch (...) {
            return false;
        }
    }

    bool LoadConfiguration()
    {
        const fs::path path = FixPath / sConfigFile;
        if (!fs::exists(path)) {
            std::ofstream defaultIni(path);
            defaultIni << "[Resolution]\nWidth = 1280\nHeight = 800\n\n"
                          "[Fixes]\nAspectRatio = true\nHUD = true\n";
            defaultIni.close();
        }

        std::ifstream iniFile(path);
        if (!iniFile) return false;

        inipp::Ini<char> ini;
        ini.parse(iniFile);
        ini.strip_trailing_comments();

        inipp::get_value(ini.sections["Resolution"], "Width", iTargetWidth);
        inipp::get_value(ini.sections["Resolution"], "Height", iTargetHeight);
        inipp::get_value(ini.sections["Fixes"], "AspectRatio", bAspectFix);
        inipp::get_value(ini.sections["Fixes"], "HUD", bHUDFix);

        fHardcodedAR = static_cast<float>(iTargetWidth) / static_cast<float>(iTargetHeight);
        pushValue = 1080.0f + static_cast<float>(iTargetWidth / iTargetHeight) * 10.0f;

        SPDLOG_INFO("Config -> Resolution: {}x{}, AR: {:.5f}, HUD Shift: {:.1f}.",
                    iTargetWidth, iTargetHeight, fHardcodedAR, pushValue);
        return true;
    }

    bool InstallDirectHook(safetyhook::InlineHook& hook, std::uint32_t rva, void* cave,
                           std::uint8_t*& returnAddress, std::size_t instructionSize, const char* name)
    {
        auto address = reinterpret_cast<std::uint8_t*>(exeModule) + rva;
        auto result = safetyhook::InlineHook::create(address, cave, safetyhook::InlineHook::StartDisabled);
        if (!result) return false;

        returnAddress = address + instructionSize;
        hook = std::move(*result);
        if (!hook.enable()) {
            hook.reset();
            return false;
        }
        SPDLOG_INFO("{} hook enabled at RVA 0x{:X}.", name, rva);
        return true;
    }

    void ApplyPatches()
    {
        if (bAspectFix) {
            if (!InstallDirectHook(aspect1Hook, offsetAspect1, &pokeAspect1CodeCave, aspect1Return, 6, "Aspect1")) return;
            if (!InstallDirectHook(aspect2Hook, offsetAspect2, &pokeAspect2CodeCave, aspect2Return, 5, "Aspect2")) {
                aspect1Hook.reset();
                return;
            }
        }
        if (bHUDFix) {
            if (!InstallDirectHook(HUDHook, offsetHUD, &pokeHUDCodeCave, HUDReturn, 6, "HUD")) {
                aspect2Hook.reset();
                aspect1Hook.reset();
            }
        }
    }

    DWORD WINAPI SetupThread(void*)
    {
        if (!InitializeLogging()) return 1;
        if (!LoadConfiguration()) return 1;

        if (bAspectFix || bHUDFix) {
            Sleep(2000);
            ApplyPatches();
        }
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        thisModule = hModule;
        if (HANDLE thread = CreateThread(nullptr, 0, SetupThread, nullptr, 0, nullptr)) {
            CloseHandle(thread);
        }
    }
    return TRUE;
}