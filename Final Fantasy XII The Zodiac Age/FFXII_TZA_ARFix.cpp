#include "stdafx.h"
#include "version.h"

#include <inipp/inipp.h>
#include <safetyhook.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include "helper.hpp"

#include <cstdint>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <stdexcept>
#include <string>

namespace
{
    HMODULE module = nullptr;
    SafetyHookMid resolutionHook;
    SafetyHookMid projectionHook;
    SafetyHookMid frustumHook;
    SafetyHookMid viewportHook;
    std::atomic<float> renderAspect{ 0.0f };

    constexpr char resolutionPattern[] =
        "44 8B D2 0F 57 C9 41 8B C0 4C 8B C9 44 89 51 10 44 89 41 14 "
        "F3 49 0F 2A C2 F3 48 0F 2A C8 0F 28 D0 F3 0F 5E D1 0F 2F DA 72 ?? "
        "F3 0F 5E C3 C7 41 04 00 00 80 3F C7 41 08 00 00 80 3F "
        "33 C9 41 89 49 0C 41 89 09 F3 0F 2C C0 41 89 41 14 C3";
    constexpr char projectionPattern[] =
        "0F 28 E0 F3 0F 59 A3 D4 00 00 00 0F 28 CB F3 0F 5C CE "
        "F3 0F 59 EB F3 0F 58 F3 0F 28 C4 F3 0F 5E 83 C8 00 00 00 "
        "48 89 43 3C 48 89 43 44 F3 0F 5E D1 F3 0F 11 43 38 F3 0F 11 63 4C";
    constexpr char frustumPattern[] =
        "F3 0F 10 86 D0 00 00 00 F3 0F 5E C7 E8 ?? ?? ?? ?? "
        "F3 0F 10 96 C8 00 00 00 48 8D 8D D0 01 00 00 F3 0F 59 C7 "
        "0F 28 DE F3 44 0F 11 6C 24 20 0F 28 C8 E8 ?? ?? ?? ??";
    constexpr char viewportPattern[] =
        "0F 29 74 24 30 0F 57 F6 F3 48 0F 2A F7 E8 ?? ?? ?? ?? "
        "8B 4C 24 70 44 8B C6 C7 44 24 28 00 00 00 00 89 4C 24 20 "
        "0F 57 C9 F3 48 0F 2A C9 48 8B 4B 08 F3 0F 59 C1 "
        "F3 0F 5C F0 F3 0F 2C C6 2B F8 99 2B C2 44 8B CF D1 F8 8D 14 28";

    std::uint8_t* FindHookSite(void* image, const char* name, const char* pattern, std::size_t offset)
    {
        const auto matches = Memory::PatternScanAll(image, pattern);
        if (matches.size() != 1)
            throw std::runtime_error(std::string(name) + " signature matched "
                + std::to_string(matches.size()) + " locations; hook not installed");
        return matches.front() + offset;
    }

    struct Settings
    {
        bool enabled = true;
        bool borderless = true;
        int width = 1280;
        int height = 960;
        bool resolution = true;
        bool fov = true;
    } settings;

