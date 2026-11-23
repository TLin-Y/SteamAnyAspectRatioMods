#include "stdafx.h"
#include "version.h"
#include "helper.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <inipp/inipp.h>
#include <safetyhook.hpp>
#include <d3d9.h>
#include <mutex>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

#if !defined(_M_IX86)
#error Dynasty Warriors 6 is a 32-bit game. Compile this project as x86.
#endif

namespace
{
    HMODULE exeModule = GetModuleHandleW(nullptr);
    HMODULE thisModule = nullptr;
    std::filesystem::path FixPath;
    constexpr char sConfigFile[] = "DW6_ARFix.ini";
    constexpr char sLogFile[] = "DW6_ARFix.log";
    constexpr float kNativeAspect = 16.0f / 9.0f;

    constexpr char PATTERN_PERSPECTIVE[] =
        "D9 44 24 08 DC 0D ?? ?? ?? ?? D9 5C 24 08 D9 44 24 08 E8 ?? ?? ?? ?? D9 5C 24 08 F6 44 24 18 02";
    constexpr char PATTERN_PERSPECTIVE_END[] = "D9 58 2C D8 74 24 0C D9 18 C3";

    int iWidth = 1280;
    int iHeight = 800;
    float fFovMultiplier = 1.0f;
    float fAspect = 1.6f;
    bool bStretch = true;

    SafetyHookInline perspectiveHook;

    std::vector<int> ParsePattern(const char* pattern)
    {
        std::vector<int> bytes;
        const char* end = pattern + std::strlen(pattern);
        for (char* cur = const_cast<char*>(pattern); cur < end; ++cur) {
            if (*cur == ' ') continue;
            if (*cur == '?') {
                if (cur[1] == '?') ++cur;
                bytes.push_back(-1);
            } else {
                bytes.push_back(static_cast<int>(std::strtoul(cur, &cur, 16)));
                --cur;
            }
        }
        return bytes;
    }

    bool Matches(const std::uint8_t* at, const std::vector<int>& pattern)
    {
        for (size_t j = 0; j < pattern.size(); ++j)
            if (pattern[j] != -1 && at[j] != pattern[j]) return false;
        return true;
    }

