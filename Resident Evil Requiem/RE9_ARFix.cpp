#include "stdafx.h"
#include <windows.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <inipp/inipp.h>
#include <safetyhook.hpp>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <vector>

#if !defined(_M_X64)
#error Resident Evil Requiem is a 64-bit game. Compile this project as x64.
#endif

namespace fs = std::filesystem;

namespace
{
    HMODULE exeModule = GetModuleHandleW(nullptr);
    HMODULE thisModule = nullptr;
    fs::path FixPath;
    constexpr char sConfigFile[] = "RE9_ARFix.ini";
    constexpr char sLogFile[] = "RE9_ARFix.log";

    bool bEnableARFix = true;
    int iAspectWidth = 16;
    int iAspectHeight = 10;
    float fTargetAR = 1.6f;
    bool bEnableFOVFix = true;
    float fFOVScale = 1.0f;

    SafetyHookMid aspectHook{};
    SafetyHookMid projectionHook{};
    std::atomic<std::uintptr_t> mainCamera{ 0 };
    int projectionCameraReg = -1;

    struct Pattern
    {
        std::vector<int> bytes;
    };

    Pattern ParsePattern(const char* text)
    {
        Pattern p;
        for (const char* c = text; *c;) {
            if (*c == ' ') { ++c; continue; }
            if (*c == '?') { p.bytes.push_back(-1); while (*c == '?') ++c; continue; }
            p.bytes.push_back(static_cast<int>(strtoul(std::string(c, 2).c_str(), nullptr, 16)));
            c += 2;
        }
        return p;
    }

