#include "stdafx.h"
#include "helper.hpp"
#include <windows.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <inipp/inipp.h>
#include <safetyhook.hpp>
#include <d3d9.h>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <mutex>

#if !defined(_M_IX86)
#error Dynasty Warriors 7 XL is a 32-bit game. You MUST compile this project as x86 (32-bit).
#endif

namespace fs = std::filesystem;

namespace
{
    HMODULE thisModule = nullptr;
    fs::path FixPath;
    constexpr char sConfigFile[] = "DW7XL_ARFix.ini";
    constexpr char sLogFile[] = "DW7XL_ARFix.log";

    int iTargetWidth = 1280;
    int iTargetHeight = 800;
    float fCustomAR = 1.6f;
    bool bStretch = true;

    safetyhook::MidHook aspectHook;

    bool InitializeLogging()
    {
        WCHAR dllPath[MAX_PATH] = {};
        if (GetModuleFileNameW(thisModule, dllPath, MAX_PATH) == 0) return false;
        FixPath = fs::path(dllPath).parent_path();

        std::ofstream clearLog(FixPath / sLogFile, std::ios::trunc);
        if (clearLog.is_open()) clearLog.close();

        try {
            auto logger = std::make_shared<spdlog::logger>(
                "DW7XL_ARFix",
                std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    (FixPath / sLogFile).string(), 5 * 1024 * 1024, 1));
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::info);
            SPDLOG_INFO("Dynasty Warriors 7 XL AR Fix initialized.");
            return true;
        } catch (...) {
            return false;
        }
    }

    bool LoadConfiguration()
    {
        const fs::path path = FixPath / sConfigFile;
        if (!fs::exists(path)) {
            std::ofstream defaultIni(path);
            defaultIni << "[Resolution]\nWidth = 1280\nHeight = 800\n\n[Stretch]\nEnabled = true\n";
            defaultIni.close();
            SPDLOG_INFO("Created default config.");
        }

        std::ifstream iniFile(path);
        if (!iniFile) return false;

        inipp::Ini<char> ini;
        ini.parse(iniFile);
        ini.strip_trailing_comments();

        inipp::get_value(ini.sections["Resolution"], "Width", iTargetWidth);
        inipp::get_value(ini.sections["Resolution"], "Height", iTargetHeight);
        inipp::get_value(ini.sections["Stretch"], "Enabled", bStretch);

        if (iTargetHeight > 0) {
            fCustomAR = static_cast<float>(iTargetWidth) / static_cast<float>(iTargetHeight);
        }

        SPDLOG_INFO("Config loaded -> Resolution: {}x{}, Target Aspect Ratio: {:.5f}, Stretch: {}",
                    iTargetWidth, iTargetHeight, fCustomAR, bStretch);
        return true;
    }

    namespace Stretch
    {
        std::mutex mtx;
        bool failed = false;
        IDirect3DSurface9* temp = nullptr;
        UINT tempW = 0, tempH = 0;
        D3DFORMAT tempFormat = D3DFMT_UNKNOWN;
        UINT seenW = 0, seenH = 0;
        SafetyHookInline hkCreateDevice, hkPresent, hkReset, hkCreateSwapChain, hkSwapChainPresent;
        bool borderless = false;
        HWND borderlessWindow = nullptr;

        void* VFunc(void* obj, size_t index) { return (*reinterpret_cast<void***>(obj))[index]; }

        bool RunningOnMacOS()
        {
            using HostVersion = void (CDECL*)(const char**, const char**);
            const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
            const auto getHostVersion = ntdll
                ? reinterpret_cast<HostVersion>(GetProcAddress(ntdll, "wine_get_host_version"))
                : nullptr;
            if (!getHostVersion) return false;
            const char* sysname = nullptr;
            const char* release = nullptr;
            getHostVersion(&sysname, &release);
            return sysname && std::strcmp(sysname, "Darwin") == 0;
        }

        void MakeWindowed(D3DPRESENT_PARAMETERS* pp, HWND focus)
        {
            if (!borderless || !pp || pp->Windowed) return;
            pp->Windowed = TRUE;
            pp->FullScreen_RefreshRateInHz = 0;
            borderlessWindow = pp->hDeviceWindow ? pp->hDeviceWindow : focus;
            SPDLOG_INFO("Fullscreen {}x{} switched to a borderless window covering the screen.", pp->BackBufferWidth, pp->BackBufferHeight);
        }

        void CoverScreen()
        {
            if (!borderlessWindow) return;
            MONITORINFO mi{ sizeof(mi) };
            if (!GetMonitorInfoW(MonitorFromWindow(borderlessWindow, MONITOR_DEFAULTTONEAREST), &mi)) return;
            RECT wr{};
            GetWindowRect(borderlessWindow, &wr);
            constexpr LONG edgeStyles = WS_OVERLAPPEDWINDOW | WS_BORDER | WS_DLGFRAME;
            constexpr LONG edgeExStyles = WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_DLGMODALFRAME;
            LONG style = GetWindowLongW(borderlessWindow, GWL_STYLE);
            LONG exStyle = GetWindowLongW(borderlessWindow, GWL_EXSTYLE);
            if (EqualRect(&wr, &mi.rcMonitor) && !(style & edgeStyles) && !(exStyle & edgeExStyles)) return;
            SetWindowLongW(borderlessWindow, GWL_STYLE, (style & ~edgeStyles) | WS_POPUP | WS_VISIBLE);
            SetWindowLongW(borderlessWindow, GWL_EXSTYLE, exStyle & ~edgeExStyles);
            SetWindowPos(borderlessWindow, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }

        bool ContentRect(UINT w, UINT h, RECT& r)
        {
            if (!w || !h) return false;
            double ratio = static_cast<double>(w) / h, native = 16.0 / 9.0;
            if (std::abs(ratio - native) / native < 0.005) return false;
            LONG cw = w, ch = h;
            if (ratio < native) ch = static_cast<LONG>(std::lround(w * 9.0 / 16.0));
            else cw = static_cast<LONG>(std::lround(h * 16.0 / 9.0));
            r.left = (static_cast<LONG>(w) - cw) / 2;
            r.top = (static_cast<LONG>(h) - ch) / 2;
            r.right = r.left + cw;
            r.bottom = r.top + ch;
            return cw > 0 && ch > 0;
        }

        void ReleaseTemp()
        {
            if (temp) { temp->Release(); temp = nullptr; }
            tempW = tempH = 0;
        }

        void Apply(IDirect3DDevice9* device, IDirect3DSurface9* bb)
        {
            D3DSURFACE_DESC desc{};
            bb->GetDesc(&desc);
            RECT src{};
            bool stretch = desc.MultiSampleType == D3DMULTISAMPLE_NONE && ContentRect(desc.Width, desc.Height, src);
            if (desc.Width != seenW || desc.Height != seenH) {
                seenW = desc.Width;
                seenH = desc.Height;
                SPDLOG_INFO("Back buffer {}x{}: {}.", desc.Width, desc.Height,
                            stretch ? "stretching the 16:9 picture to fill it" : "no stretch needed");
            }
            if (!stretch) return;
            UINT cw = src.right - src.left, ch = src.bottom - src.top;
            if (!temp || tempW != cw || tempH != ch || tempFormat != desc.Format) {
                ReleaseTemp();
                HRESULT hr = device->CreateRenderTarget(cw, ch, desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &temp, nullptr);
                if (FAILED(hr)) {
                    failed = true;
                    SPDLOG_ERROR("Stretch disabled: CreateRenderTarget failed (0x{:08X}).", static_cast<unsigned>(hr));
                    return;
                }
                tempW = cw; tempH = ch; tempFormat = desc.Format;
            }
            HRESULT hr = device->StretchRect(bb, &src, temp, nullptr, D3DTEXF_NONE);
            if (SUCCEEDED(hr)) hr = device->StretchRect(temp, nullptr, bb, nullptr, D3DTEXF_LINEAR);
            if (FAILED(hr)) {
                failed = true;
                SPDLOG_ERROR("Stretch disabled: StretchRect failed (0x{:08X}).", static_cast<unsigned>(hr));
            }
        }

        HRESULT STDMETHODCALLTYPE Present(IDirect3DDevice9* self, const RECT* s, const RECT* d, HWND w, const RGNDATA* r)
        {
            CoverScreen();
            if (!failed) {
                std::lock_guard lk(mtx);
                IDirect3DSurface9* bb = nullptr;
                if (SUCCEEDED(self->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb) {
                    Apply(self, bb);
                    bb->Release();
                }
            }
            return hkPresent.stdcall<HRESULT>(self, s, d, w, r);
        }

        HRESULT STDMETHODCALLTYPE SwapChainPresent(IDirect3DSwapChain9* self, const RECT* s, const RECT* d, HWND w,
                                                   const RGNDATA* r, DWORD flags)
        {
            if (!failed) {
                std::lock_guard lk(mtx);
                IDirect3DDevice9* device = nullptr;
                if (SUCCEEDED(self->GetDevice(&device)) && device) {
                    IDirect3DSurface9* bb = nullptr;
                    if (SUCCEEDED(self->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb) {
                        Apply(device, bb);
                        bb->Release();
                    }
                    device->Release();
                }
            }
            return hkSwapChainPresent.stdcall<HRESULT>(self, s, d, w, r, flags);
        }

        HRESULT STDMETHODCALLTYPE Reset(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* pp)
        {
            {
                std::lock_guard lk(mtx);
                ReleaseTemp();
            }
            MakeWindowed(pp, nullptr);
            return hkReset.stdcall<HRESULT>(self, pp);
        }

        HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* pp,
                                                            IDirect3DSwapChain9** out)
        {
            HRESULT hr = hkCreateSwapChain.stdcall<HRESULT>(self, pp, out);
            if (SUCCEEDED(hr) && out && *out && !hkSwapChainPresent) {
                hkSwapChainPresent = safetyhook::create_inline(VFunc(*out, 3), &SwapChainPresent);
            }
            return hr;
        }

        HRESULT STDMETHODCALLTYPE CreateDevice(IDirect3D9* self, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,
                                               D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out)
        {
            MakeWindowed(pp, focus);
            HRESULT hr = hkCreateDevice.stdcall<HRESULT>(self, adapter, type, focus, flags, pp, out);
            if (SUCCEEDED(hr) && out && *out && !hkPresent) {
                hkPresent = safetyhook::create_inline(VFunc(*out, 17), &Present);
                hkReset = safetyhook::create_inline(VFunc(*out, 16), &Reset);
                hkCreateSwapChain = safetyhook::create_inline(VFunc(*out, 13), &CreateAdditionalSwapChain);
                SPDLOG_INFO("Stretch hooks: {}.", hkPresent && hkReset ? "installed" : "failed");
            }
            return hr;
        }

        void Install()
        {
            if (!bStretch) return;
            borderless = RunningOnMacOS();
            if (borderless) SPDLOG_INFO("macOS host: fullscreen will use a borderless window.");
            HMODULE d3d9 = LoadLibraryW(L"d3d9.dll");
            auto create = d3d9 ? reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(d3d9, "Direct3DCreate9")) : nullptr;
            IDirect3D9* d3d = create ? create(D3D_SDK_VERSION) : nullptr;
            if (!d3d) {
                SPDLOG_ERROR("Stretch disabled: Direct3DCreate9 failed.");
                return;
            }
            hkCreateDevice = safetyhook::create_inline(VFunc(d3d, 16), &CreateDevice);
            d3d->Release();
            if (!hkCreateDevice) SPDLOG_ERROR("Stretch disabled: CreateDevice hook failed.");
        }
    }

    bool InstallFix()
    {
        std::uint8_t* aspect = Memory::PatternScan(GetModuleHandleW(nullptr),
            "D9 98 34 01 00 00 88 88 55 01 00 00 D9 44 24 1C D9 98 38 01 00 00");
        if (!aspect) return false;

        aspectHook = safetyhook::create_mid(aspect + 0x16, [](SafetyHookContext& ctx) {
            *reinterpret_cast<float*>(ctx.esp + 0x20) = fCustomAR;
        });
        SPDLOG_INFO("Aspect ratio hook: {}.", aspectHook ? "installed" : "failed");
        return true;
    }

    bool bFixInstalled = false;

    DWORD WINAPI SetupThread(void*)
    {
        Stretch::Install();
        for (int i = 0; i < 10000 && !bFixInstalled; ++i) {
            bFixInstalled = InstallFix();
            if (!bFixInstalled) Sleep(1);
        }
        if (!bFixInstalled) SPDLOG_ERROR("Aspect ratio code not found.");
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        thisModule = hModule;
        if (!InitializeLogging() || !LoadConfiguration()) return TRUE;
        bFixInstalled = InstallFix();
        if (HANDLE thread = CreateThread(nullptr, 0, SetupThread, nullptr, 0, nullptr)) {
            CloseHandle(thread);
        }
    }
    return TRUE;
}