    std::filesystem::path ModulePath(HMODULE handle)
    {
        std::vector<wchar_t> buffer(32768);
        const DWORD length = GetModuleFileNameW(handle, buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (!length || length >= buffer.size())
            throw std::runtime_error("Cannot resolve module path");
        return std::filesystem::path(std::wstring(buffer.data(), length));
    }

    template<typename T>
    T ReadSetting(const inipp::Ini<char>& ini, const char* section, const char* key, T initial)
    {
        const auto found = ini.sections.find(section);
        if (found != ini.sections.end())
        {
            const auto value = found->second.find(key);
            if (value != found->second.end()
                && !inipp::get_value(found->second, key, initial))
                throw std::runtime_error(std::string("Invalid INI value: [")
                    + section + "] " + key);
        }
        SPDLOG_INFO("[{}] {} = {}", section, key, initial);
        return initial;
    }

    void ReadSettings(const std::filesystem::path& directory)
    {
        const auto path = directory / (sFixName + ".ini");
        std::ifstream input(path);
        if (!input)
            throw std::runtime_error("Cannot open configuration: " + path.string());

        inipp::Ini<char> ini;
        ini.parse(input);
        ini.strip_trailing_comments();
        if (input.bad() || !ini.errors.empty())
            throw std::runtime_error("Malformed configuration: " + path.string());

        settings.enabled = ReadSetting(ini, "Window", "Enabled", settings.enabled);
        settings.borderless = ReadSetting(ini, "Window", "Borderless", settings.borderless);
        settings.width = ReadSetting(ini, "Window", "Width", settings.width);
        settings.height = ReadSetting(ini, "Window", "Height", settings.height);
        settings.resolution = ReadSetting(ini, "Resolution", "Enabled", settings.resolution);
        settings.fov = ReadSetting(ini, "FOV", "Enabled", settings.fov);

        if ((settings.width == 0) != (settings.height == 0)
            || settings.width < 0 || settings.height < 0
            || settings.width > 16384 || settings.height > 16384)
            throw std::runtime_error("Window Width/Height must both be zero or 1..16384");
    }

    void PreserveRenderHeight(SafetyHookContext& ctx)
    {
        const auto width = static_cast<std::uint32_t>(ctx.r10);
        const auto height = static_cast<std::uint32_t>(ctx.r8);
        ctx.xmm0.f32[0] = static_cast<float>(height);
        if (height)
            renderAspect.store(static_cast<float>(width) / height, std::memory_order_relaxed);
        SPDLOG_INFO("Resolution clamp bypassed: requested {}x{}, render size {}x{}.",
            width, height, width, height);
    }

    void PreserveHorizontalFraming(SafetyHookContext& ctx)
    {
        if (!ctx.rbx)
        {
            SPDLOG_ERROR("Projection camera is null; leaving its projection unchanged.");
            return;
        }
        const float aspect = *reinterpret_cast<const float*>(ctx.rbx + 0xC8);
        const float scale = ctx.xmm4.f32[0];
        const float screenAspect = renderAspect.load(std::memory_order_relaxed);
        constexpr float nativeAspect = 16.0f / 9.0f;
        if (!std::isfinite(aspect) || aspect <= 0.0f
            || !std::isfinite(scale) || scale <= 0.0f)
        {
            static std::atomic<bool> logged{ false };
            if (!logged.exchange(true))
                SPDLOG_ERROR("Invalid perspective aspect/scale ({}, {}); projection unchanged.",
                    aspect, scale);
            return;
        }
        if (!std::isfinite(screenAspect) || screenAspect <= 0.0f
            || screenAspect >= nativeAspect
            || (std::abs(aspect - screenAspect) > 0.0001f
                && std::abs(aspect - nativeAspect) > 0.0001f))
            return;

        ctx.xmm4.f32[0] = scale * (screenAspect / nativeAspect);
        ctx.xmm0.f32[0] = scale / nativeAspect;
        thread_local float lastAspect = 0.0f;
        if (lastAspect != screenAspect)
        {
            SPDLOG_INFO("Perspective framing: aspect={}, horizontal scale={}, vertical scale={}; "
                "preserving 16:9 horizontal coverage with the real render aspect.",
                screenAspect, ctx.xmm0.f32[0], ctx.xmm4.f32[0]);
            lastAspect = screenAspect;
        }
    }

    void InstallResolutionHook()
    {
        if (!settings.resolution)
        {
            SPDLOG_INFO("Resolution clamp fix disabled.");
            return;
        }
        auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
        auto* site = FindHookSite(base, "Resolution clamp", resolutionPattern, 0x45);
        auto result = SafetyHookMid::create(site, PreserveRenderHeight,
            SafetyHookMid::StartDisabled);
        if (!result)
            throw std::runtime_error("Cannot create resolution hook: "
                + std::to_string(static_cast<int>(result.error().type)));
        resolutionHook = std::move(*result);
        if (const auto enabled = resolutionHook.enable(); !enabled)
        {
            resolutionHook.reset();
            throw std::runtime_error("Cannot enable resolution hook: "
                + std::to_string(static_cast<int>(enabled.error().type)));
        }
        SPDLOG_INFO("16:9 minimum render-height clamp hook installed at FFXII_TZA.exe+0x{:X}.",
            static_cast<std::uintptr_t>(site - base));
    }

    void PreserveSceneFrustum(SafetyHookContext& ctx)
    {
        if (!ctx.rsi)
        {
            SPDLOG_ERROR("Scene frustum camera is null; leaving culling unchanged.");
            return;
        }
        const float cameraAspect = *reinterpret_cast<const float*>(ctx.rsi + 0xC8);
        const float fov = ctx.xmm0.f32[0];
        const float screenAspect = renderAspect.load(std::memory_order_relaxed);
        constexpr float nativeAspect = 16.0f / 9.0f;
        if (!std::isfinite(screenAspect) || screenAspect <= 0.0f
            || screenAspect >= nativeAspect
            || (std::abs(cameraAspect - screenAspect) > 0.0001f
                && std::abs(cameraAspect - nativeAspect) > 0.0001f))
            return;
        if (!std::isfinite(cameraAspect) || cameraAspect <= 0.0f
            || !std::isfinite(fov) || fov <= 0.0f || fov >= std::numbers::pi_v<float>)
        {
            static std::atomic<bool> logged{ false };
            if (!logged.exchange(true))
                SPDLOG_ERROR("Invalid scene frustum aspect/FOV ({}, {}); culling unchanged.",
                    cameraAspect, fov);
            return;
        }
        ctx.xmm0.f32[0] = 2.0f * std::atan(std::tan(fov * 0.5f) * nativeAspect / screenAspect);
        ctx.xmm2.f32[0] = screenAspect;
        static std::atomic<unsigned> observations{ 0 };
        if (observations.fetch_add(1, std::memory_order_relaxed) < 8)
            SPDLOG_INFO("Scene frustum framing: aspect={} -> {}, vertical FOV={} -> {}.",
                cameraAspect, screenAspect, fov, ctx.xmm0.f32[0]);
    }

    void InstallFovHook(SafetyHookMid& hook, const char* name, const char* pattern,
        std::size_t offset, void (*callback)(SafetyHookContext&))
    {
        if (!settings.fov || !settings.resolution)
            return;
        auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
        auto* site = FindHookSite(base, name, pattern, offset);
        auto result = SafetyHookMid::create(site, callback,
            SafetyHookMid::StartDisabled);
        if (!result)
            throw std::runtime_error(std::string("Cannot create ") + name + " hook: "
                + std::to_string(static_cast<int>(result.error().type)));
        hook = std::move(*result);
        if (const auto enabled = hook.enable(); !enabled)
        {
            hook.reset();
            throw std::runtime_error(std::string("Cannot enable ") + name + " hook: "
                + std::to_string(static_cast<int>(enabled.error().type)));
        }
        SPDLOG_INFO("{} hook installed at FFXII_TZA.exe+0x{:X}.",
            name, static_cast<std::uintptr_t>(site - base));
    }

    void PreserveViewportBounds(SafetyHookContext& ctx)
    {
        const float width = ctx.xmm6.f32[0];
        const float height = ctx.xmm1.f32[0];
        const float aspect = ctx.xmm0.f32[0];
        if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(aspect)
            || width <= 0.0f || height <= 0.0f || aspect <= 0.0f)
        {
            static std::atomic<bool> logged{ false };
            if (!logged.exchange(true))
                SPDLOG_ERROR("Invalid viewport frame/aspect ({}, {}, {}); viewport unchanged.",
                    width, height, aspect);
            return;
        }
        constexpr float nativeAspect = 16.0f / 9.0f;
        if (width / height < nativeAspect && std::abs(aspect - nativeAspect) < 0.0001f)
            ctx.xmm0.f32[0] = width / height;
        thread_local std::uintptr_t lastTarget = 0;
        thread_local float lastWidth = -1.0f;
        thread_local float lastHeight = -1.0f;
        static std::atomic<unsigned> observations{ 0 };
        if (ctx.rcx == lastTarget && width == lastWidth && height == lastHeight)
            return;
        lastTarget = ctx.rcx;
        lastWidth = width;
        lastHeight = height;
        if (observations.fetch_add(1, std::memory_order_relaxed) < 32)
            SPDLOG_INFO("Viewport bounds: target=0x{:X}, frame={}x{}, "
                "layout aspect={} -> {}, viewport width={}, horizontal excess={}.",
                ctx.rcx, width, height, aspect, ctx.xmm0.f32[0],
                height * ctx.xmm0.f32[0], height * ctx.xmm0.f32[0] - width);
    }

