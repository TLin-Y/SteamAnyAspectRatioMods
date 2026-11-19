// Custom resolution and HUD fixes adapted from WO3UFix by Lyall (https://codeberg.org/Lyall/WO3UFix), MIT License, Copyright (c) 2025 Lyall.
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
std::string sConfigFile = "WO3U_ARFix.ini"; 

std::shared_ptr<spdlog::logger> logger;
std::string sLogFile = "WO3U_ARFix.log";
std::filesystem::path ExePath;

bool bEnableARFix = true;
int iAspectWidth = 4;
int iAspectHeight = 3;
float fTargetARValue = 1.3333333f;

bool bEnableFOVFix = true;
float fFOVPercent = 120.0f; 
float fTargetFOVValue = 0.01745329238f * 1.20f; 

std::pair<int, int> DesktopDimensions = { 0, 0 };
constexpr float fNativeAspect = 16.0f / 9.0f;
float fAspectRatio = 16.0f / 9.0f;
float fAspectMultiplier = 1.0f;
float fHUDWidth = 1920.0f;
float fHUDWidthOffset = 0.0f;
float fHUDHeight = 1080.0f;
float fHUDHeightOffset = 0.0f;

bool bCustomResolution = true;
int iCustomResX = 0;
int iCustomResY = 0;
bool bFixHUD = true;

int iCurrentResX = 0;
int iCurrentResY = 0;
std::uint8_t* HUD_1920 = nullptr;
std::uint8_t* HUD_1080 = nullptr;
std::uint8_t* HUD_1280 = nullptr;
std::uint8_t* HUD_720 = nullptr;
std::string sExeName = "WO3U.exe";

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

        logger = std::make_shared<spdlog::logger>("WO3U_Fix", std::make_shared<spdlog::sinks::rotating_file_sink_st>((ExePath / sLogFile).string(), 10 * 1024 * 1024, 1));
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
        spdlog::flush_on(spdlog::level::debug);
        spdlog::set_level(spdlog::level::debug); 
        
        SPDLOG_INFO("Warriors Orochi 3 Ultimate Final AR & FOV Fix Initialized");
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
    
    float fBaseFOV = 0.01745329238f;
    fTargetFOVValue = fBaseFOV * (fFOVPercent / 100.0f);
    SPDLOG_INFO("Target FOV Calculated: {:.6f} ({}%)", fTargetFOVValue, fFOVPercent);

    bCustomResolution = GetConfigAndLog(ini, "Custom Resolution", "Enabled", true);
    iCustomResX = GetConfigAndLog(ini, "Custom Resolution", "Width", 0);
    iCustomResY = GetConfigAndLog(ini, "Custom Resolution", "Height", 0);
    bFixHUD = GetConfigAndLog(ini, "Fix HUD", "Enabled", true);
}

void CalculateAspectRatio(bool bLog)
{
    if (iCurrentResX <= 0 || iCurrentResY <= 0)
        return;

    fAspectRatio = static_cast<float>(iCurrentResX) / iCurrentResY;
    fAspectMultiplier = fAspectRatio / fNativeAspect;

    fHUDWidth = static_cast<float>(iCurrentResY) * fNativeAspect;
    fHUDHeight = static_cast<float>(iCurrentResY);
    fHUDWidthOffset = (static_cast<float>(iCurrentResX) - fHUDWidth) / 2.0f;
    fHUDHeightOffset = 0.0f;

    if (fAspectRatio < fNativeAspect) {
        fHUDWidth = static_cast<float>(iCurrentResX);
        fHUDHeight = static_cast<float>(iCurrentResX) / fNativeAspect;
        fHUDWidthOffset = 0.0f;
        fHUDHeightOffset = (static_cast<float>(iCurrentResY) - fHUDHeight) / 2.0f;
    }

    if (bLog) {
        SPDLOG_INFO("----------");
        SPDLOG_INFO("Current Resolution: Resolution: {:d}x{:d}", iCurrentResX, iCurrentResY);
        SPDLOG_INFO("Current Resolution: fAspectRatio: {}", fAspectRatio);
        SPDLOG_INFO("Current Resolution: fAspectMultiplier: {}", fAspectMultiplier);
        SPDLOG_INFO("Current Resolution: fHUDWidth: {}", fHUDWidth);
        SPDLOG_INFO("Current Resolution: fHUDHeight: {}", fHUDHeight);
        SPDLOG_INFO("Current Resolution: fHUDWidthOffset: {}", fHUDWidthOffset);
        SPDLOG_INFO("Current Resolution: fHUDHeightOffset: {}", fHUDHeightOffset);
        SPDLOG_INFO("----------");
    }
}

