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
#include <cstring>

#if !defined(_M_IX86)
#error Rayman Legends is a 32-bit game. Compile this project as x86.
#endif

#pragma comment(lib, "advapi32.lib")

namespace
{
    constexpr double kWide = 16.0 / 9.0;
    constexpr float kDesignWidth = 1280.0f;
    constexpr float kDesignHeight = 720.0f;

    HMODULE game = GetModuleHandleW(nullptr);
    std::uint8_t* base = reinterpret_cast<std::uint8_t*>(game);
    std::filesystem::path gameDir;
    int iniWidth = 1280;
    int iniHeight = 800;
    std::atomic<double> screenAspect{kWide};

    template <typename T>
    T& At(void* object, std::size_t offset)
    {
        return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
    }

    void Write(void* address, const void* data, std::size_t size)
    {
        DWORD old;
        VirtualProtect(address, size, PAGE_READWRITE, &old);
        std::memcpy(address, data, size);
        VirtualProtect(address, size, old, &old);
    }

    PIMAGE_NT_HEADERS Headers()
    {
        return reinterpret_cast<PIMAGE_NT_HEADERS>(base + reinterpret_cast<PIMAGE_DOS_HEADER>(base)->e_lfanew);
    }

    template <typename Visit>
    void ForEachSectionByte(bool executable, std::size_t step, Visit&& visit)
    {
        auto section = IMAGE_FIRST_SECTION(Headers());
        for (WORD i = 0; i < Headers()->FileHeader.NumberOfSections; ++i, ++section) {
            if (((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) != executable) continue;
            auto begin = base + section->VirtualAddress;
            for (auto p = begin; p + 8 <= begin + section->Misc.VirtualSize; p += step) visit(p);
        }
    }

    namespace Letterbox
    {
        double* fit = nullptr;

        bool IsFitConstant(double* candidate)
        {
            int fits = 0, others = 0;
            ForEachSectionByte(true, 1, [&](std::uint8_t* p) {
                if (At<double*>(p, 2) != candidate) return;
                const bool x87Qword = (p[0] == 0xDC || p[0] == 0xDD) && (p[1] & 0xC7) == 0x05;
                constexpr std::uint8_t kFildEcx[] = {0xDB, 0x81};
                const bool afterSizeRead = std::search(p - 0x20, p, std::begin(kFildEcx), std::end(kFildEcx)) != p;
                if (!x87Qword) ++others;
                else if (afterSizeRead) ++fits;
            });
            return others == 0 && fits >= 3;
        }

        bool Init()
        {
            const double wide = double(float(kWide));
            int found = 0;
            ForEachSectionByte(false, sizeof(double), [&](std::uint8_t* p) {
                if (std::memcmp(p, &wide, sizeof(wide)) == 0 && IsFitConstant(reinterpret_cast<double*>(p))) {
                    fit = reinterpret_cast<double*>(p);
                    ++found;
                }
            });
            return found == 1;
        }
    }

    namespace Camera
    {
        using PerspectiveFn = void*(WINAPI*)(void*, float, float, float, float);
        PerspectiveFn original = nullptr;

        void* WINAPI PerspectiveFovRH(void* out, float fovY, float aspect, float zNear, float zFar)
        {
            if (aspect < kWide - 0.01 && std::fabs(aspect - screenAspect) < 0.01)
                fovY = float(2.0 * std::atan(std::tan(fovY * 0.5) * kWide / aspect));
            return original(out, fovY, aspect, zNear, zFar);
        }

        bool Init()
        {
            auto& directory = Headers()->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
            for (auto dll = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(base + directory.VirtualAddress); dll->Name; ++dll) {
                auto names = reinterpret_cast<PIMAGE_THUNK_DATA>(base + dll->OriginalFirstThunk);
                auto slots = reinterpret_cast<PIMAGE_THUNK_DATA>(base + dll->FirstThunk);
                for (; names->u1.AddressOfData; ++names, ++slots) {
                    if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
                    if (std::strcmp(reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(base + names->u1.AddressOfData)->Name, "D3DXMatrixPerspectiveFovRH") != 0) continue;
                    original = reinterpret_cast<PerspectiveFn>(slots->u1.Function);
                    auto detour = reinterpret_cast<DWORD>(&PerspectiveFovRH);
                    Write(&slots->u1.Function, &detour, sizeof(detour));
                    return true;
                }
            }
            return false;
        }
    }

    namespace Menus
    {
        struct Vec2 { float x, y; };
        struct Vec3 { float x, y, z; };

        constexpr std::size_t kActor = 0x08;
        constexpr std::size_t kEnabled = 0x10;
        constexpr std::size_t kLaidOutFor = 0x1C;
        constexpr std::size_t kFlags = 0x0C;
        constexpr DWORD kIs2D = 0x400;
        constexpr std::size_t kParent = 0x2A0;

        float* designSize = nullptr;
        Vec2*(__thiscall* getPos)(void*, Vec2*) = nullptr;
        float(__thiscall* getDepth)(void*) = nullptr;
        void(__thiscall* setPos)(void*, const Vec3*, int, int, int) = nullptr;
        SafetyHookInline relayout;

        void __fastcall Relayout(void* component, void*, int force)
        {
            auto laidOutFor = &At<float>(component, kLaidOutFor);
            void* actor = At<void*>(component, kActor);
            if (At<int>(component, kEnabled) && actor && (At<DWORD>(actor, kFlags) & kIs2D)
                && (laidOutFor[0] != designSize[0] || laidOutFor[1] != designSize[1])) {
                if (!At<void*>(actor, kParent)) {
                    Vec2 pos;
                    getPos(actor, &pos);
                    const Vec3 centered{pos.x + (designSize[0] - laidOutFor[0]) / 2, pos.y + (designSize[1] - laidOutFor[1]) / 2, getDepth(actor)};
                    setPos(actor, &centered, 1, 0, 0);
                }
                laidOutFor[0] = std::nextafter(designSize[0], 0.0f);
                laidOutFor[1] = std::nextafter(designSize[1], 0.0f);
            }
            relayout.thiscall<void>(component, force);
        }

        std::uint8_t* FindIn(std::uint8_t* function, const char* pattern)
        {
            auto hits = Memory::PatternScanAll(game, pattern);
            hits.erase(std::remove_if(hits.begin(), hits.end(), [&](auto p) { return p < function || p > function + 0x300; }), hits.end());
            return hits.size() == 1 ? hits[0] : nullptr;
        }

        template <typename Fn>
        bool CallIn(std::uint8_t* function, const char* pattern, std::size_t callAt, Fn& target)
        {
            auto hit = FindIn(function, pattern);
            if (hit) target = reinterpret_cast<Fn>(hit + callAt + 5 + At<std::int32_t>(hit, callAt + 1));
            return hit != nullptr;
        }

        bool Init()
        {
            auto hits = Memory::PatternScanAll(game,
                "55 8B EC 83 EC 30 56 8B F1 83 7E 10 00 0F 84 ?? ?? ?? ?? 8B 46 08 F7 40 0C 00 04 00 00 0F 84 ?? ?? ?? ?? "
                "D9 46 1C D9 05 ?? ?? ?? ?? DA E9 DF E0 F6 C4 44 7A ?? D9 46 20 D9 05 ?? ?? ?? ??");
            if (hits.size() != 1) return false;
            auto function = hits[0];
            designSize = At<float*>(function, 40);
            if (At<float*>(function, 58) != designSize + 1 || designSize[0] != kDesignWidth || designSize[1] != kDesignHeight) return false;
            if (!FindIn(function, "8B B9 A0 02 00 00 85 FF")) return false;
            if (!CallIn(function, "8D 55 DC 52 E8 ?? ?? ?? ?? 8D 4D F0 8B F8 E8", 4, getPos)) return false;
            if (!CallIn(function, "D9 5D F4 E8 ?? ?? ?? ?? 8B 4E 08 D9 5D 08", 3, getDepth)) return false;
            if (!CallIn(function, "8D 45 D0 D9 45 08 50 D9 5D D8 E8", 10, setPos)) return false;
            auto hook = safetyhook::InlineHook::create(function, &Relayout, safetyhook::InlineHook::StartDisabled);
            if (!hook) return false;
            relayout = std::move(*hook);
            return bool(relayout.enable());
        }

        void Fit(double aspect)
        {
            designSize[0] = aspect >= kWide ? float(kDesignHeight * aspect) : kDesignWidth;
            designSize[1] = aspect >= kWide ? kDesignHeight : float(kDesignWidth / aspect);
        }
    }

    void ApplyAspect(double aspect)
    {
        Write(Letterbox::fit, &aspect, sizeof(aspect));
        if (Menus::designSize) Menus::Fit(aspect);
        screenAspect = aspect;
    }

    DWORD WINAPI FollowWindow(void*)
    {
        for (double applied = 0;; Sleep(250)) {
            struct { DWORD pid; RECT best; } search{GetCurrentProcessId(), {}};
            EnumWindows([](HWND window, LPARAM param) -> BOOL {
                auto& s = *reinterpret_cast<decltype(search)*>(param);
                DWORD pid = 0;
                RECT r;
                GetWindowThreadProcessId(window, &pid);
                if (pid == s.pid && IsWindowVisible(window) && GetClientRect(window, &r) && r.right * r.bottom > s.best.right * s.best.bottom) s.best = r;
                return TRUE;
            }, reinterpret_cast<LPARAM>(&search));
            if (search.best.right <= 0 || search.best.bottom <= 0) continue;
            const double aspect = double(search.best.right) / search.best.bottom;
            if (aspect == applied) continue;
            ApplyAspect(applied = aspect);
            spdlog::info("Screen {}x{}", search.best.right, search.best.bottom);
        }
    }

    void SetGameResolution()
    {
        HKEY key;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Ubisoft\\Rayman Legends\\Settings", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
        for (auto [name, value] : {std::pair{L"ScreenWidth", iniWidth}, std::pair{L"ScreenHeight", iniHeight}}) {
            const auto text = std::to_wstring(value);
            RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(text.c_str()), DWORD((text.size() + 1) * sizeof(wchar_t)));
        }
        RegCloseKey(key);
    }

