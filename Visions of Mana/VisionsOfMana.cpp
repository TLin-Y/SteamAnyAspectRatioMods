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
#include <sstream>

namespace
{
    HMODULE exeModule = GetModuleHandle(nullptr);
    HMODULE thisModule = nullptr;

    inipp::Ini<char> ini;
    constexpr char sConfigFile[] = "VoM_ARFix.ini";
    std::filesystem::path FixPath;
    std::filesystem::path ExePath;

    bool bEnableAR = true;
    bool bEnableFOV = true;
    bool bForceWindow = true;
    
    int iTargetWidth = 1280;
    int iTargetHeight = 800;
    float fFovMultiplier = 1.10f;

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
            std::ofstream file(ExePath / "VoM_ARFix.log", std::ios::trunc);
            if (file.is_open()) file.close();

            auto logger = std::make_shared<spdlog::logger>(
                "VoM_Fix",
                std::make_shared<spdlog::sinks::rotating_file_sink_st>(
                    (ExePath / "VoM_ARFix.log").string(), 10 * 1024 * 1024, 1));
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::debug);
            spdlog::set_level(spdlog::level::debug);

            SPDLOG_INFO("Visions of Mana Smart Ultrawide Patcher Initialized.");
        } catch (...) {}
    }

    std::vector<uint8_t> ParseHexStringToBytes(const std::string& hexStr) {
        std::vector<uint8_t> bytes;
        std::stringstream ss(hexStr);
        std::string byteStr;
        while (ss >> byteStr) {
            bytes.push_back(static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16)));
        }
        return bytes;
    }

    std::vector<uint8_t> FloatToBytes(float value) {
        std::vector<uint8_t> bytes(4);
        std::memcpy(bytes.data(), &value, 4);
        return bytes;
    }

    void Configuration()
    {
        std::ifstream iniFile(FixPath / sConfigFile);
        if (!iniFile) return;
        
        ini.parse(iniFile);
        ini.strip_trailing_comments();
        
        inipp::get_value(ini.sections["Resolution"], "Enable", bForceWindow);
        inipp::get_value(ini.sections["Resolution"], "Width", iTargetWidth);
        inipp::get_value(ini.sections["Resolution"], "Height", iTargetHeight);

        inipp::get_value(ini.sections["Ultrawide"], "EnableAR", bEnableAR);
        
        inipp::get_value(ini.sections["FOV"], "EnableFOV", bEnableFOV);
        inipp::get_value(ini.sections["FOV"], "Multiplier", fFovMultiplier);
    }

    int ReplaceAllInDataSections(const std::vector<uint8_t>& pattern, const std::vector<uint8_t>& replacement)
    {
        int replaceCount = 0;
        PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)exeModule;
        PIMAGE_NT_HEADERS pNtHeaders = (PIMAGE_NT_HEADERS)((std::uint8_t*)exeModule + pDosHeader->e_lfanew);
        PIMAGE_SECTION_HEADER pSection = IMAGE_FIRST_SECTION(pNtHeaders);

        for (WORD i = 0; i < pNtHeaders->FileHeader.NumberOfSections; i++, pSection++) {
            if ((pSection->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) continue;
            std::uint8_t* sectionStart = (std::uint8_t*)exeModule + pSection->VirtualAddress;
            size_t sectionSize = pSection->Misc.VirtualSize;
            if (sectionSize < pattern.size()) continue;

            std::uint8_t* current = sectionStart;
            std::uint8_t* end = sectionStart + sectionSize - pattern.size();

            while (current <= end) {
                if (std::memcmp(current, pattern.data(), pattern.size()) == 0) {
                    DWORD oldProtect;
                    VirtualProtect(current, replacement.size(), PAGE_READWRITE, &oldProtect);
                    std::memcpy(current, replacement.data(), replacement.size());
                    VirtualProtect(current, replacement.size(), oldProtect, &oldProtect);
                    replaceCount++;
                    current += pattern.size();
                } else current++;
            }
        }
        return replaceCount;
    }

    int ReplaceARInCodeSections(const std::vector<uint8_t>& pattern, const std::vector<uint8_t>& replacement)
    {
        int replaceCount = 0;
        PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)exeModule;
        PIMAGE_NT_HEADERS pNtHeaders = (PIMAGE_NT_HEADERS)((std::uint8_t*)exeModule + pDosHeader->e_lfanew);
        PIMAGE_SECTION_HEADER pSection = IMAGE_FIRST_SECTION(pNtHeaders);

        for (WORD i = 0; i < pNtHeaders->FileHeader.NumberOfSections; i++, pSection++) {
            if ((pSection->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
            std::uint8_t* sectionStart = (std::uint8_t*)exeModule + pSection->VirtualAddress;
            size_t sectionSize = pSection->Misc.VirtualSize;
            if (sectionSize < pattern.size()) continue;

            std::uint8_t* current = sectionStart + 6; 
            std::uint8_t* end = sectionStart + sectionSize - pattern.size();

            while (current <= end) {
                if (std::memcmp(current, pattern.data(), pattern.size()) == 0) {
                    if (*(current - 6) == 0xC7) {
                        DWORD oldProtect;
                        VirtualProtect(current, replacement.size(), PAGE_EXECUTE_READWRITE, &oldProtect);
                        std::memcpy(current, replacement.data(), replacement.size());
                        VirtualProtect(current, replacement.size(), oldProtect, &oldProtect);
                        replaceCount++;
                    }
                    current += pattern.size();
                } else current++;
            }
        }
        return replaceCount;
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

                SPDLOG_INFO("Window seamlessly forced to {}x{} (Borderless)", iTargetWidth, iTargetHeight);
                return FALSE; 
            }
        }
        return TRUE;
    }

    DWORD WINAPI WindowOverrideThread(void*) {
        if (!bForceWindow) return 0;
        for (int i = 0; i < 15; ++i) {
            Sleep(1000);
            EnumWindows(EnumWindowsCallback, 0);
        }
        return 0;
    }

    DWORD WINAPI MemoryInjectThread(void*)
    {
        Logging();
        Configuration();

        if (bEnableAR && iTargetHeight > 0) {
            float fNewAR = static_cast<float>(iTargetWidth) / static_cast<float>(iTargetHeight);
            std::vector<uint8_t> origAR = ParseHexStringToBytes("3B 8E E3 3F"); 
            std::vector<uint8_t> newAR = FloatToBytes(fNewAR);
            
            int count = ReplaceARInCodeSections(origAR, newAR);
            SPDLOG_INFO("Auto-Calculated AR: {:.5f}. Patched {} .text instances.", fNewAR, count);
        }

        if (bEnableFOV) {
            float fBaseFOV = 0.008726646259f; 
            float fNewFOV = fBaseFOV * fFovMultiplier;
            std::vector<uint8_t> origFOV = ParseHexStringToBytes("36 FA 0E 3C"); 
            std::vector<uint8_t> newFOV = FloatToBytes(fNewFOV);

            int count = ReplaceAllInDataSections(origFOV, newFOV);
            SPDLOG_INFO("Auto-Calculated FOV Multiplier: {}x. Patched {} data instances.", fFovMultiplier, count);
        }

        if (HANDLE winHandle = CreateThread(nullptr, 0, WindowOverrideThread, nullptr, 0, nullptr)) {
            CloseHandle(winHandle);
        }

        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        thisModule = hModule;
        if (HANDLE mainHandle = CreateThread(nullptr, 0, MemoryInjectThread, nullptr, 0, nullptr)) {
            CloseHandle(mainHandle);
        }
    }
    return TRUE;
}