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

    constexpr char sConfigFile[] = "Guac2_ARFix.ini";
    fs::path FixPath;
    fs::path ExePath;

    int iTargetWidth = 1280;
    int iTargetHeight = 800;
    float fHardcodedAR = 1.6f;

    void InitializeLogging()
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
            std::ofstream file(ExePath / "Guac2_ARFix.log", std::ios::trunc);
            if (file.is_open()) file.close();

            auto logger = std::make_shared<spdlog::logger>(
                "Guac2_ARFix",
                std::make_shared<spdlog::sinks::rotating_file_sink_st>(
                    (ExePath / "Guac2_ARFix.log").string(), 5 * 1024 * 1024, 1));
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::debug);
            spdlog::set_level(spdlog::level::debug);

            SPDLOG_INFO("Guacamelee! 2 Borderless & AR Fix Initialized.");
        } catch (...) {}
    }

    void LoadConfiguration()
    {
        inipp::Ini<char> ini;
        std::ifstream iniFile(FixPath / sConfigFile);
        if (iniFile) {
            ini.parse(iniFile);
            ini.strip_trailing_comments();
            inipp::get_value(ini.sections["Resolution"], "Width", iTargetWidth);
            inipp::get_value(ini.sections["Resolution"], "Height", iTargetHeight);
        } else {
            std::ofstream defaultIni(FixPath / sConfigFile);
            if (defaultIni.is_open()) {
                defaultIni << "[Resolution]\n";
                defaultIni << "Width = 1280\n";
                defaultIni << "Height = 800\n";
                defaultIni.close();
            }
            SPDLOG_INFO("Config file not found. Created default Guac2_ARFix.ini");
        }

        if (iTargetWidth <= 0 || iTargetHeight <= 0) {
            SPDLOG_WARN("Invalid target resolution {}x{}. Using default 1280x800.",
                        iTargetWidth, iTargetHeight);
            iTargetWidth = 1280;
            iTargetHeight = 800;
        }
        fHardcodedAR = static_cast<float>(iTargetWidth) / static_cast<float>(iTargetHeight);

        SPDLOG_INFO("Config Loaded -> Target Resolution: {}x{}, Aspect Ratio: {:.5f}",
                    iTargetWidth, iTargetHeight, fHardcodedAR);
    }

    DWORD WINAPI WindowOverrideThread(void*)
    {
        HWND hwnd = nullptr;
        while (!hwnd) {
            hwnd = FindWindowA(nullptr, "Guacamelee! 2");
            if (!hwnd) {
                HWND hIter = GetTopWindow(nullptr);
                while (hIter) {
                    DWORD pid = 0;
                    GetWindowThreadProcessId(hIter, &pid);
                    if (pid == GetCurrentProcessId() && IsWindowVisible(hIter)) {
                        hwnd = hIter;
                        break;
                    }
                    hIter = GetWindow(hIter, GW_HWNDNEXT);
                }
            }
            Sleep(200);
        }

        if (hwnd) {
            SPDLOG_INFO("Game window handle found: 0x{:X}", reinterpret_cast<uintptr_t>(hwnd));

            LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
            style &= ~WS_CAPTION;
            style &= ~WS_THICKFRAME;
            style &= ~WS_MINIMIZEBOX;
            style &= ~WS_MAXIMIZEBOX;
            style |= WS_POPUP; // Convert to borderless popup style
            SetWindowLongPtrW(hwnd, GWL_STYLE, style);

            SetWindowPos(hwnd, HWND_TOP, 0, 0, iTargetWidth, iTargetHeight,
                         SWP_SHOWWINDOW | SWP_FRAMECHANGED);

            SPDLOG_INFO("Successfully forced borderless window style and size to {}x{}.", iTargetWidth, iTargetHeight);
        }

        return 0;
    }

    DWORD WINAPI SetupThread(void*)
    {
        InitializeLogging();
        LoadConfiguration();

        Sleep(1000); // Wait for modules to settle

        uint8_t* moduleBase = reinterpret_cast<uint8_t*>(exeModule);

        const char* arPattern = "39 8E E3 3F";
        uint8_t* pAddress = Memory::PatternScan(moduleBase, arPattern);

        if (pAddress) {
            DWORD oldProtect;
            if (VirtualProtect(pAddress, 4, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                std::memcpy(pAddress, &fHardcodedAR, sizeof(float));
                VirtualProtect(pAddress, 4, oldProtect, &oldProtect);

                SPDLOG_INFO("Successfully patched internal Aspect Ratio at 0x{:X} with value {:.5f}",
                            reinterpret_cast<uintptr_t>(pAddress), fHardcodedAR);
            } else {
                SPDLOG_ERROR("Failed to change memory protection for AR patch.");
            }
        } else {
            SPDLOG_WARN("Pattern scan for Aspect Ratio failed! Check if game version matches.");
        }

        if (HANDLE hWindowThread = CreateThread(nullptr, 0, WindowOverrideThread, nullptr, 0, nullptr)) {
            CloseHandle(hWindowThread);
        }

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