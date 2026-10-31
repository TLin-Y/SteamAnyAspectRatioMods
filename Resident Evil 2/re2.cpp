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
#include <safetyhook.hpp>
#include <cmath>
#include <cstring>
#include <windows.h>

HMODULE exeModule = GetModuleHandle(NULL);
HMODULE thisModule;

inipp::Ini<char> ini;
std::string sConfigFile = "RE2_ARFix.ini"; 

std::shared_ptr<spdlog::logger> logger;
std::string sLogFile = "RE2_ARFix.log";
std::filesystem::path ExePath;

bool bEnableARFix = true;
int iAspectWidth = 4;
int iAspectHeight = 3;
float fTargetARValue = 1.3333333f;

bool bEnableFOVFix = true;
float fFOVPercent = 120.0f; 

template<typename T>
T GetConfigAndLog(inipp::Ini<char>& ini, const std::string& section, const std::string& key, T defaultValue) {
    T value = defaultValue;
    inipp::get_value(ini.sections[section], key, value);
    SPDLOG_INFO("Config Parse: [{}]: {} = {}", section, key, value);
    return value;
}

void Logging() {
    WCHAR gameExePath[_MAX_PATH] = {0};
    GetModuleFileNameW(exeModule, gameExePath, MAX_PATH);
    ExePath = gameExePath;
    ExePath = ExePath.remove_filename();

    try {
        std::ofstream file(ExePath / sLogFile, std::ios::trunc);
        if (file.is_open()) file.close();

        logger = std::make_shared<spdlog::logger>("RE2_Fix", std::make_shared<spdlog::sinks::rotating_file_sink_st>((ExePath / sLogFile).string(), 10 * 1024 * 1024, 1));
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
        spdlog::flush_on(spdlog::level::debug);
        spdlog::set_level(spdlog::level::debug); 
        
        SPDLOG_INFO("Resident Evil 2 Remake AR & FOV Fix Initialized");
    }
    catch (...) {}
}

void Configuration() {
    std::ifstream iniFile(ExePath / sConfigFile);
    if (!iniFile) {
        SPDLOG_WARN("Config file {} not found! Using hardcoded defaults.", (ExePath / sConfigFile).string());
        return;
    }
    
    ini.parse(iniFile);
    ini.strip_trailing_comments();
    
    bEnableARFix = GetConfigAndLog(ini, "AspectRatio", "EnableARFix", true);
    iAspectWidth = GetConfigAndLog(ini, "AspectRatio", "AspectWidth", 4);
    iAspectHeight = GetConfigAndLog(ini, "AspectRatio", "AspectHeight", 3);
    
    if (iAspectHeight > 0) {
        fTargetARValue = static_cast<float>(iAspectWidth) / static_cast<float>(iAspectHeight);
    }
    SPDLOG_INFO("Target Aspect Ratio: {} / {} = {:.6f}", iAspectWidth, iAspectHeight, fTargetARValue);

    bEnableFOVFix = GetConfigAndLog(ini, "FOV", "EnableFOVFix", true);
    fFOVPercent = GetConfigAndLog(ini, "FOV", "FOVPercent", 120.0f);
}

void AspectRatio() {
    if (!bEnableARFix) return;

    std::uint8_t* arWriteScan = Memory::PatternScan(exeModule, 
        "75 0B F3 44 0F 5E F6 F3 44 0F 11 70 44 49 8B 4E 40 41 8B FF");

    if (arWriteScan) {
        std::uint8_t* patchAddress = arWriteScan + 2; 
        uint32_t hexAR = *reinterpret_cast<uint32_t*>(&fTargetARValue);

        std::uint8_t patchBytes[11] = {
            0xC7, 0x40, 0x44,                                
            static_cast<uint8_t>(hexAR & 0xFF),              
            static_cast<uint8_t>((hexAR >> 8) & 0xFF),       
            static_cast<uint8_t>((hexAR >> 16) & 0xFF),      
            static_cast<uint8_t>((hexAR >> 24) & 0xFF),      
            0x90, 0x90, 0x90, 0x90                           
        };

        DWORD oldProtect;
        if (VirtualProtect(patchAddress, sizeof(patchBytes), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            std::memcpy(patchAddress, patchBytes, sizeof(patchBytes));
            VirtualProtect(patchAddress, sizeof(patchBytes), oldProtect, &oldProtect);
            SPDLOG_INFO("AR Machine-Code Patch injected successfully at 0x{:X}", reinterpret_cast<uintptr_t>(patchAddress));
        } else {
            SPDLOG_ERROR("Failed to change memory protection for AR patch!");
        }
    } else {
        SPDLOG_ERROR("AR Writer pattern scan failed! Game updated?");
    }
}

void CameraFOV() {
    if (!bEnableFOVFix) return;

    std::uint8_t* fovScan = Memory::PatternScan(exeModule, "F3 0F 11 41 38 48 8B 43 50 48 8B 48 18 48"); 

    if (fovScan) {
        static SafetyHookMid FOVHook{};
        
        FOVHook = safetyhook::create_mid(fovScan, [](SafetyHookContext& ctx) {
            ctx.xmm0.f32[0] *= (fFOVPercent / 100.0f);
        });
        SPDLOG_INFO("FOV Hook injected successfully at 0x{:X}", reinterpret_cast<uintptr_t>(fovScan));
    } else {
        SPDLOG_ERROR("FOV pattern scan failed! Please verify CT AOB.");
    }
}

DWORD __stdcall Main(void*) {
    Sleep(1000); 
    
    Logging();
    Configuration();
    
    AspectRatio();
    CameraFOV();

    return 1;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        thisModule = hModule;
        if (HANDLE mainHandle = CreateThread(NULL, 0, Main, 0, NULL, 0)) {
            SetThreadPriority(mainHandle, THREAD_PRIORITY_HIGHEST);
            CloseHandle(mainHandle);
        }
    }
    return TRUE;
}