    void Init()
    {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(game, path, MAX_PATH);
        gameDir = std::filesystem::path(path).parent_path();
        try {
            spdlog::set_default_logger(spdlog::basic_logger_mt("RaymanLegends", (gameDir / "RaymanLegends_ARFix.log").string(), true));
            spdlog::set_pattern("[%H:%M:%S.%e] %v");
            spdlog::flush_on(spdlog::level::info);
        } catch (...) {}
        spdlog::info("RaymanLegends_ARFix {}", VERSION_STRING);

        if (std::ifstream file(gameDir / "RaymanLegends_ARFix.ini"); file) {
            inipp::Ini<char> ini;
            ini.parse(file);
            ini.strip_trailing_comments();
            inipp::get_value(ini.sections["Resolution"], "Width", iniWidth);
            inipp::get_value(ini.sections["Resolution"], "Height", iniHeight);
        }

        using SetDpiContext = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto setDpiContext = reinterpret_cast<SetDpiContext>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
        if (!setDpiContext || !setDpiContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) SetProcessDPIAware();

        if (!Letterbox::Init()) { spdlog::error("16:9 fit constant not found; mod disabled"); return; }
        spdlog::info("Camera: {}, menus: {}", Camera::Init(), Menus::Init());
        if (iniWidth > 0 && iniHeight > 0) {
            ApplyAspect(double(iniWidth) / iniHeight);
            SetGameResolution();
            spdlog::info("Resolution {}x{}", iniWidth, iniHeight);
        }
        if (HANDLE thread = CreateThread(nullptr, 0, FollowWindow, nullptr, 0, nullptr)) CloseHandle(thread);
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        Init();
    }
    return TRUE;
}
