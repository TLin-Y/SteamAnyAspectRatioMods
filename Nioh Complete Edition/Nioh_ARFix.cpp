#include "stdafx.h"
#include <windows.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <inipp/inipp.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>
#include <cstring>

#if !defined(_M_X64)
#error Nioh is a 64-bit game. Compile this project as x64.
#endif

namespace fs = std::filesystem;

namespace
{
    HMODULE exeModule = GetModuleHandleW(nullptr);
    HMODULE thisModule = nullptr;
    fs::path FixPath;
    constexpr char sConfigFile[] = "Nioh_ARFix.ini";
    constexpr char sLogFile[] = "Nioh_ARFix.log";

    int iTargetWidth = 1280;
    int iTargetHeight = 800;
    float fRenderScale = 1.0f;

    constexpr char PATTERN_ASPECTRATIO1[] = "C7 43 50 39 8E E3 3F";
    constexpr char PATTERN_ASPECTRATIO2[] = "00 00 87 44 00 00 F0 44";
    constexpr char PATTERN_ASPECTRATIO3[] = "8B D7 0F 95 05";
    constexpr char PATTERN_RESOLUTION[] = "80 07 00 00 38 04 00 00 00 00 00 00 00 00 00 00";

    std::vector<int> ParsePattern(const char* pattern)
    {
        std::vector<int> bytes;
        const char* end = pattern + std::strlen(pattern);
        for (char* cur = const_cast<char*>(pattern); cur < end; ++cur) {
            if (*cur == ' ') continue;
            if (*cur == '?') {
                if (cur[1] == '?') ++cur;
                bytes.push_back(-1);
            } else {
                bytes.push_back(static_cast<int>(std::strtoul(cur, &cur, 16)));
                --cur;
            }
        }
        return bytes;
    }

    std::vector<std::uint8_t*> FindAll(const char* signature)
    {
        std::vector<std::uint8_t*> results;
        auto base = reinterpret_cast<std::uint8_t*>(exeModule);
        auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
        auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
        auto section = IMAGE_FIRST_SECTION(nt);
        auto pattern = ParsePattern(signature);
        const size_t n = pattern.size();

        for (WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++section) {
            if (std::memcmp(section->Name, ".bind", 5) == 0) continue;
            size_t size = section->Misc.VirtualSize;
            if (size < n) continue;
            std::uint8_t* start = base + section->VirtualAddress;
            for (size_t i = 0; i + n <= size; ++i) {
                bool found = true;
                for (size_t j = 0; j < n; ++j) {
                    if (pattern[j] != -1 && start[i + j] != pattern[j]) { found = false; break; }
                }
                if (found) {
                    results.push_back(start + i);
                    i += n - 1;
                }
            }
        }
        return results;
    }

