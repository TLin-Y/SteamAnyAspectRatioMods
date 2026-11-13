#include <algorithm>
#include <cmath>
#include <numbers>

namespace
{
    constexpr float originalAspect = 16.0f / 9.0f;

    float ViewScale(float aspect)
    {
        return std::max(1.0f, originalAspect / aspect);
    }

    float AdjustFieldOfView(float fieldOfView, float scale)
    {
        if (scale == 1.0f)
            return fieldOfView;
        constexpr float radians = std::numbers::pi_v<float> / 180.0f;
        return 2.0f * std::atan(std::tan(fieldOfView * radians * 0.5f) * scale) / radians;
    }
}

#ifdef PENTIMENT_ARFIX_TEST
#include <array>
#include <cstdlib>
#include <iostream>

int main()
{
    constexpr std::array aspects{ 4.0f / 3.0f, 16.0f / 10.0f, 16.0f / 9.0f,
        21.0f / 9.0f, 32.0f / 9.0f, 9.0f / 16.0f };
    for (const float aspect : aspects)
    {
        const float scale = ViewScale(aspect);
        const float expectedWidth = aspect < originalAspect ? originalAspect : aspect;
        if (std::abs(scale * aspect - expectedWidth) > 0.00001f ||
            (aspect >= originalAspect && scale != 1.0f))
            return EXIT_FAILURE;
        for (const float fieldOfView : { 30.0f, 60.0f, 90.0f })
        {
            const float adjusted = AdjustFieldOfView(fieldOfView, scale);
            constexpr float radians = std::numbers::pi_v<float> / 180.0f;
            const float projectedWidth = std::tan(adjusted * radians * 0.5f) * aspect;
            const float expected = std::tan(fieldOfView * radians * 0.5f) * expectedWidth;
            if (std::abs(projectedWidth - expected) > 0.00001f ||
                (scale == 1.0f && adjusted != fieldOfView))
                return EXIT_FAILURE;
        }
    }
    std::cout << "PASS: width preserved below 16:9; height preserved at/above 16:9; no lens stretching.\n";
    return EXIT_SUCCESS;
}
#else
#include "stdafx.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <optional>
#include <string_view>
#include <inipp/inipp.h>
#include <safetyhook.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace
{
    constexpr const char* fixName = "Pentiment_ARFix";
    HMODULE thisModule = nullptr;
    std::array<SafetyHookMid, 4> hooks;
    bool enabled = true;
    bool borderless = false;
    int targetWidth = 1280;
    int targetHeight = 800;
    std::atomic<bool> invalidLensLogged{ false };
    using GetAspect = float (*)(void*, const void*);
    using GetTargetTexture = void* (*)(void*, const void*);
    using SetResolution = void (*)(int, int, int, const void*);
    GetAspect getAspect = nullptr;
    GetTargetTexture getTargetTexture = nullptr;
    SetResolution setResolution = nullptr;

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

    BOOL CALLBACK FindGameWindow(HWND window, LPARAM parameter)
    {
        DWORD processId = 0;
        GetWindowThreadProcessId(window, &processId);
        if (processId != GetCurrentProcessId() || !IsWindowVisible(window) ||
            GetWindow(window, GW_OWNER))
            return TRUE;
        wchar_t className[64]{};
        if (GetClassNameW(window, className, 64) && std::wcscmp(className, L"UnityWndClass") == 0)
            *reinterpret_cast<HWND*>(parameter) = window;
        return TRUE;
    }

    bool RemoveWindowFrame(HWND window)
    {
        SetLastError(ERROR_SUCCESS);
        const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
        if (style == 0 && GetLastError() != ERROR_SUCCESS)
        {
            SPDLOG_ERROR("Cannot read game window style: Win32 error {}.", GetLastError());
            return false;
        }
        constexpr LONG_PTR frame = WS_CAPTION | WS_THICKFRAME;
        if (!(style & frame) || IsIconic(window))
            return true;
        RECT client{};
        if (!GetClientRect(window, &client))
        {
            SPDLOG_ERROR("Cannot read game client size: Win32 error {}.", GetLastError());
            return false;
        }
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        if (width <= 0 || height <= 0)
        {
            SPDLOG_ERROR("Invalid game client size {}x{}; leaving the window frame unchanged.", width, height);
            return false;
        }
        SetLastError(ERROR_SUCCESS);
        if (SetWindowLongPtrW(window, GWL_STYLE, style & ~frame) == 0 && GetLastError() != ERROR_SUCCESS)
        {
            SPDLOG_ERROR("Cannot remove game window frame: Win32 error {}.", GetLastError());
            return false;
        }
        if (!SetWindowPos(window, nullptr, 0, 0, width, height,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED))
        {
            const DWORD error = GetLastError();
            SetLastError(ERROR_SUCCESS);
            if (SetWindowLongPtrW(window, GWL_STYLE, style) == 0 && GetLastError() != ERROR_SUCCESS)
                SPDLOG_ERROR("Cannot restore game window style: Win32 error {}.", GetLastError());
            if (!SetWindowPos(window, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED))
                SPDLOG_ERROR("Cannot refresh restored game window frame: Win32 error {}.", GetLastError());
            SPDLOG_ERROR("Cannot preserve borderless client size: Win32 error {}.", error);
            return false;
        }
        SPDLOG_INFO("Borderless window enabled; preserving client size {}x{}.", width, height);
        return true;
    }

    bool MaintainBorderlessWindow()
    {
        bool foundWindow = false;
        unsigned attempts = 0;
        for (;;)
        {
            HWND window = nullptr;
            if (!EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&window)))
            {
                SPDLOG_ERROR("Cannot enumerate game windows: Win32 error {}.", GetLastError());
                return false;
            }
            if (window)
            {
                foundWindow = true;
                if (!RemoveWindowFrame(window))
                    return false;
            }
            else if (!foundWindow && ++attempts >= 240)
            {
                SPDLOG_ERROR("Unity game window did not appear within 60 seconds.");
                return false;
            }
            // Unity can recreate its frame after a resolution or fullscreen-mode change.
            Sleep(250);
        }
    }

    void KeepStartingResolution(SafetyHookContext& ctx)
    {
        ctx.rax = (ctx.rax & ~std::uintptr_t{ 0xFF }) | 1;
        if (targetWidth > 0)
        {
            setResolution(targetWidth, targetHeight, 3, nullptr);
            SPDLOG_INFO("Requesting Unity windowed resolution: {}x{}.", targetWidth, targetHeight);
        }
        else
            SPDLOG_INFO("Keeping the game's current resolution and fullscreen mode; bypassing the 16:9 clamp.");
    }

    void AllowResolution(SafetyHookContext& ctx)
    {
        ctx.rax = (ctx.rax & ~std::uintptr_t{ 0xFF }) | 1;
    }

    void AdjustLens(SafetyHookContext& ctx, bool perspective)
    {
        auto* camera = reinterpret_cast<void*>(ctx.rdi);
        if (!camera)
        {
            if (!invalidLensLogged.exchange(true))
                SPDLOG_ERROR("Null Cinemachine output camera; leaving the lens unchanged.");
            return;
        }
        // Book pages and snapshots have their own render-target framing.
        if (getTargetTexture(camera, nullptr))
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
        const float scale = ViewScale(aspect);
        const float adjusted = perspective ? AdjustFieldOfView(lens, scale) : lens * scale;
        if (!std::isfinite(adjusted) || adjusted <= 0.0f || (perspective && adjusted >= 180.0f))
        {
            if (!invalidLensLogged.exchange(true))
                SPDLOG_ERROR("Invalid adjusted camera lens {}; leaving the lens unchanged.", adjusted);
            return;
        }
        ctx.xmm1.f32[0] = adjusted;
        if (!perspective)
        {
            thread_local void* lastCamera = nullptr;
            thread_local float lastAspect = 0.0f;
            if (camera != lastCamera || aspect != lastAspect)
            {
                SPDLOG_INFO("Screen camera aspect {:.6f}, lens scale {:.6f}: {}.",
                    aspect, scale, aspect < originalAspect ? "preserving width" : "preserving height");
                lastCamera = camera;
                lastAspect = aspect;
            }
        }
    }

    bool ReadConfig(const std::filesystem::path& directory)
    {
        const auto path = directory / (std::string(fixName) + ".ini");
        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        if (error)
        {
            SPDLOG_ERROR("Cannot inspect configuration: {}", error.message());
            return false;
        }
        if (!exists)
        {
            SPDLOG_INFO("No INI found; using Enabled=true and 1280x800 windowed mode.");
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
        if (resolution.contains("Borderless") && !inipp::get_value(resolution, "Borderless", borderless))
        {
            SPDLOG_ERROR("Invalid [Resolution] Borderless; expected true or false.");
            return false;
        }
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
        return true;
    }

    bool Initialize()
    {
        static_assert(sizeof(void*) == 8, "Pentiment requires Windows/x64.");
        wchar_t path[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(thisModule, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
        {
            OutputDebugStringA("Pentiment ARFix: Cannot resolve ASI path.\n");
            return false;
        }
        const auto directory = std::filesystem::path(path).parent_path();
        try
        {
            auto logger = spdlog::basic_logger_mt(fixName, (directory / (std::string(fixName) + ".log")).string(), true);
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::info);
        }
        catch (const spdlog::spdlog_ex& error)
        {
            OutputDebugStringA(error.what());
            MessageBoxA(nullptr, error.what(), "Pentiment ARFix: Logging failed", MB_OK | MB_ICONERROR);
            return false;
        }
        SPDLOG_INFO("Pentiment ARFix v0.1.0: experimental fix for 1.3.1842.");
        if (!ReadConfig(directory))
            return false;
        SPDLOG_INFO("Enabled={}, resolution={}x{} (0/0 keeps game settings), Borderless={}.",
            enabled, targetWidth, targetHeight, borderless);
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
            SPDLOG_ERROR("GameAssembly.dll did not load within 60 seconds. No hooks installed.");
            return false;
        }
        struct HookSite
        {
            std::uint8_t* address;
            safetyhook::MidHookFn callback;
            const char* name;
        };
        auto* lens = FindUnique(assembly, "Lens",
            "F3 0F 10 4B 04 45 33 C0 48 8B CF E8 ?? ?? ?? ?? F3 0F 10 0B 45 33 C0 48 8B CF E8 ?? ?? ?? ?? F3 0F 10 43 1C", 0);
        const std::array sites{
            HookSite{ FindUnique(assembly, "Startup 16:9 clamp",
                "F3 0F 10 15 ?? ?? ?? ?? 45 33 C9 F3 0F 10 0D ?? ?? ?? ?? 0F 28 C7 E8 ?? ?? ?? ?? 0F 28 7C 24 50 84 C0 0F 85", 27),
                KeepStartingResolution, "startup 16:9 clamp" },
            HookSite{ FindUnique(assembly, "Resolution list filter",
                "F3 0F 5E C1 45 33 C9 F3 0F 10 15 ?? ?? ?? ?? F3 0F 10 4C 24 3C E8 ?? ?? ?? ?? 84 C0 0F 84", 26),
                AllowResolution, "resolution list filter" },
            HookSite{ lens ? lens + 5 : nullptr,
                [](SafetyHookContext& ctx) { AdjustLens(ctx, false); }, "orthographic lens" },
            HookSite{ lens ? lens + 20 : nullptr,
                [](SafetyHookContext& ctx) { AdjustLens(ctx, true); }, "perspective lens" }
        };
        for (const auto& site : sites)
        {
            if (!site.address)
            {
                SPDLOG_ERROR("{} code not identified. No hooks installed.", site.name);
                return false;
            }
        }
        using ResolveIcall = void* (*)(const char*);
        const auto resolveIcall = reinterpret_cast<ResolveIcall>(GetProcAddress(assembly, "il2cpp_resolve_icall"));
        if (!resolveIcall)
        {
            SPDLOG_ERROR("il2cpp_resolve_icall not exported. No hooks installed.");
            return false;
        }
        getAspect = reinterpret_cast<GetAspect>(resolveIcall("UnityEngine.Camera::get_aspect()"));
        getTargetTexture = reinterpret_cast<GetTargetTexture>(resolveIcall("UnityEngine.Camera::get_targetTexture()"));
        setResolution = reinterpret_cast<SetResolution>(resolveIcall(
            "UnityEngine.Screen::SetResolution(System.Int32,System.Int32,UnityEngine.FullScreenMode,System.Int32)"));
        if (!getAspect || !getTargetTexture || !setResolution)
        {
            SPDLOG_ERROR("Unity camera/screen functions not resolved. No hooks installed.");
            return false;
        }
        for (std::size_t index = 0; index < sites.size(); ++index)
        {
            const auto& site = sites[index];
            auto result = SafetyHookMid::create(site.address, site.callback, SafetyHookMid::StartDisabled);
            if (!result)
            {
                SPDLOG_ERROR("Cannot install {} hook: error {}. Removing installed hooks.",
                    site.name, static_cast<int>(result.error().type));
                for (auto& hook : hooks)
                    hook.reset();
                return false;
            }
            hooks[index] = std::move(*result);
        }
        for (std::size_t remaining = sites.size(); remaining > 0; --remaining)
        {
            const std::size_t index = remaining - 1;
            const auto result = hooks[index].enable();
            if (!result)
            {
                SPDLOG_ERROR("Cannot enable {} hook: error {}. Removing installed hooks.",
                    sites[index].name, static_cast<int>(result.error().type));
                for (auto& hook : hooks)
                    hook.reset();
                return false;
            }
            SPDLOG_INFO("{} hook installed.", sites[index].name);
        }
        return true;
    }

    DWORD WINAPI Main(void*)
    {
        if (!Initialize())
        {
            MessageBoxW(nullptr, L"Initialization failed. Check Pentiment_ARFix.log. The fix is not active.",
                L"Pentiment ARFix", MB_OK | MB_ICONERROR);
            return 1;
        }
        if (enabled && borderless && !MaintainBorderlessWindow())
        {
            MessageBoxW(nullptr, L"Borderless window setup failed. Check Pentiment_ARFix.log. Aspect-ratio hooks remain active.",
                L"Pentiment ARFix", MB_OK | MB_ICONERROR);
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
        const HANDLE thread = CreateThread(nullptr, 0, Main, nullptr, 0, nullptr);
        if (!thread)
        {
            OutputDebugStringA("Pentiment ARFix: Cannot create initialization thread.\n");
            return FALSE;
        }
        CloseHandle(thread);
    }
    return TRUE;
}
#endif
