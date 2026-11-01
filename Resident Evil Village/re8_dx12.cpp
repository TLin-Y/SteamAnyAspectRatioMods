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
#include <windows.h>
#include <string>

HMODULE exeModule = GetModuleHandle(NULL);
HMODULE thisModule;

inipp::Ini<char> ini;
std::string sConfigFile = "RE8_AR_FOV_Fix.ini"; 

std::shared_ptr<spdlog::logger> logger;
std::filesystem::path ExePath;

bool bEnableARFix = true;
float fTargetARValue = 1.3333333f;

bool bEnableFOVFix = true;
float fTargetFOV = 95.0f;
float fTargetFOVADS = 70.0f;

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
        std::ofstream file(ExePath / "RE8_Fix.log", std::ios::trunc);
        if (file.is_open()) file.close();

        logger = std::make_shared<spdlog::logger>("RE8_Fix", std::make_shared<spdlog::sinks::rotating_file_sink_st>((ExePath / "RE8_Fix.log").string(), 10 * 1024 * 1024, 1));
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
        spdlog::flush_on(spdlog::level::debug);
        spdlog::set_level(spdlog::level::debug); 
        
        SPDLOG_INFO("Resident Evil 8 DX12 (AR + FOV) Ultimate Fix Initialized");
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
    int iAspectWidth = GetConfigAndLog(ini, "AspectRatio", "AspectWidth", 4);
    int iAspectHeight = GetConfigAndLog(ini, "AspectRatio", "AspectHeight", 3);
    if (iAspectHeight > 0) {
        fTargetARValue = static_cast<float>(iAspectWidth) / static_cast<float>(iAspectHeight);
    }
    
    bEnableFOVFix = GetConfigAndLog(ini, "FOV", "EnableFOVFix", true);
    fTargetFOV = GetConfigAndLog(ini, "FOV", "IdleFOV", 95.0f);
    fTargetFOVADS = GetConfigAndLog(ini, "FOV", "AimFOV", 70.0f);
}

void AspectRatioFix() {
    if (!bEnableARFix) return;

    std::uint8_t* arWriteScan = Memory::PatternScan(exeModule, 
        "75 0B F3 44 0F 5E F6 F3 44 0F 11 70 44 48 8B 4E 48 41 8B FD");

    if (arWriteScan) {
        static SafetyHookMid ARWriteHook{};
        
        std::uint8_t* hookTarget = arWriteScan + 13;

        ARWriteHook = safetyhook::create_mid(hookTarget, [](SafetyHookContext& ctx) {
            if (std::abs(ctx.xmm14.f32[0] - 1.7777778f) < 0.01f) {
                if (ctx.rax) {
                    float* pAR = reinterpret_cast<float*>(ctx.rax + 0x44);
                    *pAR = fTargetARValue;
                }
            }
        });
        SPDLOG_INFO("AR Hook injected successfully for DX12 at 0x{:X}", reinterpret_cast<uintptr_t>(hookTarget));
    } else {
        SPDLOG_ERROR("AR pattern scan failed on DX12 version!");
    }
}

uintptr_t ResolveRIP(uintptr_t instructionAddress, int offsetToDisp, int instructionSize) {
    int32_t disp = *reinterpret_cast<int32_t*>(instructionAddress + offsetToDisp);
    return instructionAddress + instructionSize + disp;
}

DWORD __stdcall FOVThread(void*) {
    if (!bEnableFOVFix) return 0;

    std::uint8_t* scan = Memory::PatternScan(exeModule, 
        "48 8B ? ? ? ? ? 48 39 ? ? 48 0F ? ? EB ? 48 8B ? 48 8B ? ? 48 39 ? ? 74 ? 0F 57");
        
    if (!scan) {
        SPDLOG_ERROR("FOV basePlayer AOB scan failed for DX12!");
        return 0;
    }

    uintptr_t basePlayerPtr = ResolveRIP(reinterpret_cast<uintptr_t>(scan), 3, 7);
    SPDLOG_INFO("FOV BasePlayer resolved to: 0x{:X}", basePlayerPtr);

    bool bFovLogged = false;

    while (true) {
        Sleep(100);

        __try {
            uintptr_t ptr = *reinterpret_cast<uintptr_t*>(basePlayerPtr);
            if (!ptr) { bFovLogged = false; continue; }

            ptr = *reinterpret_cast<uintptr_t*>(ptr + 0x1F8); if (!ptr) { bFovLogged = false; continue; }
            ptr = *reinterpret_cast<uintptr_t*>(ptr + 0x68);  if (!ptr) { bFovLogged = false; continue; }
            ptr = *reinterpret_cast<uintptr_t*>(ptr + 0x78);  if (!ptr) { bFovLogged = false; continue; }
            ptr = *reinterpret_cast<uintptr_t*>(ptr + 0x130); if (!ptr) { bFovLogged = false; continue; }
            ptr = *reinterpret_cast<uintptr_t*>(ptr + 0x20);  if (!ptr) { bFovLogged = false; continue; }
            ptr = *reinterpret_cast<uintptr_t*>(ptr + 0x20);  if (!ptr) { bFovLogged = false; continue; }
            ptr = *reinterpret_cast<uintptr_t*>(ptr + 0x50);  if (!ptr) { bFovLogged = false; continue; }

            float* idleFov = reinterpret_cast<float*>(ptr + 0x38);
            float* adsFov  = reinterpret_cast<float*>(ptr + 0x3C);
            
            if (!bFovLogged) {
                SPDLOG_INFO("Original FOV detected -> Idle: {:.2f}, ADS: {:.2f}", *idleFov, *adsFov);
            }

            *idleFov = fTargetFOV;
            *adsFov = fTargetFOVADS;

            if (!bFovLogged) {
                SPDLOG_INFO("New FOV successfully applied -> Idle: {:.2f}, ADS: {:.2f}", *idleFov, *adsFov);
                bFovLogged = true;
            }

        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return 0;
}

DWORD __stdcall Main(void*) {
    Sleep(2000); 
    
    Logging();
    Configuration();
    
    AspectRatioFix();
    
    if (HANDLE hFOVThread = CreateThread(NULL, 0, FOVThread, 0, NULL, 0)) {
        CloseHandle(hFOVThread);
    }

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