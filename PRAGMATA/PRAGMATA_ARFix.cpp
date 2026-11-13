#include "stdafx.h"
#include <windows.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <inipp/inipp.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <vector>
#include <string>
#include <cstring>

#if !defined(_M_X64)
#error PRAGMATA is a 64-bit game. Compile this project as x64.
#endif

namespace fs = std::filesystem;

namespace
{
    HMODULE exeModule = GetModuleHandleW(nullptr);
    HMODULE thisModule = nullptr;
    fs::path FixPath;
    constexpr char sConfigFile[] = "PRAGMATA_ARFix.ini";
    constexpr char sLogFile[] = "PRAGMATA_ARFix.log";

    struct Settings
    {
        bool windowEnabled = true;
        bool borderless = true;
        int width = 1280;
        int height = 800;
        bool forceFit = true;
        bool ignoreFixedSize = true;
    } settings;

    struct Region
    {
        std::uint8_t* start;
        size_t size;
    };

    std::vector<Region> ReadableImageRegions()
    {
        std::vector<Region> regions;
        auto base = reinterpret_cast<std::uint8_t*>(exeModule);
        auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
        auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
        std::uint8_t* end = base + nt->OptionalHeader.SizeOfImage;

        MEMORY_BASIC_INFORMATION mbi{};
        for (std::uint8_t* addr = base; addr < end; addr = static_cast<std::uint8_t*>(mbi.BaseAddress) + mbi.RegionSize) {
            if (!VirtualQuery(addr, &mbi, sizeof(mbi))) break;
            if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) continue;
            auto start = static_cast<std::uint8_t*>(mbi.BaseAddress);
            size_t size = mbi.RegionSize;
            if (start + size > end) size = end - start;
            regions.push_back({ start, size });
        }
        return regions;
    }

    bool InRegions(const std::vector<Region>& regions, const std::uint8_t* p, size_t len)
    {
        for (const auto& r : regions) {
            if (p >= r.start && p + len <= r.start + r.size) return true;
        }
        return false;
    }

    std::vector<std::uint8_t*> FindDisplayTypeReads(const std::vector<Region>& regions)
    {
        static constexpr std::uint8_t lea[] = { 0x45, 0x8D, 0x7E, 0xF9 };
        std::vector<std::uint8_t*> hits;
        for (const auto& r : regions) {
            for (size_t i = 0; i + 20 <= r.size; ++i) {
                const std::uint8_t* p = r.start + i;
                if (p[0] != 0x44 || p[1] != 0x8B || (p[2] & 0xF8) != 0x70 || (p[2] & 0x07) == 0x04 || p[3] != 0x70) continue;
                for (size_t k = 4; k + sizeof(lea) <= 16; ++k) {
                    if (std::memcmp(p + k, lea, sizeof(lea)) == 0) {
                        hits.push_back(r.start + i);
                        break;
                    }
                }
            }
        }
        return hits;
    }

    std::set<std::uint8_t*> FindFixedSizeGetters(const std::vector<Region>& regions, const std::vector<std::uint8_t*>& reads)
    {
        std::set<std::uint8_t*> getters;
        for (auto read : reads) {
            for (std::uint8_t* p = read - 0x80; p < read; ++p) {
                if (!InRegions(regions, p, 5) || p[0] != 0xE8) continue;
                std::int32_t rel;
                std::memcpy(&rel, p + 1, sizeof(rel));
                std::uint8_t* target = p + 5 + rel;
                if (!InRegions(regions, target, 8)) continue;
                if (target[0] == 0x0F && target[1] == 0xB6 && target[2] == 0x81 && target[7] == 0xC3) getters.insert(target);
            }
        }
        return getters;
    }

    bool WriteMemory(void* address, const void* data, size_t size)
    {
        DWORD oldProtect;
        if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
        std::memcpy(address, data, size);
        VirtualProtect(address, size, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), address, size);
        return true;
    }

    std::uintptr_t Rva(const void* p)
    {
        return reinterpret_cast<std::uintptr_t>(p) - reinterpret_cast<std::uintptr_t>(exeModule);
    }

    void RemoveLetterbox()
    {
        std::vector<Region> regions;
        std::vector<std::uint8_t*> reads;
        for (int attempt = 0; attempt < 240 && reads.empty(); ++attempt) {
            regions = ReadableImageRegions();
            reads = FindDisplayTypeReads(regions);
            if (reads.empty()) Sleep(500);
        }
        if (reads.empty()) {
            SPDLOG_ERROR("Scene view layout code not found; letterbox not removed.");
            return;
        }

        if (settings.ignoreFixedSize) {
            static constexpr std::uint8_t returnFalse[] = { 0x31, 0xC0, 0xC3 };
            auto getters = FindFixedSizeGetters(regions, reads);
            if (getters.empty()) SPDLOG_WARN("Fixed output size check not found.");
            for (auto getter : getters) {
                if (WriteMemory(getter, returnFalse, sizeof(returnFalse))) SPDLOG_INFO("Fixed output size ignored at RVA 0x{:X}.", Rva(getter));
            }
        }

        if (settings.forceFit) {
            static constexpr std::uint8_t forceFit[] = { 0x45, 0x31, 0xF6, 0x90 };
            for (auto read : reads) {
                if (WriteMemory(read, forceFit, sizeof(forceFit))) SPDLOG_INFO("DisplayType forced to Fit at RVA 0x{:X}.", Rva(read));
            }
        }
    }

    struct WindowCandidate
    {
        HWND handle = nullptr;
        std::int64_t area = 0;
    };

    BOOL CALLBACK EnumGameWindow(HWND window, LPARAM parameter)
    {
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        if (process != GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window, GW_OWNER) || IsIconic(window)) return TRUE;

        RECT client{};
        if (!GetClientRect(window, &client)) return TRUE;

        auto& candidate = *reinterpret_cast<WindowCandidate*>(parameter);
        const auto area = static_cast<std::int64_t>(client.right) * client.bottom;
        if (client.right >= 320 && client.bottom >= 180 && area > candidate.area) {
            candidate.handle = window;
            candidate.area = area;
        }
        return TRUE;
    }

    HWND FindGameWindow()
    {
        WindowCandidate candidate{};
        EnumWindows(EnumGameWindow, reinterpret_cast<LPARAM>(&candidate));
        return candidate.handle;
    }

    bool ResizeWindow(HWND window)
    {
        MONITORINFO monitor{ sizeof(MONITORINFO) };
        if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) return false;

        RECT client{};
        if (!GetClientRect(window, &client)) return false;
        const int width = settings.width ? settings.width : client.right;
        const int height = settings.height ? settings.height : client.bottom;

        const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
        const LONG_PTR desiredStyle = settings.borderless
            ? (style & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW | WS_MAXIMIZE)) | static_cast<LONG_PTR>(WS_POPUP)
            : style;
        RECT frame{ 0, 0, width, height };
        if (!settings.borderless) {
            AdjustWindowRectEx(&frame, static_cast<DWORD>(desiredStyle), GetMenu(window) != nullptr,
                static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE)));
        }

        const int outerWidth = frame.right - frame.left;
        const int outerHeight = frame.bottom - frame.top;
        const RECT bounds = settings.borderless ? monitor.rcMonitor : monitor.rcWork;
        const int x = bounds.left + (bounds.right - bounds.left - outerWidth) / 2;
        const int y = bounds.top + (bounds.bottom - bounds.top - outerHeight) / 2;

        RECT current{};
        GetWindowRect(window, &current);
        if (style == desiredStyle && current.left == x && current.top == y
            && current.right - current.left == outerWidth && current.bottom - current.top == outerHeight) {
            return false;
        }

        if (style != desiredStyle) SetWindowLongPtrW(window, GWL_STYLE, desiredStyle);
        SetWindowPos(window, nullptr, x, y, outerWidth, outerHeight, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        SPDLOG_INFO("Window client size set to {}x{}; borderless={}.", width, height, settings.borderless);
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
                "PRAGMATA_ARFix",
                std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    (FixPath / sLogFile).string(), 5 * 1024 * 1024, 1));
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::info);
            SPDLOG_INFO("PRAGMATA Any Aspect Ratio fix initialized.");
            return true;
        } catch (...) {
            return false;
        }
    }

    void LoadConfiguration()
    {
        const fs::path path = FixPath / sConfigFile;
        if (!fs::exists(path)) {
            std::ofstream defaultIni(path);
            defaultIni << "[Window]\n"
                       << "Enabled = true\n"
                       << "Borderless = true\n"
                       << "Width = 1280\n"
                       << "Height = 800\n\n"
                       << "[Letterbox]\n"
                       << "ForceFit = true\n"
                       << "IgnoreFixedSize = true\n";
        }
        std::ifstream iniFile(path);
        if (iniFile) {
            inipp::Ini<char> ini;
            ini.parse(iniFile);
            ini.strip_trailing_comments();
            inipp::get_value(ini.sections["Window"], "Enabled", settings.windowEnabled);
            inipp::get_value(ini.sections["Window"], "Borderless", settings.borderless);
            inipp::get_value(ini.sections["Window"], "Width", settings.width);
            inipp::get_value(ini.sections["Window"], "Height", settings.height);
            inipp::get_value(ini.sections["Letterbox"], "ForceFit", settings.forceFit);
            inipp::get_value(ini.sections["Letterbox"], "IgnoreFixedSize", settings.ignoreFixedSize);
        }
        if (settings.width < 0 || settings.height < 0 || settings.width > 16384 || settings.height > 16384
            || (settings.width == 0) != (settings.height == 0)) {
            SPDLOG_ERROR("Invalid window size {}x{}; using 1280x800.", settings.width, settings.height);
            settings.width = 1280;
            settings.height = 800;
        }
        SPDLOG_INFO("Config: Window={} {}x{} borderless={}, ForceFit={}, IgnoreFixedSize={}",
            settings.windowEnabled, settings.width, settings.height, settings.borderless,
            settings.forceFit, settings.ignoreFixedSize);
    }

    DWORD WINAPI MainThread(void*)
    {
        if (!InitializeLogging()) return 1;
        LoadConfiguration();

        if (settings.forceFit || settings.ignoreFixedSize) RemoveLetterbox();
        if (!settings.windowEnabled) return 0;

        for (;;) {
            if (HWND window = FindGameWindow()) ResizeWindow(window);
            Sleep(1000);
        }
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        thisModule = hModule;
        if (HANDLE thread = CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr)) {
            CloseHandle(thread);
        }
    }
    return TRUE;
}