    std::vector<std::uint8_t*> FindAll(const char* signature)
    {
        std::vector<std::uint8_t*> results;
        auto base = reinterpret_cast<std::uint8_t*>(exeModule);
        auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + reinterpret_cast<PIMAGE_DOS_HEADER>(base)->e_lfanew);
        auto section = IMAGE_FIRST_SECTION(nt);
        auto pattern = ParsePattern(signature);
        const size_t n = pattern.size();
        for (WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++section) {
            if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
            size_t size = section->Misc.VirtualSize;
            std::uint8_t* start = base + section->VirtualAddress;
            for (size_t i = 0; i + n <= size; ++i)
                if (Matches(start + i, pattern)) results.push_back(start + i);
        }
        return results;
    }

    bool InitializeLogging()
    {
        WCHAR dllPath[MAX_PATH] = {};
        if (GetModuleFileNameW(thisModule, dllPath, MAX_PATH) == 0) return false;
        FixPath = std::filesystem::path(dllPath).parent_path();
        try {
            auto logger = std::make_shared<spdlog::logger>(
                "DW6_ARFix", std::make_shared<spdlog::sinks::basic_file_sink_mt>((FixPath / sLogFile).string(), true));
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::info);
            spdlog::info("Dynasty Warriors 6 Any Aspect Ratio fix initialized.");
            return true;
        } catch (...) {
            return false;
        }
    }

    void LoadConfiguration()
    {
        const auto path = FixPath / sConfigFile;
        std::ifstream iniFile(path);
        if (!iniFile) {
            spdlog::warn("{} not found; using 1280x800.", sConfigFile);
        } else {
            inipp::Ini<char> ini;
            ini.parse(iniFile);
            ini.strip_trailing_comments();
            inipp::get_value(ini.sections["Resolution"], "Width", iWidth);
            inipp::get_value(ini.sections["Resolution"], "Height", iHeight);
            inipp::get_value(ini.sections["FOV"], "Multiplier", fFovMultiplier);
            inipp::get_value(ini.sections["Stretch"], "Enabled", bStretch);
        }
        if (iWidth <= 0 || iHeight <= 0) {
            spdlog::error("Invalid aspect {}x{}; using 1280x800.", iWidth, iHeight);
            iWidth = 1280;
            iHeight = 800;
        }
        if (!(fFovMultiplier >= 0.5f && fFovMultiplier <= 2.0f)) {
            spdlog::error("FOV Multiplier must be 0.5-2.0; using 1.0.");
            fFovMultiplier = 1.0f;
        }
        fAspect = static_cast<float>(iWidth) / static_cast<float>(iHeight);
        spdlog::info("Config: {}x{} (aspect {:.4f}), FOV multiplier {:.2f}, stretch {}", iWidth, iHeight, fAspect, fFovMultiplier, bStretch);
    }

    void LogView(float aspectIn, float fovIn, float fovOut, bool changed)
    {
        static float seen[32][2];
        static int count = 0;
        for (int i = 0; i < count; ++i)
            if (std::fabs(seen[i][0] - aspectIn) < 0.001f && std::fabs(seen[i][1] - fovIn) < 0.001f) return;
        if (count >= 32) return;
        seen[count][0] = aspectIn;
        seen[count][1] = fovIn;
        ++count;
        if (changed)
            spdlog::info("Camera aspect {:.4f} -> {:.4f}, FOV {:.4f} -> {:.4f}", aspectIn, fAspect, fovIn, fovOut);
        else
            spdlog::info("Camera aspect {:.4f}, FOV {:.4f} (unchanged)", aspectIn, fovIn);
    }

    void* __cdecl Perspective(void* matrix, float fov, float aspect, float zn, float zf, int flags, float scale)
    {
        const bool target = std::fabs(aspect - kNativeAspect) < 0.001f && fov > 0.0f && fov < 3.1f &&
                            (std::fabs(fAspect - kNativeAspect) > 0.001f || fFovMultiplier != 1.0f);
        if (!target) {
            LogView(aspect, fov, fov, false);
            return perspectiveHook.ccall<void*>(matrix, fov, aspect, zn, zf, flags, scale);
        }
        const float hfov = (std::min)(2.0f * std::atan(std::tan(fov * 0.5f) * kNativeAspect) * fFovMultiplier, 3.0f);
        const float tanH = std::tan(hfov * 0.5f) * (std::max)(1.0f, fAspect / kNativeAspect);
        const float fovOut = 2.0f * std::atan(tanH / fAspect);
        LogView(aspect, fov, fovOut, true);
        return perspectiveHook.ccall<void*>(matrix, fovOut, fAspect, zn, zf, flags, scale);
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
            spdlog::info("Fullscreen {}x{} switched to a borderless window covering the screen.", pp->BackBufferWidth, pp->BackBufferHeight);
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
                spdlog::info("Back buffer {}x{}: {}.", desc.Width, desc.Height,
                            stretch ? "stretching the 16:9 picture to fill it" : "no stretch needed");
            }
            if (!stretch) return;
            UINT cw = src.right - src.left, ch = src.bottom - src.top;
            if (!temp || tempW != cw || tempH != ch || tempFormat != desc.Format) {
                ReleaseTemp();
                HRESULT hr = device->CreateRenderTarget(cw, ch, desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &temp, nullptr);
                if (FAILED(hr)) {
                    failed = true;
                    spdlog::error("Stretch disabled: CreateRenderTarget failed (0x{:08X}).", static_cast<unsigned>(hr));
                    return;
                }
                tempW = cw; tempH = ch; tempFormat = desc.Format;
            }
            HRESULT hr = device->StretchRect(bb, &src, temp, nullptr, D3DTEXF_NONE);
            if (SUCCEEDED(hr)) hr = device->StretchRect(temp, nullptr, bb, nullptr, D3DTEXF_LINEAR);
            if (FAILED(hr)) {
                failed = true;
                spdlog::error("Stretch disabled: StretchRect failed (0x{:08X}).", static_cast<unsigned>(hr));
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
                spdlog::info("Stretch hooks: {}.", hkPresent && hkReset ? "installed" : "failed");
            }
            return hr;
        }

        void Install()
        {
            if (!bStretch) return;
            borderless = RunningOnMacOS();
            if (borderless) spdlog::info("macOS host: fullscreen will use a borderless window.");
            HMODULE d3d9 = LoadLibraryW(L"d3d9.dll");
            auto create = d3d9 ? reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(d3d9, "Direct3DCreate9")) : nullptr;
            IDirect3D9* d3d = create ? create(D3D_SDK_VERSION) : nullptr;
            if (!d3d) {
                spdlog::error("Stretch disabled: Direct3DCreate9 failed.");
                return;
            }
            hkCreateDevice = safetyhook::create_inline(VFunc(d3d, 16), &CreateDevice);
            d3d->Release();
            if (!hkCreateDevice) spdlog::error("Stretch disabled: CreateDevice hook failed.");
        }
    }

    void InstallHooks()
    {
        auto sites = FindAll(PATTERN_PERSPECTIVE);
        if (sites.size() != 1) {
            spdlog::error("Perspective builder: expected 1 match, found {}. Unsupported game version?", sites.size());
        } else if (!Matches(sites[0] + 0xBF, ParsePattern(PATTERN_PERSPECTIVE_END))) {
            spdlog::error("Perspective builder: unexpected layout. Unsupported game version?");
        } else {
            perspectiveHook = safetyhook::create_inline(sites[0], reinterpret_cast<void*>(&Perspective));
            spdlog::info("Aspect ratio and FOV hook: {}.", perspectiveHook ? "installed" : "failed");
        }
        Stretch::Install();
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        thisModule = hModule;
        if (InitializeLogging()) {
            LoadConfiguration();
            InstallHooks();
        }
    }
    return TRUE;
}
