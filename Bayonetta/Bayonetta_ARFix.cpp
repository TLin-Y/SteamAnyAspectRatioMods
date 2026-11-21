#include "stdafx.h"
#include <windows.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <inipp/inipp.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <d3d9.h>

#if !defined(_M_IX86)
#error Bayonetta is a 32-bit game. Compile this project as x86.
#endif

namespace fs = std::filesystem;

namespace
{
    HMODULE exeModule = GetModuleHandleW(nullptr);
    HMODULE thisModule = nullptr;
    fs::path FixPath;
    constexpr char sConfigFile[] = "Bayonetta_ARFix.ini";
    constexpr char sLogFile[] = "Bayonetta_ARFix.log";
    constexpr float kNativeAspect = 16.0f / 9.0f;
    constexpr float kCanvasWidth = 1280.0f;
    constexpr float kCanvasHeight = 720.0f;

    int iTargetWidth = 1280;
    int iTargetHeight = 800;
    float fFovMultiplier = 1.0f;

    constexpr char PATTERN_UNLOCK[] = "85 C0 75 35 8B 54 24 24 8D 44 24 20 50 8D 4C 24 14 51 6A 00 6A 00 68 ?? ?? ?? ?? 52";
    constexpr char PATTERN_ORTHO_CALL[] = "F3 0F 11 4C 24 04 F3 0F 11 04 24 50 E8 ?? ?? ?? ?? 83 C4 1C C3";
    constexpr char PATTERN_PERSP_MAIN[] = "51 E8 ?? ?? ?? ?? 0F 57 C0 F3 0F 2A C0 F3 0F 11 04 24 E8 ?? ?? ?? ?? F3 0F 10 44 24 14 83 EC 10";
    constexpr char PATTERN_PERSP_CAM[] = "F3 0F 10 86 94 00 00 00 F3 0F 11 04 24 57 E8 ?? ?? ?? ?? 83 C4 14";
    constexpr char PATTERN_LOCK_CALL[] = "8B 74 24 0C 56 50 52 51 E8 ?? ?? ?? ?? A1";
    constexpr char PATTERN_UI_LAYOUT[] = "89 9E 24 01 00 00 C7 86 20 01 00 00 01 00 00 00 E8 ?? ?? ?? ?? 0F 57 C0 F3 0F 11 86 C8 01 00 00";
    constexpr char PATTERN_UI_PASS[] = "83 C4 28 F3 0F 11 44 24 04 0F 57 C0 8D 8C 24 C8 00 00 00 F3 0F 11 04 24 51 E8 ?? ?? ?? ?? 83 C4 0C";
    constexpr char PATTERN_MOVIE[] = "8D 94 24 B8 00 00 00 F3 0F 11 04 24 52 E8 ?? ?? ?? ?? F3 0F 10 05 ?? ?? ?? ?? F3 0F 11";

