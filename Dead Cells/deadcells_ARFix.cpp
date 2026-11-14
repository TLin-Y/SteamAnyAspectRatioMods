#include "stdafx.h"
#include <windows.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <inipp/inipp.h>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if !defined(_M_IX86)
#error Dead Cells is a 32-bit game. Compile this project as x86.
#endif

namespace fs = std::filesystem;

namespace
{
    HMODULE thisModule = nullptr;
    fs::path FixPath;

    enum Op : std::int32_t { OMov = 0, OGetThis = 40, OJSGte = 49, OJNotEq = 57, OJAlways = 58, OToInt = 62, ORet = 67 };
    constexpr std::int32_t HOBJ = 11;

    struct Settings
    {
        bool windowEnabled = true;
        bool borderless = true;
        int width = 1280;
        int height = 800;
        bool fillEnabled = true;
    } settings;

    struct HlOpcode { std::int32_t op, p1, p2, p3; void* extra; };
    struct HlFunction { std::int32_t findex, nregs, nops, ref; void* type; void* regs; HlOpcode* ops; void* debug; void* obj; void* name; };
    struct HlType { std::int32_t kind; std::uint8_t* obj; void* vobjProto; void* markBits; };
    struct HlProto { const wchar_t* name; std::int32_t findex, pindex, hashedName; };
    static_assert(sizeof(HlOpcode) == 20 && sizeof(HlFunction) == 40 && sizeof(HlType) == 16 && sizeof(HlProto) == 16);

    struct Header { int version, nints, nfloats, nstrings, ntypes, nglobals, nnatives, nfunctions; } header{};

    int stagePatched = 0;
    bool displayModeLocked = false;
    std::vector<std::string> warnings;

    bool ReadHeader()
    {
        HMODULE exe = GetModuleHandleW(nullptr);
        HRSRC resource = FindResourceW(exe, L"HLBOOT.DAT", MAKEINTRESOURCEW(10));
        const auto* data = resource ? static_cast<const std::uint8_t*>(LockResource(LoadResource(exe, resource))) : nullptr;
        if (!data || SizeofResource(exe, resource) < 64 || std::memcmp(data, "HLB", 3) != 0) return false;
        header.version = data[3];
        size_t pos = 4;
        auto index = [&] {
            const int b = data[pos++];
            if (!(b & 0x80)) return b & 0x7F;
            if (!(b & 0x40)) {
                const int v = data[pos++] | ((b & 31) << 8);
                return (b & 0x20) ? -v : v;
            }
            const int v = ((b & 31) << 24) | (data[pos] << 16) | (data[pos + 1] << 8) | data[pos + 2];
            pos += 3;
            return (b & 0x20) ? -v : v;
        };
        index();
        header.nints = index();
        header.nfloats = index();
        header.nstrings = index();
        if (header.version >= 5) index();
        header.ntypes = index();
        header.nglobals = index();
        header.nnatives = index();
        header.nfunctions = index();
        return header.nfunctions > 0 && header.ntypes > 0;
    }

    bool Readable(std::uintptr_t start, size_t size)
    {
        MEMORY_BASIC_INFORMATION mbi{};
        for (const auto end = start + size; start < end; start = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
            if (!VirtualQuery(reinterpret_cast<void*>(start), &mbi, sizeof(mbi))) return false;
            if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
        }
        return true;
    }

    bool IsFunctionArray(std::uintptr_t p)
    {
        if (!p || !Readable(p, sizeof(HlFunction) * header.nfunctions)) return false;
        const auto* f = reinterpret_cast<const HlFunction*>(p);
        for (int i = 0; i < header.nfunctions; i += header.nfunctions / 64 + 1) {
            if (f[i].findex < 0 || f[i].findex >= header.nfunctions + header.nnatives || f[i].nops <= 0
                || !Readable(reinterpret_cast<std::uintptr_t>(f[i].ops), sizeof(HlOpcode))) return false;
        }
        return true;
    }

    bool IsTypeArray(std::uintptr_t p)
    {
        if (!p || !Readable(p, sizeof(HlType) * header.ntypes)) return false;
        const auto* t = reinterpret_cast<const HlType*>(p);
        for (int i = 0; i < header.ntypes; ++i) {
            if (t[i].kind < 0 || t[i].kind > 22) return false;
        }
        return true;
    }

    template <typename Pred>
    std::uintptr_t FindField(const std::uintptr_t* code, Pred pred)
    {
        for (int i = 8; i < 40; ++i) {
            if (pred(code[i])) return code[i];
        }
        return 0;
    }

