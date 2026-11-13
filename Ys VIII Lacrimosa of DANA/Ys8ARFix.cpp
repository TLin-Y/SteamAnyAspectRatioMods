#include "stdafx.h"
#include "version.h"

#if !defined(_M_X64)
#error Ys8_AspectPatch must be built for Windows x64 (64-bit), not x86.
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
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <vector>
#include <exception>
#include <filesystem>
#include <fstream>
#include <utility>
#include <windows.h>

namespace fs = std::filesystem;

namespace
{
    HMODULE exeModule = GetModuleHandleW(nullptr);
    HMODULE thisModule = nullptr;
    fs::path FixPath;
    constexpr char sConfigFile[] = "Ys8_AspectPatch.ini";
    constexpr char sLogFile[] = "Ys8_AspectPatch.log";

    int iTargetWidth = 1280;
    int iTargetHeight = 800;
    float fCustomAR = 1.6f;
    bool bCameraAspect = true;
    bool bRemoveLetterbox = true;
    bool bSharedProjection = false;
    safetyhook::MidHook letterboxHook;
    std::atomic<bool> letterboxObserved = false;

    void LetterboxCallback(safetyhook::Context& ctx)
    {
        const int width = static_cast<int>(ctx.r10 & 0xFFFFFFFF);
        const int height = static_cast<int>(ctx.r8 & 0xFFFFFFFF);
        if (width <= 0 || height <= 0) {
            SPDLOG_ERROR("Letterbox hook received invalid dimensions {}x{}; keeping original fitting.", width, height);
            return;
        }
        ctx.xmm2.f32[0] = std::nextafter(
            static_cast<float>(height) / static_cast<float>(width), INFINITY);
        if (!letterboxObserved.exchange(true)) {
            SPDLOG_INFO("Letterbox hook executed: logical {}x{}, configured {}x{}.",
                        width, height, iTargetWidth, iTargetHeight);
            if (width != iTargetWidth || height != iTargetHeight)
                SPDLOG_WARN("Set the game's resolution to the configured dimensions; this ASI does not force resolution.");
        }
    }

