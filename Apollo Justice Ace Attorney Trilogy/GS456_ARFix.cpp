#include "stdafx.h"
#include <windows.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <inipp/inipp.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#if !defined(_M_X64)
#error Apollo Justice: Ace Attorney Trilogy is a 64-bit game. Compile this project as x64.
#endif

namespace fs = std::filesystem;

namespace
{
    HMODULE exeModule = GetModuleHandleW(nullptr);
    HMODULE thisModule = nullptr;
    fs::path FixPath;
    constexpr char sConfigFile[] = "GS456_ARFix.ini";
    constexpr char sLogFile[] = "GS456_ARFix.log";
    constexpr float ReferenceWidth = 1920.0f;
    constexpr float ReferenceHeight = 1080.0f;
    constexpr float ReferenceAspect = ReferenceWidth / ReferenceHeight;

    constexpr size_t SceneViewRectOffset = 0x30;
    constexpr size_t SceneViewDisplayTypeOffset = 0x6C;
    constexpr size_t ParentOffset = 0x30;
    constexpr size_t GuiOffset = 0x20;
    constexpr size_t DirtyOffset = 0x4E;
    constexpr size_t FlagsOffset = 0x50;
    constexpr size_t PositionOffset = 0x60;
    constexpr size_t ScreenSizeOffset = 0x1E4;
    constexpr size_t GuiViewOffset = 0x188;
    constexpr size_t GuiGameObjectOffset = 0x10;
    constexpr size_t GameObjectNameOffset = 0x30;
    constexpr std::uint64_t AdjustBit = 1ull << 15;
    constexpr int AnchorShift = 37;
    constexpr int ScaleShift = 43;
    constexpr std::uint64_t AdjustMask = AdjustBit | (0xFull << AnchorShift) | (0x3ull << ScaleShift);
    constexpr std::uint64_t ScaleStretch = 1;
    constexpr std::uint64_t ScaleFitSmall = 2;
    constexpr std::uint64_t ScaleFitLarge = 3;
    constexpr std::uint64_t AnchorCenter = 4;

    struct Settings
    {
        bool windowEnabled = true;
        bool borderless = true;
        int width = 1280;
        int height = 800;
        bool fixEnabled = true;
        float backgroundHeight = 1280.0f;
    } settings;

    const std::vector<std::wstring> SceneLayers{ L"FP_Drawer", L"Guruguru_GS4" };
    const std::vector<std::wstring> Faders{ L"ScrnFader", L"MasterFader" };

    std::atomic<float> windowAspect{ ReferenceAspect };
    std::atomic<bool> refreshRequested{ false };

    struct TypeIds
    {
        std::uintptr_t sceneView = 0;
        std::uintptr_t guiView = 0;
    } typeIds;

    struct Region
    {
        std::uintptr_t start;
        size_t size;
        std::uintptr_t allocation;
    };

