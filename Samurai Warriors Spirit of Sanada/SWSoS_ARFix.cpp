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

namespace fs = std::filesystem;

namespace
{
    HMODULE exeModule = GetModuleHandle(nullptr);
    HMODULE thisModule = nullptr;

    constexpr char sConfigFile[] = "SWSanada_ARFix.ini";
    std::filesystem::path FixPath;
    std::filesystem::path ExePath;

    int iTargetWidth = 1280;
    int iTargetHeight = 960;
    float fTargetAspect = 1.333333f;

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
            std::ofstream file(ExePath / "SWSanada_ARFix.log", std::ios::trunc);
            if (file.is_open()) file.close();

            auto logger = std::make_shared<spdlog::logger>(
                "Sanada_ARFix",
                std::make_shared<spdlog::sinks::rotating_file_sink_st>(
                    (ExePath / "SWSanada_ARFix.log").string(), 10 * 1024 * 1024, 1));
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::debug);
            spdlog::set_level(spdlog::level::debug);

            SPDLOG_INFO("Samurai Warriors: Spirit of Sanada Pure ARFix Initialized.");
        } catch (...) {}
    }

    void Configuration()
    {
        inipp::Ini<char> ini;
        std::ifstream iniFile(FixPath / sConfigFile);
        if (iniFile) {
            ini.parse(iniFile);
            ini.strip_trailing_comments();
            inipp::get_value(ini.sections["Resolution"], "Width", iTargetWidth);
            inipp::get_value(ini.sections["Resolution"], "Height", iTargetHeight);
        }
        
        if (iTargetHeight > 0) {
            fTargetAspect = static_cast<float>(iTargetWidth) / static_cast<float>(iTargetHeight);
        }
        SPDLOG_INFO("Config Loaded - Target Aspect Ratio: {:.5f} ({}x{})", fTargetAspect, iTargetWidth, iTargetHeight);
    }

    float* FindCodeCave(uint8_t* moduleBase) {
        PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)moduleBase;
        PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)(moduleBase + dos->e_lfanew);
        PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(nt);
        
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, section++) {
            if (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) {
                uint8_t* start = moduleBase + section->VirtualAddress;
                size_t size = section->Misc.VirtualSize;
                
                for (size_t j = 0; j < size - 8; j++) {
                    if (start[j] == 0xCC && start[j+1] == 0xCC && start[j+2] == 0xCC && start[j+3] == 0xCC &&
                        start[j+4] == 0xCC && start[j+5] == 0xCC && start[j+6] == 0xCC && start[j+7] == 0xCC) {
                        return (float*)&start[j];
                    }
                }
            }
        }
        return nullptr;
    }

    DWORD WINAPI SetupThread(void*)
    {
        Logging();
        Configuration();

        uint8_t* moduleBase = reinterpret_cast<uint8_t*>(exeModule);

        float* pCustomAR = FindCodeCave(moduleBase);
        if (pCustomAR) {
            DWORD oldProtect;
            VirtualProtect(pCustomAR, 4, PAGE_EXECUTE_READWRITE, &oldProtect);
            *pCustomAR = fTargetAspect;
            VirtualProtect(pCustomAR, 4, oldProtect, &oldProtect);
            SPDLOG_INFO("Custom AR {:.5f} injected at Cave 0x{:X}", fTargetAspect, reinterpret_cast<uintptr_t>(pCustomAR));
        } else {
            SPDLOG_ERROR("Failed to find code cave!");
            return 1;
        }

        int patchedCount = 0;
        DWORD oldProtect;

        uint8_t* pRenderRead = Memory::PatternScan(moduleBase, "F3 0F 10 89 B0 01 00 00 41 8B E8 8B F2");
        if (pRenderRead) {
            VirtualProtect(pRenderRead, 8, PAGE_EXECUTE_READWRITE, &oldProtect);
            pRenderRead[0] = 0xF3;
            pRenderRead[1] = 0x0F;
            pRenderRead[2] = 0x10;
            pRenderRead[3] = 0x0D; 
            *(int32_t*)(pRenderRead + 4) = static_cast<int32_t>((uint8_t*)pCustomAR - (pRenderRead + 8));
            VirtualProtect(pRenderRead, 8, oldProtect, &oldProtect);
            patchedCount++;
        }

        uint8_t* pStructCopy = Memory::PatternScan(moduleBase, "8B 83 B0 01 00 00 89 87 B0 01 00 00 8B 83 B4 01 00 00");
        if (pStructCopy) {
            VirtualProtect(pStructCopy, 6, PAGE_EXECUTE_READWRITE, &oldProtect);
            pStructCopy[0] = 0x8B;
            pStructCopy[1] = 0x05; 
            *(int32_t*)(pStructCopy + 2) = static_cast<int32_t>((uint8_t*)pCustomAR - (pStructCopy + 6));
            VirtualProtect(pStructCopy, 6, oldProtect, &oldProtect);
            patchedCount++;
        }

        SPDLOG_INFO("Successfully patched {} / 2 AR read instructions.", patchedCount);

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