#include "stdafx.h"
#include <windows.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <inipp/inipp.h>
#include <safetyhook.hpp>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <d3d9.h>
#include <cmath>
#include <cstring>
#include <mutex>

#if !defined(_M_IX86)
#error Dynasty Warriors 8 XL is a 32-bit game. Compile this project as x86.
#endif

namespace fs = std::filesystem;

namespace
{
    HMODULE exeModule = GetModuleHandleW(nullptr);
    HMODULE thisModule = nullptr;
    fs::path FixPath;
    constexpr char sConfigFile[] = "DW8XL_ARFix.ini";
    constexpr char sLogFile[] = "DW8XL_ARFix.log";

    int iTargetWidth = 1280;
    int iTargetHeight = 800;
    double dCustomAR = 1.6;
    float fCustomAR_SP = 1.6f;
    float fCustomAR_MP = 3.2f;
    static double g_EngineAR = 1.6;

    float fFovMultiplier = 1.0f; // 1.0 = 100%
    bool bStretch = true;

    struct dw8_camera_state {
        float min_distance;
        float max_distance;
        float height;
        float UNKNOWN_1;
        float angle;
        float UNKNOWN_2;
        float UNKNOWN_3;
    };
    struct dw8_camera_settings {
        dw8_camera_state standard;
        dw8_camera_state blocking;
    };

    safetyhook::MidHook aspectUpdateHook;

    std::uint8_t* PatternScan(HMODULE module, const char* signature)
    {
        static auto pattern_to_byte = [](const char* pattern) {
            auto bytes = std::vector<int>{};
            auto start = const_cast<char*>(pattern);
            auto end = const_cast<char*>(pattern) + strlen(pattern);
            for (auto current = start; current < end; ++current) {
                if (*current == '?') {
                    ++current;
                    if (*current == '?') ++current;
                    bytes.push_back(-1);
                } else {
                    bytes.push_back(strtoul(current, &current, 16));
                }
            }
            return bytes;
        };

        auto dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(module);
        auto ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<std::uint8_t*>(module) + dosHeader->e_lfanew);
        auto sizeOfImage = ntHeaders->OptionalHeader.SizeOfImage;
        auto patternBytes = pattern_to_byte(signature);
        auto scanBytes = reinterpret_cast<std::uint8_t*>(module);
        auto s = patternBytes.size();
        auto d = patternBytes.data();

