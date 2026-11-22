#include "stdafx.h"
#include "version.h"
#include "helper.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <inipp/inipp.h>
#include <safetyhook.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>

#if !defined(_M_IX86)
#error DmC Devil May Cry is a 32-bit game. Compile this project as x86.
#endif

namespace
{
    HMODULE exeModule = GetModuleHandleW(nullptr);
    std::filesystem::path FixPath;

    struct Settings
    {
        int width = 1280;
        int height = 800;
        bool borderless = true;
        float fov = 1.0f;
    } settings;

    std::atomic<int> currentWidth{0}, currentHeight{0};
    std::atomic<bool> fullscreen{false};
    SafetyHookMid startHook, resizeHook, fovHook;

    void Log()
    {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(exeModule, path, MAX_PATH);
        FixPath = std::filesystem::path(path).remove_filename();
        try {
            auto logger = spdlog::basic_logger_mt("DmC", (FixPath / "DMC-DevilMayCry_ARFix.log").string(), true);
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%H:%M:%S.%e] %v");
            spdlog::flush_on(spdlog::level::info);
        } catch (...) {}
    }

    void Config()
    {
        std::ifstream file(FixPath / "DMC-DevilMayCry_ARFix.ini");
        if (!file) { spdlog::warn("INI not found, using defaults"); return; }
        inipp::Ini<char> ini;
        ini.parse(file);
        ini.strip_trailing_comments();
        inipp::get_value(ini.sections["Resolution"], "Width", settings.width);
        inipp::get_value(ini.sections["Resolution"], "Height", settings.height);
        inipp::get_value(ini.sections["Resolution"], "Borderless", settings.borderless);
        inipp::get_value(ini.sections["Camera"], "FOV", settings.fov);
        if (settings.width < 640 || settings.height < 480) settings.width = settings.height = 0;
        settings.fov = std::clamp(settings.fov, 0.5f, 2.0f);
        spdlog::info("Resolution {}x{} borderless {}, FOV {}", settings.width, settings.height, settings.borderless, settings.fov);
    }

    std::uint8_t* Find(const char* name, const char* pattern)
    {
        auto r = Memory::PatternScanAll(exeModule, pattern);
        if (r.size() != 1) { spdlog::error("{}: pattern matched {} times", name, r.size()); return nullptr; }
        spdlog::info("{}: {:X}", name, uintptr_t(r.front()));
        return r.front();
    }

    DWORD WINAPI BorderlessWindow(void*);

    void Install()
    {
        if (auto p = Find("startup", "8B 8A D8 04 00 00 8B 11 8B 92 40 01 00 00 55 8B AC 24 ?? ?? ?? ?? 55 56 57 50")) {
            startHook = safetyhook::create_mid(p + 14, [](SafetyHookContext& ctx) {
                fullscreen = ctx.ebp != 0;
                if (settings.width > 0) {
                    spdlog::info("Startup resolution {}x{} -> {}x{}", int(ctx.edi), int(ctx.esi), settings.width, settings.height);
                    ctx.edi = settings.width;
                    ctx.esi = settings.height;
                }
                currentWidth = int(ctx.edi);
                currentHeight = int(ctx.esi);
            });
        }

        if (auto p = Find("resize", "8B 44 24 34 8B 4C 24 38 8B 54 24 3C 89 46 50 8B 46 60 33 44 24 40 33 DB 83 E0 01")) {
            resizeHook = safetyhook::create_mid(p, [](SafetyHookContext& ctx) {
                auto args = reinterpret_cast<int*>(ctx.esp + 0x34);
                if (settings.width > 0 && (args[0] != settings.width || args[1] != settings.height)) {
                    spdlog::info("Resolution {}x{} -> {}x{}", args[0], args[1], settings.width, settings.height);
                    args[0] = settings.width;
                    args[1] = settings.height;
                }
                currentWidth = args[0];
                currentHeight = args[1];
                fullscreen = (args[3] & 1) != 0;
            });
        }

        if (settings.borderless)
            if (HANDLE t = CreateThread(nullptr, 0, BorderlessWindow, nullptr, 0, nullptr)) CloseHandle(t);

        if (settings.fov != 1.0f) {
            if (auto p = Find("fov", "D9 5C 24 0C D9 44 24 ?? D9 5C 24 08 D9 44 24 ?? D9 54 24 04 D9 1C 24 E8 ?? ?? ?? ?? B9 10 00 00 00 8B F0 8D BC 24")) {
                fovHook = safetyhook::create_mid(p + 23, [](SafetyHookContext& ctx) {
                    auto halfFov = reinterpret_cast<float*>(ctx.esp);
                    for (int i = 0; i < 2; ++i)
                        halfFov[i] = std::clamp(std::atan(std::tan(halfFov[i]) * settings.fov), 0.01f, 1.55f);
                });
            }
        }
    }

    BOOL CALLBACK FindGameWindow(HWND window, LPARAM result)
    {
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        RECT client{};
        if (process == GetCurrentProcessId() && IsWindowVisible(window) && !GetWindow(window, GW_OWNER) && !IsIconic(window)
            && GetClientRect(window, &client) && client.right >= 320 && client.bottom >= 200) {
            *reinterpret_cast<HWND*>(result) = window;
            return FALSE;
        }
        return TRUE;
    }

    DWORD WINAPI BorderlessWindow(void*)
    {
        for (;; Sleep(250)) {
            int w = currentWidth, h = currentHeight;
            if (fullscreen || w <= 0 || h <= 0) continue;
            HWND window = nullptr;
            EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&window));
            if (!window) continue;
            LONG style = GetWindowLongW(window, GWL_STYLE);
            if (style & WS_MAXIMIZE) continue;
            MONITORINFO monitor{ sizeof(monitor) };
            if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) continue;
            const RECT& m = monitor.rcMonitor;
            const float fit = (std::min)({ 1.0f, float(m.right - m.left) / w, float(m.bottom - m.top) / h });
            if (fit < 1.0f) {
                w = (std::max)(1, int(std::lround(w * fit)));
                h = (std::max)(1, int(std::lround(h * fit)));
            }
            int x = m.left + (m.right - m.left - w) / 2, y = m.top + (m.bottom - m.top - h) / 2;
            LONG desired = (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP;
            RECT current{};
            GetWindowRect(window, &current);
            if (style == desired && current.left == x && current.top == y && current.right - x == w && current.bottom - y == h) continue;
            if (style != desired) SetWindowLongW(window, GWL_STYLE, desired);
            SetWindowPos(window, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            spdlog::info("Borderless window {}x{}", w, h);
        }
    }

    void EnableDpiAwareness()
    {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        using SetContext = BOOL(WINAPI*)(HANDLE);
        auto setContext = user32 ? reinterpret_cast<SetContext>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")) : nullptr;
        if (setContext && setContext(reinterpret_cast<HANDLE>(-4))) {
            spdlog::info("DPI awareness: per monitor");
        } else if (SetProcessDPIAware()) {
            spdlog::info("DPI awareness: system");
        } else {
            spdlog::warn("DPI awareness not set");
        }
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        Log();
        spdlog::info("DmC Devil May Cry AR Fix {}", sFixVersion);
        Config();
        EnableDpiAwareness();
        Install();
    }
    return TRUE;
}
