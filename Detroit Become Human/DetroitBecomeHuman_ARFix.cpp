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

    std::uint8_t** renderGlobal = nullptr;
    std::uint32_t windowWidthOffset = 0, windowHeightOffset = 0, displayModeOffset = 0;
    std::atomic<bool> aspectDone{false}, fovDone{false}, hudDone{false}, modeDone{false};
    SafetyHookMid fovHook, hudHook, modeHook;

    void Log()
    {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(exeModule, path, MAX_PATH);
        FixPath = std::filesystem::path(path).remove_filename();
        try {
            auto logger = spdlog::basic_logger_mt("DBH", (FixPath / "DetroitBecomeHuman_ARFix.log").string(), true);
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%H:%M:%S.%e] %v");
            spdlog::flush_on(spdlog::level::info);
        } catch (...) {}
    }

    void Config()
    {
        std::ifstream file(FixPath / "DetroitBecomeHuman_ARFix.ini");
        if (!file) { spdlog::warn("INI not found, using defaults"); return; }
        inipp::Ini<char> ini;
        ini.parse(file);
        ini.strip_trailing_comments();
        inipp::get_value(ini.sections["Resolution"], "Width", settings.width);
        inipp::get_value(ini.sections["Resolution"], "Height", settings.height);
        inipp::get_value(ini.sections["Resolution"], "Borderless", settings.borderless);
        inipp::get_value(ini.sections["Camera"], "FOV", settings.fov);
        if (settings.width < 640 || settings.height < 360) settings.width = settings.height = 0;
        settings.fov = std::clamp(settings.fov, 0.5f, 2.0f);
        spdlog::info("Resolution {}x{} borderless {}, FOV {}", settings.width, settings.height, settings.borderless, settings.fov);
    }

    std::uint8_t* Find(const char* name, const char* pattern, bool quiet)
    {
        auto r = Memory::PatternScanAll(exeModule, pattern);
        if (r.size() != 1) {
            if (!quiet) spdlog::error("{}: pattern matched {} times", name, r.size());
            return nullptr;
        }
        spdlog::info("{}: {:X}", name, uintptr_t(r.front()) - uintptr_t(exeModule));
        return r.front();
    }

    std::uint8_t* Render()
    {
        return renderGlobal ? *renderGlobal : nullptr;
    }

    float WindowAspect()
    {
        auto r = Render();
        if (!r) return 0.0f;
        auto w = *reinterpret_cast<std::uint32_t*>(r + windowWidthOffset), h = *reinterpret_cast<std::uint32_t*>(r + windowHeightOffset);
        return h ? float(w) / float(h) : 0.0f;
    }

    void InstallAspect(bool quiet)
    {
        auto p = Find("aspect", "0F B6 51 45 85 D2 74 ?? 83 EA 01 74 ?? 83 FA 01 74 ?? F3 0F 10 05 ?? ?? ?? ?? C3 48 8B 0D ?? ?? ?? ?? 0F 57 C0 0F 57 C9 8B 81 ?? ?? ?? ?? F3 48 0F 2A C0 8B 81", quiet);
        if (!p) return;
        renderGlobal = reinterpret_cast<std::uint8_t**>(Memory::GetRelativeAddr(p + 0x1E));
        windowWidthOffset = *reinterpret_cast<std::uint32_t*>(p + 0x2A);
        windowHeightOffset = *reinterpret_cast<std::uint32_t*>(p + 0x35);
        Memory::PatchBytes(p, "\x31\xD2\xB2\x02\x85\xD2", 6);
        aspectDone = true;
    }

    void InstallFov(bool quiet)
    {
        auto p = Find("fov", "F3 44 0F 10 81 A4 01 00 00 4C 8B D1 ?? ?? ?? ?? ?? 41 0F 28 C0 F3 44 0F 10 89 9C 01 00 00 41 0F 28 C9", quiet);
        if (!p) return;
        fovHook = safetyhook::create_mid(p + 0x1E, [](SafetyHookContext& ctx) {
            float aspect = ctx.xmm8.f32[0], fov = ctx.xmm9.f32[0];
            if (!(fov > 0.0f && fov < 3.1f) || !(aspect > 0.0f)) return;
            float zoom = settings.fov;
            float screen = WindowAspect();
            if (aspect < 16.0f / 9.0f && std::fabs(aspect - screen) < 0.01f) zoom *= (16.0f / 9.0f) / aspect;
            if (zoom == 1.0f) return;
            ctx.xmm9.f32[0] = std::clamp(2.0f * std::atan(std::tan(fov * 0.5f) * zoom), 0.01f, 3.0f);
        });
        fovDone = true;
    }

    void InstallHud(bool quiet)
    {
        auto p = Find("hud", "F3 44 0F 10 35 ?? ?? ?? ?? 44 0F 28 C0 F3 45 0F 59 C6", quiet);
        if (!p) return;
        hudHook = safetyhook::create_mid(p + 9, [](SafetyHookContext& ctx) {
            float screen = WindowAspect();
            if (screen > 16.0f / 9.0f + 0.01f) ctx.xmm14.f32[0] *= screen / (16.0f / 9.0f);
        });
        hudDone = true;
    }

    void InstallMode(bool quiet)
    {
        auto p = Find("display mode", "83 FE 03 76 ?? 44 38 AF ?? ?? 00 00 74 ?? 44 88 AF ?? ?? 00 00 EB ?? 40 38 B7 ?? ?? 00 00 74 ?? 40 88 B7", quiet);
        if (!p) return;
        displayModeOffset = *reinterpret_cast<std::uint32_t*>(p + 8);
        if (settings.borderless) {
            modeHook = safetyhook::create_mid(p, [](SafetyHookContext& ctx) {
                std::uint32_t mode = settings.width > 0 ? 2 : 1;
                spdlog::info("Display mode {} -> {}", std::uint32_t(ctx.rsi), mode);
                ctx.rsi = mode;
            });
        }
        modeDone = true;
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

    void BorderlessWindow()
    {
        auto r = Render();
        if (!r || !displayModeOffset || r[displayModeOffset] != 2) return;
        HWND window = nullptr;
        EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&window));
        if (!window) return;
        LONG style = GetWindowLongW(window, GWL_STYLE);
        if (style & WS_MAXIMIZE) return;
        MONITORINFO monitor{ sizeof(monitor) };
        if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) return;
        const RECT& m = monitor.rcMonitor;
        int w = std::min<int>(settings.width, m.right - m.left), h = std::min<int>(settings.height, m.bottom - m.top);
        int x = m.left + (m.right - m.left - w) / 2, y = m.top + (m.bottom - m.top - h) / 2;
        LONG desired = (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP;
        RECT current{};
        GetWindowRect(window, &current);
        if (style == desired && current.left == x && current.top == y && current.right - x == w && current.bottom - y == h) return;
        if (style != desired) SetWindowLongW(window, GWL_STYLE, desired);
        SetWindowPos(window, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        spdlog::info("Borderless window {}x{}", w, h);
    }

    DWORD WINAPI Worker(void*)
    {
        for (int i = 0; i < 200 && !(aspectDone && fovDone && hudDone && modeDone); ++i) {
            Sleep(50);
            bool last = i == 199;
            if (!aspectDone) InstallAspect(!last);
            if (!fovDone) InstallFov(!last);
            if (!hudDone) InstallHud(!last);
            if (!modeDone) InstallMode(!last);
        }
        if (!settings.borderless || settings.width <= 0) return 0;
        for (;; Sleep(250)) BorderlessWindow();
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        Log();
        spdlog::info("Detroit: Become Human AR Fix {}", sFixVersion);
        Config();
        InstallAspect(true);
        InstallFov(true);
        InstallHud(true);
        InstallMode(true);
        if (HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr)) CloseHandle(t);
    }
    return TRUE;
}
