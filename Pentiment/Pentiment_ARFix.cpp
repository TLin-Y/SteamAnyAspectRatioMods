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
        auto* base = reinterpret_cast<std::uint8_t*>(assembly);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            nt->FileHeader.TimeDateStamp != 0x65E1470A || nt->OptionalHeader.SizeOfImage != 0x3637000)
        {
            SPDLOG_ERROR("Unsupported GameAssembly build (timestamp 0x{:X}, image size 0x{:X}). No hooks installed.",
                nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
            return false;
        }

        struct HookSite
        {
            std::uintptr_t rva;
            std::vector<std::uint8_t> signature;
            safetyhook::MidHookFn callback;
            const char* name;
        };
        const std::array sites{
            HookSite{ 0x474602, { 0x0F, 0x28, 0x7C, 0x24, 0x50, 0x84, 0xC0, 0x0F, 0x85, 0x9B, 0x01, 0x00, 0x00 },
                KeepStartingResolution, "startup 16:9 clamp" },
            HookSite{ 0x3EE5AB, { 0x84, 0xC0, 0x0F, 0x84, 0x94, 0x02, 0x00, 0x00 },
                AllowResolution, "resolution list filter" },
            HookSite{ 0x66C133, { 0x45, 0x33, 0xC0, 0x48, 0x8B, 0xCF, 0xE8, 0x22, 0xB5, 0xBA, 0x01 },
                [](SafetyHookContext& ctx) { AdjustLens(ctx, false); }, "orthographic lens" },
            HookSite{ 0x66C142, { 0x45, 0x33, 0xC0, 0x48, 0x8B, 0xCF, 0xE8, 0x23, 0xB1, 0xBA, 0x01 },
                [](SafetyHookContext& ctx) { AdjustLens(ctx, true); }, "perspective lens" }
        };
        for (const auto& site : sites)
        {
            if (std::memcmp(base + site.rva, site.signature.data(), site.signature.size()) != 0)
            {
                SPDLOG_ERROR("{} signature does not match. No hooks installed.", site.name);
                return false;
            }
        }
        const std::array apiSites{
            HookSite{ 0x2215A80, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x05, 0xD3, 0xF7, 0xE9, 0x00 },
                nullptr, "Camera.get_aspect" },
            HookSite{ 0x2216A90, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x05, 0x03, 0xE9, 0xE9, 0x00 },
                nullptr, "Camera.get_targetTexture" },
            HookSite{ 0x2238F70, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20 },
                nullptr, "Screen.SetResolution" }
        };
        for (const auto& site : apiSites)
        {
            if (std::memcmp(base + site.rva, site.signature.data(), site.signature.size()) != 0)
            {
                SPDLOG_ERROR("{} signature does not match. No hooks installed.", site.name);
                return false;
            }
        }
        getAspect = reinterpret_cast<GetAspect>(base + 0x2215A80);
        getTargetTexture = reinterpret_cast<GetTargetTexture>(base + 0x2216A90);
        setResolution = reinterpret_cast<SetResolution>(base + 0x2238F70);
        for (std::size_t index = 0; index < sites.size(); ++index)
        {
            const auto& site = sites[index];
            auto result = SafetyHookMid::create(base + site.rva, site.callback, SafetyHookMid::StartDisabled);
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
            SPDLOG_INFO("{} hook installed at GameAssembly+0x{:X}.", sites[index].name, sites[index].rva);
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