    int ScanRegion(const Pattern& pattern, std::uint8_t* start, size_t size, std::uint8_t** found)
    {
        const size_t n = pattern.bytes.size();
        const int first = pattern.bytes[0];
        int count = 0;
        __try {
            for (size_t i = 0; i + n <= size; ++i) {
                if (start[i] != first) continue;
                size_t j = 1;
                while (j < n && (pattern.bytes[j] < 0 || start[i + j] == pattern.bytes[j])) ++j;
                if (j == n) {
                    *found = start + i;
                    ++count;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        return count;
    }

    int FindUnique(const Pattern& pattern, std::uint8_t** found)
    {
        auto base = reinterpret_cast<std::uint8_t*>(exeModule);
        auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
        auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
        std::uint8_t* end = base + nt->OptionalHeader.SizeOfImage;
        constexpr DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;

        int count = 0;
        MEMORY_BASIC_INFORMATION mbi{};
        for (std::uint8_t* addr = base; addr < end; addr = static_cast<std::uint8_t*>(mbi.BaseAddress) + mbi.RegionSize) {
            if (!VirtualQuery(addr, &mbi, sizeof(mbi))) break;
            if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || !(mbi.Protect & executable)) continue;
            auto start = static_cast<std::uint8_t*>(mbi.BaseAddress);
            size_t size = mbi.RegionSize;
            if (start + size > end) size = end - start;
            count += ScanRegion(pattern, start, size, found);
        }
        return count;
    }

    std::uintptr_t Rva(const void* p)
    {
        return reinterpret_cast<std::uintptr_t>(p) - reinterpret_cast<std::uintptr_t>(exeModule);
    }

    std::uintptr_t ContextReg(const SafetyHookContext& ctx, int reg)
    {
        switch (reg) {
        case 0: return ctx.rax;
        case 1: return ctx.rcx;
        case 2: return ctx.rdx;
        case 3: return ctx.rbx;
        case 5: return ctx.rbp;
        case 6: return ctx.rsi;
        case 7: return ctx.rdi;
        default: return 0;
        }
    }

    void InstallHooks()
    {
        const Pattern aspectPattern = ParsePattern("75 ?? C4 ?? ?? 5E ?? C5 FA 11 40 44");
        const Pattern projectionPattern = ParsePattern("C5 FA 10 ?? 38 C5 FA 59 05 ?? ?? ?? ?? C5 FA 59 05 ?? ?? ?? ?? C5 FA 10 ?? 30");

        std::uint8_t* aspectSite = nullptr;
        int aspectCount = 0;
        for (int attempt = 0; attempt < 240; ++attempt) {
            aspectSite = nullptr;
            aspectCount = FindUnique(aspectPattern, &aspectSite);
            if (aspectCount) break;
            Sleep(500);
        }
        if (aspectCount != 1) {
            SPDLOG_ERROR("Camera aspect writer found {} times (expected 1); fix not applied.", aspectCount);
            return;
        }

        std::uint8_t* aspectTarget = aspectSite + 7;
        aspectHook = safetyhook::create_mid(aspectTarget, [](SafetyHookContext& ctx) {
            if (ctx.rax) mainCamera = ctx.rax;
            if (bEnableARFix) ctx.xmm0.f32[0] = fTargetAR;
        });
        if (!aspectHook) {
            SPDLOG_ERROR("Camera aspect hook could not be installed.");
            return;
        }
        SPDLOG_INFO("Camera aspect hook installed at RVA 0x{:X}.", Rva(aspectTarget));

        if (!bEnableFOVFix || fFOVScale == 1.0f) return;

        std::uint8_t* projectionSite = nullptr;
        const int projectionCount = FindUnique(projectionPattern, &projectionSite);
        if (projectionCount != 1) {
            SPDLOG_ERROR("Camera projection code found {} times (expected 1); FOV not changed.", projectionCount);
            return;
        }
        const std::uint8_t modrm = projectionSite[3];
        if ((modrm & 0xC0) != 0x40 || (modrm & 7) == 4 || (projectionSite[24] & 7) != (modrm & 7)) {
            SPDLOG_ERROR("Unexpected camera projection code; FOV not changed.");
            return;
        }
        projectionCameraReg = modrm & 7;

        std::uint8_t* projectionTarget = projectionSite + 21;
        projectionHook = safetyhook::create_mid(projectionTarget, [](SafetyHookContext& ctx) {
            const std::uintptr_t camera = mainCamera;
            if (camera && ContextReg(ctx, projectionCameraReg) == camera) {
                float halfAngle = ctx.xmm0.f32[0] * fFOVScale;
                if (halfAngle < 0.001f) halfAngle = 0.001f;
                if (halfAngle > 1.48f) halfAngle = 1.48f;
                ctx.xmm0.f32[0] = halfAngle;
            }
        });
        if (projectionHook) {
            SPDLOG_INFO("Camera FOV hook installed at RVA 0x{:X}.", Rva(projectionTarget));
        } else {
            SPDLOG_ERROR("Camera FOV hook could not be installed.");
        }
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
                "RE9_ARFix",
                std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    (FixPath / sLogFile).string(), 5 * 1024 * 1024, 1));
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::info);
            SPDLOG_INFO("Resident Evil Requiem Any Aspect Ratio fix initialized.");
            return true;
        } catch (...) {
            return false;
        }
    }

    void LoadConfiguration()
    {
        std::ifstream iniFile(FixPath / sConfigFile);
        if (iniFile) {
            inipp::Ini<char> ini;
            ini.parse(iniFile);
            ini.strip_trailing_comments();
            inipp::get_value(ini.sections["AspectRatio"], "EnableARFix", bEnableARFix);
            inipp::get_value(ini.sections["AspectRatio"], "AspectWidth", iAspectWidth);
            inipp::get_value(ini.sections["AspectRatio"], "AspectHeight", iAspectHeight);
            inipp::get_value(ini.sections["FOV"], "EnableFOVFix", bEnableFOVFix);
            inipp::get_value(ini.sections["FOV"], "FOVScale", fFOVScale);
        } else {
            SPDLOG_WARN("{} not found; using defaults.", sConfigFile);
        }
        if (iAspectWidth > 0 && iAspectHeight > 0) {
            fTargetAR = static_cast<float>(iAspectWidth) / static_cast<float>(iAspectHeight);
        } else {
            SPDLOG_ERROR("Invalid aspect {}:{}; using 16:10.", iAspectWidth, iAspectHeight);
        }
        if (!(fFOVScale > 0.0f && fFOVScale <= 2.0f)) {
            SPDLOG_ERROR("FOVScale {} out of range (0 - 2.0); using 1.0.", fFOVScale);
            fFOVScale = 1.0f;
        }
        SPDLOG_INFO("Config: AR={} ({}:{} = {:.4f}), FOV={} scale {:.3f}",
            bEnableARFix, iAspectWidth, iAspectHeight, fTargetAR, bEnableFOVFix, fFOVScale);
    }

    DWORD WINAPI MainThread(void*)
    {
        if (!InitializeLogging()) return 1;
        LoadConfiguration();
        Sleep(5000);
        if (bEnableARFix || bEnableFOVFix) InstallHooks();
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        thisModule = hModule;
        if (HANDLE thread = CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr)) {
            CloseHandle(thread);
        }
    }
    return TRUE;
}
