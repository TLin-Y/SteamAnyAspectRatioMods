#include "stdafx.h"
#include "version.h"
#include "helper.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <inipp/inipp.h>
#include <safetyhook.hpp>
#include <algorithm>
#include <cmath>
#include <dxgi.h>
#include <atomic>
#include <mutex>
#include <set>
#include <string>

#if !defined(_M_IX86)
#error Devil May Cry 4 Special Edition is a 32-bit game. Compile this project as x86.
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
        bool enabled = true;
        bool eventBars = false;
        float fov = 1.0f;
    } settings;

    float aspect = 16.0f / 9.0f;
    uintptr_t rendererPtr = 0;
    std::uint32_t sizeOffset = 0;
    std::atomic<ULONGLONG> lastMovieTick{0};
    SafetyHookInline readerHook;
    using NextSampleFn = HRESULT(__stdcall*)(void*, WORD, void**, std::uint64_t*, std::uint64_t*, DWORD*, DWORD*, WORD*);
    NextSampleFn origNextSample = nullptr;

    std::mutex readersMutex;
    std::set<void*> widgetReaders;

    HRESULT __stdcall NextSample(void* reader, WORD stream, void** sample, std::uint64_t* time, std::uint64_t* duration, DWORD* flags, DWORD* output, WORD* streamOut)
    {
        bool widget;
        {
            std::lock_guard lock(readersMutex);
            widget = widgetReaders.count(reader) != 0;
        }
        if (!widget) lastMovieTick = GetTickCount64();
        return origNextSample(reader, stream, sample, time, duration, flags, output, streamOut);
    }

    HRESULT __stdcall CreateSyncReader(void* cert, DWORD rights, void** reader)
    {
        HRESULT hr = readerHook.stdcall<HRESULT>(cert, rights, reader);
        if (SUCCEEDED(hr) && reader && *reader && !origNextSample) {
            auto vt = *reinterpret_cast<void***>(*reader);
            DWORD old;
            if (VirtualProtect(&vt[7], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
                origNextSample = reinterpret_cast<NextSampleFn>(vt[7]);
                vt[7] = reinterpret_cast<void*>(&NextSample);
                VirtualProtect(&vt[7], sizeof(void*), old, &old);
            }
        }
        return hr;
    }
    SafetyHookMid movieOpenHook, clientSizeHook, createSizeHook, applySizeHook, compositeHook, modeListHook, initResolutionHook, resolutionHook, viewportHook, cameraHook, fovHook, guiHook;

    using GetModesFn = HRESULT(__stdcall*)(IDXGIOutput*, DXGI_FORMAT, UINT, UINT*, DXGI_MODE_DESC*);
    GetModesFn origGetModes = nullptr;

    HRESULT __stdcall GetModes(IDXGIOutput* output, DXGI_FORMAT format, UINT flags, UINT* count, DXGI_MODE_DESC* desc)
    {
        UINT n = 0;
        HRESULT hr = origGetModes(output, format, flags, &n, nullptr);
        if (FAILED(hr) || !count) return hr;
        std::vector<DXGI_MODE_DESC> modes(n);
        if (n && FAILED(hr = origGetModes(output, format, flags, &n, modes.data()))) return hr;
        modes.resize(n);
        bool listed = std::any_of(modes.begin(), modes.end(), [](const DXGI_MODE_DESC& m) { return int(m.Width) == settings.width && int(m.Height) == settings.height; });
        if (!listed && !modes.empty()) {
            DXGI_MODE_DESC m = modes.back();
            m.Width = settings.width;
            m.Height = settings.height;
            modes.push_back(m);
        }
        if (!desc) { *count = UINT(modes.size()); return S_OK; }
        UINT copied = std::min<UINT>(*count, UINT(modes.size()));
        std::copy_n(modes.begin(), copied, desc);
        *count = copied;
        return copied < modes.size() ? DXGI_ERROR_MORE_DATA : S_OK;
    }

    void Log()
    {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(exeModule, path, MAX_PATH);
        FixPath = std::filesystem::path(path).remove_filename();
        try {
            auto logger = spdlog::basic_logger_mt("DMC4SE", (FixPath / "DevilMayCry4SpecialEdition_ARFix.log").string(), true);
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%H:%M:%S.%e] %v");
            spdlog::flush_on(spdlog::level::info);
        } catch (...) {}
    }

    void Config()
    {
        std::ifstream file(FixPath / "DevilMayCry4SpecialEdition_ARFix.ini");
        if (!file) { spdlog::warn("INI not found, using defaults"); return; }
        inipp::Ini<char> ini;
        ini.parse(file);
        ini.strip_trailing_comments();
        inipp::get_value(ini.sections["Resolution"], "Width", settings.width);
        inipp::get_value(ini.sections["Resolution"], "Height", settings.height);
        inipp::get_value(ini.sections["Resolution"], "Borderless", settings.borderless);
        inipp::get_value(ini.sections["AspectRatio"], "Enabled", settings.enabled);
        inipp::get_value(ini.sections["AspectRatio"], "EventBars", settings.eventBars);
        inipp::get_value(ini.sections["Camera"], "FOV", settings.fov);
        if (settings.width < 640 || settings.height < 480) settings.width = settings.height = 0;
        settings.fov = std::clamp(settings.fov, 0.5f, 2.0f);
        spdlog::info("Resolution {}x{} borderless {}, AspectRatio {}, EventBars {}, FOV {}", settings.width, settings.height, settings.borderless, settings.enabled, settings.eventBars, settings.fov);
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
        if (auto r = Memory::PatternScanAll(exeModule, "8B 15 ?? ?? ?? ?? 56 8B F1 B1 20 66 0F 6E 82 C0 00 00 00"); r.size() == 1)
            rendererPtr = *reinterpret_cast<uintptr_t*>(r.front() + 2);
        if (settings.width > 0) {
            if (auto p = Find("mode list", "52 6A 1C 50 8B 00 FF 50 20 50 E8")) {
                modeListHook = safetyhook::create_mid(p + 6, [](SafetyHookContext& ctx) {
                    if (origGetModes || !ctx.eax) return;
                    auto vt = reinterpret_cast<void**>(ctx.eax);
                    DWORD old;
                    if (!VirtualProtect(&vt[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return;
                    origGetModes = reinterpret_cast<GetModesFn>(vt[8]);
                    vt[8] = reinterpret_cast<void*>(&GetModes);
                    VirtualProtect(&vt[8], sizeof(void*), old, &old);
                });
            }
            if (auto p = Find("initial resolution", "8D 9E ?? ?? ?? ?? 8B 08 89 0B 8B 40 04 89 43 04 8B 44 24 10 3B C8 7F 08")) {
                initResolutionHook = safetyhook::create_mid(p + 0x25, [](SafetyHookContext& ctx) {
                    auto size = reinterpret_cast<int*>(ctx.ebx);
                    spdlog::info("Startup resolution {}x{} -> {}x{}", size[0], size[1], settings.width, settings.height);
                    size[0] = settings.width;
                    size[1] = settings.height;
                });
            }
            if (auto p = Find("client size", "E8 ?? ?? ?? ?? 8B 08 89 8E C0 00 00 00 8B 40 04 89 86 C4 00 00 00")) {
                clientSizeHook = safetyhook::create_mid(p + 5, [](SafetyHookContext& ctx) {
                    auto size = reinterpret_cast<int*>(ctx.eax);
                    size[0] = settings.width;
                    size[1] = settings.height;
                });
            }
            if (auto p = Find("create size", "C7 44 24 14 00 00 00 00 8B 03 0F 57 C0 8B 0D")) {
                createSizeHook = safetyhook::create_mid(p + 8, [](SafetyHookContext& ctx) {
                    auto size = reinterpret_cast<int*>(ctx.ebx);
                    size[0] = settings.width;
                    size[1] = settings.height;
                });
            }
            if (auto p = Find("apply size", "56 8B F1 8B 86 EC 00 00 00 8B 96 ?? ?? ?? ?? 89 96 C0 00 00 00")) {
                applySizeHook = safetyhook::create_mid(p + 9, [](SafetyHookContext& ctx) {
                    auto size = reinterpret_cast<int*>(ctx.esi + sizeOffset);
                    if (sizeOffset && (size[0] != settings.width || size[1] != settings.height)) {
                        spdlog::info("Applied size {}x{} -> {}x{}", size[0], size[1], settings.width, settings.height);
                        size[0] = settings.width;
                        size[1] = settings.height;
                    }
                });
            }
            if (auto p = Find("resolution", "8B 44 24 08 56 8B F1 8B 4C 24 08 39 8E ?? ?? ?? ?? 75 08 39 86")) {
                sizeOffset = *reinterpret_cast<std::uint32_t*>(p + 13);
                resolutionHook = safetyhook::create_mid(p, [](SafetyHookContext& ctx) {
                    auto args = reinterpret_cast<int*>(ctx.esp + 4);
                    if (args[0] == settings.width && args[1] == settings.height) return;
                    spdlog::info("Game resolution {}x{} -> {}x{}", args[0], args[1], settings.width, settings.height);
                    args[0] = settings.width;
                    args[1] = settings.height;
                    *reinterpret_cast<int*>(ctx.ecx + sizeOffset) = 0;
                });
            }
        }

        if (settings.borderless)
            if (HANDLE t = CreateThread(nullptr, 0, BorderlessWindow, nullptr, 0, nullptr)) CloseHandle(t);

        if (!settings.enabled) return;

        if (auto p = Find("modes", "0F 2F 05 ?? ?? ?? ?? 0F 87 ?? ?? ?? ?? F6 C3 07"))
            Memory::PatchBytes(p + 7, "\x90\x90\x90\x90\x90\x90", 6);

        if (auto p = Find("viewport", "89 8B B4 00 00 00 F3 0F 59 0D ?? ?? ?? ?? F3 0F 5C C1 F3 0F 2C C0")) {
            viewportHook = safetyhook::create_mid(p + 14, [](SafetyHookContext& ctx) {
                auto w = *reinterpret_cast<int*>(ctx.ebx + 0xC0);
                auto h = *reinterpret_cast<int*>(ctx.ebx + 0xC4);
                if (w > 0 && h > 0) aspect = float(w) / float(h);
                ctx.xmm1.f32[0] = ctx.xmm0.f32[0];
                spdlog::info("Screen {}x{}", w, h);
            });
        }

        if (auto p = Find("camera", "85 DB 74 07 C7 43 38 39 8E E3 3F")) {
            cameraHook = safetyhook::create_mid(p + 11, [](SafetyHookContext& ctx) {
                if (ctx.ebx) *reinterpret_cast<float*>(ctx.ebx + 0x38) = aspect;
            });
        }

        if (auto p = Find("projection", "8D 84 24 20 01 00 00 8B CB 50 8B 03 FF 50 50 F3 0F 10 00")) {
            fovHook = safetyhook::create_mid(p + 15, [](SafetyHookContext& ctx) {
                if (!ctx.ebx || !ctx.eax || std::fabs(*reinterpret_cast<float*>(ctx.ebx + 0x38) - aspect) > 1e-4f) return;
                float zoom = settings.fov;
                if (aspect < 16.0f / 9.0f) zoom *= (16.0f / 9.0f) / aspect;
                if (zoom == 1.0f) return;
                auto m = reinterpret_cast<float*>(ctx.eax);
                m[0] /= zoom;
                m[5] /= zoom;
            });
        }

        if (auto p = Find("gui", "8B 15 ?? ?? ?? ?? 56 8B F1 B1 20 66 0F 6E 82 C0 00 00 00")) {
            rendererPtr = *reinterpret_cast<uintptr_t*>(p + 2);
            guiHook = safetyhook::create_mid(p + 0x8C, [](SafetyHookContext& ctx) {
                auto r = *reinterpret_cast<uintptr_t*>(rendererPtr);
                if (!r) return;
                float w = float(*reinterpret_cast<int*>(r + 0xC0)), h = float(*reinterpret_cast<int*>(r + 0xC4));
                float fw = std::fmin(w, h * 16.0f / 9.0f), s = fw / 1920.0f;
                auto f = reinterpret_cast<float*>(ctx.esi);
                f[16] = (w - fw) / 2.0f;
                f[17] = (h - fw * 9.0f / 16.0f) / 2.0f;
                f[24] = f[25] = f[26] = s;
            });
        }

        if (auto p = Find("movie open", "8D B7 F8 01 00 00 56 6A 01 6A 00 FF 15 ?? ?? ?? ?? 85 C0 0F 88")) {
            movieOpenHook = safetyhook::create_mid(p + 17, [](SafetyHookContext& ctx) {
                if (static_cast<LONG>(ctx.eax) < 0) return;
                auto reader = *reinterpret_cast<void**>(ctx.edi + 0x1F8);
                auto path = reinterpret_cast<const char*>(*reinterpret_cast<uintptr_t*>(ctx.edi + 4) + 8);
                std::string name(path, strnlen(path, 64));
                bool widget = name.find("\\id\\") != std::string::npos;
                {
                    std::lock_guard lock(readersMutex);
                    if (widget) widgetReaders.insert(reader); else widgetReaders.erase(reader);
                }
                spdlog::info("Movie {} ({})", name, widget ? "in-menu" : "full screen");
            });
        }

        if (auto f = GetProcAddress(LoadLibraryW(L"wmvcore.dll"), "WMCreateSyncReader"))
            readerHook = safetyhook::create_inline(f, &CreateSyncReader);
        else
            spdlog::error("WMCreateSyncReader not found");

        if (auto p = Find("composite", "6A 0F 89 86 ?? ?? ?? ?? E8 ?? ?? ?? ?? 66 0F 6E 87")) {
            compositeHook = safetyhook::create_mid(p + 13, [](SafetyHookContext& ctx) {
                auto rect = reinterpret_cast<int*>(ctx.edi + 0xAC);
                int w = rect[5], h = rect[6];
                int top = 0, bottom = h;
                auto now = GetTickCount64();
                if (now - lastMovieTick < 250 && w * 9 < h * 16) {
                    top = (h - w * 9 / 16) / 2;
                    bottom = top + w * 9 / 16;
                }
                rect[1] = top;
                rect[3] = bottom;
            });
        }

        if (!settings.eventBars) {
            if (auto p = Find("cinemascope", "80 7F 50 00 0F 84 ?? ?? ?? ?? A1 ?? ?? ?? ?? 83 B8 50 01 00 00 14"))
                Memory::PatchBytes(p + 4, "\x90\xE9", 2);
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
            auto r = rendererPtr ? *reinterpret_cast<uintptr_t*>(rendererPtr) : 0;
            if (!r) continue;
            int w = *reinterpret_cast<int*>(r + 0xC0), h = *reinterpret_cast<int*>(r + 0xC4);
            if (settings.width > 0) { w = settings.width; h = settings.height; }
            HWND window = nullptr;
            EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&window));
            if (!window || w <= 0 || h <= 0) continue;
            LONG style = GetWindowLongW(window, GWL_STYLE);
            if (style & WS_MAXIMIZE) continue;
            MONITORINFO monitor{ sizeof(monitor) };
            if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) continue;
            const RECT& m = monitor.rcMonitor;
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

    DWORD WINAPI Retry(void*)
    {
        for (int i = 0; i < 300; ++i, Sleep(100)) {
            if (Memory::PatternScanAll(exeModule, "85 DB 74 07 C7 43 38 39 8E E3 3F").empty()) continue;
            spdlog::info("Game code ready after {} ms", i * 100);
            Install();
            return 0;
        }
        spdlog::error("Game code not found");
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        Log();
        spdlog::info("Devil May Cry 4 Special Edition AR Fix {}", sFixVersion);
        Config();
        if (Memory::PatternScanAll(exeModule, "85 DB 74 07 C7 43 38 39 8E E3 3F").empty()) {
            if (HANDLE t = CreateThread(nullptr, 0, Retry, nullptr, 0, nullptr)) CloseHandle(t);
        } else {
            Install();
        }
    }
    return TRUE;
}
