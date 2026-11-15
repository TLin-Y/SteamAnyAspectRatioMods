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
float fFOVScale = 1.0f;

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
    fFOVScale = GetConfigAndLog(ini, "FOV", "FOVScale", 1.0f);
    if (!(fFOVScale > 0.0f && fFOVScale <= 2.0f)) {
        SPDLOG_ERROR("FOVScale {} out of range (0 - 2.0); using 1.0.", fFOVScale);
        fFOVScale = 1.0f;
    }
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

void FOV() {
    if (!bEnableFOVFix || fFOVScale == 1.0f) return;

    auto hits = Memory::PatternScanAll(exeModule,
        "F3 0F 10 ?? 38 F3 0F 59 05 ?? ?? ?? ?? 0F 29 74 24 30 F3 0F 10 ?? 30 F3 0F 59 05 ?? ?? ?? ?? 0F 29 7C 24 20 F3 0F 10 ?? 34 F3 0F 59 05 ?? ?? ?? ?? E8");
    if (hits.size() != 1) {
        SPDLOG_ERROR("Camera projection code found {} times (expected 1); FOV not changed.", hits.size());
        return;
    }

    static int cameraReg = hits[0][3] & 7;
    static SafetyHookMid FOVHook{};
    FOVHook = safetyhook::create_mid(hits[0] + 0x29, [](SafetyHookContext& ctx) {
        std::uintptr_t regs[8] = { ctx.rax, ctx.rcx, ctx.rdx, ctx.rbx, ctx.rsp, ctx.rbp, ctx.rsi, ctx.rdi };
        std::uintptr_t camera = regs[cameraReg];
        if (bEnableARFix && *reinterpret_cast<float*>(camera + 0x44) != fTargetARValue) return;
        float angle = ctx.xmm0.f32[0] * fFOVScale;
        if (angle < 0.002f) angle = 0.002f;
        if (angle > 2.96f) angle = 2.96f;
        ctx.xmm0.f32[0] = angle;
    });
    if (FOVHook) {
        SPDLOG_INFO("FOV hook installed at 0x{:X}", reinterpret_cast<uintptr_t>(hits[0] + 0x29));
    } else {
        SPDLOG_ERROR("FOV hook could not be installed.");
    }
}

DWORD __stdcall Main(void*) {
    Sleep(2000); 
    
    Logging();
    Configuration();
    
    AspectRatioFix();
    FOV();

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