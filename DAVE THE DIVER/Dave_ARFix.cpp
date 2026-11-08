#include "stdafx.h"
#include "version.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <inipp/inipp.h>
#include <safetyhook.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace
{
    HMODULE thisModule = nullptr;
    SafetyHookMid viewportHook;
    SafetyHookMid resolutionHook;
    std::atomic<bool> invalidDimensionsLogged{ false };
    bool enabled = true;
    int targetWidth = 0;
    int targetHeight = 0;

    bool ValidResolution(int width, int height)
    {
        return (width == 0 && height == 0) || (width > 0 && height > 0);
    }

    void OverrideResolution(SafetyHookContext& ctx)
    {
        ctx.xmm1.f32[0] = static_cast<float>(targetWidth);
        ctx.xmm2.f32[0] = static_cast<float>(targetHeight);
        ctx.r9 = 2;
        SPDLOG_INFO("Requesting Unity windowed resolution: {}x{}.", targetWidth, targetHeight);
    }

    struct CameraResolution
    {
        std::byte prefix[0x58];
        int screenWidth;
        int screenHeight;
    };

    static_assert(sizeof(void*) == 8, "Dave the Diver requires Windows/x64.");
    static_assert(offsetof(CameraResolution, screenWidth) == 0x58);
    static_assert(offsetof(CameraResolution, screenHeight) == 0x5C);

    void UnlockViewport(SafetyHookContext& ctx)
    {
        const auto* resolution = reinterpret_cast<const CameraResolution*>(ctx.rdi);
        if (!resolution || resolution->screenWidth <= 0 || resolution->screenHeight <= 0 ||
            !std::isfinite(ctx.xmm9.f32[0]) || ctx.xmm9.f32[0] <= 0.0f)
        {
            if (!invalidDimensionsLogged.exchange(true))
                SPDLOG_ERROR("Invalid render dimensions/aspect factor; leaving this viewport calculation unchanged.");
            return;
        }

        ctx.xmm9.f32[0] = 1.0f;
        constexpr std::array<float, 4> fullRect{ 0.0f, 0.0f, 1.0f, 1.0f };
        std::memcpy(reinterpret_cast<void*>(ctx.rsp + 0x30), fullRect.data(), sizeof(fullRect));

        thread_local int lastWidth = 0;
        thread_local int lastHeight = 0;
        if (resolution->screenWidth != lastWidth || resolution->screenHeight != lastHeight)
        {
            SPDLOG_INFO("Camera viewport: {}x{}, aspect {:.6f}; full viewport, original camera zoom.",
                resolution->screenWidth, resolution->screenHeight,
                static_cast<float>(resolution->screenWidth) / resolution->screenHeight);
            lastWidth = resolution->screenWidth;
            lastHeight = resolution->screenHeight;
        }
    }

    bool Initialize()
    {
        wchar_t path[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(thisModule, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
        {
            OutputDebugStringA("Dave ARFix: Cannot resolve ASI path.\n");
            MessageBoxW(nullptr, L"Cannot resolve ASI path.", L"Dave ARFix", MB_OK | MB_ICONERROR);
            return false;
        }
        const auto directory = std::filesystem::path(path).parent_path();
        try
        {
            auto logger = spdlog::basic_logger_mt(sFixName,
                (directory / (sFixName + ".log")).string(), true);
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::info);
        }
        catch (const spdlog::spdlog_ex& error)
        {
            OutputDebugStringA(error.what());
            MessageBoxA(nullptr, error.what(), "Dave ARFix: Logging failed", MB_OK | MB_ICONERROR);
            return false;
        }
        SPDLOG_INFO("{} v{}: Dave the Diver aspect-ratio test implementation.", sFixName, sFixVersion);

        const auto configPath = directory / (sFixName + ".ini");
        std::error_code error;
        const bool configExists = std::filesystem::exists(configPath, error);
        if (error)
        {
            SPDLOG_ERROR("Cannot inspect configuration: {}", error.message());
            return false;
        }
        if (configExists)
        {
            std::ifstream file(configPath);
            if (!file)
            {
                SPDLOG_ERROR("Cannot open configuration: {}", configPath.string());
                return false;
            }
            inipp::Ini<char> ini;
            ini.parse(file);
            ini.strip_trailing_comments();
            if (file.bad() || !ini.errors.empty())
            {
                SPDLOG_ERROR("Malformed configuration: {}", configPath.string());
                return false;
            }
            const auto& section = ini.sections["AspectRatio"];
            if (section.find("Enabled") != section.end() && !inipp::get_value(section, "Enabled", enabled))
            {
                SPDLOG_ERROR("Invalid [AspectRatio] Enabled; expected true or false.");
                return false;
            }
            const auto& resolution = ini.sections["Resolution"];
            for (const auto* key : { "Width", "Height" })
            {
                int& value = std::strcmp(key, "Width") == 0 ? targetWidth : targetHeight;
                if (resolution.find(key) != resolution.end() && !inipp::get_value(resolution, key, value))
                {
                    SPDLOG_ERROR("Invalid [Resolution] {}; expected an integer.", key);
                    return false;
                }
            }
        }
        else
            SPDLOG_INFO("No INI found; using Enabled=true and automatic render aspect.");

        if (!ValidResolution(targetWidth, targetHeight))
        {
            SPDLOG_ERROR("Invalid resolution {}x{}: specify two positive values, or 0/0 to keep game settings.",
                targetWidth, targetHeight);
            return false;
        }
        SPDLOG_INFO("Enabled={}. VSync, camera zoom and canvas scaling remain game-controlled.",
            enabled);
        SPDLOG_INFO("Custom resolution: {}x{} (0/0 = game settings; positive values = Unity windowed mode).",
            targetWidth, targetHeight);
        if (!enabled)
            return true;

        HMODULE assembly = nullptr;
        for (int attempt = 0; attempt < 600 && !assembly; ++attempt)
        {
            assembly = GetModuleHandleW(L"GameAssembly.dll");
            if (!assembly)
                Sleep(100);
        }
        if (!assembly)
        {
            SPDLOG_ERROR("GameAssembly.dll did not load within 60 seconds. No hook installed.");
            return false;
        }
        auto* base = reinterpret_cast<std::uint8_t*>(assembly);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        SPDLOG_INFO("GameAssembly: timestamp 0x{:X}, image size 0x{:X}.",
            nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
        if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            nt->FileHeader.TimeDateStamp != 0x6AEAF48E || nt->OptionalHeader.SizeOfImage != 0x868E000)
        {
            SPDLOG_ERROR("Unsupported GameAssembly build. No hook installed.");
            return false;
        }

        constexpr std::array<std::uint8_t, 13> signature{
            0xF3, 0x44, 0x0F, 0x10, 0x05, 0xE2, 0x8E, 0xFD, 0x03,
            0x45, 0x0F, 0x28, 0xD8 };
        auto* target = base + 0x1D9FCD5;
        if (std::memcmp(target, signature.data(), signature.size()) != 0)
        {
            SPDLOG_ERROR("CoUpdateResolution instruction signature does not match. No hook installed.");
            return false;
        }
        auto* resolutionTarget = base + 0x1D91FB0;
        constexpr std::array<std::uint8_t, 15> resolutionSignature{
            0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x57,
            0x48, 0x81, 0xEC, 0xD0, 0x00, 0x00, 0x00 };
        if (targetWidth > 0 &&
            std::memcmp(resolutionTarget, resolutionSignature.data(), resolutionSignature.size()) != 0)
        {
            SPDLOG_ERROR("SetResolution instruction signature does not match. No hooks installed.");
            return false;
        }
        auto result = SafetyHookMid::create(target, UnlockViewport);
        if (!result)
        {
            SPDLOG_ERROR("Cannot install viewport hook: error {}.", static_cast<int>(result.error().type));
            return false;
        }
        viewportHook = std::move(*result);
        SPDLOG_INFO("Viewport/bar factor hook installed at GameAssembly+1D9FCD5.");
        if (targetWidth > 0)
        {
            auto resolutionResult = SafetyHookMid::create(resolutionTarget, OverrideResolution);
            if (!resolutionResult)
            {
                SPDLOG_ERROR("Cannot install resolution hook: error {}. Removing viewport hook.",
                    static_cast<int>(resolutionResult.error().type));
                viewportHook.reset();
                return false;
            }
            resolutionHook = std::move(*resolutionResult);
            SPDLOG_INFO("Game resolution request hook installed at GameAssembly+1D91FB0.");
        }
        return true;
    }

    DWORD WINAPI Main(void*)
    {
        return Initialize() ? 0 : 1;
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        thisModule = module;
        DisableThreadLibraryCalls(module);
        const HANDLE thread = CreateThread(nullptr, 0, Main, nullptr, 0, nullptr);
        if (!thread)
        {
            OutputDebugStringA("Dave ARFix: Cannot create initialization thread.\n");
            return FALSE;
        }
        CloseHandle(thread);
    }
    return TRUE;
}