    bool Readable(const MEMORY_BASIC_INFORMATION& mbi)
    {
        return mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));
    }

    std::vector<Region> ImageRegions()
    {
        std::vector<Region> regions;
        auto base = reinterpret_cast<std::uint8_t*>(exeModule);
        auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
        auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
        const auto end = reinterpret_cast<std::uintptr_t>(base) + nt->OptionalHeader.SizeOfImage;
        MEMORY_BASIC_INFORMATION mbi{};
        for (auto addr = reinterpret_cast<std::uintptr_t>(base); addr < end;
             addr = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
            if (!VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi))) break;
            if (!Readable(mbi)) continue;
            const auto start = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
            regions.push_back({ start, std::min<size_t>(mbi.RegionSize, end - start), 0 });
        }
        return regions;
    }

    std::vector<Region> HeapRegions()
    {
        std::vector<Region> regions;
        MEMORY_BASIC_INFORMATION mbi{};
        for (std::uintptr_t addr = 0x10000; addr < 0x7FFFFFFF0000ull;
             addr = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
            if (!VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi))) break;
            if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE || mbi.Protect != PAGE_READWRITE) continue;
            regions.push_back({ reinterpret_cast<std::uintptr_t>(mbi.BaseAddress), mbi.RegionSize,
                reinterpret_cast<std::uintptr_t>(mbi.AllocationBase) });
        }
        return regions;
    }

    template <typename T>
    T& At(std::uintptr_t base, size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    const char* PoolString(std::uintptr_t pool, std::int32_t offset)
    {
        return offset >= 0 ? reinterpret_cast<const char*>(pool + offset) : "";
    }

    bool TryReadTypes(std::uintptr_t tdb, TypeIds& ids, bool& valid)
    {
        valid = false;
        const auto version = At<std::uint32_t>(tdb, 4);
        const auto numTypes = At<std::uint32_t>(tdb, 8);
        if (version < 71 || version > 73 || numTypes == 0 || numTypes >= (1u << 19)) return false;
        const auto types = At<std::uintptr_t>(tdb, 0x60);
        const auto typesImpl = At<std::uintptr_t>(tdb, 0x68);
        const auto stringPool = At<std::uintptr_t>(tdb, 0xD0);
        const auto bytePool = At<std::uintptr_t>(tdb, 0xD8);
        if (!types || !typesImpl || !stringPool || bytePool <= stringPool) return false;
        valid = true;

        for (std::uint32_t i = 0; i < numTypes; ++i) {
            const auto record = types + static_cast<std::uintptr_t>(i) * 0x48;
            const auto impl = (At<std::uint64_t>(record, 8) >> 38) & 0x3FFFF;
            const auto implRecord = typesImpl + impl * 0x30;
            const char* name = PoolString(stringPool, At<std::int32_t>(implRecord, 0));
            const char* ns = PoolString(stringPool, At<std::int32_t>(implRecord, 4));
            const auto vt = At<std::uintptr_t>(record, 0x40);
            if (std::strcmp(name, "SceneView") == 0 && std::strcmp(ns, "via") == 0) ids.sceneView = vt;
            else if (std::strcmp(name, "View") == 0 && std::strcmp(ns, "via.gui") == 0) ids.guiView = vt;
        }
        return ids.sceneView && ids.guiView;
    }

    std::uintptr_t ScanForTdb(const Region& r, bool& sawTdb, TypeIds& ids)
    {
        __try {
            for (size_t i = 0; i + 0x100 <= r.size; i += 4) {
                const auto p = r.start + i;
                if (*reinterpret_cast<const std::uint32_t*>(p) != 0x00424454u) continue;
                bool valid = false;
                if (TryReadTypes(p, ids, valid)) return p;
                sawTdb |= valid;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        return 0;
    }

    bool FindTypes()
    {
        for (int attempt = 0; attempt < 150; ++attempt) {
            bool sawTdb = false;
            for (const auto& r : ImageRegions()) {
                TypeIds ids;
                if (const auto p = ScanForTdb(r, sawTdb, ids)) {
                    typeIds = ids;
                    SPDLOG_INFO("Type database version {} found at RVA 0x{:X}.", At<std::uint32_t>(p, 4),
                        p - reinterpret_cast<std::uintptr_t>(exeModule));
                    return true;
                }
            }
            if (attempt == 0 || attempt % 15 == 14) {
                SPDLOG_INFO(sawTdb ? "Waiting for scene view types to load." : "Waiting for the type database.");
            }
            Sleep(2000);
        }
        return false;
    }

    std::wstring ViewName(std::uintptr_t gui)
    {
        const auto gameObject = At<std::uintptr_t>(gui, GuiGameObjectOffset);
        if (!gameObject) return {};
        const auto text = At<std::uintptr_t>(gameObject, GameObjectNameOffset);
        if (!text) return {};
        const int length = At<int>(text, 0x10);
        if (length <= 0 || length > 128) return {};
        return std::wstring(reinterpret_cast<const wchar_t*>(text + 0x14), static_cast<size_t>(length));
    }

    bool Contains(const std::vector<std::wstring>& list, const std::wstring& name)
    {
        return std::find(list.begin(), list.end(), name) != list.end();
    }

    std::string Narrow(const std::wstring& text)
    {
        std::string out;
        for (wchar_t c : text) out.push_back(c < 128 ? static_cast<char>(c) : '?');
        return out;
    }

    std::set<std::wstring> loggedViews;
    bool layerSeen = false;

    void SetAdjust(std::uintptr_t view, bool enabled, std::uint64_t scale, bool& changed)
    {
        auto& flags = At<std::uint64_t>(view, FlagsOffset);
        const std::uint64_t desired = enabled ? (AdjustBit | (AnchorCenter << AnchorShift) | (scale << ScaleShift)) : (flags & ((0x3ull << ScaleShift) | (0xFull << AnchorShift)));
        if ((flags & AdjustMask) != desired) {
            flags = (flags & ~AdjustMask) | desired;
            changed = true;
        }
    }

    void ApplyView(std::uintptr_t view, bool narrow)
    {
        if (At<std::uintptr_t>(view, ParentOffset) != 0) return;
        const auto gui = At<std::uintptr_t>(view, GuiOffset);
        if (!gui || At<std::uintptr_t>(gui, GuiViewOffset) != view) return;

        float* screen = &At<float>(view, ScreenSizeOffset);
        const std::wstring name = ViewName(gui);
        if (loggedViews.insert(name).second) {
            SPDLOG_INFO("GUI view '{}' canvas {}x{}.", Narrow(name), screen[0], screen[1]);
        }

        bool changed = false;
        const bool scene = Contains(SceneLayers, name);
        const bool standard = screen[0] == ReferenceWidth && (screen[1] == ReferenceHeight || (scene && screen[1] == settings.backgroundHeight));
        if (scene) {
            if (standard) {
                const float height = narrow ? settings.backgroundHeight : ReferenceHeight;
                const float offset = narrow ? (settings.backgroundHeight - ReferenceHeight) * 0.5f : 0.0f;
                if (screen[1] != height) {
                    screen[1] = height;
                    changed = true;
                }
                float* position = &At<float>(view, PositionOffset);
                if (position[1] != offset) {
                    position[1] = offset;
                    changed = true;
                }
                SetAdjust(view, true, narrow ? ScaleFitLarge : ScaleFitSmall, changed);
            } else {
                SetAdjust(view, narrow, ScaleFitLarge, changed);
                if (changed) refreshRequested.store(true);
                layerSeen |= narrow;
            }
        } else if (standard) {
            SetAdjust(view, true, Contains(Faders, name) ? ScaleStretch : ScaleFitSmall, changed);
        }
        if (changed) At<std::uint16_t>(view, DirtyOffset) |= 1;
    }

    void ApplySceneView(std::uintptr_t view)
    {
        auto& displayType = At<std::int32_t>(view, SceneViewDisplayTypeOffset);
        const float* rect = &At<float>(view, SceneViewRectOffset);
        if (displayType <= 0 || displayType > 0x20 || !(rect[0] > 0.0f && rect[0] < 65536.0f && rect[1] > 0.0f && rect[1] < 65536.0f)) return;
        SPDLOG_INFO("Scene view display type {} set to Fit.", displayType);
        displayType = 0;
    }

    struct ScanResult
    {
        std::vector<std::uintptr_t> sceneViews;
        std::vector<std::uintptr_t> views;
        std::set<std::uintptr_t> allocations;
    };

    void ScanRegion(const Region& r, ScanResult& result)
    {
        __try {
            const auto* p = reinterpret_cast<const std::uintptr_t*>(r.start);
            const size_t count = r.size / sizeof(std::uintptr_t);
            for (size_t i = 0; i < count; ++i) {
                const auto value = p[i];
                if (value == typeIds.guiView) {
                    result.views.push_back(reinterpret_cast<std::uintptr_t>(p + i));
                    result.allocations.insert(r.allocation);
                } else if (value == typeIds.sceneView) {
                    result.sceneViews.push_back(reinterpret_cast<std::uintptr_t>(p + i));
                    result.allocations.insert(r.allocation);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    void GuardedApplyView(std::uintptr_t view, bool narrow)
    {
        __try {
            ApplyView(view, narrow);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    void GuardedApplySceneView(std::uintptr_t view)
    {
        __try {
            ApplySceneView(view);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    DWORD WINAPI SceneThread(void*)
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        if (!FindTypes()) {
            SPDLOG_ERROR("Scene view types not found; fix not applied.");
            return 1;
        }

        std::set<std::uintptr_t> arenas;
        size_t lastViews = 0;
        unsigned refreshTick = 0;
        for (unsigned tick = 0;; ++tick) {
            const bool full = arenas.empty() || tick % 30 == 0;
            ScanResult result;
            for (const auto& r : HeapRegions()) {
                if (full || arenas.count(r.allocation)) ScanRegion(r, result);
            }
            if (full) arenas = result.allocations;
            else arenas.insert(result.allocations.begin(), result.allocations.end());

            const bool narrow = windowAspect.load(std::memory_order_relaxed) < ReferenceAspect - 0.001f;
            for (auto view : result.sceneViews) GuardedApplySceneView(view);
            layerSeen = false;
            for (auto view : result.views) GuardedApplyView(view, narrow);
            if (layerSeen && tick == refreshTick) refreshRequested.store(true);
            if (result.views.size() != lastViews) {
                lastViews = result.views.size();
                refreshTick = tick + 2;
                SPDLOG_INFO("{} GUI views, {} scene views, {} heap arenas.", lastViews, result.sceneViews.size(), arenas.size());
            }
            Sleep(1000);
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

    void UpdateAspect(HWND window)
    {
        RECT client{};
        if (!GetClientRect(window, &client) || client.right <= 0 || client.bottom <= 0) return;
        const float aspect = static_cast<float>(client.right) / static_cast<float>(client.bottom);
        if (std::abs(aspect - windowAspect.exchange(aspect)) > 0.001f) {
            SPDLOG_INFO("Window aspect {:.4f} ({}x{}).", aspect, client.right, client.bottom);
        }
    }

    void RefreshLayout(HWND window)
    {
        RECT client{};
        if (IsIconic(window) || !GetClientRect(window, &client) || client.right <= 0 || client.bottom <= 0) return;
        PostMessageW(window, WM_SIZE, SIZE_RESTORED, MAKELPARAM(client.right - 1, client.bottom));
        Sleep(300);
        PostMessageW(window, WM_SIZE, SIZE_RESTORED, MAKELPARAM(client.right, client.bottom));
        SPDLOG_INFO("Scene layer layout refreshed at {}x{}.", client.right, client.bottom);
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
                "GS456_ARFix",
                std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    (FixPath / sLogFile).string(), 5 * 1024 * 1024, 1));
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::info);
            SPDLOG_INFO("Apollo Justice: Ace Attorney Trilogy Any Aspect Ratio fix initialized.");
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
                       << "[Scene]\n"
                       << "Enabled = true\n"
                       << "BackgroundHeight = 1280\n";
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
            inipp::get_value(ini.sections["Scene"], "Enabled", settings.fixEnabled);
            inipp::get_value(ini.sections["Scene"], "BackgroundHeight", settings.backgroundHeight);
        }
        if (settings.width < 0 || settings.height < 0 || settings.width > 16384 || settings.height > 16384
            || (settings.width == 0) != (settings.height == 0)) {
            SPDLOG_ERROR("Invalid window size {}x{}; using 1280x800.", settings.width, settings.height);
            settings.width = 1280;
            settings.height = 800;
        }
        if (!(settings.backgroundHeight >= ReferenceHeight && settings.backgroundHeight <= 4096.0f)) {
            SPDLOG_ERROR("Invalid BackgroundHeight {}; using 1280.", settings.backgroundHeight);
            settings.backgroundHeight = 1280.0f;
        }
        SPDLOG_INFO("Config: Window={} {}x{} borderless={}, Scene={} BackgroundHeight={}",
            settings.windowEnabled, settings.width, settings.height, settings.borderless,
            settings.fixEnabled, settings.backgroundHeight);
    }

    DWORD WINAPI MainThread(void*)
    {
        if (!InitializeLogging()) return 1;
        LoadConfiguration();
        SPDLOG_INFO("Waiting 15 seconds before applying the fix.");
        Sleep(15000);

        if (settings.fixEnabled) {
            if (HANDLE thread = CreateThread(nullptr, 0, SceneThread, nullptr, 0, nullptr)) CloseHandle(thread);
        }

        for (;;) {
            if (HWND window = FindGameWindow()) {
                if (settings.windowEnabled) ResizeWindow(window);
                UpdateAspect(window);
                if (refreshRequested.exchange(false)) RefreshLayout(window);
            }
            Sleep(250);
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
