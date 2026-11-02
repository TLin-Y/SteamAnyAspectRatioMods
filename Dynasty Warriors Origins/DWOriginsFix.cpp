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
#include <cstdio>

namespace
{
    HMODULE exeModule = GetModuleHandle(nullptr);
    HMODULE thisModule = nullptr;

    inipp::Ini<char> ini;
    constexpr char sConfigFile[] = "DWOriginsFix.ini";
    std::filesystem::path FixPath;
    std::filesystem::path ExePath;
    std::string sExeName;
    std::shared_ptr<spdlog::logger> logger;

    bool bCustomResolution = true;
    int iCustomResX = 1280;
    int iCustomResY = 960;
    
    int iTargetResX = 1280;
    int iTargetResY = 720;
    
    bool bUncapCutsceneFPS = false;

    constexpr float fNativeAspect = 16.0f / 9.0f;
    float fAspectRatio = fNativeAspect;
    float fAspectMultiplier = 1.0f;
    float fHUDWidth = 1920.0f;
    float fHUDHeight = 1080.0f;
    float fHUDWidthOffset = 0.0f;
    float fHUDHeightOffset = 0.0f;

    template<typename T>
    T GetConfigAndLog(const std::string& section, const std::string& key, T defaultValue)
    {
        T value = defaultValue;
        inipp::get_value(ini.sections[section], key, value);
        SPDLOG_INFO("Config Parse: [{}]: {} = {}", section, key, value);
        return value;
    }

