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

    constexpr char sConfigFile[] = "P5R_ARFix.ini";
    std::filesystem::path FixPath;
    std::filesystem::path ExePath;

    int iTargetWidth = 1280;
    int iTargetHeight = 800;
    float fTargetAspect = 1.6f;

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
            std::ofstream file(ExePath / "P5R_ARFix.log", std::ios::trunc);
            if (file.is_open()) file.close();

            auto logger = std::make_shared<spdlog::logger>(
                "P5R_Fix",
                std::make_shared<spdlog::sinks::rotating_file_sink_st>(
                    (ExePath / "P5R_ARFix.log").string(), 10 * 1024 * 1024, 1));
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::debug);
            spdlog::set_level(spdlog::level::debug);

            SPDLOG_INFO("Persona 5 Royal Inline RIP Patcher Initialized.");
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
        SPDLOG_INFO("Target Aspect Ratio Calculated: {:.3f}", fTargetAspect);
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

    void PatchRIPRelative(uint8_t* pInst, size_t instLen, float* pTarget, uint8_t modRM) {
        DWORD old;
        VirtualProtect(pInst, instLen, PAGE_EXECUTE_READWRITE, &old);
        
        int32_t offset = static_cast<int32_t>((uint8_t*)pTarget - (pInst + instLen));
        
        if (instLen == 8) {
            pInst[3] = modRM;
            *(int32_t*)(pInst + 4) = offset;
        } else if (instLen == 9) {
            pInst[4] = modRM;
            *(int32_t*)(pInst + 5) = offset;
        }
        
        VirtualProtect(pInst, instLen, old, &old);
    }

    DWORD WINAPI SetupThread(void*)
    {
        Logging();
        Configuration();

        uint8_t* moduleBase = reinterpret_cast<uint8_t*>(exeModule);

        float* pCustomAR = FindCodeCave(moduleBase);
        if (!pCustomAR) {
            SPDLOG_ERROR("Failed to find code cave in .text section!");
            return 1;
        }
        DWORD oldProtect;
        VirtualProtect(pCustomAR, 4, PAGE_EXECUTE_READWRITE, &oldProtect);
        *pCustomAR = fTargetAspect;
        VirtualProtect(pCustomAR, 4, oldProtect, &oldProtect);
        SPDLOG_INFO("Injected Custom AR {:.3f} at Cave 0x{:X}", fTargetAspect, reinterpret_cast<uintptr_t>(pCustomAR));

        uint8_t* resCode = Memory::PatternScan(moduleBase, "F3 0F 59 05 ? ? ? ? F3 48 ? ? ? 3B ? 72 ? 0F 57 ? F3 48 ? ? ? F3 0F 59 05");
        if (resCode) {
            uint8_t* inst = resCode + 25;
            int32_t relOffset = *(int32_t*)(inst + 4);
            float* pRes = reinterpret_cast<float*>(inst + 8 + relOffset);
            DWORD old;
            VirtualProtect(pRes, 4, PAGE_EXECUTE_READWRITE, &old);
            *pRes = fTargetAspect;
            VirtualProtect(pRes, 4, old, &old);
            SPDLOG_INFO("Global UI AR forced to {:.3f}", fTargetAspect);
        }

        int patchedCount = 0;

        uint8_t* p1 = Memory::PatternScan(moduleBase, "44 0F 28 D8 44 0F 28 E0 F3 44 0F 5E 9E AC 01 00 00");
        if (p1) { PatchRIPRelative(p1 + 8, 9, pCustomAR, 0x1D); patchedCount++; }

        uint8_t* p2 = Memory::PatternScan(moduleBase, "F3 0F 10 BE A0 01 00 00 49 8B CE F3 44 0F 10 86 AC 01 00 00");
        if (p2) { PatchRIPRelative(p2 + 11, 9, pCustomAR, 0x05); patchedCount++; }

        uint8_t* p3 = Memory::PatternScan(moduleBase, "48 89 44 24 38 F3 0F 11 44 24 30 F3 0F 10 81 AC 01 00 00");
        if (p3) { PatchRIPRelative(p3 + 11, 8, pCustomAR, 0x05); patchedCount++; }

        uint8_t* p4 = Memory::PatternScan(moduleBase, "F3 0F 5E D1 0F 28 C8 F3 0F 5E 8B AC 01 00 00");
        if (p4) { PatchRIPRelative(p4 + 7, 8, pCustomAR, 0x0D); patchedCount++; }

        SPDLOG_INFO("Successfully inline-patched {} / 4 3D rendering instructions.", patchedCount);
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