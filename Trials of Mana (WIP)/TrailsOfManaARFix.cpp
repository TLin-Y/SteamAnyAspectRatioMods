#include "stdafx.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <inipp/inipp.h>
#include <windows.h>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <thread>

HMODULE exeModule = GetModuleHandle(NULL);
HMODULE thisModule;
std::shared_ptr<spdlog::logger> logger;
std::filesystem::path ExePath;

int iTargetWidth = 1280;
int iTargetHeight = 800;
bool bEnableBorderless = true;

uint8_t targetFovByte = 0x0E;

void InitLogging() {
    WCHAR gameExePath[_MAX_PATH] = { 0 };
    GetModuleFileNameW(exeModule, gameExePath, MAX_PATH);
    ExePath = gameExePath;
    ExePath = ExePath.remove_filename();

    try {
        std::string logPath = (ExePath / "TrailsOfMana_ARFix.log").string();
        logger = std::make_shared<spdlog::logger>(
            "APT_Fix", std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logPath, 10 * 1024 * 1024, 1)
        );
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        spdlog::flush_on(spdlog::level::info);

        SPDLOG_INFO("========================================");
        SPDLOG_INFO("[System] Logging initialized.");
    } catch (...) {}
}

void LoadConfiguration() {
    std::filesystem::path iniPath = ExePath / "TrailsOfMana_ARFix.ini";
    if (std::filesystem::exists(iniPath)) {
        std::ifstream iniFile(iniPath);
        if (iniFile.is_open()) {
            inipp::Ini<char> ini;
            ini.parse(iniFile);
            inipp::get_value(ini.sections["Resolution"], "TargetWidth", iTargetWidth);
            inipp::get_value(ini.sections["Resolution"], "TargetHeight", iTargetHeight);

            std::string fovHexStr;
            if (inipp::get_value(ini.sections["Resolution"], "FovHex", fovHexStr)) {
                try {
                    targetFovByte = static_cast<uint8_t>(std::stoi(fovHexStr, nullptr, 16));
                } catch (...) {}
            }

            SPDLOG_INFO("[Config] Target Resolution: {}x{}", iTargetWidth, iTargetHeight);
            SPDLOG_INFO("[Config] Applied FOV Hex Byte: 0x{:02X}", targetFovByte);
        }
    } else {
        SPDLOG_WARN("[Config] INI not found. Using defaults (1280x960, FOV: 0x0E).");
    }
}

void ApplyExactFovPatch() {
    SPDLOG_INFO("[FOVPatch] Scanning contextual pattern...");

    uintptr_t moduleBase = reinterpret_cast<uintptr_t>(exeModule);
    PIMAGE_DOS_HEADER dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(moduleBase);
    PIMAGE_NT_HEADERS ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(moduleBase + dosHeader->e_lfanew);
    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(ntHeaders);

    // 上下文特征码[cite: 2]
    std::vector<uint8_t> pattern = {
        0xD0, 0x07, 0x25, 0x40,
        0x01, 0x00, 0x00, 0x00,
        0x8D, 0x7A, 0x08, 0x3C,
        0x35, 0xFA, 0x0E, 0x3C
    };

    bool found = false;

    for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++, section++) {
        if (section->Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA) {
            uint8_t* sectionBase = reinterpret_cast<uint8_t*>(moduleBase + section->VirtualAddress);
            DWORD sectionSize = section->Misc.VirtualSize;

            for (DWORD j = 0; j <= sectionSize - pattern.size(); ++j) {
                bool match = true;
                for (size_t k = 0; k < pattern.size(); ++k) {
                    if (k == 14) continue;
                    if (sectionBase[j + k] != pattern[k]) {
                        match = false;
                        break;
                    }
                }

                if (match) {
                    uint8_t* pByteToPatch = sectionBase + j + 14;
                    DWORD oldProtect;
                    if (VirtualProtect(pByteToPatch, 1, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                        *pByteToPatch = targetFovByte;
                        VirtualProtect(pByteToPatch, 1, oldProtect, &oldProtect);

                        SPDLOG_INFO("[FOVPatch] Successfully patched FOV byte to: 0x{:02X} at address: 0x{:X}",
                            targetFovByte, reinterpret_cast<uintptr_t>(pByteToPatch));
                        found = true;
                        break;
                    }
                }
            }
        }
        if (found) break;
    }
}

BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);

    if (processId == GetCurrentProcessId()) {
        HWND parent = GetParent(hwnd);
        if (parent == NULL) {
            if (bEnableBorderless) {
                LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
                style &= ~WS_CAPTION;
                style &= ~WS_THICKFRAME;
                style &= ~WS_MINIMIZEBOX;
                style &= ~WS_MAXIMIZEBOX;
                SetWindowLongPtr(hwnd, GWL_STYLE, style);
            }

            int screenWidth = GetSystemMetrics(SM_CXSCREEN);
            int screenHeight = GetSystemMetrics(SM_CYSCREEN);
            int posX = (screenWidth - iTargetWidth) / 2;
            int posY = (screenHeight - iTargetHeight) / 2;
            if (posX < 0) posX = 0;
            if (posY < 0) posY = 0;

            SetWindowPos(hwnd, HWND_TOP, posX, posY, iTargetWidth, iTargetHeight,
                         SWP_SHOWWINDOW | SWP_FRAMECHANGED);

            SPDLOG_INFO("[WindowFix] Window forced to {}x{}", iTargetWidth, iTargetHeight);
            return FALSE;
        }
    }
    return TRUE;
}

void ApplyWindowOverride() {
    for (int i = 0; i < 10; ++i) {
        Sleep(1000);
        EnumWindows(EnumWindowsCallback, 0);
    }
}

DWORD __stdcall MainThread(void*) {
    Sleep(1500);

    InitLogging();
    LoadConfiguration();
    ApplyExactFovPatch();
    ApplyWindowOverride();

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        thisModule = hModule;
        if (HANDLE mainHandle = CreateThread(NULL, 0, MainThread, 0, NULL, 0)) {
            SetThreadPriority(mainHandle, THREAD_PRIORITY_NORMAL);
            CloseHandle(mainHandle);
        }
    }
    return TRUE;
}