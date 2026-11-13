#include "stdafx.h"
#include "version.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string_view>
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

    std::vector<std::optional<std::uint8_t>> ParsePattern(std::string_view text)
    {
        std::vector<std::optional<std::uint8_t>> bytes;
        for (std::size_t i = 0; i < text.size();)
        {
            if (text[i] == ' ')
            {
                ++i;
                continue;
            }
            if (text[i] == '?')
            {
                bytes.emplace_back(std::nullopt);
                i += (i + 1 < text.size() && text[i + 1] == '?') ? 2 : 1;
                continue;
            }
            bytes.emplace_back(static_cast<std::uint8_t>(std::stoul(std::string(text.substr(i, 2)), nullptr, 16)));
            i += 2;
        }
        return bytes;
    }

    std::vector<std::uint8_t*> FindPattern(HMODULE module, std::string_view text)
    {
        const auto pattern = ParsePattern(text);
        std::vector<std::uint8_t*> matches;
        auto* base = reinterpret_cast<std::uint8_t*>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const auto* section = IMAGE_FIRST_SECTION(nt);
        for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section)
        {
            if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE))
                continue;
            auto* start = base + section->VirtualAddress;
            const std::size_t size = section->Misc.VirtualSize;
            if (size < pattern.size())
                continue;
            for (std::size_t offset = 0; offset <= size - pattern.size(); ++offset)
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
        SPDLOG_INFO("{} found at GameAssembly+{:X}.", name,
            address - reinterpret_cast<std::uint8_t*>(module));
        return address;
    }

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
        SPDLOG_INFO("{} v{}: Dave the Diver aspect-ratio fix.", sFixName, sFixVersion);

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
        auto* target = FindUnique(assembly, "Viewport",
            "89 47 58 33 C9 E8 ?? ?? ?? ?? 89 47 5C F3 44 0F 10 05 ?? ?? ?? ?? "
            "45 0F 28 D8 F3 45 0F 5E D9 45 0F 2F C1", 13);
        if (!target)
        {
            SPDLOG_ERROR("Viewport code not identified. No hooks installed.");
            return false;
        }

        std::uint8_t* resolutionTarget = nullptr;
        if (targetWidth > 0)
        {
            resolutionTarget = FindUnique(assembly, "SetResolution",
                "48 8B C4 48 89 58 08 57 48 81 EC D0 00 00 00 83 79 50 00 0F 57 C0 "
                "0F 29 70 E8 41 8B F9 0F 29 78 D8 0F 28 F2 0F 28 F9", 0);
            if (!resolutionTarget)
            {
                SPDLOG_ERROR("SetResolution code not identified. No hooks installed.");
                return false;
            }
        }
        auto result = SafetyHookMid::create(target, UnlockViewport);
        if (!result)
        {
            SPDLOG_ERROR("Cannot install viewport hook: error {}.", static_cast<int>(result.error().type));
            return false;
        }
        viewportHook = std::move(*result);
        SPDLOG_INFO("Viewport/bar factor hook installed.");
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
            SPDLOG_INFO("Game resolution request hook installed.");
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
