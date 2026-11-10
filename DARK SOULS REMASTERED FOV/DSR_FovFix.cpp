#include "stdafx.h"
#include "version.h"

#if !defined(_M_X64)
#error DSR_FovFix must be built for Windows x64.
#endif

#ifdef _DEBUG
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#else
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#endif

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <inipp/inipp.h>
#include <safetyhook.hpp>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>

namespace fs = std::filesystem;

namespace
{
    HMODULE exeModule = GetModuleHandleW(nullptr);
    HMODULE thisModule = nullptr;
    fs::path FixPath;
    constexpr char sConfigFile[] = "DSR_FovFix.ini";
    constexpr char sLogFile[] = "DSR_FovFix.log";

    float fFovMultiplier = 1.0f; // 1.0f = 100% Original FOV
    safetyhook::MidHook fovHook;

    // Simple IDA-style pattern scanner
    std::uint8_t* PatternScan(HMODULE module, const char* signature)
    {
        static auto pattern_to_byte = [](const char* pattern) {
            auto bytes = std::vector<int>{};
            auto start = const_cast<char*>(pattern);
            auto end = const_cast<char*>(pattern) + strlen(pattern);
            for (auto current = start; current < end; ++current) {
                if (*current == '?') {
                    ++current;
                    if (*current == '?') ++current;
                    bytes.push_back(-1);
                } else {
                    bytes.push_back(strtoul(current, &current, 16));
                }
            }
            return bytes;
        };

        auto dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(module);
        auto ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<std::uint8_t*>(module) + dosHeader->e_lfanew);
        auto sizeOfImage = ntHeaders->OptionalHeader.SizeOfImage;
        auto patternBytes = pattern_to_byte(signature);
        auto scanBytes = reinterpret_cast<std::uint8_t*>(module);
        auto s = patternBytes.size();
        auto d = patternBytes.data();

        for (auto i = 0ul; i < sizeOfImage - s; ++i) {
            bool found = true;
            for (auto j = 0ul; j < s; ++j) {
                if (scanBytes[i + j] != d[j] && d[j] != -1) {
                    found = false;
                    break;
                }
            }
            if (found) return &scanBytes[i];
        }
        return nullptr;
    }

    bool InitializeLogging()
    {
        WCHAR dllPath[MAX_PATH] = {};
        if (GetModuleFileNameW(thisModule, dllPath, MAX_PATH) == 0) return false;
        FixPath = fs::path(dllPath).parent_path();

        std::ofstream clearLog(FixPath / sLogFile, std::ios::trunc);
        if (clearLog.is_open()) clearLog.close();

        try {
            auto logger = std::make_shared<spdlog::logger>(
                "DSR_FovFix",
                std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    (FixPath / sLogFile).string(), 5 * 1024 * 1024, 1));
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::info);
            SPDLOG_INFO("Dark Souls: Remastered FOV Fix initialized.");
            return true;
        } catch (...) {
            return false;
        }
    }

    bool LoadConfiguration()
    {
        const fs::path path = FixPath / sConfigFile;
        if (!fs::exists(path)) {
            std::ofstream defaultIni(path);
            defaultIni << "[Camera]\n"
                          "; 1.0 = 100% (Default), 1.2 = 120%, etc.\n"
                          "FovMultiplier = 1.0\n";
            defaultIni.close();
            SPDLOG_INFO("Created default {}.", sConfigFile);
        }

        std::ifstream iniFile(path);
        if (!iniFile) return false;

        inipp::Ini<char> ini;
        ini.parse(iniFile);
        ini.strip_trailing_comments();

        inipp::get_value(ini.sections["Camera"], "FovMultiplier", fFovMultiplier);

        if (fFovMultiplier <= 0.1f) fFovMultiplier = 0.1f;
        if (fFovMultiplier >= 3.0f) fFovMultiplier = 3.0f;

        SPDLOG_INFO("Config loaded -> FOV Multiplier: {:.2f}x ({}%)",
                    fFovMultiplier, static_cast<int>(fFovMultiplier * 100.0f));
        return true;
    }

    void InstallFovHook()
    {
        std::uint8_t* fovAddress = PatternScan(exeModule, "8B 42 50 4C 8B C2 89");
        if (!fovAddress) {
            SPDLOG_ERROR("Failed to find FOV reading AOB signature.");
            return;
        }

        fovHook = safetyhook::create_mid(fovAddress + 3, [](SafetyHookContext& ctx) {
            union { uint32_t u; float f; } val;
            val.u = static_cast<uint32_t>(ctx.rax);

            val.f *= fFovMultiplier;

            ctx.rax = (ctx.rax & 0xFFFFFFFF00000000ULL) | val.u;
        });

        if (fovHook) {
            SPDLOG_INFO("FOV Mid-Hook successfully injected at 0x{:X}", reinterpret_cast<uintptr_t>(fovAddress + 3));
        } else {
            SPDLOG_ERROR("Failed to inject FOV Mid-Hook.");
        }
    }

    DWORD WINAPI SetupThread(void*)
    {
        if (!InitializeLogging()) return 1;
        if (!LoadConfiguration()) return 1;

        Sleep(1500);
        InstallFovHook();

        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        thisModule = hModule;
        if (HANDLE thread = CreateThread(nullptr, 0, SetupThread, nullptr, 0, nullptr)) {
            CloseHandle(thread);
        }
    }
    return TRUE;
}