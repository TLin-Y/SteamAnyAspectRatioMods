#include "stdafx.h"
#include "version.h"
#include "helper.hpp"

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <inipp/inipp.h>
#include <safetyhook.hpp>
#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace
{
    HMODULE module = nullptr;
    HMODULE executable = nullptr;
    constexpr float nativeAspect = 16.0f / 9.0f;
    std::atomic<std::uint64_t> renderSize{0};
    std::vector<SafetyHookMid> hooks;
    std::uint8_t* dialogueWidth = nullptr;

    struct Settings
    {
        bool windowEnabled = true;
        bool borderless = true;
        int width = 1280;
        int height = 800;
        bool resolution = true;
        bool fov = true;
        bool hud = true;
    } settings;

    struct Layout
    {
        int width;
        int height;
        float aspect;
        float multiplier;
        float scale;
        float hudWidth;
        float hudHeight;
        float offsetX;
        float offsetY;
        float virtualOffsetX;
        float virtualOffsetY;
    };

    Layout CurrentLayout()
    {
        const auto size = renderSize.load(std::memory_order_relaxed);
        const int width = size ? static_cast<int>(size >> 32) : 1920;
        const int height = size ? static_cast<int>(size & 0xffffffff) : 1080;
        const float aspect = static_cast<float>(width) / height;
        const float scale = std::min(width / 1920.0f, height / 1080.0f);
        return {
            width, height, aspect, aspect / nativeAspect, scale,
            1920.0f * scale, 1080.0f * scale,
            (width - 1920.0f * scale) / 2.0f,
            0.0f, // Top-align the HUD on screens narrower than 16:9.
            std::max(0.0f, (1080.0f * aspect - 1920.0f) / 2.0f),
            std::max(0.0f, (1920.0f / aspect - 1080.0f) / 2.0f),
        };
    }

    bool ValidDimensions(float width, float height)
    {
        return std::isfinite(width) && std::isfinite(height)
            && width >= 1 && height >= 1 && width <= 65535 && height <= 65535;
    }

    void ObserveResolution(float width, float height)
    {
        static std::atomic<bool> invalidLogged{false};
        if (!ValidDimensions(width, height)) {
            if (!invalidLogged.exchange(true, std::memory_order_relaxed))
                SPDLOG_WARN("Ignoring invalid render dimensions: {}x{}", width, height);
            return;
        }
        invalidLogged.store(false, std::memory_order_relaxed);
        const auto w = static_cast<std::uint32_t>(width);
        const auto h = static_cast<std::uint32_t>(height);
        const auto size = (static_cast<std::uint64_t>(w) << 32) | h;
        if (renderSize.exchange(size, std::memory_order_relaxed) != size)
            SPDLOG_INFO("Observed game render size: {}x{} (aspect {})", w, h,
                static_cast<float>(w) / h);
    }

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
        if (found != ini.sections.end()) {
            const auto value = found->second.find(key);
            if (value != found->second.end()
                && !inipp::get_value(found->second, key, initial))
                throw std::runtime_error(std::string("Invalid INI value: [")
                    + section + "] " + key);
        }
        SPDLOG_INFO("[{}] {} = {}", section, key, initial);
        return initial;
    }

    void LoadSettings(const std::filesystem::path& folder)
    {
        const auto path = folder / "Absolum_ARFix.ini";
        std::ifstream input(path);
        if (!input)
            throw std::runtime_error("Cannot open INI: " + path.string());
        inipp::Ini<char> ini;
        ini.parse(input);
        ini.strip_trailing_comments();
        if (!ini.errors.empty())
            throw std::runtime_error("Malformed INI: " + path.string());
        settings.windowEnabled = ReadSetting(ini, "Window", "Enabled", true);
        settings.borderless = ReadSetting(ini, "Window", "Borderless", true);
        settings.width = ReadSetting(ini, "Window", "Width", 1280);
        settings.height = ReadSetting(ini, "Window", "Height", 800);
        settings.resolution = ReadSetting(ini, "Fix Resolution", "Enabled", true);
        settings.fov = ReadSetting(ini, "Fix FOV", "Enabled", true);
        settings.hud = ReadSetting(ini, "Fix HUD", "Enabled", true);
        if ((settings.width == 0) != (settings.height == 0)
            || settings.width < 0 || settings.height < 0
            || settings.width > 16384 || settings.height > 16384)
            throw std::runtime_error("Window Width/Height must both be zero or 1..16384");
    }

    std::uint8_t* Find(const char* name, const char* signature)
    {
        const auto matches = Memory::PatternScanAll(executable, signature);
        if (matches.size() != 1) {
            SPDLOG_ERROR("{}: expected one signature match, found {}; feature skipped",
                name, matches.size());
            return nullptr;
        }
        SPDLOG_INFO("{}: Absolum.exe+{:x}", name,
            matches[0] - reinterpret_cast<std::uint8_t*>(executable));
        return matches[0];
    }

    bool Install(const char* name, std::uint8_t* address, safetyhook::MidHookFn callback)
    {
        if (!address)
            return false;
        auto hook = SafetyHookMid::create(address, callback);
        if (!hook) {
            SPDLOG_ERROR("{}: hook creation failed (error {})", name,
                static_cast<int>(hook.error().type));
            return false;
        }
        hooks.push_back(std::move(*hook));
        return true;
    }

    void PatchInstruction(std::uint8_t* address, const std::array<std::uint8_t, 8>& bytes)
    {
        DWORD oldProtect = 0;
        if (!VirtualProtect(address, bytes.size(), PAGE_EXECUTE_READWRITE, &oldProtect))
            throw std::runtime_error("Cannot make aspect instruction writable");
        std::copy(bytes.begin(), bytes.end(), address);
        const BOOL flushed = FlushInstructionCache(GetCurrentProcess(), address, bytes.size());
        DWORD ignored = 0;
        if (!VirtualProtect(address, bytes.size(), oldProtect, &ignored))
            throw std::runtime_error("Cannot restore aspect instruction protection");
        if (!flushed)
            throw std::runtime_error("Cannot flush aspect instruction cache");
    }

    bool InstallAspect(const char* name, std::uint8_t* address, safetyhook::MidHookFn callback)
    {
        std::array<std::uint8_t, 8> original{};
        std::copy_n(address, original.size(), original.begin());
        std::array<std::uint8_t, 8> nops{};
        nops.fill(0x90);
        PatchInstruction(address, nops);
        if (Install(name, address, callback))
            return true;
        PatchInstruction(address, original);
        return false;
    }

    void ApplyAspect(SafetyHookContext& ctx)
    {
        const float width = ctx.xmm2.f32[0];
        const float height = ctx.xmm0.f32[0];
        ctx.xmm2.f32[0] = settings.resolution && ValidDimensions(width, height)
            ? width / height : nativeAspect;
    }

    bool InstallRenderHooks()
    {
        auto* resolution = Find("Render size",
            "48 8B 54 24 ?? F3 0F 10 15 ?? ?? ?? ?? E8 ?? ?? ?? ?? 0F 10 84 24 ?? ?? ?? ?? 0F 11 84 24 ?? ?? ?? ?? F3 0F 2C 84 24");
        auto* scaling = settings.resolution ? Find("Render scaling",
            "48 8B 54 24 ?? F3 0F 10 15 ?? ?? ?? ?? E8 ?? ?? ?? ?? 0F 10 84 24 ?? ?? ?? ?? 0F 11 84 24 ?? ?? ?? ?? F3 0F 2C 9C 24") : nullptr;
        if (!resolution || (settings.resolution && !scaling))
            return false;
        if (!InstallAspect("Render size/aspect", resolution + 5, [](SafetyHookContext& ctx) {
            ObserveResolution(ctx.xmm2.f32[0], ctx.xmm0.f32[0]);
            ApplyAspect(ctx);
        }))
            return false;
        if (settings.resolution && !InstallAspect("Render scaling", scaling + 5, ApplyAspect)) {
            SPDLOG_ERROR("Scaling hook failed; resolution is only partially fixed");
            return false;
        }
        if (settings.resolution) {
            Install("Aspect restriction", Find("Aspect restriction",
                "80 ?? ?? ?? ?? ?? 00 75 ?? 48 8B ?? ?? ?? 48 83 ?? ?? ?? C3"),
                [](SafetyHookContext& ctx) {
                    if (ctx.rsi)
                        *reinterpret_cast<BYTE*>(ctx.rsi + 0x239) = 0;
                });
        }
        if (settings.fov) {
            Install("Narrow-screen FOV", Find("Narrow-screen FOV",
                "F3 0F ?? ?? ?? ?? 48 8D ?? ?? ?? ?? ?? 4C 8B ?? ?? 0F 28 ?? E8 ?? ?? ?? ??"),
                [](SafetyHookContext& ctx) {
                    const auto layout = CurrentLayout();
                    const float angle = ctx.xmm6.f32[0];
                    if (layout.aspect < nativeAspect && std::isfinite(angle)
                        && angle > 0 && angle < std::numbers::pi_v<float>)
                        ctx.xmm6.f32[0] = 2.0f * std::atan(
                            std::tan(angle / 2.0f) / layout.multiplier);
                });
        }
        return true;
    }

    void InstallHudHooks()
    {
        if (!settings.hud)
            return;
        Install("HUD menus", Find("HUD menus",
            "0F 57 ?? F3 0F ?? ?? ?? F3 0F ?? ?? F3 0F ?? ?? ?? F3 0F ?? ?? ?? F3 0F ?? ?? ?? 48 8B ?? C3"),
            [](SafetyHookContext& ctx) {
                const auto l = CurrentLayout();
                if (ctx.rdx && *reinterpret_cast<int*>(ctx.rdx + 8) == l.width
                    && *reinterpret_cast<int*>(ctx.rdx + 12) == l.height) {
                    if (l.aspect > nativeAspect) {
                        ctx.xmm0.f32[0] = l.offsetX;
                        ctx.xmm2.f32[0] = l.hudWidth;
                    } else {
                        ctx.xmm1.f32[0] = l.offsetY;
                        ctx.xmm3.f32[0] = l.hudHeight;
                    }
                }
            });
        auto* player = Find("Player HUD",
            "49 8B CF 0F 10 85 ?? ?? ?? ?? 0F 11 85 ?? ?? ?? ?? 0F 10 85 ?? ?? ?? ?? 0F 11 85 ?? ?? ?? ?? 48 8B 95 ?? ?? ?? ?? 48 89 95 ?? ?? ?? ?? 48 8B 95 ?? ?? ?? ?? 48 89 95 ?? ?? ?? ?? 48 8D 95 ?? ?? ?? ?? 39 09 E8");
        Install("Player HUD", player ? player + 0xA : nullptr,
            [](SafetyHookContext& ctx) {
                const auto l = CurrentLayout();
                if (l.aspect > nativeAspect)
                    ctx.xmm0.f32[0] = l.offsetX;
                else
                    ctx.xmm0.f32[1] = l.offsetY;
                ctx.xmm0.f32[2] = ctx.xmm0.f32[3] = l.scale;
            });
        Install("Combo meter", Find("Combo meter",
            "33 ?? 0F 57 ?? 0F 11 ?? ?? ?? 0F 11 ?? ?? ?? ?? ?? ?? 48 89 ?? ?? ?? ?? ?? ?? 48 89 ?? ?? ?? ?? ?? ?? 48 8D ?? ?? ?? F3 0F ?? ?? ?? 70"),
            [](SafetyHookContext& ctx) {
                const auto l = CurrentLayout();
                ctx.xmm2.f32[0] = l.scale;
                if (l.aspect > nativeAspect)
                    ctx.xmm0.f32[0] = l.offsetX;
                else
                    ctx.xmm1.f32[0] = l.offsetY;
            });
        Install("Interaction markers", Find("Interaction markers",
            "F3 0F ?? ?? 48 8B ?? ?? 48 83 ?? ?? F3 0F ?? ?? 0F 28 ?? F3 0F ?? ?? ?? F3 0F ?? ??"),
            [](SafetyHookContext& ctx) {
                ctx.xmm0.f32[0] -= CurrentLayout().virtualOffsetY;
            });

        auto* floatingPosition = Find("Floating element position",
            "F3 0F ?? ?? 48 8D ?? ?? 74 F3 0F ?? ?? F3 0F ?? ?? ?? ?? ?? ?? F3 0F ?? ?? F3 0F ?? ?? ?? ??");
        auto* floatingFov = Find("Floating element FOV",
            "F2 0F ?? ?? F3 0F ?? ?? ?? ?? ?? ?? F3 0F ?? ?? 48 8D ?? ?? ?? ?? ?? ?? 0F 28 ?? F3 0F ?? ?? F3 0F ?? ?? 48 8D ?? ?? ?? ?? ?? ?? F3 0F ?? ?? F3 0F ?? ??");
        if (floatingPosition && floatingFov) {
            Install("Floating elements horizontal", floatingPosition,
                [](SafetyHookContext& ctx) {
                    const auto l = CurrentLayout();
                    if (l.aspect > nativeAspect)
                        ctx.xmm0.f32[0] *= l.multiplier;
                });
            Install("Floating elements vertical", floatingPosition + 0x15,
                [](SafetyHookContext& ctx) {
                    const auto l = CurrentLayout();
                    if (l.aspect < nativeAspect)
                        ctx.xmm0.f32[0] /= l.multiplier;
                });
            Install("Floating elements FOV", floatingFov + 0x10,
                [](SafetyHookContext& ctx) {
                    const auto l = CurrentLayout();
                    if (l.aspect < nativeAspect)
                        ctx.xmm6.f32[0] /= l.multiplier;
                });
            Install("Floating dialogue", Find("Floating dialogue",
                "48 8B ?? ?? ?? ?? ?? 48 89 ?? ?? ?? ?? ?? 48 8B ?? FF ?? ?? 48 89 ?? ?? ?? ?? ?? F3 0F ?? ?? ?? ?? ?? ??"),
                [](SafetyHookContext& ctx) {
                    const auto l = CurrentLayout();
                    if (l.aspect > nativeAspect && ctx.rbx >= 4)
                        *reinterpret_cast<float*>(ctx.rbx - 4) -= l.virtualOffsetX;
                    else if (l.aspect < nativeAspect)
                        ctx.xmm6.f32[0] -= l.virtualOffsetY;
                });
        }

        auto* dialogue = Find("Dialogue",
            "F3 0F ?? 05 ?? ?? ?? ?? 48 8B ?? ?? ?? ?? ?? 48 89 ?? ?? ?? ?? ?? 48 8D ?? ?? ?? ?? ?? 0F 28 ?? F3 0F ?? ?? F3 0F ?? ?? 48 8D ?? ?? ?? ?? ?? 0F 28 ??");
        if (dialogue) {
            dialogueWidth = Memory::GetRelativeAddr(dialogue + 4);
            Install("Dialogue width", dialogue, [](SafetyHookContext&) {
                const float width = std::max(1920.0f, 1080.0f * CurrentLayout().aspect);
                if (*reinterpret_cast<float*>(dialogueWidth) != width)
                    Memory::Write(dialogueWidth, width);
            });
            Install("Dialogue offset", dialogue + 0x4A, [](SafetyHookContext& ctx) {
                const auto l = CurrentLayout();
                if (l.aspect > nativeAspect)
                    ctx.xmm1.f32[0] += l.offsetX;
                else
                    ctx.xmm2.f32[0] += l.offsetY;
            });
        }
        auto* components = Find("HUD components",
            "F3 0F ?? ?? 48 8D 8C ?? ?? ?? ?? ?? F3 0F 58 ?? F3 0F ?? ?? F3 0F ?? ?? ?? 88 ?? ?? ??");
        if (components) {
            Install("Critical life horizontal", components, [](SafetyHookContext& ctx) {
                const auto l = CurrentLayout();
                if (ctx.rcx && l.aspect > nativeAspect
                    && std::fabs(*reinterpret_cast<float*>(ctx.rcx) + 675.4f) < 1e-4f
                    && std::fabs(*reinterpret_cast<float*>(ctx.rcx + 4) + 134.3f) < 1e-4f)
                    ctx.xmm2.f32[0] -= l.virtualOffsetX;
            });
            Install("HUD components vertical", components + 0x10, [](SafetyHookContext& ctx) {
                const auto l = CurrentLayout();
                if (ctx.rcx < 4 || l.aspect >= nativeAspect)
                    return;
                const float x = *reinterpret_cast<float*>(ctx.rcx - 4);
                const float y = *reinterpret_cast<float*>(ctx.rcx);
                if (std::fabs(x + 160.0f) < 1e-4f && std::fabs(y - 880.0f) < 1e-4f)
                    ctx.xmm3.f32[0] += l.virtualOffsetY;
                if (std::fabs(x + 675.4f) < 1e-4f && std::fabs(y + 134.3f) < 1e-4f)
                    ctx.xmm3.f32[0] -= l.virtualOffsetY;
            });
        }
    }

    struct WindowCandidate
    {
        HWND handle = nullptr;
        std::int64_t area = 0;
    };

    BOOL CALLBACK FindGameWindow(HWND window, LPARAM data)
    {
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        if (process != GetCurrentProcessId() || !IsWindowVisible(window)
            || GetWindow(window, GW_OWNER) || IsIconic(window))
            return TRUE;
        RECT client{};
        if (!GetClientRect(window, &client))
            return TRUE;
        const auto area = static_cast<std::int64_t>(client.right) * client.bottom;
        auto& candidate = *reinterpret_cast<WindowCandidate*>(data);
        if (client.right >= 320 && client.bottom >= 180 && area > candidate.area) {
            candidate.handle = window;
            candidate.area = area;
        }
        return TRUE;
    }

    void ResizeWindow(HWND window)
    {
        MONITORINFO monitor{sizeof(MONITORINFO)};
        if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
            throw std::runtime_error("GetMonitorInfo failed: " + std::to_string(GetLastError()));
        const RECT bounds = settings.borderless ? monitor.rcMonitor : monitor.rcWork;
        const int width = settings.width ? settings.width : bounds.right - bounds.left;
        const int height = settings.height ? settings.height : bounds.bottom - bounds.top;
        SetLastError(0);
        const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
        if (!style && GetLastError())
            throw std::runtime_error("Cannot read window style");
        const LONG_PTR desired = settings.borderless
            ? (style & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW | WS_MAXIMIZE)) | WS_POPUP
            : (style & ~static_cast<LONG_PTR>(WS_POPUP)) | WS_OVERLAPPEDWINDOW;
        RECT frame{0, 0, width, height};
        if (!settings.borderless
            && !AdjustWindowRectEx(&frame, static_cast<DWORD>(desired),
                GetMenu(window) != nullptr,
                static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE))))
            throw std::runtime_error("AdjustWindowRectEx failed");
        const int outerWidth = frame.right - frame.left;
        const int outerHeight = frame.bottom - frame.top;
        const int x = bounds.left + (bounds.right - bounds.left - outerWidth) / 2;
        const int y = bounds.top + (bounds.bottom - bounds.top - outerHeight) / 2;
        RECT current{};
        if (!GetWindowRect(window, &current))
            throw std::runtime_error("GetWindowRect failed");
        if (style == desired && current.left == x && current.top == y
            && current.right - current.left == outerWidth
            && current.bottom - current.top == outerHeight)
            return;
        if (style != desired) {
            SetLastError(0);
            if (!SetWindowLongPtrW(window, GWL_STYLE, desired) && GetLastError())
                throw std::runtime_error("Cannot change window style");
        }
        if (!SetWindowPos(window, nullptr, x, y, outerWidth, outerHeight,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED))
            throw std::runtime_error("SetWindowPos failed: " + std::to_string(GetLastError()));
        RECT client{};
        if (!GetClientRect(window, &client))
            throw std::runtime_error("Cannot verify resized client area");
        if (client.right != width || client.bottom != height)
            throw std::runtime_error("Game window did not accept requested client dimensions");
        SPDLOG_INFO("Window client size verified: {}x{}; borderless={}",
            client.right, client.bottom, settings.borderless);
    }

    DWORD WINAPI Main(void*)
    {
        try {
            executable = GetModuleHandleW(nullptr);
            const auto folder = ModulePath(module).parent_path();
            const auto exePath = ModulePath(executable);
            auto logger = std::make_shared<spdlog::logger>("Absolum_ARFix",
                std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                    (folder / "Absolum_ARFix.log").string(), true));
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::info);
            SPDLOG_INFO("Absolum_ARFix v{}; target {}", sFixVersion, exePath.string());
            if (_wcsicmp(exePath.filename().c_str(), L"Absolum.exe") != 0)
                throw std::runtime_error("This implementation supports Absolum.exe only");
            // Hooks and the window worker must not outlive this module.
            HMODULE pinned = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&Main), &pinned))
                throw std::runtime_error("Cannot pin fix module");
            LoadSettings(folder);
            hooks.reserve(20);
            if (InstallRenderHooks())
                InstallHudHooks();
            else
                SPDLOG_ERROR("Render-size hooks unavailable; skipping dependent aspect/FOV/HUD fixes");
            SPDLOG_INFO("Installed {} hooks", hooks.size());
            if (!settings.windowEnabled)
                return 0;
            std::string lastError;
            bool waitingLogged = false;
            for (;;) {
                WindowCandidate candidate;
                if (!EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&candidate)))
                    throw std::runtime_error("EnumWindows failed");
                if (candidate.handle) {
                    try {
                        ResizeWindow(candidate.handle);
                        lastError.clear();
                    } catch (const std::runtime_error& error) {
                        if (lastError != error.what()) {
                            SPDLOG_ERROR("Window resize: {}", error.what());
                            lastError = error.what();
                        }
                    }
                } else if (!waitingLogged) {
                    SPDLOG_INFO("Waiting for a visible Absolum game window");
                    waitingLogged = true;
                }
                Sleep(500);
            }
        } catch (const std::exception& error) {
            OutputDebugStringA(error.what());
            SPDLOG_ERROR("Absolum fix initialization failed: {}", error.what());
        }
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE handle, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        module = handle;
        DisableThreadLibraryCalls(handle);
        const HANDLE worker = CreateThread(nullptr, 0, Main, nullptr, 0, nullptr);
        if (!worker) {
            OutputDebugStringA("Absolum fix: cannot create initialization thread");
            return FALSE;
        }
        CloseHandle(worker);
    }
    return TRUE;
}
