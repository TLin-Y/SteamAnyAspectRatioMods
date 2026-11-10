#include "stdafx.h"
#include <windows.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <inipp/inipp.h>
#include <safetyhook.hpp>
#include <filesystem>
#include <fstream>
#include <cstdint>

#if !defined(_M_IX86)
#error Dynasty Warriors 7 XL is a 32-bit game. You MUST compile this project as x86 (32-bit).
#endif

namespace fs = std::filesystem;

namespace
{
    HMODULE thisModule = nullptr;
    fs::path FixPath;
    constexpr char sConfigFile[] = "DW7XL_ARFix.ini";
    constexpr char sLogFile[] = "DW7XL_ARFix.log";

    int iTargetWidth = 1280;
    int iTargetHeight = 800;
    float fCustomAR = 1.6f; 

    safetyhook::MidHook aspectHook;

    bool InitializeLogging()
    {
        WCHAR dllPath[MAX_PATH] = {};
        if (GetModuleFileNameW(thisModule, dllPath, MAX_PATH) == 0) return false;
        FixPath = fs::path(dllPath).parent_path();

        std::ofstream clearLog(FixPath / sLogFile, std::ios::trunc);
        if (clearLog.is_open()) clearLog.close();

        try {
            auto logger = std::make_shared<spdlog::logger>(
                "DW7XL_ARFix",
                std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    (FixPath / sLogFile).string(), 5 * 1024 * 1024, 1));
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::info);
            SPDLOG_INFO("Dynasty Warriors 7 XL AR Fix initialized.");
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
            defaultIni << "[Resolution]\nWidth = 1280\nHeight = 800\n";
            defaultIni.close();
            SPDLOG_INFO("Created default config.");
        }

        std::ifstream iniFile(path);
        if (!iniFile) return false;

        inipp::Ini<char> ini;
        ini.parse(iniFile);
        ini.strip_trailing_comments();

        inipp::get_value(ini.sections["Resolution"], "Width", iTargetWidth);
        inipp::get_value(ini.sections["Resolution"], "Height", iTargetHeight);

        if (iTargetHeight > 0) {
            fCustomAR = static_cast<float>(iTargetWidth) / static_cast<float>(iTargetHeight);
        }

        SPDLOG_INFO("Config loaded -> Resolution: {}x{}, Target Aspect Ratio: {:.5f}", 
                    iTargetWidth, iTargetHeight, fCustomAR);
        return true;
    }

    void InstallFix()
    {
        HMODULE exeModule = GetModuleHandleW(nullptr);
        std::uint8_t* base = reinterpret_cast<std::uint8_t*>(exeModule);
        
        std::uint8_t* addrHook = base + 0x432049;
        
        aspectHook = safetyhook::create_mid(addrHook, [](SafetyHookContext& ctx) {

            float* pAspectOnStack = reinterpret_cast<float*>(ctx.esp + 0x20);
            *pAspectOnStack = fCustomAR;
        });

        if (aspectHook) {
            SPDLOG_INFO("Successfully mid-hooked Aspect Ratio read at base + 0x432049.");
        } else {
            SPDLOG_ERROR("Failed to create mid-hook for Aspect Ratio.");
        }
    }

    DWORD WINAPI SetupThread(void*)
    {
        if (!InitializeLogging()) return 1;
        if (!LoadConfiguration()) return 1;
        
        Sleep(1500); 
        InstallFix();
        
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