    bool InitializeLogging()
    {
        WCHAR dllPath[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(thisModule, dllPath, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) {
            OutputDebugStringA("Ys8_AspectPatch: Failed to resolve ASI path.\n");
            return false;
        }
        FixPath = fs::path(dllPath).parent_path();

        std::ofstream clearLog(FixPath / sLogFile, std::ios::trunc);
        if (clearLog.is_open()) clearLog.close();

        try {
            auto logger = std::make_shared<spdlog::logger>(
                "Ys8_AspectPatch",
                std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    (FixPath / sLogFile).string(), 5 * 1024 * 1024, 1));
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::info);
            SPDLOG_INFO("Ys8 x64 AspectPatch initialized.");
            return true;
        } catch (const std::exception& error) {
            OutputDebugStringA(error.what());
            OutputDebugStringA("\nYs8_AspectPatch: Logging failed.\n");
            return false;
        }
    }

    template<typename T>
    bool ReadSetting(inipp::Ini<char>& ini, const char* section, const char* key, T& value)
    {
        const auto& values = ini.sections[section];
        if (values.find(key) == values.end())
            return true;
        if (inipp::get_value(values, key, value))
            return true;
        SPDLOG_ERROR("Invalid [{}] {}.", section, key);
        return false;
    }

    bool LoadConfiguration()
    {
        const fs::path path = FixPath / sConfigFile;
        if (!fs::exists(path)) {
            std::ofstream defaultIni(path);
            defaultIni << "[Resolution]\nWidth = 1280\nHeight = 800\n\n"
                          "[AspectRatio]\n; Optional manual override\n; CustomAR = 1.6\n\n"
                          "[Fixes]\nCameraAspect = true\nRemoveLetterbox = true\n"
                          "; Shared camera/UI projection: experimental, may distort UI\n"
                          "SharedProjection = false\n";
            defaultIni.close();
            if (!defaultIni) {
                SPDLOG_ERROR("Failed to create {}.", path.string());
                return false;
            }
            SPDLOG_INFO("Created default {}.", sConfigFile);
        }

        std::ifstream iniFile(path);
        if (!iniFile) {
            SPDLOG_ERROR("Failed to open {}.", path.string());
            return false;
        }

        inipp::Ini<char> ini;
        ini.parse(iniFile);
        ini.strip_trailing_comments();

        if (!ini.errors.empty()) {
            for (const auto& error : ini.errors)
                SPDLOG_ERROR("INI parse error: {}", error);
            return false;
        }
        if (!ReadSetting(ini, "Resolution", "Width", iTargetWidth) ||
            !ReadSetting(ini, "Resolution", "Height", iTargetHeight) ||
            !ReadSetting(ini, "Fixes", "CameraAspect", bCameraAspect) ||
            !ReadSetting(ini, "Fixes", "RemoveLetterbox", bRemoveLetterbox) ||
            !ReadSetting(ini, "Fixes", "SharedProjection", bSharedProjection))
            return false;
        if (iTargetWidth <= 0 || iTargetHeight <= 0) {
            SPDLOG_ERROR("Resolution Width/Height must be positive.");
            return false;
        }
        fCustomAR = static_cast<float>(iTargetWidth) / static_cast<float>(iTargetHeight);
        if (!ReadSetting(ini, "AspectRatio", "CustomAR", fCustomAR))
            return false;
        if (!std::isfinite(fCustomAR) || fCustomAR <= 0.0f) {
            SPDLOG_ERROR("CustomAR must be finite and positive.");
            return false;
        }

        SPDLOG_INFO("Config loaded -> Resolution: {}x{}, Target Aspect Ratio: {:.5f}",
                    iTargetWidth, iTargetHeight, fCustomAR);
        return true;
    }

    std::vector<std::uint8_t*> FindPattern(HMODULE module, std::string_view text)
    {
        std::vector<std::optional<std::uint8_t>> pattern;
        for (std::size_t i = 0; i < text.size();)
        {
            if (text[i] == ' ')
                ++i;
            else if (text[i] == '?')
            {
                pattern.emplace_back(std::nullopt);
                i += (i + 1 < text.size() && text[i + 1] == '?') ? 2 : 1;
            }
            else
            {
                pattern.emplace_back(static_cast<std::uint8_t>(std::stoul(std::string(text.substr(i, 2)), nullptr, 16)));
                i += 2;
            }
        }
        std::vector<std::uint8_t*> matches;
        auto* base = reinterpret_cast<std::uint8_t*>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const auto* section = IMAGE_FIRST_SECTION(nt);
        for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section)
        {
            if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE) || section->Misc.VirtualSize < pattern.size())
                continue;
            auto* start = base + section->VirtualAddress;
            const std::size_t last = section->Misc.VirtualSize - pattern.size();
            for (std::size_t offset = 0; offset <= last; ++offset)
            {
                std::size_t i = 0;
                while (i < pattern.size() && (!pattern[i] || start[offset + i] == *pattern[i]))
                    ++i;
                if (i == pattern.size())
                    matches.push_back(start + offset);
            }
        }
        return matches;
    }

    std::uint8_t* FindUnique(HMODULE module, const char* name, std::string_view pattern, std::ptrdiff_t offset)
    {
        const auto matches = FindPattern(module, pattern);
        if (matches.size() != 1)
        {
            SPDLOG_ERROR("{} pattern: expected 1 match, found {}.", name, matches.size());
            return nullptr;
        }
        auto* address = matches.front() + offset;
        SPDLOG_INFO("{} found at +{:X}.", name, address - reinterpret_cast<std::uint8_t*>(module));
        return address;
    }

    bool WriteFloat(std::uint8_t* address, float value)
    {
        DWORD oldProtect = 0;
        if (!VirtualProtect(address, sizeof(value), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            SPDLOG_ERROR("VirtualProtect failed at 0x{:X}: {}.", reinterpret_cast<std::uintptr_t>(address), GetLastError());
            return false;
        }
        std::memcpy(address, &value, sizeof(value));
        DWORD unused = 0;
        const BOOL restored = VirtualProtect(address, sizeof(value), oldProtect, &unused);
        if (!restored)
            SPDLOG_ERROR("Memory protection restoration failed: {}.", GetLastError());
        const BOOL flushed = FlushInstructionCache(GetCurrentProcess(), address, sizeof(value));
        if (!flushed)
            SPDLOG_ERROR("Instruction-cache flush failed: {}.", GetLastError());
        return restored && flushed;
    }

    bool ApplyMemoryPatch()
    {
        constexpr float originalAR = 16.0f / 9.0f;
        std::uint8_t* aspect1 = nullptr;
        std::uint8_t* aspect2 = nullptr;
        std::uint8_t* letterbox = nullptr;
        std::uint8_t* sharedAR = nullptr;
        if (bCameraAspect) {
            aspect1 = FindUnique(exeModule, "Camera aspect 1", "C7 87 30 08 00 00 39 8E E3 3F", 6);
            aspect2 = FindUnique(exeModule, "Camera aspect 2", "C7 83 30 08 00 00 39 8E E3 3F", 6);
            if (!aspect1 || !aspect2)
                return false;
        }
        if (bRemoveLetterbox) {
            letterbox = FindUnique(exeModule, "Letterbox",
                "F3 0F 10 15 ?? ?? ?? ?? 66 41 0F 6E CA 0F 5B C9 66 41 0F 6E F0", 8);
            if (!letterbox)
                return false;
        }
        if (bSharedProjection) {
            auto* load = FindUnique(exeModule, "Shared projection",
                "F3 0F 10 1D ?? ?? ?? ?? F3 0F 10 15 ?? ?? ?? ?? 0F 11 03 F3 0F 10 05", 8);
            if (!load)
                return false;
            std::int32_t displacement = 0;
            std::memcpy(&displacement, load + 4, sizeof(displacement));
            sharedAR = load + 8 + displacement;
            float value = 0.0f;
            std::memcpy(&value, sharedAR, sizeof(value));
            if (value != originalAR) {
                SPDLOG_ERROR("Shared projection value is {}, expected 16/9; nothing changed.", value);
                return false;
            }
        }

        if (bRemoveLetterbox) {
            auto result = safetyhook::MidHook::create(
                letterbox, LetterboxCallback, safetyhook::MidHook::StartDisabled);
            if (!result) {
                SPDLOG_ERROR("Letterbox hook creation failed: {}.", static_cast<int>(result.error().type));
                return false;
            }
            letterboxHook = std::move(*result);
        }
        const auto rollback = [=]() {
            if (bCameraAspect) {
                const bool restored1 = WriteFloat(aspect1, originalAR);
                const bool restored2 = WriteFloat(aspect2, originalAR);
                if (!restored1 || !restored2)
                    SPDLOG_ERROR("Camera patch rollback failed; restart the game.");
            }
            if (bSharedProjection && !WriteFloat(sharedAR, originalAR))
                SPDLOG_ERROR("Shared projection rollback failed; restart the game.");
            letterboxHook.reset();
        };
        if (bCameraAspect && (!WriteFloat(aspect1, fCustomAR) || !WriteFloat(aspect2, fCustomAR))) {
            rollback();
            return false;
        }
        if (bSharedProjection && !WriteFloat(sharedAR, fCustomAR)) {
            rollback();
            return false;
        }
        if (bRemoveLetterbox) {
            const auto enabled = letterboxHook.enable();
            if (!enabled) {
                SPDLOG_ERROR("Letterbox hook enable failed: {}.", static_cast<int>(enabled.error().type));
                rollback();
                return false;
            }
        }
        SPDLOG_INFO("Applied: CameraAspect={}, RemoveLetterbox={}, SharedProjection={}.",
                    bCameraAspect, bRemoveLetterbox, bSharedProjection);
        SPDLOG_WARN("Experimental: HUD, scripted FOV, map zoom and resolution changes still require in-game testing.");
        return true;
    }

    DWORD WINAPI SetupThread(void*)
    {
        if (!InitializeLogging()) return 1;
        try {
            HMODULE pinned = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                   reinterpret_cast<LPCWSTR>(&SetupThread), &pinned)) {
                SPDLOG_ERROR("Failed to pin ASI for active hooks: {}.", GetLastError());
                return 1;
            }
            if (!LoadConfiguration()) return 1;
            return ApplyMemoryPatch() ? 0 : 1;
        } catch (const std::exception& error) {
            SPDLOG_ERROR("Setup failed: {}.", error.what());
            return 1;
        }
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        thisModule = hModule;
        if (HANDLE thread = CreateThread(nullptr, 0, SetupThread, nullptr, 0, nullptr)) {
            CloseHandle(thread);
        } else {
            OutputDebugStringA("Ys8_AspectPatch: Setup thread creation failed.\n");
            return FALSE;
        }
    }
    return TRUE;
}