        for (auto i = 0ul; i < sizeOfImage - s; ++i) {
            bool found = true;
            for (auto j = 0ul; j < s; ++j) {
                if (scanBytes[i + j] != d[j] && d[j] != -1) {
                    found = false;
                    break;
                }
            }
            if (found) return &scanBytes[i];
        }
        return nullptr;
    }

    void WriteMemory(void* address, void* data, size_t size)
    {
        DWORD oldProtect;
        if (VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            std::memcpy(address, data, size);
            VirtualProtect(address, size, oldProtect, &oldProtect);
        }
    }

    bool InitializeLogging()
    {
        WCHAR dllPath[MAX_PATH] = {};
        if (GetModuleFileNameW(thisModule, dllPath, MAX_PATH) == 0) return false;
        FixPath = fs::path(dllPath).parent_path();

        std::ofstream clearLog(FixPath / sLogFile, std::ios::trunc);
        if (clearLog.is_open()) clearLog.close();

        try {
            auto logger = std::make_shared<spdlog::logger>(
                "DW8XL_ARFix",
                std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    (FixPath / sLogFile).string(), 5 * 1024 * 1024, 1));
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::info);
            SPDLOG_INFO("Dynasty Warriors 8 XL Ultrawide & FOV Fix initialized.");
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
            defaultIni << "[Resolution]\nWidth = 1280\nHeight = 800\n\n"
                       << "[Camera]\n"
                       << "; Global FOV (Camera Distance) Multiplier to compensate for non-16:9 screens.\n"
                       << "; 1.0  = 100% (Default 16:9)\n"
                       << "; 1.10 = 110% (Recommended for 16:10, e.g. Steam Deck)\n"
                       << "; 1.16 = 116% (Recommended for 3:2)\n"
                       << "; 1.29 = 129% (Recommended for 4:3)\n"
                       << "FovMultiplier = 1.0\n";
            defaultIni.close();
            SPDLOG_INFO("Created default config {}.", sConfigFile);
        }

        std::ifstream iniFile(path);
        if (!iniFile) return false;

        inipp::Ini<char> ini;
        ini.parse(iniFile);
        ini.strip_trailing_comments();

        inipp::get_value(ini.sections["Resolution"], "Width", iTargetWidth);
        inipp::get_value(ini.sections["Resolution"], "Height", iTargetHeight);

        if (iTargetHeight > 0) {
            dCustomAR = static_cast<double>(iTargetWidth) / static_cast<double>(iTargetHeight);
            fCustomAR_SP = static_cast<float>(dCustomAR);
            fCustomAR_MP = static_cast<float>(dCustomAR) * 2.0f;
            g_EngineAR = dCustomAR;
        }

        inipp::get_value(ini.sections["Camera"], "FovMultiplier", fFovMultiplier);
        inipp::get_value(ini.sections["Stretch"], "Enabled", bStretch);

        if (fFovMultiplier < 0.1f) fFovMultiplier = 0.1f;
        if (fFovMultiplier > 5.0f) fFovMultiplier = 5.0f;

        SPDLOG_INFO("Config loaded -> {}x{}, AR: {:.5f}", iTargetWidth, iTargetHeight, fCustomAR_SP);
        SPDLOG_INFO("Camera Settings -> FOV Multiplier: {:.2f}x", fFovMultiplier);
        return true;
    }

    void ApplyResolutionAndCameraPatches()
    {
        auto addrWidth1 = PatternScan(exeModule, "C3 B8 ? ? 00 00 5D C2 04 00");
        if (addrWidth1) WriteMemory(addrWidth1 + 2, &iTargetWidth, 4);

        auto addrWindowWidth = PatternScan(exeModule, "B8 ? ? 00 00 6A 40 68 00 00 00 80");
        if (addrWindowWidth) WriteMemory(addrWindowWidth + 1, &iTargetWidth, 4);

        auto addrWindowHeight = PatternScan(exeModule, "B8 ? ? 00 00 5B 5F 5E C2 08 00");
        if (addrWindowHeight) WriteMemory(addrWindowHeight + 1, &iTargetHeight, 4);

        auto addrMemLimit = PatternScan(exeModule, "80 07 00 00 E0 01 58 02");
        if (addrMemLimit) {
            uint16_t w16 = static_cast<uint16_t>(iTargetWidth);
            WriteMemory(addrMemLimit, &w16, 2);
        }

        auto addrHudFix = PatternScan(exeModule, "C7 44 24 0C ? ? 00 00 89 4C 24 10");
        if (addrHudFix) WriteMemory(addrHudFix + 4, &iTargetWidth, 4);

        auto addrRageHud = PatternScan(exeModule, "10 00 ? ? 00 00 EB 1C");
        if (addrRageHud) WriteMemory(addrRageHud + 1, &iTargetWidth, 4);

        auto addrCutsceneAR = PatternScan(exeModule, "40 39 8E E3 3F");
        if (addrCutsceneAR) {
            WriteMemory(addrCutsceneAR + 1, &fCustomAR_SP, 4);
            WriteMemory(addrCutsceneAR + 9, &fCustomAR_MP, 4);
        }

        auto addrEngineAR = PatternScan(exeModule, "D9 43 14 D8 73 18");
        if (addrEngineAR) {
            std::uint8_t patch[6] = { 0xDD, 0x05, 0x00, 0x00, 0x00, 0x00 };
            std::uint32_t pAddr = reinterpret_cast<std::uint32_t>(&g_EngineAR);
            std::memcpy(patch + 2, &pAddr, 4);
            WriteMemory(addrEngineAR, patch, 6);
        }

        auto addrFuncAR = PatternScan(exeModule, "33 C4 89 44 24 44 D9 05");
        if (addrFuncAR) {
            aspectUpdateHook = safetyhook::create_mid(addrFuncAR - 8, [](SafetyHookContext& ctx) {
                float* a1 = *reinterpret_cast<float**>(ctx.esp + 8);
                if (a1 != nullptr) {
                    g_EngineAR = (a1[6] == 360.0f) ? (dCustomAR * 2.0) : dCustomAR;
                }
            });
        }

        if (fFovMultiplier != 1.0f) {
            auto addrCamera = PatternScan(exeModule, "00 00 E1 43 00 00 FA 43 00 00 02 43");
            if (addrCamera) {
                DWORD oldProtect;
                if (VirtualProtect(addrCamera, sizeof(dw8_camera_settings), PAGE_READWRITE, &oldProtect)) {
                    auto cam = reinterpret_cast<dw8_camera_settings*>(addrCamera);

                    cam->standard.min_distance *= fFovMultiplier;
                    cam->standard.max_distance *= fFovMultiplier;

                    cam->blocking.min_distance *= fFovMultiplier;
                    cam->blocking.max_distance *= fFovMultiplier;

                    VirtualProtect(addrCamera, sizeof(dw8_camera_settings), oldProtect, &oldProtect);
                    SPDLOG_INFO("Camera distance successfully scaled by {:.2f}x.", fFovMultiplier);
                }
            } else {
                SPDLOG_ERROR("Failed to find Camera Structure AOB.");
            }
        }

        SPDLOG_INFO("Deployment complete.");
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

    DWORD WINAPI SetupThread(void*)
    {
        if (!InitializeLogging()) return 1;
        if (!LoadConfiguration()) return 1;
        Stretch::Install();
        Sleep(2000);
        ApplyResolutionAndCameraPatches();
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        thisModule = hModule;
        if (HANDLE thread = CreateThread(nullptr, 0, SetupThread, nullptr, 0, nullptr)) {
            CloseHandle(thread);
        }
    }
    return TRUE;
}