    const std::uintptr_t* FindCode()
    {
        MEMORY_BASIC_INFORMATION mbi{};
        for (std::uintptr_t a = 0x10000; a < 0x7FFF0000 && VirtualQuery(reinterpret_cast<void*>(a), &mbi, sizeof(mbi));
             a = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
            if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE || mbi.Protect != PAGE_READWRITE) continue;
            const auto* p = static_cast<const std::int32_t*>(mbi.BaseAddress);
            const size_t count = mbi.RegionSize / 4;
            for (size_t i = 0; i + 48 < count; ++i, ++p) {
                if (p[0] != header.version || p[1] != header.nints || p[2] != header.nfloats || p[3] != header.nstrings) continue;
                const int skip = p[4] == header.ntypes ? 0 : 1;
                if (p[4 + skip] != header.ntypes || p[7 + skip] != header.nfunctions) continue;
                const auto* code = reinterpret_cast<const std::uintptr_t*>(p);
                if (FindField(code, IsFunctionArray)) return code;
            }
        }
        return nullptr;
    }

    bool NameEquals(const wchar_t* text, const wchar_t* expected)
    {
        __try {
            return text && std::wcscmp(text, expected) == 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    HlFunction* FindMethod(const HlType* types, HlFunction* functions, const wchar_t* typeName, const wchar_t* methodName)
    {
        int findex = -1;
        for (int i = 0; i < header.ntypes && findex < 0; ++i) {
            const auto* obj = types[i].obj;
            if (types[i].kind != HOBJ || !obj || !NameEquals(*reinterpret_cast<const wchar_t* const*>(obj + 12), typeName)) continue;
            const int nproto = *reinterpret_cast<const std::int32_t*>(obj + 4);
            const auto* protos = *reinterpret_cast<const HlProto* const*>(obj + 24);
            for (int j = 0; j < nproto && findex < 0; ++j) {
                if (NameEquals(protos[j].name, methodName)) findex = protos[j].findex;
            }
        }
        for (int i = 0; i < header.nfunctions && findex >= 0; ++i) {
            if (functions[i].findex == findex) return &functions[i];
        }
        return nullptr;
    }

    void PatchStage(HlFunction& f)
    {
        std::vector<HlOpcode*> hits;
        HlOpcode* o = f.ops;
        for (int k = 1; k + 3 < f.nops; ++k) {
            if (o[k].op == OJSGte && o[k].p3 == 2 && o[k - 1].op == OToInt && o[k - 1].p1 == o[k].p1
                && o[k + 1].op == OMov && o[k + 1].p2 == o[k].p1 && o[k + 2].op == OJAlways && o[k + 2].p1 == 1
                && o[k + 3].op == OMov && o[k + 3].p1 == o[k + 1].p1 && o[k + 3].p2 == o[k].p2) {
                hits.push_back(&o[k]);
            }
        }
        if (hits.size() != 2) return;
        for (auto* op : hits) op->p1 = op->p2;
        stagePatched = 2;
    }

    void LockDisplayMode(HlFunction& f)
    {
        HlOpcode* o = f.ops;
        if (f.nops < 3 || o[0].op != OGetThis || o[1].op != OJNotEq || o[2].op != ORet || o[1].p2 != o[0].p1) return;
        o[1].p2 = o[1].p1;
        displayModeLocked = true;
    }

    void PatchBytecode()
    {
        if (!ReadHeader()) return warnings.push_back("HLBOOT.DAT resource not found.");
        const auto* code = FindCode();
        if (!code) return warnings.push_back("Game bytecode not found in memory.");
        auto* functions = reinterpret_cast<HlFunction*>(FindField(code, IsFunctionArray));
        const auto* types = reinterpret_cast<const HlType*>(FindField(code, IsTypeArray));
        if (!types) return warnings.push_back("Game type table not found.");
        if (settings.fillEnabled) {
            if (auto* f = FindMethod(types, functions, L"Main", L"onResize")) PatchStage(*f);
            if (!stagePatched) warnings.push_back("Main.onResize not patched; black bars remain.");
        }
        if (settings.windowEnabled) {
            if (auto* f = FindMethod(types, functions, L"dx.Window", L"set_displayMode")) LockDisplayMode(*f);
            if (!displayModeLocked) warnings.push_back("dx.Window.set_displayMode not patched; the game keeps its display modes.");
        }
    }

    BOOL CALLBACK EnumGameWindow(HWND window, LPARAM parameter)
    {
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        RECT client{};
        if (process == GetCurrentProcessId() && IsWindowVisible(window) && !GetWindow(window, GW_OWNER) && !IsIconic(window)
            && GetClientRect(window, &client) && client.right >= 320 && client.bottom >= 180) {
            *reinterpret_cast<HWND*>(parameter) = window;
            return FALSE;
        }
        return TRUE;
    }

    void ResizeWindow(HWND window)
    {
        MONITORINFO monitor{ sizeof(MONITORINFO) };
        if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) return;
        const RECT screen = settings.borderless ? monitor.rcMonitor : monitor.rcWork;
        const int width = settings.width ? settings.width : monitor.rcMonitor.right - monitor.rcMonitor.left;
        const int height = settings.height ? settings.height : monitor.rcMonitor.bottom - monitor.rcMonitor.top;

        const LONG style = GetWindowLongW(window, GWL_STYLE);
        const LONG desired = (style & ~(WS_OVERLAPPEDWINDOW | WS_POPUP | WS_MAXIMIZE)) | (settings.borderless ? WS_POPUP : WS_OVERLAPPEDWINDOW);
        RECT frame{ 0, 0, width, height };
        if (!settings.borderless) AdjustWindowRectEx(&frame, desired, FALSE, GetWindowLongW(window, GWL_EXSTYLE));
        const int w = frame.right - frame.left;
        const int h = frame.bottom - frame.top;
        const int x = screen.left + (screen.right - screen.left - w) / 2;
        const int y = screen.top + (screen.bottom - screen.top - h) / 2;

        RECT current{};
        GetWindowRect(window, &current);
        if (style == desired && current.left == x && current.top == y && current.right - x == w && current.bottom - y == h) return;
        if (style != desired) SetWindowLongW(window, GWL_STYLE, desired);
        SetWindowPos(window, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        SPDLOG_INFO("Window client size set to {}x{}; borderless={}.", width, height, settings.borderless);
    }

    void LoadConfiguration()
    {
        WCHAR dllPath[MAX_PATH] = {};
        GetModuleFileNameW(thisModule, dllPath, MAX_PATH);
        FixPath = fs::path(dllPath).parent_path();
        std::ifstream file(FixPath / "deadcells_ARFix.ini");
        if (!file) return;
        inipp::Ini<char> ini;
        ini.parse(file);
        ini.strip_trailing_comments();
        inipp::get_value(ini.sections["Window"], "Enabled", settings.windowEnabled);
        inipp::get_value(ini.sections["Window"], "Borderless", settings.borderless);
        inipp::get_value(ini.sections["Window"], "Width", settings.width);
        inipp::get_value(ini.sections["Window"], "Height", settings.height);
        inipp::get_value(ini.sections["AspectRatio"], "Enabled", settings.fillEnabled);
        if (settings.width < 0 || settings.height < 0 || settings.width > 16384 || settings.height > 16384
            || (settings.width == 0) != (settings.height == 0)) {
            warnings.push_back("Invalid window size; using 1280x800.");
            settings.width = 1280;
            settings.height = 800;
        }
    }

    DWORD WINAPI MainThread(void*)
    {
        try {
            spdlog::set_default_logger(spdlog::basic_logger_mt("deadcells_ARFix", (FixPath / "deadcells_ARFix.log").string(), true));
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::info);
        } catch (...) {
            return 1;
        }
        SPDLOG_INFO("Dead Cells Any Aspect Ratio fix {}.{}.{} loaded.", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
        SPDLOG_INFO("Config: Window={} {}x{} borderless={}, AspectRatio={}", settings.windowEnabled,
            settings.width, settings.height, settings.borderless, settings.fillEnabled);
        SPDLOG_INFO("Stage clamps removed: {}. Display mode locked: {}.", stagePatched, displayModeLocked);
        for (const auto& warning : warnings) SPDLOG_WARN("{}", warning);

        while (settings.windowEnabled) {
            HWND window = nullptr;
            EnumWindows(EnumGameWindow, reinterpret_cast<LPARAM>(&window));
            if (window) ResizeWindow(window);
            Sleep(250);
        }
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        thisModule = hModule;
        LoadConfiguration();
        if (settings.fillEnabled || settings.windowEnabled) PatchBytecode();
        if (HANDLE thread = CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr)) CloseHandle(thread);
    }
    return TRUE;
}