    void WriteMemory(void* address, const void* data, size_t size)
    {
        DWORD oldProtect;
        if (VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            std::memcpy(address, data, size);
            VirtualProtect(address, size, oldProtect, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), address, size);
        }
    }

    bool ExpectCount(const char* name, size_t expected, size_t actual)
    {
        if (actual != expected) {
            SPDLOG_ERROR("{}: expected {} match(es), found {}. Unsupported game version?", name, expected, actual);
            return false;
        }
        return true;
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
                "Nioh_ARFix",
                std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    (FixPath / sLogFile).string(), 5 * 1024 * 1024, 1));
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::info);
            SPDLOG_INFO("Nioh Any Aspect Ratio fix initialized.");
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
            defaultIni << "[Resolution]\n"
                       << "; Replaces the 1920x1080 entry. In the Nioh launcher pick\n"
                       << "; Screen Resolution 1920x1080 and Render Resolution High.\n"
                       << "Width = 1280\nHeight = 800\n\n"
                       << "; Internal render resolution = Width/Height x RenderScale.\n"
                       << "; Below 1.0 improves performance. Does not affect the UI.\n"
                       << "RenderScale = 1.0\n";
            defaultIni.close();
            SPDLOG_INFO("Created default config {}.", sConfigFile);
        }

        std::ifstream iniFile(path);
        if (!iniFile) return false;

        inipp::Ini<char> ini;
        ini.parse(iniFile);
        ini.strip_trailing_comments();

        inipp::get_value(ini.sections["Resolution"], "Width", iTargetWidth);
        inipp::get_value(ini.sections["Resolution"], "Height", iTargetHeight);
        inipp::get_value(ini.sections["Resolution"], "RenderScale", fRenderScale);

        if (iTargetWidth <= 0 || iTargetHeight <= 0) {
            SPDLOG_ERROR("Invalid resolution {}x{}; using 1280x800.", iTargetWidth, iTargetHeight);
            iTargetWidth = 1280;
            iTargetHeight = 800;
        }
        if (!(fRenderScale > 0.1f)) fRenderScale = 1.0f;
        if (fRenderScale > 4.0f) fRenderScale = 4.0f;

        SPDLOG_INFO("Config loaded -> {}x{}, RenderScale {:.2f}", iTargetWidth, iTargetHeight, fRenderScale);
        return true;
    }

    void ApplyDataPatches()
    {
        const float ratio = static_cast<float>(iTargetWidth) / static_cast<float>(iTargetHeight);
        float ratioWidth = 1920.0f;
        float ratioHeight = 1080.0f;
        if (ratio < 1.77777f) ratioHeight = ratioWidth / ratio;
        else ratioWidth = ratioHeight * ratio;

        auto ar2 = FindAll(PATTERN_ASPECTRATIO2);
        if (ExpectCount("PATTERN_ASPECTRATIO2", 1, ar2.size())) {
            float pair[2] = { ratioHeight, ratioWidth };
            WriteMemory(ar2[0], pair, sizeof(pair));
            SPDLOG_INFO("Aspect ratio #2 patched -> {:.2f}x{:.2f}", ratioWidth, ratioHeight);
        }

        auto res = FindAll(PATTERN_RESOLUTION);
        if (ExpectCount("PATTERN_RESOLUTION", 2, res.size())) {
            int screen[2] = { iTargetWidth, iTargetHeight };
            WriteMemory(res[0], screen, sizeof(screen));

            int render[2] = {
                static_cast<int>(std::lround(iTargetWidth * fRenderScale)),
                static_cast<int>(std::lround(iTargetHeight * fRenderScale))
            };
            WriteMemory(res[1], render, sizeof(render));
            SPDLOG_INFO("Resolution patched -> screen {}x{}, render {}x{}", screen[0], screen[1], render[0], render[1]);
        }
    }

    bool CodeIsDecrypted()
    {
        return FindAll(PATTERN_ASPECTRATIO1).size() == 1;
    }

    void ApplyCodePatches()
    {
        const float ratio = static_cast<float>(iTargetWidth) / static_cast<float>(iTargetHeight);

        auto ar1 = FindAll(PATTERN_ASPECTRATIO1);
        if (ExpectCount("PATTERN_ASPECTRATIO1", 1, ar1.size())) {
            WriteMemory(ar1[0] + 3, &ratio, sizeof(ratio));
            SPDLOG_INFO("Aspect ratio #1 patched -> {:.5f}", ratio);
        }

        // setne byte [rip+disp32] -> mov byte [rip+disp32], 0 (same length)
        auto ar3 = FindAll(PATTERN_ASPECTRATIO3);
        if (ExpectCount("PATTERN_ASPECTRATIO3", 2, ar3.size())) {
            for (auto p : ar3) {
                std::uint8_t patch[7] = { 0xC6, 0x05, p[5], p[6], p[7], p[8], 0x00 };
                WriteMemory(p + 2, patch, sizeof(patch));
            }
            SPDLOG_INFO("Aspect ratio #3 patched (2 sites).");
        }
    }

    DWORD WINAPI WaitForDecryptThread(void*)
    {
        // .text is encrypted by SteamStub until the game entry point runs
        for (int i = 0; i < 30000; ++i) {
            if (CodeIsDecrypted()) {
                ApplyCodePatches();
                SPDLOG_INFO("Deployment complete (after {} ms wait).", i);
                return 0;
            }
            Sleep(1);
        }
        SPDLOG_ERROR("Timed out waiting for game code. Unsupported game version?");
        return 1;
    }

    void Initialize()
    {
        if (!InitializeLogging()) return;
        if (!LoadConfiguration()) return;

        ApplyDataPatches();

        if (CodeIsDecrypted()) {
            ApplyCodePatches();
            SPDLOG_INFO("Deployment complete.");
        } else {
            SPDLOG_INFO("Game code still encrypted; waiting for SteamStub.");
            if (HANDLE thread = CreateThread(nullptr, 0, WaitForDecryptThread, nullptr, 0, nullptr)) {
                CloseHandle(thread);
            }
        }
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        thisModule = hModule;
        Initialize();
    }
    return TRUE;
}