    void InstallViewportHook()
    {
        if (!settings.resolution)
            return;
        auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
        auto* site = FindHookSite(base, "Viewport", viewportPattern, 0x31);
        auto viewport = SafetyHookMid::create(site, PreserveViewportBounds,
            SafetyHookMid::StartDisabled);
        if (!viewport)
            throw std::runtime_error("Cannot create viewport hook: "
                + std::to_string(static_cast<int>(viewport.error().type)));
        viewportHook = std::move(*viewport);
        if (const auto enabled = viewportHook.enable(); !enabled)
        {
            viewportHook.reset();
            throw std::runtime_error("Cannot enable viewport hook: "
                + std::to_string(static_cast<int>(enabled.error().type)));
        }
        SPDLOG_INFO("Narrow-screen viewport hook installed at FFXII_TZA.exe+0x{:X}.",
            static_cast<std::uintptr_t>(site - base));
    }

    struct WindowCandidate
    {
        HWND handle = nullptr;
        std::int64_t area = 0;
    };

    BOOL CALLBACK FindGameWindow(HWND window, LPARAM parameter)
    {
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        if (process != GetCurrentProcessId() || !IsWindowVisible(window)
            || GetWindow(window, GW_OWNER) || IsIconic(window))
            return TRUE;

        wchar_t className[64]{};
        if (!GetClassNameW(window, className, static_cast<int>(std::size(className)))
            || std::wcscmp(className, L"PhyreFrameworkClass") != 0)
            return TRUE;

        RECT client{};
        if (!GetClientRect(window, &client))
            return TRUE;

        auto& candidate = *reinterpret_cast<WindowCandidate*>(parameter);
        const auto area = static_cast<std::int64_t>(client.right) * client.bottom;
        if (client.right >= 320 && client.bottom >= 180 && area > candidate.area)
        {
            candidate.handle = window;
            candidate.area = area;
        }
        return TRUE;
    }

