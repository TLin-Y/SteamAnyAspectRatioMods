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
#include <cmath>
#include <cstring>
#include <windows.h>

HMODULE exeModule = GetModuleHandle(NULL);
HMODULE thisModule;

inipp::Ini<char> ini;
std::string sConfigFile = "DevilMayCry5_ARFix.ini";

std::shared_ptr<spdlog::logger> logger;
std::string sLogFile = "DevilMayCry5_ARFix.log";
std::filesystem::path ExePath;

bool bEnableARFix = true;
int iAspectWidth = 16;
int iAspectHeight = 10;
float fTargetARValue = 1.6f;

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

        logger = std::make_shared<spdlog::logger>("DMC5_Fix", std::make_shared<spdlog::sinks::rotating_file_sink_st>((ExePath / sLogFile).string(), 10 * 1024 * 1024, 1));
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
        spdlog::flush_on(spdlog::level::debug);
        spdlog::set_level(spdlog::level::debug);

        SPDLOG_INFO("Devil May Cry 5 AR Fix {} Initialized", sFixVersion);
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
    iAspectWidth = GetConfigAndLog(ini, "AspectRatio", "AspectWidth", 16);
    iAspectHeight = GetConfigAndLog(ini, "AspectRatio", "AspectHeight", 10);

    if (iAspectWidth > 0 && iAspectHeight > 0) {
        fTargetARValue = static_cast<float>(iAspectWidth) / static_cast<float>(iAspectHeight);
    }
    SPDLOG_INFO("Target Aspect Ratio: {} / {} = {:.6f}", iAspectWidth, iAspectHeight, fTargetARValue);
}

void AspectRatio() {
    if (!bEnableARFix) return;

    const char* pattern = "75 0B F3 ?? 0F 5E ?? F3 44 0F 11 70 44";
    std::vector<std::uint8_t*> matches;

    for (int attempt = 0; attempt < 60; ++attempt) {
        matches = Memory::PatternScanAll(exeModule, pattern);
        if (!matches.empty()) break;
        Sleep(500);
    }

    if (matches.size() != 1) {
        SPDLOG_ERROR("AR pattern found {} times (expected 1). Game version not supported.", matches.size());
        return;
    }

    std::uint8_t* patchAddress = matches.front() + 2;
    uint32_t hexAR = 0;
    std::memcpy(&hexAR, &fTargetARValue, sizeof(hexAR));

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
        FlushInstructionCache(GetCurrentProcess(), patchAddress, sizeof(patchBytes));
        SPDLOG_INFO("AR patch applied at 0x{:X}", reinterpret_cast<uintptr_t>(patchAddress));
    } else {
        SPDLOG_ERROR("Failed to change memory protection for AR patch!");
    }
}

DWORD __stdcall Main(void*) {
    Sleep(1000);

    Logging();
    Configuration();

    AspectRatio();

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
