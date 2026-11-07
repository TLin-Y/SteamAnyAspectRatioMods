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

    constexpr std::array<std::uint8_t, 10> aspect1Signature =
        {0xC7, 0x87, 0x30, 0x08, 0, 0, 0x39, 0x8E, 0xE3, 0x3F};
    constexpr std::array<std::uint8_t, 10> aspect2Signature =
        {0xC7, 0x83, 0x30, 0x08, 0, 0, 0x39, 0x8E, 0xE3, 0x3F};
    constexpr std::array<std::uint8_t, 8> letterboxLoadSignature =
        {0xF3, 0x0F, 0x10, 0x15, 0xBF, 0x13, 0x40, 0};
    constexpr std::array<std::uint8_t, 8> letterboxHookSignature =
        {0x66, 0x41, 0x0F, 0x6E, 0xCA, 0x0F, 0x5B, 0xC9};

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
        auto base = reinterpret_cast<std::uint8_t*>(exeModule);
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000) {
            SPDLOG_ERROR("Invalid executable DOS header.");
            return false;
        }
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            nt->FileHeader.TimeDateStamp != 0x641ED741 || nt->OptionalHeader.SizeOfImage != 0x74A000) {
            SPDLOG_ERROR("Unsupported executable build. Expected timestamp 0x641ED741 / image size 0x74A000.");
            return false;
        }
        const auto matches = [base](std::uint32_t rva, const auto& signature) {
            return std::memcmp(base + rva, signature.data(), signature.size()) == 0;
        };
        if (!matches(0x204C4D, aspect1Signature) || !matches(0x2052E2, aspect2Signature) ||
            !matches(0x1B1E51, letterboxLoadSignature) || !matches(0x1B1E59, letterboxHookSignature)) {
            SPDLOG_ERROR("Original instruction validation failed. Disable other aspect patches; nothing changed.");
            return false;
        }
        constexpr float originalAR = 16.0f / 9.0f;
        float sharedAR = 0.0f;
        std::memcpy(&sharedAR, base + 0x5B5484, sizeof(sharedAR));
        if (sharedAR != originalAR) {
            SPDLOG_ERROR("Shared projection already modified; nothing changed.");
            return false;
        }

        if (bRemoveLetterbox) {
            auto result = safetyhook::MidHook::create(
                base + 0x1B1E59, LetterboxCallback, safetyhook::MidHook::StartDisabled);
            if (!result) {
                SPDLOG_ERROR("Letterbox hook creation failed: {}.", static_cast<int>(result.error().type));
                return false;
            }
            letterboxHook = std::move(*result);
        }
        const auto rollback = [base, originalAR]() {
            if (bCameraAspect) {
                const bool restored1 = WriteFloat(base + 0x204C53, originalAR);
                const bool restored2 = WriteFloat(base + 0x2052E8, originalAR);
                if (!restored1 || !restored2)
                    SPDLOG_ERROR("Camera patch rollback failed; restart the game.");
            }
            if (bSharedProjection && !WriteFloat(base + 0x5B5484, originalAR))
                SPDLOG_ERROR("Shared projection rollback failed; restart the game.");
            letterboxHook.reset();
        };
        // These RVAs already point at the immediate operands; do NOT add another six bytes.
        if (bCameraAspect &&
            (!WriteFloat(base + 0x204C53, fCustomAR) || !WriteFloat(base + 0x2052E8, fCustomAR))) {
            rollback();
            return false;
        }
        if (bSharedProjection && !WriteFloat(base + 0x5B5484, fCustomAR)) {
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
        SPDLOG_INFO("Logarithm table at RVA 0x60F400 and shared 9/16 constant left untouched.");
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