void Resolution()
{
    if (bCustomResolution)
    {
        if (iCustomResX <= 0 || iCustomResY <= 0) {
            DesktopDimensions = Util::GetPhysicalDesktopDimensions();
            iCustomResX = DesktopDimensions.first;
            iCustomResY = DesktopDimensions.second;
            spdlog::info("Custom Resolution: Desktop Resolution: {}x{}", DesktopDimensions.first, DesktopDimensions.second);
        }

        std::uint8_t* CustomResolutionHorScanResult = Memory::PatternScan(exeModule, "80 07 00 00 00 0A 00 00 00 0F 00 00");
        std::uint8_t* CustomResolutionVertScanResult = Memory::PatternScan(exeModule, "C7 ?? ?? A0 05 00 00 C7 ?? ?? 70 08 00 00 83 ?? 09 73 ??");
        if (CustomResolutionHorScanResult && CustomResolutionVertScanResult) {
            SPDLOG_INFO("Custom Resolution: Horizontal: Address is {:s}+{:x}", sExeName, CustomResolutionHorScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            Memory::Write(CustomResolutionHorScanResult + 0x8, iCustomResX);
            SPDLOG_INFO("Custom Resolution: Vertical: Address is {:s}+{:x}", sExeName, CustomResolutionVertScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            Memory::Write(CustomResolutionVertScanResult + 0xA, iCustomResY);
            SPDLOG_INFO("Custom Resolution: Replaced 3840x2160 with {}x{}", iCustomResX, iCustomResY);
        }
        else {
            SPDLOG_ERROR("Custom Resolution: Pattern scan(s) failed.");
        }

        std::uint8_t* ResolutionStringScanResult = Memory::PatternScan(exeModule, "4C ?? ?? F3 ?? 0F ?? ?? ?? ?? 41 ?? 01 49 ?? ?? E8 ?? ?? ?? ?? 44 ?? ?? 75 ??");
        if (ResolutionStringScanResult) {
            spdlog::info("Resolution String: Address is {:s}+{:x}", sExeName, ResolutionStringScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
	        
            static bool bChangedResString = false;
            static std::string sTargetResolution = "3840 x 2160";
            static std::string sNewResolution = std::to_string(iCustomResX) + " x " + std::to_string(iCustomResY);
            
            static SafetyHookMid ResolutionStringMidHook{};
            ResolutionStringMidHook = safetyhook::create_mid(ResolutionStringScanResult,
                [](SafetyHookContext& ctx) {
                    if (!ctx.rax || bChangedResString) return;

                    char* str = (char*)ctx.rax;
                    if (strncmp(str, sTargetResolution.c_str(), sTargetResolution.size()) == 0 && sNewResolution.size() <= strlen(str)) {
                        SPDLOG_INFO("Resolution String: Changed {} to {}", str, sNewResolution);
                        strncpy_s(str, strlen(str) + 1, sNewResolution.c_str(), sNewResolution.size());
                        str[sNewResolution.size()] = '\0';
                        bChangedResString = true;
                    }
                });
        }
        else {
            SPDLOG_ERROR("Resolution String: Pattern scan failed.");
        } 
    }
        
    std::uint8_t* CurrentResolutionScanResult = Memory::PatternScan(exeModule, "0F ?? ?? ?? ?? ?? ?? 0F ?? ?? ?? ?? ?? ?? 0F ?? ?? ?? ?? ?? ?? 48 8B ?? ?? ?? ?? ?? 48 85 ?? 74 ??");
    if (CurrentResolutionScanResult) {
        SPDLOG_INFO("Current Resolution: Address is {:s}+{:x}", sExeName, CurrentResolutionScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        static SafetyHookMid CurrentResolutionMidHook{};
        CurrentResolutionMidHook = safetyhook::create_mid(CurrentResolutionScanResult,
            [](SafetyHookContext& ctx) {
                int iResX = ctx.xmm0.u32[3];
                int iResY = ctx.xmm1.u32[0];
                
                if (iResX != iCurrentResX || iResY != iCurrentResY) {
                    iCurrentResX = iResX;
                    iCurrentResY = iResY;
                    CalculateAspectRatio(true);
                }

                if (bFixHUD) {
                    if (HUD_1920 && HUD_1080 && HUD_1280 && HUD_720) {
                        if (fAspectRatio > fNativeAspect) {
                            Memory::Write(HUD_1920, 1080.0f * fAspectRatio);
                            Memory::Write(HUD_1080, 1080.0f);
                            Memory::Write(HUD_1280, 720.0f * fAspectRatio);
                            Memory::Write(HUD_720,  720.0f);
                        }
                        else {
                            Memory::Write(HUD_1920, 1920.0f);
                            Memory::Write(HUD_1080, 1920.0f / fAspectRatio);
                            Memory::Write(HUD_1280, 1280.0f);
                            Memory::Write(HUD_720,  1280.0f / fAspectRatio);
                        }
                    }
                }
            });
    }
    else {
        SPDLOG_ERROR("Current Resolution: Pattern scan failed.");
    }  
}

void HUD()
{
    std::uint8_t* HUD_1920ScanResult = Memory::PatternScan(exeModule, "F3 0F ?? ?? ?? ?? ?? ?? F3 0F ?? ?? ?? ?? ?? ?? F3 0F ?? ?? ?? 0F 57 ?? F3 0F ?? ?? ?? E8 ?? ?? ?? ??");
    std::uint8_t* HUD_1280ScanResult = Memory::PatternScan(exeModule, "F3 0F ?? ?? ?? ?? ?? ?? 48 8B ?? F3 0F ?? ?? ?? ?? ?? ?? 48 8B ?? ?? ?? ?? ?? F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? ??");
    if (HUD_1920ScanResult) {
        SPDLOG_INFO("HUD Size: 1920x1080: Address is {:s}+{:x}", sExeName, HUD_1920ScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        HUD_1920 = Memory::GetRelativeAddr(HUD_1920ScanResult + 0x4);
        HUD_1080 = Memory::GetRelativeAddr(HUD_1920ScanResult + 0xC);
        SPDLOG_INFO("HUD Size: 1280x720: Address is {:s}+{:x}", sExeName, HUD_1280ScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        HUD_1280 = Memory::GetRelativeAddr(HUD_1280ScanResult + 0x4);
        HUD_720  = Memory::GetRelativeAddr(HUD_1280ScanResult + 0xF);
    }
    else {
        SPDLOG_ERROR("HUD Size: Pattern scan(s) failed.");
    } 
    
    
    std::uint8_t* HUDOffsetScanResult = Memory::PatternScan(exeModule, "0F 5B ?? F3 0F ?? ?? 66 0F ?? ?? ?? 8B ?? ?? ?? ?? ?? 44 ?? ?? 0F ?? ?? ?? 0F B7 ?? 41 ?? ?? ??");
    std::uint8_t* HUDOffsetTransitionScanResult = Memory::PatternScan(exeModule, "0F 5B ?? F3 0F ?? ?? 66 0F ?? ?? ?? 0F ?? ?? ?? 0F B7 ?? 4A ?? ?? ?? ?? ?? ?? ?? 41 ?? ??");
    if (HUDOffsetScanResult && HUDOffsetTransitionScanResult) {
        SPDLOG_INFO("HUD: Offset: Address is {:s}+{:x}", sExeName, HUDOffsetScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        static SafetyHookMid HUDOffsetMidHook{};
        HUDOffsetMidHook = safetyhook::create_mid(HUDOffsetScanResult,
            [](SafetyHookContext& ctx) {
                if (*reinterpret_cast<int*>(ctx.rbx + 0x8C) == 0) {
                    if (ctx.rcx == 0x0 || ctx.rcx == 0x10 || ctx.rcx == 0x1A0) {
                        if (fAspectRatio > fNativeAspect)
                            ctx.xmm2.f32[0] = -1.0f / fAspectMultiplier;
                        else if (fAspectRatio < fNativeAspect)
                            ctx.xmm3.f32[0] = 1.0f * fAspectMultiplier;
                    }
                }
            });

        SPDLOG_INFO("HUD: Offset: Transition: Address is {:s}+{:x}", sExeName, HUDOffsetTransitionScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        static SafetyHookMid HUDOffsetTransitionMidHook{};
        HUDOffsetTransitionMidHook = safetyhook::create_mid(HUDOffsetTransitionScanResult,
            [](SafetyHookContext& ctx) {
                if (fAspectRatio > fNativeAspect)
                    ctx.xmm2.f32[0] = -1.0f / fAspectMultiplier;
                else if (fAspectRatio < fNativeAspect)
                    ctx.xmm3.f32[0] = 1.0f * fAspectMultiplier;
            });
    }
    else {
        SPDLOG_ERROR("HUD: Offset: Pattern scan(s) failed.");
    }  

    std::uint8_t* MenuPositionScanResult = Memory::PatternScan(exeModule, "F3 0F ?? ?? F3 0F ?? ?? ?? E8 ?? ?? ?? ?? 8B ?? ?? E8 ?? ?? ?? ?? 66 ?? ?? ?? ??");
    if (MenuPositionScanResult) {
        SPDLOG_INFO("HUD: Menu Position: Address is {:s}+{:x}", sExeName, MenuPositionScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        static SafetyHookMid MenuPositionHorMidHook{};
        MenuPositionHorMidHook = safetyhook::create_mid(MenuPositionScanResult,
            [](SafetyHookContext& ctx) {
                if (fAspectRatio > fNativeAspect)
                    ctx.xmm7.f32[0] = fHUDWidth;
            });

        static SafetyHookMid MenuPositionVerMidHook{};
        MenuPositionVerMidHook = safetyhook::create_mid(MenuPositionScanResult + 0x1F,
            [](SafetyHookContext& ctx) {
                if (fAspectRatio < fNativeAspect)
                    ctx.xmm10.f32[0] = fHUDHeight;
            });
    }
    else {
        SPDLOG_ERROR("HUD: Menu Position: Pattern scan failed.");
    } 

    std::uint8_t* PauseScreenCaptureScanResult = Memory::PatternScan(exeModule, "F3 0F ?? ?? EC ?? ?? ?? E8 ?? ?? ?? ?? 48 8B ?? ?? ?? 48 8B ?? ?? ?? 48 83 ?? ?? ?? C3");
    if (PauseScreenCaptureScanResult) {
        SPDLOG_INFO("HUD: Pause Screen Capture: Address is {:s}+{:x}", sExeName, PauseScreenCaptureScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        static SafetyHookMid PauseScreenCaptureMidHook{};
        PauseScreenCaptureMidHook = safetyhook::create_mid(PauseScreenCaptureScanResult,
            [](SafetyHookContext& ctx) {
                if (fAspectRatio > fNativeAspect) {
                    float widthOffset = (1280.0f - 720.0f * fAspectRatio) / 2.0f;
                    *reinterpret_cast<float*>(ctx.rbx + 0x156E0) = widthOffset;
                    *reinterpret_cast<float*>(ctx.rbx + 0x156E8) = static_cast<float>(iCurrentResX) + widthOffset;
                }
                else if (fAspectRatio < fNativeAspect) {
                    float heightOffset = (720.0f - 1280.0f / fAspectRatio) / 2.0f;
                    *reinterpret_cast<float*>(ctx.rbx + 0x156E4) = heightOffset;
                    ctx.xmm1.f32[0] = static_cast<float>(iCurrentResY) + heightOffset;
                }
            });
    }
    else {
        SPDLOG_ERROR("HUD: Pause Screen Capture: Pattern scan failed.");
    } 

    std::uint8_t* GauntletScreenCaptureScanResult = Memory::PatternScan(exeModule, "F3 0F ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B ?? ?? ?? ?? ?? 40 ?? ?? ?? 74 ?? 48 8B ?? E8 ?? ?? ?? ??");
    if (GauntletScreenCaptureScanResult) {
        SPDLOG_INFO("HUD: Gauntlet Screen Capture: Address is {:s}+{:x}", sExeName, GauntletScreenCaptureScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        static SafetyHookMid GauntletScreenCaptureMidHook{};
        GauntletScreenCaptureMidHook = safetyhook::create_mid(GauntletScreenCaptureScanResult,
            [](SafetyHookContext& ctx) {
                if (fAspectRatio > fNativeAspect) {
                    float widthOffset = (1280.0f - 720.0f * fAspectRatio) / 2.0f;
                    *reinterpret_cast<float*>(ctx.rbx + 0x15730) = widthOffset;
                    *reinterpret_cast<float*>(ctx.rbx + 0x15738) = static_cast<float>(iCurrentResX) + widthOffset;
                }
                else if (fAspectRatio < fNativeAspect) {
                    float heightOffset = (720.0f - 1280.0f / fAspectRatio) / 2.0f;
                    *reinterpret_cast<float*>(ctx.rbx + 0x15734) = heightOffset;
                    ctx.xmm0.f32[0] = static_cast<float>(iCurrentResY) + heightOffset;
                }
            });
    }
    else {
        SPDLOG_ERROR("HUD: Gauntlet Screen Capture: Pattern scan failed.");
    } 

    std::uint8_t* ConfirmationDialogPositionScanResult = Memory::PatternScan(exeModule, "0F ?? ?? 48 8B ?? ?? ?? ?? ?? ?? F3 0F ?? ?? F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? ?? ?? ??");
    if (ConfirmationDialogPositionScanResult) {
        SPDLOG_INFO("HUD: Confirmation Dialog Position: Address is {:s}+{:x}", sExeName, ConfirmationDialogPositionScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        static SafetyHookMid ConfirmationDialogPositionMidHook{};
        ConfirmationDialogPositionMidHook = safetyhook::create_mid(ConfirmationDialogPositionScanResult,
            [](SafetyHookContext& ctx) {
                ctx.xmm4.f32[0] = 1280.0f;
                ctx.xmm5.f32[0] = 720.0f;
            });
    }
    else {
        SPDLOG_ERROR("HUD: Confirmation Dialog Position: Pattern scan failed.");
    } 

    std::uint8_t* TipsDialogPositionScanResult = Memory::PatternScan(exeModule, "0F 28 ?? 48 8B ?? ?? F3 0F ?? ?? ?? F3 0F ?? ?? ?? ?? ?? ?? F3 0F ?? ?? ?? F3 0F ?? ?? F3 0F ?? ?? ?? ?? ?? ??");
    if (TipsDialogPositionScanResult) {
        SPDLOG_INFO("HUD: Tips Dialog Position: Address is {:s}+{:x}", sExeName, TipsDialogPositionScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        static SafetyHookMid TipsDialogPositionMidHook{};
        TipsDialogPositionMidHook = safetyhook::create_mid(TipsDialogPositionScanResult,
            [](SafetyHookContext& ctx) {
                ctx.xmm4.f32[0] = 1280.0f;
                ctx.xmm5.f32[0] = 720.0f;
            });
    }
    else {
        SPDLOG_ERROR("HUD: Tips Dialog Position: Pattern scan failed.");
    } 

    std::uint8_t* OptionsMenuBGScanResult = Memory::PatternScan(exeModule, "81 ?? ?? ?? ?? ?? FF FF FF ?? F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? ??");
    if (OptionsMenuBGScanResult) {
        SPDLOG_INFO("HUD: Options Menu BG: Address is {:s}+{:x}", sExeName, OptionsMenuBGScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        static SafetyHookMid OptionsMenuBGMidHook{};
        OptionsMenuBGMidHook = safetyhook::create_mid(OptionsMenuBGScanResult,
            [](SafetyHookContext& ctx) {
                ctx.xmm4.f32[0] = 1280.0f;
                ctx.xmm0.f32[0] = 720.0f;
            });
    }
    else {
        SPDLOG_ERROR("HUD: Options Menu BG: Pattern scan failed.");
    } 

    std::uint8_t* FreeModeMenuBGScanResult = Memory::PatternScan(exeModule, "48 C7 ?? ?? ?? 00 00 00 00 48 8D ?? ?? 48 8B ?? ?? ?? 0F 57 ??");
    if (FreeModeMenuBGScanResult) {
        SPDLOG_INFO("HUD: Free Mode Menu BG: Address is {:s}+{:x}", sExeName, FreeModeMenuBGScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        static SafetyHookMid FreeModeMenuBGMidHook{};
        FreeModeMenuBGMidHook = safetyhook::create_mid(FreeModeMenuBGScanResult,
            [](SafetyHookContext& ctx) {
                if (fAspectRatio > fNativeAspect)
                    ctx.rdx = static_cast<int>(fHUDWidth);
                else if (fAspectRatio < fNativeAspect)
                    ctx.rsi = static_cast<int>(fHUDHeight);
            });
    }
    else {
        SPDLOG_ERROR("HUD: Free Mode Menu BG: Pattern scan failed.");
    }

    std::uint8_t* DuelModeMenuBGScanResult = Memory::PatternScan(exeModule, "66 0F ?? ?? 48 8B ?? ?? ?? 66 0F ?? ?? 0F 5B ?? 48 89 ?? ?? 0F 5B ??");
    if (DuelModeMenuBGScanResult) {
        SPDLOG_INFO("HUD: Duel Mode Menu BG: Address is {:s}+{:x}", sExeName, DuelModeMenuBGScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        static SafetyHookMid DuelModeMenuBGMidHook{};
        DuelModeMenuBGMidHook = safetyhook::create_mid(DuelModeMenuBGScanResult,
            [](SafetyHookContext& ctx) {
                if (fAspectRatio > fNativeAspect)
                    ctx.rdi = static_cast<int>(fHUDWidth);
                else if (fAspectRatio < fNativeAspect)
                    ctx.rbp = static_cast<int>(fHUDHeight);
            });
    }
    else {
        SPDLOG_ERROR("HUD: Duel Mode Menu BG: Pattern scan failed.");
    }

    std::uint8_t* PreparationsMenuBGScanResult = Memory::PatternScan(exeModule, "66 0F ?? ?? 48 8B ?? 66 0F ?? ?? 48 8D ?? ?? ?? 0F 5B ?? 0F 5B ??");
    if (PreparationsMenuBGScanResult) {
        SPDLOG_INFO("HUD: Preparations Menu BG: Address is {:s}+{:x}", sExeName, PreparationsMenuBGScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        static SafetyHookMid PreparationsMenuBGMidHook{};
        PreparationsMenuBGMidHook = safetyhook::create_mid(PreparationsMenuBGScanResult,
            [](SafetyHookContext& ctx) {
                if (fAspectRatio > fNativeAspect)
                    ctx.rdx = static_cast<int>(fHUDWidth);
                else if (fAspectRatio < fNativeAspect)
                    ctx.rdi = static_cast<int>(fHUDHeight);
            });
    }
    else {
        SPDLOG_ERROR("HUD: Preparations Menu BG: Pattern scan failed.");
    }

    std::uint8_t* LoadingMenuBGScanResult = Memory::PatternScan(exeModule, "66 0F ?? ?? 48 8B ?? ?? ?? 48 89 ?? ?? 66 0F ?? ?? 0F 5B ?? 48 C7 ?? ?? ?? 00 00 00 00");
    if (LoadingMenuBGScanResult) {
        SPDLOG_INFO("HUD: Loading Menu BG: Address is {:s}+{:x}", sExeName, LoadingMenuBGScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        static SafetyHookMid LoadingMenuBGMidHook{};
        LoadingMenuBGMidHook = safetyhook::create_mid(LoadingMenuBGScanResult,
            [](SafetyHookContext& ctx) {
                if (fAspectRatio > fNativeAspect)
                    ctx.rdx = static_cast<int>(fHUDWidth);
                else if (fAspectRatio < fNativeAspect)
                    ctx.rsi = static_cast<int>(fHUDHeight);
            });
    }
    else {
        SPDLOG_ERROR("HUD: Loading Menu BG: Pattern scan failed.");
    }

    std::uint8_t* NowLoadingBarScanResult = Memory::PatternScan(exeModule, "49 89 ?? ?? 48 8B ?? ?? F3 0F ?? ?? ?? C7 ?? ?? 00 80 2A 44");
    if (NowLoadingBarScanResult) {
        SPDLOG_INFO("HUD: Now Loading Bar: Address is {:s}+{:x}", sExeName, NowLoadingBarScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        static SafetyHookMid NowLoadingBarMidHook{};
        NowLoadingBarMidHook = safetyhook::create_mid(NowLoadingBarScanResult,
            [](SafetyHookContext& ctx) {
                ctx.xmm4.f32[0] = 1280.0f;
            });
    }
    else {
        SPDLOG_ERROR("HUD: Now Loading Bar: Pattern scan failed.");
    }

    std::uint8_t* HUDClippingScanResult = Memory::PatternScan(exeModule, "C7 ?? 14 00 01 00 49 8B ?? ?? ?? ?? ?? 89 ?? ?? 49 8B ?? ?? ?? ?? ??");
    if (HUDClippingScanResult) {
        SPDLOG_INFO("HUD: Clipping: Address is {:s}+{:x}", sExeName, HUDClippingScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        static SafetyHookMid HUDClippingMidHook{};
        HUDClippingMidHook = safetyhook::create_mid(HUDClippingScanResult,
            [](SafetyHookContext& ctx) {
                if (fAspectRatio > fNativeAspect)
                    ctx.rcx = static_cast<int>(static_cast<float>(ctx.rcx) + fHUDWidthOffset);
                else if (fAspectRatio < fNativeAspect)
                    ctx.r9 = static_cast<int>(static_cast<float>(ctx.r9) + fHUDHeightOffset);
            });
    }
    else {
        SPDLOG_ERROR("HUD: Clipping: Pattern scan failed.");
    }

    std::uint8_t* GauntletCharacterSelectorScanResult = Memory::PatternScan(exeModule, "F3 0F ?? ?? ?? ?? ?? ?? C6 ?? ?? ?? ?? ?? 02 81 ?? ?? ?? ?? ?? FF FF EB FF");
    if (GauntletCharacterSelectorScanResult) {
        SPDLOG_INFO("HUD: Gauntlet Character Selector: Address is {:s}+{:x}", sExeName, GauntletCharacterSelectorScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        static SafetyHookMid GauntletCharacterSelectorMidHook{};
        GauntletCharacterSelectorMidHook = safetyhook::create_mid(GauntletCharacterSelectorScanResult,
            [](SafetyHookContext& ctx) {
                if (fAspectRatio > fNativeAspect) {
                    float widthOffset = (1280.0f - 720.0f * fAspectRatio) / 2.0f;
                    *reinterpret_cast<float*>(ctx.rdi + 0x9A48) = widthOffset;
                    *reinterpret_cast<float*>(ctx.rdi + 0x9A50) = static_cast<float>(iCurrentResX) + widthOffset;
                }
                else if (fAspectRatio < fNativeAspect) {
                    float heightOffset = (720.0f - 1280.0f / fAspectRatio) / 2.0f;
                    *reinterpret_cast<float*>(ctx.rdi + 0x9A4C) = heightOffset;
                    ctx.xmm6.f32[0] = static_cast<float>(iCurrentResY) + heightOffset;
                }
            });
    }
    else {
        SPDLOG_ERROR("HUD: Gauntlet Character Selector: Pattern scan failed.");
    }

    std::uint8_t* GauntletMovieSubtitlesScanResult = Memory::PatternScan(exeModule, "F3 0F ?? ?? ?? ?? ?? ?? 45 0F ?? ?? 44 ?? ?? ?? 0F 84 ?? ?? ?? ?? E8 ?? ?? ?? ??");
    if (GauntletMovieSubtitlesScanResult) {
        SPDLOG_INFO("HUD: Gauntlet Movie Subtitles: Address is {:s}+{:x}", sExeName, GauntletMovieSubtitlesScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        static SafetyHookMid GauntletMovieSubtitlesMidHook{};
        GauntletMovieSubtitlesMidHook = safetyhook::create_mid(GauntletMovieSubtitlesScanResult,
            [](SafetyHookContext& ctx) {
                ctx.xmm8.f32[0] = 1280.0f;
            });
    }
    else {
        SPDLOG_ERROR("HUD: Gauntlet Movie Subtitles: Pattern scan failed.");
    }

    std::uint8_t* AnnouncementsScanResult = Memory::PatternScan(exeModule, "8B ?? 80 ?? 00 66 0F ?? ?? 0F 5B ?? F3 41 ?? ?? ?? F3 41 ?? ?? ??");
    if (AnnouncementsScanResult) {
        SPDLOG_INFO("HUD: Announcements: Address is {:s}+{:x}", sExeName, AnnouncementsScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        static SafetyHookMid AnnouncementsMidHook{};
        AnnouncementsMidHook = safetyhook::create_mid(AnnouncementsScanResult + 0x2,
            [](SafetyHookContext& ctx) {
                if (fAspectRatio > fNativeAspect)
                    ctx.rcx = static_cast<int>(fHUDWidth);
            });
    }
    else {
        SPDLOG_ERROR("HUD: Announcements: Pattern scan failed.");
    }
    
    std::uint8_t* FadesScanResult = Memory::PatternScan(exeModule, "48 89 ?? ?? 81 ?? ?? ?? ?? ?? FF 03 ?? ?? 7D ?? 48 8B ?? ?? ?? ?? ?? 48 8D ?? ?? 48 89 ?? 48 8B ?? ?? ?? ?? ??");
    if (FadesScanResult) {
        SPDLOG_INFO("HUD: Fades: Address is {:s}+{:x}", sExeName, FadesScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        static SafetyHookMid FadesMidHook{};
        FadesMidHook = safetyhook::create_mid(FadesScanResult + 0x4,
            [](SafetyHookContext& ctx) {
                if (fAspectRatio > fNativeAspect) {
                    float widthOffset = (1280.0f - 720.0f * fAspectRatio) / 2.0f;
                    *reinterpret_cast<float*>(ctx.rdi + 0x38) = widthOffset;
                    *reinterpret_cast<float*>(ctx.rdi + 0x40) = static_cast<float>(iCurrentResX) - widthOffset;
                }
                else if (fAspectRatio < fNativeAspect) {
                     float heightOffset = (720.0f - 1280.0f / fAspectRatio) / 2.0f;
                    *reinterpret_cast<float*>(ctx.rdi + 0x3C) = heightOffset;
                    *reinterpret_cast<float*>(ctx.rdi + 0x44) = static_cast<float>(iCurrentResY) - heightOffset;
                }
            });
    }
    else {
        SPDLOG_ERROR("HUD: Fades: Pattern scan failed.");
    }

    std::uint8_t* IntroLogosScanResult = Memory::PatternScan(exeModule, "66 0F ?? ?? 33 ?? 66 0F ?? ?? 0F ?? ?? 0F ?? ?? 39 ?? ?? 7E ??");
    if (IntroLogosScanResult) {
        SPDLOG_INFO("HUD: Intro Logos: Address is {:s}+{:x}", sExeName, IntroLogosScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        static SafetyHookMid IntroLogosMidHook{};
        IntroLogosMidHook = safetyhook::create_mid(IntroLogosScanResult,
            [](SafetyHookContext& ctx) {
                if (fAspectRatio > fNativeAspect)
                    ctx.rdx = static_cast<int>(fHUDWidth);
                else if (fAspectRatio < fNativeAspect)
                    ctx.rdi = static_cast<int>(fHUDHeight);
            });
    }
    else {
        SPDLOG_ERROR("HUD: Intro Logos: Pattern scan failed.");
    }

    std::uint8_t* BattlefieldSelectionScanResult = Memory::PatternScan(exeModule, "F3 0F ?? ?? ?? ?? ?? ?? F3 0F ?? ?? ?? ?? ?? ?? F3 0F ?? ?? ?? F3 0F ?? ?? ?? 48 8B ?? ?? 48 89 ?? ?? ?? ?? ?? 48 8B ?? ?? ?? ?? ?? 48 83 ?? ??");
    if (BattlefieldSelectionScanResult) {
        SPDLOG_INFO("HUD: Battlefield Selection: Address is {:s}+{:x}", sExeName, BattlefieldSelectionScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        static SafetyHookMid BattlefieldSelectionMidHook{};
        BattlefieldSelectionMidHook = safetyhook::create_mid(BattlefieldSelectionScanResult,
            [](SafetyHookContext& ctx) {
                if (fAspectRatio > fNativeAspect)
                    ctx.xmm0.f32[0] += (720.0f * fAspectRatio - 1280.0f) / 2.0f;
            });
    }
    else {
        SPDLOG_ERROR("HUD: Battlefield Selection: Pattern scan failed.");
    }

    std::uint8_t* MoviesNormalScanResult = Memory::PatternScan(exeModule, "48 8B ?? 0F 57 ?? F3 0F ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8D ?? ?? ?? 48 8B ?? E8 ?? ?? ?? ?? 83 FE 02 48 8B ?? 0F ?? ??");
    std::uint8_t* MoviesMenuScanResult = Memory::PatternScan(exeModule, "48 8B ?? 0F 57 ?? F3 0F ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8D ?? ?? ?? 48 8B ?? E8 ?? ?? ?? ?? 83 FB 02 48 8B ?? 0F ?? ??");
    if (MoviesNormalScanResult && MoviesMenuScanResult) {
        SPDLOG_INFO("HUD: Movies: Normal: Address is {:s}+{:x}", sExeName, MoviesNormalScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        static SafetyHookMid MoviesNormalMidHook{};
        MoviesNormalMidHook = safetyhook::create_mid(MoviesNormalScanResult,
            [](SafetyHookContext& ctx) {
                if (fAspectRatio > fNativeAspect)
                    *reinterpret_cast<int*>(ctx.rsp + 0x40) = static_cast<int>(fHUDWidth);
                else if (fAspectRatio < fNativeAspect)
                    *reinterpret_cast<int*>(ctx.rsp + 0x44) = static_cast<int>(fHUDHeight);
            });

        SPDLOG_INFO("HUD: Movies: Menu: Address is {:s}+{:x}", sExeName, MoviesMenuScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        static SafetyHookMid MoviesMenuMidHook{};
        MoviesMenuMidHook = safetyhook::create_mid(MoviesMenuScanResult,
            [](SafetyHookContext& ctx) {
                if (fAspectRatio > fNativeAspect)
                    *reinterpret_cast<int*>(ctx.rsp + 0x40) = static_cast<int>(fHUDWidth);
                else if (fAspectRatio < fNativeAspect)
                    *reinterpret_cast<int*>(ctx.rsp + 0x44) = static_cast<int>(fHUDHeight);
            });
    }
    else {
        SPDLOG_ERROR("HUD: Movies: Pattern scan(s) failed.");
    }  
}


void AspectRatio() {
    if (!bEnableARFix) return;

    std::uint8_t* arWriteScan = Memory::PatternScan(exeModule, 
        "F3 0F 10 4C 24 70 F3 0F 11 81 5C 01 00 00 F3 0F 11 89 4C 01 00 00");

    if (arWriteScan) {
        static SafetyHookMid ARWriteHook{};
        std::uint8_t* hookTarget = arWriteScan + 14;

        ARWriteHook = safetyhook::create_mid(hookTarget, [](SafetyHookContext& ctx) {
            if (std::abs(ctx.xmm1.f32[0] - 1.7777778f) < 0.01f) {
                ctx.xmm1.f32[0] = fTargetARValue;
            }
        });
        SPDLOG_INFO("AR Writer Hook injected successfully at 0x{:X}", reinterpret_cast<uintptr_t>(hookTarget));
    } else {
        SPDLOG_ERROR("AR Writer pattern scan failed!");
    }
}

void PatchStaticFOV() {
    if (!bEnableFOVFix) return;

    uintptr_t fovPointer = reinterpret_cast<uintptr_t>(exeModule) + 0xADF63C;
    DWORD oldProtect;

    if (VirtualProtect(reinterpret_cast<LPVOID>(fovPointer), sizeof(float), PAGE_READWRITE, &oldProtect)) {
        std::memcpy(reinterpret_cast<void*>(fovPointer), &fTargetFOVValue, sizeof(float));
        VirtualProtect(reinterpret_cast<LPVOID>(fovPointer), sizeof(float), oldProtect, &oldProtect);
        
        SPDLOG_INFO("Static FOV memory OVERWRITTEN successfully at 0x{:X} to {:.6f}", fovPointer, fTargetFOVValue);
    } else {
        SPDLOG_ERROR("Failed to bypass memory protection for FOV! Error Code: {}", GetLastError());
    }
}

DWORD __stdcall Main(void*) {
    Logging();
    Configuration();
    Resolution();
    HUD();

    Sleep(500);

    AspectRatio();
    PatchStaticFOV(); 

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