    void Logging()
    {
        WCHAR dllPath[MAX_PATH] = {};
        GetModuleFileNameW(thisModule, dllPath, MAX_PATH);
        FixPath = dllPath;
        FixPath = FixPath.remove_filename();

        WCHAR gameExePath[MAX_PATH] = {};
        GetModuleFileNameW(exeModule, gameExePath, MAX_PATH);
        ExePath = gameExePath;
        sExeName = ExePath.filename().string();
        ExePath = ExePath.remove_filename();

        try {
            std::ofstream file(ExePath / "DWOriginsFix.log", std::ios::trunc);
            if (file.is_open()) file.close();

            logger = std::make_shared<spdlog::logger>(
                "DWOriginsFix",
                std::make_shared<spdlog::sinks::rotating_file_sink_st>(
                    (ExePath / "DWOriginsFix.log").string(), 10 * 1024 * 1024, 1));
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::debug);
            spdlog::set_level(spdlog::level::debug);

            SPDLOG_INFO("--------------------------------------------------");
            SPDLOG_INFO("DWOriginsFix (Dynamic Target Edition) Started.");
            SPDLOG_INFO("Module Address: 0x{:x}", reinterpret_cast<uintptr_t>(exeModule));
            SPDLOG_INFO("--------------------------------------------------");
        }
        catch (...) {}
    }

    void Configuration()
    {
        SPDLOG_INFO("=> Stage: Configuration");
        std::ifstream iniFile(FixPath / sConfigFile);
        if (!iniFile) {
            SPDLOG_WARN("Config file not found! Falling back to defaults.");
            return;
        }

        ini.parse(iniFile);
        ini.strip_trailing_comments();

        bCustomResolution = GetConfigAndLog("Custom Resolution", "Enabled", true);
        iCustomResX = GetConfigAndLog("Custom Resolution", "Width", 1280);
        iCustomResY = GetConfigAndLog("Custom Resolution", "Height", 960);
        
        iTargetResX = GetConfigAndLog("Custom Resolution", "TargetWidth", 1280);
        iTargetResY = GetConfigAndLog("Custom Resolution", "TargetHeight", 720);
        
        bUncapCutsceneFPS = GetConfigAndLog("Uncap Cutscene FPS", "Enabled", false);
    }

    void CalculateAspectRatio()
    {
        if (iCustomResX <= 0 || iCustomResY <= 0) return;

        fAspectRatio = static_cast<float>(iCustomResX) / iCustomResY;
        fAspectMultiplier = fAspectRatio / fNativeAspect;
        fHUDWidth = static_cast<float>(iCustomResY) * fNativeAspect;
        fHUDHeight = static_cast<float>(iCustomResY);
        fHUDWidthOffset = (static_cast<float>(iCustomResX) - fHUDWidth) / 2.0f;
        fHUDHeightOffset = 0.0f;

        if (fAspectRatio < fNativeAspect) {
            fHUDWidth = static_cast<float>(iCustomResX);
            fHUDHeight = static_cast<float>(iCustomResX) / fNativeAspect;
            fHUDWidthOffset = 0.0f;
            fHUDHeightOffset = (static_cast<float>(iCustomResY) - fHUDHeight) / 2.0f;
        }
    }

    void Resolution()
    {
        SPDLOG_INFO("=> Stage: Resolution Injection");
        if (!bCustomResolution) return;

        if (iCustomResX <= 0 || iCustomResY <= 0 || iTargetResX <= 0 || iTargetResY <= 0) {
            SPDLOG_ERROR("Invalid dimensions detected. Aborting.");
            return;
        }

        SPDLOG_INFO("Preparing to overwrite target {}x{} with custom {}x{}", 
                    iTargetResX, iTargetResY, iCustomResX, iCustomResY);

        uint16_t tx = static_cast<uint16_t>(iTargetResX);
        uint16_t ty = static_cast<uint16_t>(iTargetResY);
        uint8_t tx_l = tx & 0xFF;        uint8_t tx_h = (tx >> 8) & 0xFF;
        uint8_t ty_l = ty & 0xFF;        uint8_t ty_h = (ty >> 8) & 0xFF;

        char sigBuffer[128];
        snprintf(sigBuffer, sizeof(sigBuffer), "%02X %02X %02X %02X %02X %02X %02X %02X",
                 tx_l, tx_h, ty_l, ty_h,
                 tx_l, tx_h, ty_l, ty_h);
        
        std::string dynamicSignature = sigBuffer;
        SPDLOG_INFO("Dynamic Signature Generated: {}", dynamicSignature);
        
        auto* resolutionEntry = Memory::PatternScan(exeModule, dynamicSignature.c_str());
        
        if (!resolutionEntry) {
            SPDLOG_ERROR("Pattern scan failed! Could not find the target resolution {}x{} in memory.", iTargetResX, iTargetResY);
            return;
        }

        SPDLOG_INFO("Pattern scan SUCCESS. Target found at: {:s}+{:x}", sExeName, resolutionEntry - reinterpret_cast<std::uint8_t*>(exeModule));
        
        Memory::Write(resolutionEntry, static_cast<std::uint16_t>(iCustomResX));
        Memory::Write(resolutionEntry + sizeof(std::uint16_t), static_cast<std::uint16_t>(iCustomResY));
        
        SPDLOG_INFO("Successfully replaced memory entry with {}x{}", iCustomResX, iCustomResY);

        CalculateAspectRatio();
    }

    void UncapCutsceneFramerate()
    {
        if (!bUncapCutsceneFPS) return;
        constexpr char signature[] = "B9 02 00 00 00 44 ?? ?? ?? ?? ?? ?? 89 ?? ?? ?? ?? ?? 0F ?? ??";
        auto* patchAddress = Memory::PatternScan(exeModule, signature);
        if (patchAddress) {
            Memory::PatchBytes(patchAddress, "\x8B\xC8\x90\x90\x90", 5);
            SPDLOG_INFO("Cutscene FPS Cap: Patched instruction successfully.");
        }
    }

    DWORD WINAPI Main(void*)
    {
        Sleep(500); 
        Logging();
        Configuration();
        Resolution();
        UncapCutsceneFramerate();
        SPDLOG_INFO("=> DWOriginsFix Initialization Complete.");
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        thisModule = hModule;
        if (HANDLE mainHandle = CreateThread(nullptr, 0, Main, nullptr, 0, nullptr)) {
            SetThreadPriority(mainHandle, THREAD_PRIORITY_HIGHEST);
            CloseHandle(mainHandle);
        }
    }
    return TRUE;
}
