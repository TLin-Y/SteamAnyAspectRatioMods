#include "stdafx.h"
#include "version.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <inipp/inipp.h>
#include <safetyhook.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace
{
    constexpr float baseAspect = 16.0f / 9.0f;
    constexpr std::uint8_t aspect16x9 = 1;
    constexpr std::uint8_t aspect21x9 = 3;

    HMODULE thisModule = nullptr;
    std::filesystem::path directory;
    std::array<SafetyHookMid, 3> hooks;
    bool enabled = true;
    int targetWidth = 1280;
    int targetHeight = 800;
    float fovMultiplier = 1.0f;
    std::atomic<bool> uiPatched{ false };
    std::atomic<bool> resolutionSeen{ false };
    std::atomic<bool> resolutionFallbackDone{ false };
    std::atomic<int> lastRequestedWidth{ 0 };
    std::atomic<int> lastRequestedHeight{ 0 };
    std::atomic<bool> invalidLensLogged{ false };
    PVOID dllNotificationCookie = nullptr;

    using GetAspect = float (*)(void*, const void*);
    using GetTargetTexture = void* (*)(void*, const void*);
    using GetInt = int (*)(const void*);
    using SetResolution = void (*)(int, int, int, int);
    GetAspect getAspect = nullptr;
    GetTargetTexture getTargetTexture = nullptr;
    GetInt getScreenWidth = nullptr;
    GetInt getScreenHeight = nullptr;
    GetInt getFullScreenMode = nullptr;
    SetResolution setResolution = nullptr;

    struct NtUnicodeString
    {
        USHORT length;
        USHORT maximumLength;
        PWSTR buffer;
    };

    struct DllLoadedData
    {
        ULONG flags;
        const NtUnicodeString* fullDllName;
        const NtUnicodeString* baseDllName;
        PVOID dllBase;
        ULONG sizeOfImage;
    };

    using DllNotification = VOID(CALLBACK*)(ULONG, const DllLoadedData*, PVOID);
    using RegisterDllNotification = LONG(NTAPI*)(ULONG, DllNotification, PVOID, PVOID*);
    using UnregisterDllNotification = LONG(NTAPI*)(PVOID);

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
        if (pattern.empty() || !pattern.front())
            return matches;
        auto* base = reinterpret_cast<std::uint8_t*>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const auto* section = IMAGE_FIRST_SECTION(nt);
        for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section)
        {
            if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE) || section->Misc.VirtualSize < pattern.size())
                continue;
            auto* cursor = base + section->VirtualAddress;
            auto* const last = cursor + section->Misc.VirtualSize - pattern.size();
            while (cursor <= last)
            {
                cursor = static_cast<std::uint8_t*>(std::memchr(cursor, *pattern.front(), last - cursor + 1));
                if (!cursor)
                    break;
                std::size_t i = 1;
                while (i < pattern.size() && (!pattern[i] || cursor[i] == *pattern[i]))
                    ++i;
                if (i == pattern.size())
                    matches.push_back(cursor);
                ++cursor;
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
        SPDLOG_INFO("{} found at GameAssembly+{:X}.", name, address - reinterpret_cast<std::uint8_t*>(module));
        return address;
    }

    bool WriteCode(std::uint8_t* address, std::initializer_list<std::uint8_t> bytes)
    {
        DWORD oldProtect = 0;
        if (!VirtualProtect(address, bytes.size(), PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;
        std::memcpy(address, bytes.begin(), bytes.size());
        VirtualProtect(address, bytes.size(), oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), address, bytes.size());
        return true;
    }

    std::uint8_t* FindFunctionStart(std::uint8_t* inside, std::size_t maxDistance)
    {
        auto* candidate = reinterpret_cast<std::uint8_t*>(reinterpret_cast<std::uintptr_t>(inside) & ~std::uintptr_t{ 0xF });
        for (std::size_t distance = 0; distance <= maxDistance; distance += 16, candidate -= 16)
        {
            if (candidate[-1] == 0xCC && candidate[0] != 0xCC)
            {
                for (int i = 0; i < 0x20; ++i)
                    if (candidate[i] == 0x80 && candidate[i + 1] == 0x3D && candidate[i + 6] == 0x00)
                        return candidate;
            }
        }
        return nullptr;
    }

    bool RunningOnMacOS()
    {
        using HostVersion = void (CDECL*)(const char**, const char**);
        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        const auto getHostVersion = ntdll
            ? reinterpret_cast<HostVersion>(GetProcAddress(ntdll, "wine_get_host_version"))
            : nullptr;
        if (!getHostVersion)
            return false;
        const char* sysname = nullptr;
        const char* release = nullptr;
        getHostVersion(&sysname, &release);
        return sysname && std::strcmp(sysname, "Darwin") == 0;
    }

    float ViewScale(float aspect)
    {
        return std::max(1.0f, baseAspect / aspect);
    }

    float AdjustFieldOfView(float fieldOfView, float scale)
    {
        if (scale == 1.0f)
            return fieldOfView;
        constexpr float radians = std::numbers::pi_v<float> / 180.0f;
        return 2.0f * std::atan(std::tan(fieldOfView * radians * 0.5f) * scale) / radians;
    }

    void PatchUi(HMODULE assembly)
    {
        if (uiPatched.exchange(true))
            return;
        auto* letterboxSite = FindUnique(assembly, "Letterbox",
            "E8 ?? ?? ?? ?? 45 33 C0 BA A0 0F 00 00 49 8B CE 48 8B F8 E8", 0);
        auto* letterbox = letterboxSite ? FindFunctionStart(letterboxSite, 0x600) : nullptr;
        if (letterbox && WriteCode(letterbox, { 0xC3 }))
            SPDLOG_INFO("Letterbox disabled at GameAssembly+{:X}.", letterbox - reinterpret_cast<std::uint8_t*>(assembly));
        else
            SPDLOG_ERROR("Letterbox code not identified; the game's letterbox stays active.");

        auto* fovAxis = FindUnique(assembly, "FOV axis",
            "F3 0F 11 4B 40 F3 0F 10 43 3C 33 C0 0F 2F C1 0F 97 C0 89 43 38", 15);
        if (!fovAxis || !WriteCode(fovAxis, { 0x90, 0x90, 0x90 }))
            SPDLOG_ERROR("FOV axis code not identified; the game may convert FOV a second time below 16:9.");

        if (targetWidth > 0)
        {
            const float aspect = static_cast<float>(targetWidth) / targetHeight;
            const std::uint8_t category = aspect > baseAspect + 0.01f ? aspect21x9 : aspect16x9;
            auto* canvas = FindUnique(assembly, "Canvas match", "33 C9 E8 ?? ?? ?? ?? 8B F8 48 8B 1D", 2);
            if (canvas && WriteCode(canvas, { 0xB8, category, 0x00, 0x00, 0x00 }))
                SPDLOG_INFO("UI canvas scaling for aspect {:.4f}: match {}.", aspect,
                    category == aspect21x9 ? "height" : "width");
            else
                SPDLOG_ERROR("Canvas match code not identified; UI scaling follows the monitor shape.");
        }
    }

    VOID CALLBACK OnDllNotification(ULONG reason, const DllLoadedData* data, PVOID)
    {
        constexpr ULONG loaded = 1;
        if (reason != loaded || !data || !data->baseDllName || !data->baseDllName->buffer)
            return;
        const std::wstring_view name(data->baseDllName->buffer, data->baseDllName->length / sizeof(wchar_t));
        constexpr std::wstring_view target = L"GameAssembly.dll";
        if (name.size() == target.size() && _wcsnicmp(name.data(), target.data(), target.size()) == 0)
            PatchUi(static_cast<HMODULE>(data->dllBase));
    }

    void WatchGameAssembly()
    {
        if (const HMODULE assembly = GetModuleHandleW(L"GameAssembly.dll"))
        {
            PatchUi(assembly);
            return;
        }
        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        const auto registerNotification = ntdll
            ? reinterpret_cast<RegisterDllNotification>(GetProcAddress(ntdll, "LdrRegisterDllNotification"))
            : nullptr;
        if (!registerNotification || registerNotification(0, OnDllNotification, nullptr, &dllNotificationCookie) != 0)
            SPDLOG_INFO("DLL load notification unavailable; UI patches wait for the initialization thread.");
    }

    void OverrideResolution(SafetyHookContext& ctx)
    {
        resolutionSeen = true;
        const int requestedWidth = static_cast<int>(ctx.rcx);
        const int requestedHeight = static_cast<int>(ctx.rdx);
        ctx.rcx = static_cast<std::uint32_t>(targetWidth);
        ctx.rdx = static_cast<std::uint32_t>(targetHeight);
        if (requestedWidth != lastRequestedWidth.exchange(requestedWidth) ||
            requestedHeight != lastRequestedHeight.exchange(requestedHeight))
            SPDLOG_INFO("Resolution request {}x{} (mode {}) -> {}x{}.", requestedWidth, requestedHeight,
                static_cast<int>(ctx.r8), targetWidth, targetHeight);
    }

    void ApplyMissedResolution()
    {
        if (targetWidth <= 0 || resolutionSeen || resolutionFallbackDone.exchange(true))
            return;
        const int width = getScreenWidth(nullptr);
        const int height = getScreenHeight(nullptr);
        if (width == targetWidth && height == targetHeight)
            return;
        const int mode = getFullScreenMode(nullptr);
        SPDLOG_INFO("Startup resolution was set before the hooks; requesting {}x{} (mode {}) now.",
            targetWidth, targetHeight, mode);
        setResolution(targetWidth, targetHeight, mode, 0);
    }

    void AdjustLens(SafetyHookContext& ctx, bool perspective)
    {
        if (perspective)
            ApplyMissedResolution();
        auto* camera = reinterpret_cast<void*>(ctx.rdi);
        if (!camera || getTargetTexture(camera, nullptr))
            return;
        const float aspect = getAspect(camera, nullptr);
        const float lens = ctx.xmm1.f32[0];
        if (!std::isfinite(aspect) || aspect <= 0.0f ||
            !std::isfinite(lens) || lens <= 0.0f || (perspective && lens >= 180.0f))
        {
            if (!invalidLensLogged.exchange(true))
                SPDLOG_ERROR("Invalid camera aspect/lens ({}, {}); leaving the lens unchanged.", aspect, lens);
            return;
        }
        const float scale = ViewScale(aspect) * (perspective ? fovMultiplier : 1.0f);
        const float adjusted = perspective ? std::min(AdjustFieldOfView(lens, scale), 170.0f) : lens * scale;
        if (!std::isfinite(adjusted) || adjusted <= 0.0f)
        {
            if (!invalidLensLogged.exchange(true))
                SPDLOG_ERROR("Invalid adjusted camera lens {}; leaving the lens unchanged.", adjusted);
            return;
        }
        ctx.xmm1.f32[0] = adjusted;
        if (perspective)
        {
            thread_local float lastAspect = 0.0f;
            if (aspect != lastAspect)
            {
                SPDLOG_INFO("Camera aspect {:.4f}: FOV {:.2f} -> {:.2f}.", aspect, lens, adjusted);
                lastAspect = aspect;
            }
        }
    }

    bool ReadConfig()
    {
        const auto path = directory / (sFixName + ".ini");
        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        if (error)
        {
            SPDLOG_ERROR("Cannot inspect configuration: {}", error.message());
            return false;
        }
        if (!exists)
        {
            SPDLOG_INFO("No INI found; using defaults.");
            return true;
        }
        std::ifstream file(path);
        if (!file)
        {
            SPDLOG_ERROR("Cannot open configuration: {}", path.string());
            return false;
        }
        inipp::Ini<char> ini;
        ini.parse(file);
        ini.strip_trailing_comments();
        if (file.bad() || !ini.errors.empty())
        {
            SPDLOG_ERROR("Malformed configuration: {}", path.string());
            return false;
        }
        const auto& aspect = ini.sections["AspectRatio"];
        if (aspect.contains("Enabled") && !inipp::get_value(aspect, "Enabled", enabled))
        {
            SPDLOG_ERROR("Invalid [AspectRatio] Enabled; expected true or false.");
            return false;
        }
        const auto& resolution = ini.sections["Resolution"];
        for (const char* key : { "Width", "Height" })
        {
            int& value = std::strcmp(key, "Width") == 0 ? targetWidth : targetHeight;
            if (resolution.contains(key) && !inipp::get_value(resolution, key, value))
            {
                SPDLOG_ERROR("Invalid [Resolution] {}; expected an integer.", key);
                return false;
            }
        }
        if (!((targetWidth == 0 && targetHeight == 0) || (targetWidth > 0 && targetHeight > 0)))
        {
            SPDLOG_ERROR("Invalid resolution {}x{}; use two positive values or 0/0.", targetWidth, targetHeight);
            return false;
        }
        const auto& fov = ini.sections["FOV"];
        if (fov.contains("Multiplier") &&
            (!inipp::get_value(fov, "Multiplier", fovMultiplier) || !std::isfinite(fovMultiplier) ||
                fovMultiplier < 0.5f || fovMultiplier > 2.0f))
        {
            SPDLOG_ERROR("Invalid [FOV] Multiplier; expected a number from 0.5 to 2.0.");
            return false;
        }
        return true;
    }

    bool Configure()
    {
        static_assert(sizeof(void*) == 8, "STAR OCEAN THE SECOND STORY R requires Windows/x64.");
        wchar_t path[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(thisModule, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
        {
            OutputDebugStringA("SO2R ARFix: Cannot resolve ASI path.\n");
            return false;
        }
        directory = std::filesystem::path(path).parent_path();
        try
        {
            auto logger = spdlog::basic_logger_mt(sFixName, (directory / (sFixName + ".log")).string(), true);
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
            spdlog::flush_on(spdlog::level::info);
        }
        catch (const spdlog::spdlog_ex& error)
        {
            OutputDebugStringA(error.what());
            return false;
        }
        SPDLOG_INFO("{} v{}.", sFixName, sFixVersion);
        if (!ReadConfig())
            return false;
        if (targetWidth > 0 && RunningOnMacOS())
        {
            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);
            if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) && mode.dmPelsWidth > 0 && mode.dmPelsHeight > 0)
            {
                SPDLOG_INFO("macOS host: using the display size {}x{} instead of {}x{}.",
                    mode.dmPelsWidth, mode.dmPelsHeight, targetWidth, targetHeight);
                targetWidth = static_cast<int>(mode.dmPelsWidth);
                targetHeight = static_cast<int>(mode.dmPelsHeight);
            }
        }
        SPDLOG_INFO("Enabled={}, resolution={}x{} (0/0 keeps game settings), FOV multiplier={:.2f}.",
            enabled, targetWidth, targetHeight, fovMultiplier);
        return true;
    }

    void ResetHooks()
    {
        for (auto& hook : hooks)
            hook.reset();
    }

    bool InstallHooks()
    {
        HMODULE assembly = nullptr;
        for (int attempt = 0; attempt < 6000 && !assembly; ++attempt)
        {
            assembly = GetModuleHandleW(L"GameAssembly.dll");
            if (!assembly)
                Sleep(10);
        }
        if (!assembly)
        {
            SPDLOG_ERROR("GameAssembly.dll did not load within 60 seconds. No hooks installed.");
            return false;
        }
        PatchUi(assembly);
        if (dllNotificationCookie)
        {
            const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
            if (const auto unregister = reinterpret_cast<UnregisterDllNotification>(
                GetProcAddress(ntdll, "LdrUnregisterDllNotification")))
                unregister(dllNotificationCookie);
            dllNotificationCookie = nullptr;
        }

        using ResolveIcall = void* (*)(const char*);
        const auto resolveIcall = reinterpret_cast<ResolveIcall>(GetProcAddress(assembly, "il2cpp_resolve_icall"));
        if (!resolveIcall)
        {
            SPDLOG_ERROR("il2cpp_resolve_icall not exported. No hooks installed.");
            return false;
        }
        const auto resolveAll = [&]
        {
            getAspect = reinterpret_cast<GetAspect>(resolveIcall("UnityEngine.Camera::get_aspect()"));
            getTargetTexture = reinterpret_cast<GetTargetTexture>(resolveIcall("UnityEngine.Camera::get_targetTexture()"));
            getScreenWidth = reinterpret_cast<GetInt>(resolveIcall("UnityEngine.Screen::get_width()"));
            getScreenHeight = reinterpret_cast<GetInt>(resolveIcall("UnityEngine.Screen::get_height()"));
            getFullScreenMode = reinterpret_cast<GetInt>(resolveIcall("UnityEngine.Screen::get_fullScreenMode()"));
            setResolution = reinterpret_cast<SetResolution>(resolveIcall(
                "UnityEngine.Screen::SetResolution(System.Int32,System.Int32,UnityEngine.FullScreenMode,System.Int32)"));
            return getAspect && getTargetTexture && getScreenWidth && getScreenHeight && getFullScreenMode && setResolution;
        };
        bool resolved = false;
        for (int attempt = 0; attempt < 3000 && !(resolved = resolveAll()); ++attempt)
            Sleep(10);
        if (!resolved)
        {
            SPDLOG_ERROR("Unity camera/screen functions not resolved. No hooks installed.");
            return false;
        }

        struct HookSite
        {
            std::uint8_t* address;
            safetyhook::MidHookFn callback;
            const char* name;
        };
        auto* lens = FindUnique(assembly, "Lens",
            "F3 0F 10 4B 04 45 33 C0 48 8B CF E8 ?? ?? ?? ?? F3 0F 10 0B 45 33 C0 48 8B CF E8 ?? ?? ?? ?? F3 0F 10 ?? 1C", 0);
        std::vector<HookSite> sites{
            { lens ? lens + 5 : nullptr, [](SafetyHookContext& ctx) { AdjustLens(ctx, false); }, "orthographic lens" },
            { lens ? lens + 20 : nullptr, [](SafetyHookContext& ctx) { AdjustLens(ctx, true); }, "perspective lens" }
        };
        if (targetWidth > 0)
            sites.push_back({ reinterpret_cast<std::uint8_t*>(setResolution), OverrideResolution, "resolution" });
        for (const auto& site : sites)
        {
            if (!site.address)
            {
                SPDLOG_ERROR("{} code not identified. No hooks installed.", site.name);
                return false;
            }
        }
        for (std::size_t index = 0; index < sites.size(); ++index)
        {
            auto result = SafetyHookMid::create(sites[index].address, sites[index].callback, SafetyHookMid::StartDisabled);
            if (!result)
            {
                SPDLOG_ERROR("Cannot install {} hook: error {}. Removing installed hooks.",
                    sites[index].name, static_cast<int>(result.error().type));
                ResetHooks();
                return false;
            }
            hooks[index] = std::move(*result);
        }
        for (std::size_t index = sites.size(); index > 0; --index)
        {
            const auto result = hooks[index - 1].enable();
            if (!result)
            {
                SPDLOG_ERROR("Cannot enable {} hook: error {}. Removing installed hooks.",
                    sites[index - 1].name, static_cast<int>(result.error().type));
                ResetHooks();
                return false;
            }
            SPDLOG_INFO("{} hook installed.", sites[index - 1].name);
        }
        return true;
    }

    bool IsGameProcess()
    {
        wchar_t path[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return false;
        const auto name = std::filesystem::path(path).filename();
        return _wcsicmp(name.c_str(), L"SO2R.exe") == 0 || _wcsicmp(name.c_str(), L"SO2R_Demo.exe") == 0;
    }

    DWORD WINAPI ReportConfigurationFailure(void*)
    {
        MessageBoxW(nullptr, L"Configuration failed. Check SO2R_ARFix.log. The fix is not active.",
            L"SO2R ARFix", MB_OK | MB_ICONERROR);
        return 1;
    }

    DWORD WINAPI Main(void*)
    {
        if (!InstallHooks())
        {
            MessageBoxW(nullptr, L"Initialization failed. Check SO2R_ARFix.log. The fix is not fully active.",
                L"SO2R ARFix", MB_OK | MB_ICONERROR);
            return 1;
        }
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        thisModule = module;
        DisableThreadLibraryCalls(module);
        if (!IsGameProcess())
            return TRUE;
        if (!Configure())
        {
            if (const HANDLE thread = CreateThread(nullptr, 0, ReportConfigurationFailure, nullptr, 0, nullptr))
                CloseHandle(thread);
            return TRUE;
        }
        if (!enabled)
            return TRUE;
        WatchGameAssembly();
        const HANDLE thread = CreateThread(nullptr, 0, Main, nullptr, 0, nullptr);
        if (!thread)
        {
            SPDLOG_ERROR("Cannot create initialization thread.");
            return TRUE;
        }
        CloseHandle(thread);
    }
    return TRUE;
}
