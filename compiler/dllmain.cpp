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

HMODULE exeModule = GetModuleHandle(NULL);
HMODULE thisModule;

// Ini
inipp::Ini<char> ini;
std::string sConfigFile = sFixName + ".ini";
std::filesystem::path FixPath;

// Logger
std::shared_ptr<spdlog::logger> logger;
std::string sLogFile = sFixName + ".log";
std::filesystem::path ExePath;
std::string sExeName;

// Aspect ratio / FOV / HUD
std::pair<int, int> DesktopDimensions = { 0, 0 };
constexpr float fPi = std::numbers::pi_v<float>;
constexpr float fNativeAspect = 16.0f / 9.0f;
float fAspectRatio = 16.0f / 9.0f;
float fAspectMultiplier = 1.0f;
float fHUDWidth = 1920.0f;
float fHUDWidthOffset = 0.0f;
float fHUDHeight = 1080.0f;
float fHUDHeightOffset = 0.0f;

// Ini settings
bool bDisableLetterboxing = false;
bool bDisableDithering = false;
bool bFixResolution = false;
bool bFixFOV = false;
bool bAdjustHUDSize = false;

// Variables
int iCurrentResX = 0;
int iCurrentResY = 0;
std::string sHUDObject;

template<typename T>
T GetConfigAndLog(inipp::Ini<char>& ini, const std::string& section, const std::string& key, T defaultValue)
{
    T value = defaultValue;
    inipp::get_value(ini.sections[section], key, value);
    SPDLOG_INFO("Config Parse: [{}]: {} = {}", section, key, value);
    return value;
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

void Logging()
{
    // Get path to DLL
    WCHAR dllPath[_MAX_PATH] = {0};
    GetModuleFileNameW(thisModule, dllPath, MAX_PATH);
    FixPath = dllPath;
    FixPath = FixPath.remove_filename();

    // Get game name and exe path
    WCHAR gameExePath[_MAX_PATH] = {0};
    GetModuleFileNameW(exeModule, gameExePath, MAX_PATH);
    ExePath = gameExePath;
    sExeName = ExePath.filename().string();
    ExePath = ExePath.remove_filename();

    // spdlog initialisation
    try
    {
        std::ofstream file(ExePath / sLogFile, std::ios::trunc);
        if (file.is_open()) file.close();

        logger = std::make_shared<spdlog::logger>(sFixName, std::make_shared<spdlog::sinks::rotating_file_sink_st>((ExePath / sLogFile).string(), 10 * 1024 * 1024, 1));
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
        spdlog::flush_on(spdlog::level::debug);
        spdlog::set_level(spdlog::level::debug);

        SPDLOG_INFO("----------");
        SPDLOG_INFO("{:s} v{:s}", sFixName, sFixVersion);
        SPDLOG_INFO("----------");
        SPDLOG_INFO("Log File: {}", (FixPath / sLogFile).string());
        SPDLOG_INFO("----------");
        SPDLOG_INFO("Module Name: {:s}", sExeName);
        SPDLOG_INFO("Module Path: {}", ExePath.string());
        SPDLOG_INFO("Module Address: 0x{:x}", reinterpret_cast<uintptr_t>(exeModule));
        SPDLOG_INFO("Module Timestamp: {:d}", Memory::GetModuleTimestamp(exeModule));
        SPDLOG_INFO("----------");
    }
    catch (const spdlog::spdlog_ex &ex) {
        Util::ConsoleExit(std::string("Log initialisation failed: ") + ex.what(), thisModule);
    }
}

void Configuration()
{
    // Inipp initialisation
    std::ifstream iniFile(FixPath / sConfigFile);
    if (!iniFile) {
        spdlog::shutdown(); // Flush log
        Util::ConsoleExit("Could not locate config file.\nMake sure " + sConfigFile + " is located in " + FixPath.string() + sConfigFile, thisModule);
    }

    SPDLOG_INFO("Config File: {}", (FixPath / sConfigFile).string());
    ini.parse(iniFile);
    ini.strip_trailing_comments();

    // Read ini
    bDisableLetterboxing = GetConfigAndLog(ini, "Disable Letterboxing", "Enabled", false);
    bDisableDithering = GetConfigAndLog(ini, "Disable Character Dithering", "Enabled", false);
    bFixResolution = GetConfigAndLog(ini, "Fix Resolution", "Enabled", false);
    bFixFOV = GetConfigAndLog(ini, "Fix FOV", "Enabled", false);
    bAdjustHUDSize = GetConfigAndLog(ini, "Adjust HUD Size", "Enabled", false);

    SPDLOG_INFO("----------");
}

void Resolution()
{
    // Get current resolution
    std::uint8_t* CurrentResolutionScanResult = Memory::PatternScan(exeModule, "2B ?? 44 ?? ?? 8B ?? 48 8B ?? ?? ?? 99 2B ?? D1 ?? 89 ?? ?? ?? ?? ??");
    if (CurrentResolutionScanResult) {
        SPDLOG_INFO("Current Resolution: Address is {:s}+{:x}", sExeName, CurrentResolutionScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        static SafetyHookMid CurrentResolutionMidHook{};
        CurrentResolutionMidHook = safetyhook::create_mid(CurrentResolutionScanResult,
            [](SafetyHookContext& ctx) {
                // Stop scaling to 21:9/16:9
                if (bFixResolution) {
                    ctx.rdi = ctx.rbp;
                    ctx.rsi = ctx.r14;
                }

                int iResX = static_cast<int>(ctx.rdi);
                int iResY = static_cast<int>(ctx.rsi);

                // Log resolution
                if (iResX != iCurrentResX || iResY != iCurrentResY) {
                    iCurrentResX = iResX;
                    iCurrentResY = iResY;
                    CalculateAspectRatio(true);
                }
            });
    }
    else {
        SPDLOG_ERROR("Current Resolution: Pattern scan failed.");
    }
}

void FOV()
{
    if (bFixFOV)
    {
        // Gameplay FOV
        std::uint8_t* GameplayFOVScanResult = Memory::PatternScan(exeModule, "F3 0F ?? ?? ?? ?? ?? ?? 48 8B ?? ?? ?? ?? ?? 48 8B ?? ?? ?? ?? ?? 8B ?? ?? ?? ?? ?? 0F ?? ??");
        if (GameplayFOVScanResult) {
            SPDLOG_INFO("FOV: Gameplay: Address is {:s}+{:x}", sExeName, GameplayFOVScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            static SafetyHookMid GameplayFOVMidHook{};
            GameplayFOVMidHook = safetyhook::create_mid(GameplayFOVScanResult,
                [](SafetyHookContext& ctx) {
                    if (fAspectRatio < fNativeAspect)
                        ctx.xmm1.f32[0] = atanf(tanf(ctx.xmm1.f32[0] / 2.0f) / fAspectRatio * fNativeAspect) * 2.0f;
                });
        }
        else {
            SPDLOG_ERROR("FOV: Gameplay: Pattern scan failed.");
        }

        // Cutscene FOV
        std::uint8_t* CutsceneFOVScanResult = Memory::PatternScan(exeModule, "F3 0F ?? ?? ?? ?? ?? ?? 40 0F ?? ?? 48 8B ?? E8 ?? ?? ?? ?? 48 8B ?? ?? ??");
        if (CutsceneFOVScanResult) {
            SPDLOG_INFO("FOV: Cutscene: Address is {:s}+{:x}", sExeName, CutsceneFOVScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            static SafetyHookMid CutsceneFOVMidHook{};
            CutsceneFOVMidHook = safetyhook::create_mid(CutsceneFOVScanResult,
                [](SafetyHookContext& ctx) {
                    if (bFixFOV && fAspectRatio < fNativeAspect)
                        ctx.xmm0.f32[0] = atanf(tanf(ctx.xmm0.f32[0] / 2.0f) / fAspectRatio * fNativeAspect) * 2.0f;
                });
        }
        else {
            SPDLOG_ERROR("FOV: Cutscene: Pattern scan failed.");
        }
    }
}

void HUD()
{
    if (bDisableLetterboxing)
    {
        // Letterboxing Top
        std::uint8_t* LetterboxingTopFrameScanResult = Memory::PatternScan(exeModule, "F3 48 0F ?? ?? F3 0F ?? ?? ?? ?? ?? ?? F3 0F ?? ?? ?? ?? ?? ?? 8B ?? ?? ?? ?? ?? 89 ?? ?? ?? ?? ??");
        if (LetterboxingTopFrameScanResult) {
            SPDLOG_INFO("HUD: Letterboxing: Top: Address is {:s}+{:x}", sExeName, LetterboxingTopFrameScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            static SafetyHookMid LetterboxingTopFrameMidHook{};
            LetterboxingTopFrameMidHook = safetyhook::create_mid(LetterboxingTopFrameScanResult,
                [](SafetyHookContext& ctx) {
                    ctx.rcx = 0; // 0 width
                });
        }
        else {
            SPDLOG_ERROR("HUD: Letterboxing: Top: Pattern scan failed.");
        }

        // Letterboxing Bottom
        std::uint8_t* LetterboxingBottomFrameScanResult = Memory::PatternScan(exeModule, "F3 48 0F ?? ?? F3 0F ?? ?? ?? ?? ?? ?? F3 0F ?? ?? ?? ?? ?? ?? 8B ?? ?? ?? ?? ?? 89 ?? ?? ?? ?? ??");
        if (LetterboxingBottomFrameScanResult) {
            SPDLOG_INFO("HUD: Letterboxing: Bottom: Address is {:s}+{:x}", sExeName, LetterboxingBottomFrameScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            static SafetyHookMid LetterboxingBottomFrameMidHook{};
            LetterboxingBottomFrameMidHook = safetyhook::create_mid(LetterboxingBottomFrameScanResult,
                [](SafetyHookContext& ctx) {
                    ctx.rcx = 0; // 0 width
                });
        }
        else {
            SPDLOG_ERROR("HUD: Letterboxing: Bottom: Pattern scan failed.");
        }

        // Cutscene Letterboxing
        std::uint8_t* CutsceneLetterboxingScanResult = Memory::PatternScan(exeModule, "88 ?? ?? ?? ?? ?? B0 01 48 8B ?? ?? 48 33 ?? E8 ?? ?? ?? ?? 48 8B ?? ?? ?? ?? ?? ??");
        if (CutsceneLetterboxingScanResult) {
            SPDLOG_INFO("HUD: Letterboxing: Cutscenes: Address is {:s}+{:x}", sExeName, CutsceneLetterboxingScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            static SafetyHookMid CutsceneLetterboxingMidHook{};
            CutsceneLetterboxingMidHook = safetyhook::create_mid(CutsceneLetterboxingScanResult,
                [](SafetyHookContext& ctx) {
                    const char* vfxName = reinterpret_cast<const char*>(ctx.rsi + 0x10);

                    if (vfxName && strstr(vfxName, "black_belt"))
                        *reinterpret_cast<std::uint32_t*>(ctx.rsi + 0x34) = 0x00010000;
                });
        }
        else {
            SPDLOG_ERROR("HUD: Letterboxing: Cutscenes: Pattern scan failed.");
        }
    }

    if (bAdjustHUDSize)
    {
        // HUD Objects
        std::uint8_t* HUDSizeScanResult = Memory::PatternScan(exeModule, "48 8D ?? ?? ?? ?? ?? 48 8B ?? E8 ?? ?? ?? ?? 66 0F ?? ?? ?? ?? ?? ?? 66 0F ?? ?? ?? ?? ?? ??");
        if (HUDSizeScanResult) {
            SPDLOG_INFO("HUD: Size: Address is {:s}+{:x}", sExeName, HUDSizeScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            static SafetyHookMid HUDSizeMidHook{};
            HUDSizeMidHook = safetyhook::create_mid(HUDSizeScanResult,
                [](SafetyHookContext& ctx) {
                    //sHUDObject = reinterpret_cast<char*>(ctx.rdi + 0x78);
                    int WidescreenAnchor = *reinterpret_cast<int*>(ctx.rdi + 0x2D4);

                    if (WidescreenAnchor != 4 && (WidescreenAnchor != 2 || *reinterpret_cast<int*>(ctx.rdi + 0x258) != 0)) {
                        ctx.r8 = 7;
                    }
                });
        }
        else {
            SPDLOG_ERROR("HUD: Size: Pattern scan failed.");
        }
    }

}

void Misc()
{
    if (bDisableDithering)
    {
        // Disable character model dithering when the camera gets close
        std::uint8_t* CharacterDitheringScanResult = Memory::PatternScan(exeModule, "72 ?? F6 ?? ?? ?? ?? ?? 10 74 ?? 89 ?? ?? ?? ?? ?? EB ?? F3 0F ?? ?? ?? ?? ?? ??");
        if (CharacterDitheringScanResult) {
            SPDLOG_INFO("Character Dithering: Address is {:s}+{:x}", sExeName, CharacterDitheringScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            Memory::PatchBytes(CharacterDitheringScanResult, "\xEB", 1);
        }
        else {
            SPDLOG_ERROR("Character Dithering: Pattern scan failed.");
        }
    }
}

DWORD __stdcall Main(void*)
{
    Logging();
    Configuration();
    Resolution();
    FOV();
    HUD();
    Misc();
    return true;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(hModule);
        thisModule = hModule;

        HANDLE mainHandle = CreateThread(NULL, 0, Main, 0, NULL, 0);
        if (mainHandle)
        {
            SetThreadPriority(mainHandle, THREAD_PRIORITY_HIGHEST);
            CloseHandle(mainHandle);
        }
        break;
    }
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}