    using OrthoFn = void* (__cdecl*)(void*, float, float, float, float, float, float);
    using CanvasFn = void* (__cdecl*)(void*, float, float);
    using PerspFn = void* (__cdecl*)(void*, float, float, float, float);
    using LockFn = int(__cdecl*)(int, int, int, int);
    using SizeFn = int(__cdecl*)();
    using Create9ExFn = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);
    using CreateDevFn = HRESULT(__stdcall*)(IDirect3D9Ex*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
    using CreateDevExFn = HRESULT(__stdcall*)(IDirect3D9Ex*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*, IDirect3DDevice9Ex**);

    enum class Fit { None, Canvas, Movie };

    OrthoFn oOrtho = nullptr;
    CanvasFn oCanvas = nullptr;
    PerspFn oPersp = nullptr;
    LockFn oLock = nullptr;
    SizeFn GetRenderWidth = nullptr;
    SizeFn GetRenderHeight = nullptr;
    Create9ExFn oCreate9Ex = nullptr;
    CreateDevFn oCreateDev = nullptr;
    CreateDevExFn oCreateDevEx = nullptr;

    IDirect3DDevice9* gDevice = nullptr;
    Fit currentFit = Fit::None;

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

    std::vector<std::uint8_t*> FindAll(const char* signature)
    {
        std::vector<std::uint8_t*> results;
        auto base = reinterpret_cast<std::uint8_t*>(exeModule);
        auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
        auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
        auto section = IMAGE_FIRST_SECTION(nt);
        auto pattern = ParsePattern(signature);
        const size_t n = pattern.size();

        for (WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++section) {
            if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
            size_t size = section->Misc.VirtualSize;
            if (size < n) continue;
            std::uint8_t* start = base + section->VirtualAddress;
            for (size_t i = 0; i + n <= size; ++i) {
                bool found = true;
                for (size_t j = 0; j < n; ++j) {
                    if (pattern[j] != -1 && start[i + j] != pattern[j]) { found = false; break; }
                }
                if (found) {
                    results.push_back(start + i);
                    i += n - 1;
                }
            }
        }
        return results;
    }

    void WriteMemory(void* address, const void* data, size_t size)
    {
        DWORD oldProtect;
        if (VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            std::memcpy(address, data, size);
            VirtualProtect(address, size, oldProtect, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), address, size);
        }
    }

    std::uint8_t* CallTarget(std::uint8_t* call)
    {
        return call + 5 + *reinterpret_cast<std::int32_t*>(call + 1);
    }

    void WriteRel32(std::uint8_t* at, std::uint8_t opcode, const void* target)
    {
        std::uint8_t bytes[5] = { opcode };
        std::int32_t rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(target) - reinterpret_cast<std::uintptr_t>(at + 5));
        std::memcpy(bytes + 1, &rel, sizeof(rel));
        WriteMemory(at, bytes, sizeof(bytes));
    }

    void PatchSlot(void** vtbl, int index, void* hook, void** original)
    {
        if (vtbl[index] == hook) return;
        *original = vtbl[index];
        WriteMemory(&vtbl[index], &hook, sizeof(void*));
    }

    bool ExpectCount(const char* name, size_t expected, size_t actual)
    {
        if (actual != expected) {
            SPDLOG_ERROR("{}: expected {} match(es), found {}. Unsupported game version?", name, expected, actual);
            return false;
        }
        return true;
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
                "Bayonetta_ARFix",
                std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    (FixPath / sLogFile).string(), 5 * 1024 * 1024, 1));
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::info);
            SPDLOG_INFO("Bayonetta Any Aspect Ratio fix initialized.");
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
            defaultIni << "[Resolution]\n"
                       << "; Game resolution. 0 = use the resolution picked in the game options.\n"
                       << "Width = 1280\nHeight = 800\n\n"
                       << "[FOV]\n"
                       << "; 1.0 = the 16:9 horizontal FOV. Wider screens see more at the sides.\n"
                       << "; 1.1 = 110%, 0.9 = 90% (range 0.5 to 2.0).\n"
                       << "Multiplier = 1.0\n";
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
        inipp::get_value(ini.sections["FOV"], "Multiplier", fFovMultiplier);

        if (iTargetWidth < 0 || iTargetHeight < 0 || (iTargetWidth == 0) != (iTargetHeight == 0)) {
            SPDLOG_ERROR("Invalid resolution {}x{}; using the game's resolution.", iTargetWidth, iTargetHeight);
            iTargetWidth = 0;
            iTargetHeight = 0;
        }
        if (!(fFovMultiplier >= 0.5f && fFovMultiplier <= 2.0f)) {
            SPDLOG_ERROR("FOV Multiplier must be 0.5-2.0; using 1.0.");
            fFovMultiplier = 1.0f;
        }

        SPDLOG_INFO("Config loaded -> {}x{}, FOV Multiplier {:.2f}", iTargetWidth, iTargetHeight, fFovMultiplier);
        return true;
    }

    float RenderAspect()
    {
        if (iTargetWidth > 0 && iTargetHeight > 0) return static_cast<float>(iTargetWidth) / static_cast<float>(iTargetHeight);
        const int w = GetRenderWidth();
        const int h = GetRenderHeight();
        if (w <= 0 || h <= 0) return kNativeAspect;
        return static_cast<float>(w) / static_cast<float>(h);
    }

    void ClearMovieBars(float aspect)
    {
        IDirect3DDevice9* dev = gDevice;
        if (!dev) return;
        IDirect3DSurface9* rt = nullptr;
        if (FAILED(dev->GetRenderTarget(0, &rt)) || !rt) return;
        D3DSURFACE_DESC desc = {};
        rt->GetDesc(&desc);
        rt->Release();
        const LONG w = static_cast<LONG>(desc.Width);
        const LONG h = static_cast<LONG>(desc.Height);
        D3DRECT rects[2] = {};
        if (aspect < kNativeAspect) {
            const LONG bar = (h - w * 9 / 16) / 2;
            if (bar <= 0) return;
            rects[0] = { 0, 0, w, bar };
            rects[1] = { 0, h - bar, w, h };
        } else {
            const LONG bar = (w - h * 16 / 9) / 2;
            if (bar <= 0) return;
            rects[0] = { 0, 0, bar, h };
            rects[1] = { w - bar, 0, w, h };
        }
        D3DVIEWPORT9 vp = {};
        dev->GetViewport(&vp);
        D3DVIEWPORT9 full = { 0, 0, desc.Width, desc.Height, 0.0f, 1.0f };
        dev->SetViewport(&full);
        dev->Clear(2, rects, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
        dev->SetViewport(&vp);
    }

    void* __cdecl HookedOrtho(void* m, float l, float r, float b, float t, float zn, float zf)
    {
        if (currentFit != Fit::None && l == 0.0f && r == kCanvasWidth && b == kCanvasHeight && t == 0.0f) {
            const float aspect = RenderAspect();
            if (aspect < kNativeAspect - 0.001f) {
                const float extra = (kCanvasWidth / aspect - kCanvasHeight) * 0.5f;
                t = -extra;
                b = kCanvasHeight + extra;
            } else if (aspect > kNativeAspect + 0.001f) {
                const float extra = (kCanvasHeight * aspect - kCanvasWidth) * 0.5f;
                l = -extra;
                r = kCanvasWidth + extra;
            }
            if (currentFit == Fit::Movie && std::fabs(aspect - kNativeAspect) > 0.001f) ClearMovieBars(aspect);
            static float loggedAspect = 0.0f;
            if (loggedAspect != aspect) {
                loggedAspect = aspect;
                SPDLOG_INFO("UI canvas at aspect {:.4f}: x {:.1f}..{:.1f}, y {:.1f}..{:.1f}", aspect, l, r, t, b);
            }
        }
        return oOrtho(m, l, r, b, t, zn, zf);
    }

    void* __cdecl FitCanvas(void* m, float zn, float zf)
    {
        currentFit = Fit::Canvas;
        void* result = oCanvas(m, zn, zf);
        currentFit = Fit::None;
        return result;
    }

    void* __cdecl FitMovieCanvas(void* m, float zn, float zf)
    {
        currentFit = Fit::Movie;
        void* result = oCanvas(m, zn, zf);
        currentFit = Fit::None;
        return result;
    }

    void* __cdecl HookedPersp(void* m, float fovY, float aspect, float zn, float zf)
    {
        const float screenAspect = RenderAspect();
        const bool target = std::fabs(aspect - screenAspect) < 0.001f;
        const float fovIn = fovY;

        if (target && fovY > 0.0f && fovY < 3.1f) {
            const float hfov = 2.0f * std::atan(std::tan(fovY * 0.5f) * kNativeAspect);
            const float hfovOut = (std::min)(hfov * fFovMultiplier, 170.0f * 0.0174532925f);
            const float tanH = std::tan(hfovOut * 0.5f) * (std::max)(1.0f, aspect / kNativeAspect);
            fovY = 2.0f * std::atan(tanH / aspect);
        }

        static float seen[16];
        static int seenCount = 0;
        bool known = false;
        for (int i = 0; i < seenCount; ++i) known |= std::fabs(seen[i] - aspect) < 0.001f;
        if (!known && seenCount < 16) {
            seen[seenCount++] = aspect;
            SPDLOG_INFO("View aspect {:.4f}: vFOV {:.2f} -> {:.2f}{}", aspect, fovIn * 57.29578f,
                        fovY * 57.29578f, target ? "" : " (not the game view, unchanged)");
        }
        return oPersp(m, fovY, aspect, zn, zf);
    }

    int __cdecl HookedLock(int mode, int width, int height, int flag)
    {
        const int inMode = mode;
        const int inW = width;
        const int inH = height;
        if (iTargetWidth > 0 && iTargetHeight > 0) {
            width = iTargetWidth;
            height = iTargetHeight;
            if (mode == 2) mode = 1;
        }
        SPDLOG_INFO("Display mode {} -> {}: {}x{} -> {}x{}", inMode, mode, inW, inH, width, height);
        return oLock(mode, width, height, flag);
    }

    HRESULT __stdcall HookedCreateDevice(IDirect3D9Ex* d3d, UINT adapter, D3DDEVTYPE type, HWND window, DWORD flags,
                                         D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** device)
    {
        HRESULT hr = oCreateDev(d3d, adapter, type, window, flags, pp, device);
        if (SUCCEEDED(hr) && device && *device) gDevice = *device;
        return hr;
    }

    HRESULT __stdcall HookedCreateDeviceEx(IDirect3D9Ex* d3d, UINT adapter, D3DDEVTYPE type, HWND window, DWORD flags,
                                           D3DPRESENT_PARAMETERS* pp, D3DDISPLAYMODEEX* mode, IDirect3DDevice9Ex** device)
    {
        HRESULT hr = oCreateDevEx(d3d, adapter, type, window, flags, pp, mode, device);
        if (SUCCEEDED(hr) && device && *device) gDevice = *device;
        return hr;
    }

    HRESULT WINAPI HookedCreate9Ex(UINT sdk, IDirect3D9Ex** out)
    {
        HRESULT hr = oCreate9Ex(sdk, out);
        if (SUCCEEDED(hr) && out && *out) {
            void** vtbl = *reinterpret_cast<void***>(*out);
            PatchSlot(vtbl, 16, reinterpret_cast<void*>(&HookedCreateDevice), reinterpret_cast<void**>(&oCreateDev));
            PatchSlot(vtbl, 20, reinterpret_cast<void*>(&HookedCreateDeviceEx), reinterpret_cast<void**>(&oCreateDevEx));
        }
        return hr;
    }

    void HookDirect3DCreate()
    {
        auto base = reinterpret_cast<std::uint8_t*>(exeModule);
        auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + reinterpret_cast<PIMAGE_DOS_HEADER>(base)->e_lfanew);
        auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        for (auto imp = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(base + dir.VirtualAddress); imp->Name; ++imp) {
            if (_stricmp(reinterpret_cast<char*>(base + imp->Name), "d3d9.dll") != 0) continue;
            auto names = reinterpret_cast<PIMAGE_THUNK_DATA>(base + imp->OriginalFirstThunk);
            auto slots = reinterpret_cast<PIMAGE_THUNK_DATA>(base + imp->FirstThunk);
            for (; names->u1.AddressOfData; ++names, ++slots) {
                if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
                auto byName = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(base + names->u1.AddressOfData);
                if (std::strcmp(reinterpret_cast<const char*>(byName->Name), "Direct3DCreate9Ex") == 0) {
                    oCreate9Ex = reinterpret_cast<Create9ExFn>(slots->u1.Function);
                    void* hook = reinterpret_cast<void*>(&HookedCreate9Ex);
                    WriteMemory(&slots->u1.Function, &hook, sizeof(void*));
                    SPDLOG_INFO("Movie bar hook installed.");
                    return;
                }
            }
        }
        SPDLOG_ERROR("Direct3DCreate9Ex import not found; movie bars keep the game background.");
    }

    void RedirectCanvasCall(const char* name, const char* pattern, size_t offset, CanvasFn hook)
    {
        auto sites = FindAll(pattern);
        if (!ExpectCount(name, 1, sites.size())) return;
        std::uint8_t* call = sites[0] + offset;
        if (CallTarget(call) != reinterpret_cast<std::uint8_t*>(oCanvas)) {
            SPDLOG_ERROR("{}: call target mismatch.", name);
            return;
        }
        WriteRel32(call, 0xE8, reinterpret_cast<void*>(hook));
    }

    void ApplyPatches()
    {
        auto unlock = FindAll(PATTERN_UNLOCK);
        if (ExpectCount("PATTERN_UNLOCK", 1, unlock.size())) {
            std::uint8_t* p = unlock[0];
            if (p[0x37] == 0x74) {
                std::uint8_t* skip = p + 0x39 + static_cast<std::int8_t>(p[0x38]);
                WriteRel32(p, 0xE9, skip);
                SPDLOG_INFO("Aspect lock disabled.");
            } else {
                SPDLOG_ERROR("PATTERN_UNLOCK: unexpected branch byte {:02X}.", p[0x37]);
            }
        }

        auto persp = FindAll(PATTERN_PERSP_MAIN);
        if (!ExpectCount("PATTERN_PERSP_MAIN", 1, persp.size()) || persp[0][0x59] != 0xE8) {
            SPDLOG_ERROR("Main camera not found; UI and FOV left unchanged.");
        } else {
            GetRenderWidth = reinterpret_cast<SizeFn>(CallTarget(persp[0] + 1));
            GetRenderHeight = reinterpret_cast<SizeFn>(CallTarget(persp[0] + 0x12));
            oPersp = reinterpret_cast<PerspFn>(CallTarget(persp[0] + 0x59));
            WriteRel32(persp[0] + 0x59, 0xE8, reinterpret_cast<void*>(&HookedPersp));

            auto cam = FindAll(PATTERN_PERSP_CAM);
            if (ExpectCount("PATTERN_PERSP_CAM", 1, cam.size()) &&
                CallTarget(cam[0] + 0x0E) == reinterpret_cast<std::uint8_t*>(oPersp)) {
                WriteRel32(cam[0] + 0x0E, 0xE8, reinterpret_cast<void*>(&HookedPersp));
            }
            SPDLOG_INFO("FOV hooks installed.");

            auto ortho = FindAll(PATTERN_ORTHO_CALL);
            if (ExpectCount("PATTERN_ORTHO_CALL", 1, ortho.size())) {
                oCanvas = reinterpret_cast<CanvasFn>(ortho[0] - 0x3E);
                oOrtho = reinterpret_cast<OrthoFn>(CallTarget(ortho[0] + 0x0C));
                WriteRel32(ortho[0] + 0x0C, 0xE8, reinterpret_cast<void*>(&HookedOrtho));
                RedirectCanvasCall("PATTERN_UI_LAYOUT", PATTERN_UI_LAYOUT, 0x10, &FitCanvas);
                RedirectCanvasCall("PATTERN_UI_PASS", PATTERN_UI_PASS, 0x19, &FitCanvas);
                RedirectCanvasCall("PATTERN_MOVIE", PATTERN_MOVIE, 0x0D, &FitMovieCanvas);
                SPDLOG_INFO("UI canvas hooks installed.");
            }
        }

        auto lock = FindAll(PATTERN_LOCK_CALL);
        if (ExpectCount("PATTERN_LOCK_CALL", 1, lock.size())) {
            oLock = reinterpret_cast<LockFn>(CallTarget(lock[0] + 8));
            WriteRel32(lock[0] + 8, 0xE8, reinterpret_cast<void*>(&HookedLock));
            SPDLOG_INFO("Resolution hook installed.");
        }

        HookDirect3DCreate();
    }

    void Initialize()
    {
        if (!InitializeLogging()) return;
        if (!LoadConfiguration()) return;
        ApplyPatches();
        SPDLOG_INFO("Deployment complete.");
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        thisModule = hModule;
        Initialize();
    }
    return TRUE;
}