    HWND FindGameWindow()
    {
        WindowCandidate candidate{};
        if (!EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&candidate)))
            throw std::runtime_error("Cannot enumerate game windows: "
                + std::to_string(GetLastError()));
        return candidate.handle;
    }

    void ResizeWindow(HWND window)
    {
        MONITORINFO monitor{ sizeof(MONITORINFO) };
        if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
            throw std::runtime_error("GetMonitorInfo failed: "
                + std::to_string(GetLastError()));

        RECT client{};
        if (!GetClientRect(window, &client))
            throw std::runtime_error("GetClientRect failed: "
                + std::to_string(GetLastError()));
        const int width = settings.width ? settings.width : client.right;
        const int height = settings.height ? settings.height : client.bottom;
        if (width <= 0 || height <= 0)
            throw std::runtime_error("Game window has invalid client dimensions");

        SetLastError(ERROR_SUCCESS);
        const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
        if (!style && GetLastError() != ERROR_SUCCESS)
            throw std::runtime_error("Cannot read game window style: "
                + std::to_string(GetLastError()));

        const LONG_PTR desiredStyle = settings.borderless
            ? (style & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW | WS_MAXIMIZE))
                | static_cast<LONG_PTR>(WS_POPUP)
            : style;
        RECT frame{ 0, 0, width, height };
        if (!settings.borderless
            && !AdjustWindowRectEx(&frame, static_cast<DWORD>(desiredStyle),
                GetMenu(window) != nullptr,
                static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE))))
            throw std::runtime_error("AdjustWindowRectEx failed: "
                + std::to_string(GetLastError()));

        const int outerWidth = frame.right - frame.left;
        const int outerHeight = frame.bottom - frame.top;
        const RECT bounds = settings.borderless ? monitor.rcMonitor : monitor.rcWork;
        const int x = bounds.left + (bounds.right - bounds.left - outerWidth) / 2;
        const int y = bounds.top + (bounds.bottom - bounds.top - outerHeight) / 2;

        RECT current{};
        if (!GetWindowRect(window, &current))
            throw std::runtime_error("GetWindowRect failed: "
                + std::to_string(GetLastError()));
        if (style == desiredStyle && current.left == x && current.top == y
            && current.right - current.left == outerWidth
            && current.bottom - current.top == outerHeight)
            return;

        if (style != desiredStyle)
        {
            SetLastError(ERROR_SUCCESS);
            if (!SetWindowLongPtrW(window, GWL_STYLE, desiredStyle)
                && GetLastError() != ERROR_SUCCESS)
                throw std::runtime_error("Cannot change game window style: "
                    + std::to_string(GetLastError()));
        }
        if (!SetWindowPos(window, nullptr, x, y, outerWidth, outerHeight,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED))
            throw std::runtime_error("SetWindowPos failed: "
                + std::to_string(GetLastError()));

        RECT resizedClient{};
        if (!GetClientRect(window, &resizedClient))
            throw std::runtime_error("Cannot verify game client dimensions: "
                + std::to_string(GetLastError()));
        if (resizedClient.right != width || resizedClient.bottom != height)
            throw std::runtime_error("Game window rejected requested client dimensions");

        SPDLOG_INFO("Window client size set to {}x{}; borderless={}.",
            width, height, settings.borderless);
    }

    DWORD WINAPI Main(void*)
    {
        try
        {
            const auto directory = ModulePath(module).parent_path();
            auto logger = spdlog::basic_logger_mt(sFixName,
                (directory / (sFixName + ".log")).string(), true);
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::info);
            SPDLOG_INFO("FFXII_TZA_ARFix v{}.", sFixVersion);

            ReadSettings(directory);
            if (settings.fov && !settings.resolution)
                SPDLOG_ERROR("FOV fix requires [Resolution] Enabled=true.");
            const auto install = [](const char* name, auto&& action)
            {
                try
                {
                    action();
                }
                catch (const std::exception& error)
                {
                    SPDLOG_ERROR("{}: {}", name, error.what());
                }
            };
            install("Resolution", [] { InstallResolutionHook(); });
            install("Perspective", [] {
                InstallFovHook(projectionHook, "Perspective", projectionPattern, 0x31,
                    PreserveHorizontalFraming);
            });
            install("Scene frustum", [] {
                InstallFovHook(frustumHook, "Scene frustum", frustumPattern, 0x2E,
                    PreserveSceneFrustum);
            });
            install("Viewport", [] { InstallViewportHook(); });
            if (!settings.enabled)
            {
                SPDLOG_INFO("Window override disabled.");
                return 0;
            }

            SPDLOG_INFO("Waiting for the Phyre game window; requested size={}x{}, borderless={}.",
                settings.width, settings.height, settings.borderless);
            bool foundWindow = false;
            bool failureLogged = false;
            unsigned attempts = 0;
            for (;;)
            {
                HWND window = FindGameWindow();
                if (window)
                {
                    foundWindow = true;
                    attempts = 0;
                    try
                    {
                        ResizeWindow(window);
                        failureLogged = false;
                    }
                    catch (const std::exception& error)
                    {
                        if (!failureLogged)
                            SPDLOG_ERROR("Window override failed: {}. Will retry.", error.what());
                        failureLogged = true;
                    }
                }
                else if (!foundWindow && ++attempts >= 240)
                {
                    SPDLOG_ERROR("Phyre game window did not appear within 60 seconds.");
                    return 1;
                }
                Sleep(250);
            }
        }
        catch (const std::exception& error)
        {
            if (const auto logger = spdlog::get(sFixName))
                SPDLOG_LOGGER_ERROR(logger, "Initialization failed: {}", error.what());
            OutputDebugStringA(error.what());
            OutputDebugStringA("\n");
            return 1;
        }
    }
}

BOOL APIENTRY DllMain(HMODULE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        module = instance;
        DisableThreadLibraryCalls(instance);
        const HANDLE thread = CreateThread(nullptr, 0, Main, nullptr, 0, nullptr);
        if (!thread)
        {
            OutputDebugStringA("FFXII_TZA_ARFix: Cannot create initialization thread.\n");
            return FALSE;
        }
        CloseHandle(thread);
    }
    return TRUE;
}
