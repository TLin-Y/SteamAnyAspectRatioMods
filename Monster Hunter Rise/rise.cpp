#include "stdafx.h"
#include "version.h"
#include "helper.hpp"

#ifdef _DEBUG
    #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#else
    #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#endif

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <safetyhook.hpp>
#include <inipp/inipp.h>
#include <cmath>
#include <cstring>
#include <mutex>
#include <set>
#include <string>

HMODULE exeModule = GetModuleHandle(NULL);
HMODULE thisModule;

std::shared_ptr<spdlog::logger> logger;
std::string sLogFile = sFixName + ".log";
std::filesystem::path ExePath;
std::string sExeName;

inipp::Ini<char> ini;
std::string sConfigFile = sFixName + ".ini";
std::filesystem::path FixPath;

float fTargetWidth = 4.0f;
float fTargetHeight = 3.0f;
float fTargetRatio = 1.3333333f;
bool bFitScreen = true;

template<typename T>
T GetConfigAndLog(inipp::Ini<char>& ini, const std::string& section, const std::string& key, T defaultValue)
{
    T value = defaultValue;
    inipp::get_value(ini.sections[section], key, value);
    SPDLOG_INFO("Config Parse: [{}]: {} = {}", section, key, value);
    return value;
}

void Logging()
{
    WCHAR dllPath[_MAX_PATH] = {0};
    GetModuleFileNameW(thisModule, dllPath, MAX_PATH);
    FixPath = dllPath;
    FixPath = FixPath.remove_filename();

    WCHAR gameExePath[_MAX_PATH] = {0};
    GetModuleFileNameW(exeModule, gameExePath, MAX_PATH);
    ExePath = gameExePath;
    sExeName = ExePath.filename().string();
    ExePath = ExePath.remove_filename();

    try
    {
        std::ofstream file(ExePath / sLogFile, std::ios::trunc);
        if (file.is_open()) file.close();

        logger = std::make_shared<spdlog::logger>(sFixName, std::make_shared<spdlog::sinks::rotating_file_sink_mt>((ExePath / sLogFile).string(), 10 * 1024 * 1024, 1));
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
        spdlog::flush_on(spdlog::level::debug);
        spdlog::set_level(spdlog::level::debug);

        SPDLOG_INFO("----------");
        SPDLOG_INFO("{:s} v{:s}", sFixName, sFixVersion);
        SPDLOG_INFO("----------");
    }
    catch (const spdlog::spdlog_ex &ex) {}
}

void Configuration()
{
    std::ifstream iniFile(FixPath / sConfigFile);
    if (!iniFile) {
        SPDLOG_WARN("Could not locate config file {}. Using default 4:3 aspect ratio.", sConfigFile);
        return;
    }

    SPDLOG_INFO("Config File: {}", (FixPath / sConfigFile).string());
    ini.parse(iniFile);
    ini.strip_trailing_comments();

    fTargetWidth = GetConfigAndLog(ini, "AspectRatio", "TargetWidth", 4.0f);
    fTargetHeight = GetConfigAndLog(ini, "AspectRatio", "TargetHeight", 3.0f);
    bFitScreen = GetConfigAndLog(ini, "FitScreen", "Enabled", true);

    if (fTargetHeight > 0.001f) {
        fTargetRatio = fTargetWidth / fTargetHeight;
    }

    SPDLOG_INFO("Calculated Target Ratio: {}", fTargetRatio);
    SPDLOG_INFO("----------");
}

void ApplyMHRAspectRatioFix()
{
    std::uint8_t* ARScanResult = Memory::PatternScan(exeModule, "F3 44 0F 5E F6 F3 44 0F 11 70 44");

    if (ARScanResult) {
        SPDLOG_INFO("MHR Aspect Ratio Fix: Found pattern! Hooking...");

        static SafetyHookMid ARMidHook{};
        ARMidHook = safetyhook::create_mid(ARScanResult,
            [](SafetyHookContext& ctx) {
                float width = ctx.xmm14.f32[0];
                float height = ctx.xmm6.f32[0];

                if (height > 0.001f) {
                    float originalRatio = width / height;

                    if (std::fabs(originalRatio - 16.0f / 9.0f) < 0.05f) {
                        ctx.xmm14.f32[0] = height * fTargetRatio;
                    }
                }
            });
    }
    else {
        SPDLOG_ERROR("MHR Aspect Ratio Fix: Pattern scan failed. Check if game updated.");
    }
}

namespace FitScreen
{
    constexpr size_t DisplayTypeOffset = 0x68;
    constexpr size_t OutputSizeOffset = 0x30;
    constexpr size_t NameOffset = 0x10;
    constexpr size_t ParentOffset = 0x30;
    constexpr size_t NextOffset = 0x38;
    constexpr size_t DirtyOffset = 0x4E;
    constexpr size_t FlagsOffset = 0x50;
    constexpr size_t ChildOffset = 0x118;
    constexpr size_t GuiViewOffset = 0x78;
    constexpr size_t GameObjectOffset = 0x10;
    constexpr size_t GameObjectNameOffset = 0x28;
    constexpr std::uint64_t AdjustBit = 1ull << 17;
    constexpr int AnchorShift = 38;
    constexpr std::uint64_t AnchorMask = 0xFull << AnchorShift;

    enum Anchor : std::uint64_t { LeftTop = 0, LeftBottom = 2, CenterTop = 3, CenterCenter = 4, CenterBottom = 5 };

    std::uintptr_t sceneViewType = 0;
    std::uintptr_t guiViewType = 0;
    std::set<std::uintptr_t> arenas;
    std::set<std::string> logged;

    template <typename T>
    T& At(std::uintptr_t base, size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    bool Pointer(std::uintptr_t p)
    {
        return p > 0x10000 && p < 0x7FFFFFFF0000ull && !(p & 7);
    }

    void LogOnce(const char* text)
    {
        if (logged.insert(text).second) SPDLOG_INFO("{}", text);
    }

    bool ReadTypes(std::uintptr_t tdb)
    {
        const auto version = At<std::uint32_t>(tdb, 4);
        if (version < 69 || version > 90) return false;
        const auto numTypes = At<std::uint32_t>(tdb, version <= 71 ? 0xC : 0x8);
        if (!numTypes || numTypes >= (1u << 19)) return false;
        const auto types = At<std::uintptr_t>(tdb, 0x60);
        const auto impls = At<std::uintptr_t>(tdb, 0x68);
        const auto pool = At<std::uintptr_t>(tdb, 0xD0);
        const auto bytes = At<std::uintptr_t>(tdb, 0xD8);
        if (!types || !impls || !pool || bytes <= pool) return false;

        std::uintptr_t scene = 0, gui = 0;
        for (std::uint32_t i = 0; i < numTypes; ++i) {
            const auto record = types + i * 0x48ull;
            const auto impl = impls + ((At<std::uint64_t>(record, 8) >> 38) & 0x3FFFF) * 0x30;
            const auto name = At<std::int32_t>(impl, 0);
            const auto ns = At<std::int32_t>(impl, 4);
            if (name < 0 || ns < 0) continue;
            const char* n = reinterpret_cast<const char*>(pool + name);
            const char* s = reinterpret_cast<const char*>(pool + ns);
            if (!std::strcmp(n, "SceneView") && !std::strcmp(s, "via")) scene = At<std::uintptr_t>(record, 0x40);
            else if (!std::strcmp(n, "View") && !std::strcmp(s, "via.gui")) gui = At<std::uintptr_t>(record, 0x40);
        }
        if (!scene || !gui) return false;
        sceneViewType = scene;
        guiViewType = gui;
        return true;
    }

    bool ScanImageRegion(std::uintptr_t start, size_t size)
    {
        __try {
            for (size_t i = 0; i + 0x100 <= size; i += 4) {
                if (At<std::uint32_t>(start, i) == 0x00424454u && ReadTypes(start + i)) return true;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        return false;
    }

    bool FindTypes()
    {
        const auto base = reinterpret_cast<std::uintptr_t>(exeModule);
        const auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + reinterpret_cast<PIMAGE_DOS_HEADER>(base)->e_lfanew);
        const auto end = base + nt->OptionalHeader.SizeOfImage;
        for (int attempt = 0; attempt < 150; ++attempt) {
            MEMORY_BASIC_INFORMATION mbi{};
            for (auto addr = base; addr < end; addr = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
                if (!VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi))) break;
                if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) continue;
                const auto start = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
                if (ScanImageRegion(start, std::min<size_t>(mbi.RegionSize, end - start))) return true;
            }
            Sleep(2000);
        }
        return false;
    }

    void ScanHeapRegion(std::uintptr_t start, size_t size, std::vector<std::uintptr_t>& scenes, std::vector<std::uintptr_t>& guis)
    {
        __try {
            const auto* p = reinterpret_cast<const std::uintptr_t*>(start);
            for (size_t i = 0, n = size / sizeof(std::uintptr_t); i < n; ++i) {
                if (p[i] == sceneViewType) scenes.push_back(reinterpret_cast<std::uintptr_t>(p + i));
                else if (p[i] == guiViewType) guis.push_back(reinterpret_cast<std::uintptr_t>(p + i));
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    size_t scannedBytes = 0;

    void Scan(bool full, std::vector<std::uintptr_t>& scenes, std::vector<std::uintptr_t>& guis)
    {
        std::set<std::uintptr_t> found;
        MEMORY_BASIC_INFORMATION mbi{};
        for (std::uintptr_t addr = 0x10000; addr < 0x7FFFFFFF0000ull; addr = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
            if (!VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi))) break;
            if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE || mbi.Protect != PAGE_READWRITE) continue;
            const auto allocation = reinterpret_cast<std::uintptr_t>(mbi.AllocationBase);
            if (!full && !arenas.count(allocation)) continue;
            const size_t before = scenes.size() + guis.size();
            ScanHeapRegion(reinterpret_cast<std::uintptr_t>(mbi.BaseAddress), mbi.RegionSize, scenes, guis);
            scannedBytes += mbi.RegionSize;
            if (scenes.size() + guis.size() != before) found.insert(allocation);
        }
        if (full) arenas = found;
        else arenas.insert(found.begin(), found.end());
    }

    int FixSceneView(std::uintptr_t view)
    {
        __try {
            auto& displayType = At<std::int32_t>(view, DisplayTypeOffset);
            const float* size = &At<float>(view, OutputSizeOffset);
            if (displayType <= 0 || displayType > 0x20) return 0;
            if (!(size[0] > 0.0f && size[0] < 65536.0f && size[1] > 0.0f && size[1] < 65536.0f)) return 0;
            const int old = displayType;
            displayType = 0;
            return old;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        return 0;
    }

    bool SetAnchor(std::uintptr_t object, Anchor from, Anchor to)
    {
        auto& flags = At<std::uint64_t>(object, FlagsOffset);
        if (!(flags & AdjustBit) || ((flags & AnchorMask) >> AnchorShift) != from) return false;
        flags = (flags & ~AnchorMask) | (static_cast<std::uint64_t>(to) << AnchorShift);
        At<std::uint16_t>(object, DirtyOffset) |= 1;
        return true;
    }

    bool ViewName(std::uintptr_t view, char* out, size_t size)
    {
        __try {
            if (At<std::uintptr_t>(view, ParentOffset) != 0) return false;
            std::uintptr_t gui = 0;
            for (size_t offset = 0x10; offset <= 0x40; offset += 8) {
                const auto candidate = At<std::uintptr_t>(view, offset);
                if (Pointer(candidate) && At<std::uintptr_t>(candidate, GuiViewOffset) == view) {
                    gui = candidate;
                    break;
                }
            }
            if (!gui) return false;
            const auto gameObject = At<std::uintptr_t>(gui, GameObjectOffset);
            if (!Pointer(gameObject)) return false;
            const auto text = At<std::uintptr_t>(gameObject, GameObjectNameOffset);
            if (!Pointer(text)) return false;
            const int length = At<int>(text, 0x10);
            if (length <= 0 || static_cast<size_t>(length) >= size) return false;
            for (int i = 0; i < length; ++i) {
                const wchar_t c = At<wchar_t>(text, 0x14 + i * 2);
                out[i] = c < 128 ? static_cast<char>(c) : '?';
            }
            out[length] = 0;
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        return false;
    }

    int FixGuiView(std::uintptr_t view, const char* name)
    {
        __try {
            if (!std::strcmp(name, "LoadingGui")) return 0;
            if (!std::strncmp(name, "hud_mapwindow", 13)) return SetAnchor(view, LeftTop, LeftBottom) ? 1 : 0;
            if (!std::strcmp(name, "hud_itemactionslider")) {
                int changed = SetAnchor(view, CenterTop, CenterBottom) ? 1 : 0;
                auto child = At<std::uintptr_t>(view, ChildOffset);
                for (int n = 0; Pointer(child) && At<std::uintptr_t>(child, ParentOffset) == view && n < 64; ++n) {
                    if (SetAnchor(child, CenterTop, CenterBottom)) ++changed;
                    child = At<std::uintptr_t>(child, NextOffset);
                }
                return changed;
            }
            return SetAnchor(view, CenterTop, CenterCenter) ? 1 : 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        return 0;
    }

    struct NamedView
    {
        std::uintptr_t view;
        std::string name;
    };

    std::mutex knownMutex;
    std::vector<NamedView> known;

    bool IsGuiView(std::uintptr_t view)
    {
        __try {
            return At<std::uintptr_t>(view, 0) == guiViewType;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        return false;
    }

    DWORD WINAPI FixerThread(void*)
    {
        std::vector<NamedView> views;
        for (;;) {
            {
                std::lock_guard lock(knownMutex);
                views = known;
            }
            for (const auto& entry : views) {
                if (IsGuiView(entry.view)) FixGuiView(entry.view, entry.name.c_str());
            }
            Sleep(8);
        }
    }

    DWORD WINAPI Thread(void*)
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        if (!FindTypes()) {
            SPDLOG_ERROR("Fit Screen: scene view types not found.");
            return 1;
        }
        SPDLOG_INFO("Fit Screen: scene view types found.");
        if (HANDLE fixer = CreateThread(NULL, 0, FixerThread, 0, NULL, 0)) {
            CloseHandle(fixer);
        }

        for (unsigned tick = 0;; ++tick) {
            std::vector<std::uintptr_t> scenes, guis;
            const bool full = arenas.empty() || tick % 100 == 0;
            const DWORD start = GetTickCount();
            scannedBytes = 0;
            Scan(full, scenes, guis);
            if (full || tick % 100 == 1) {
                SPDLOG_INFO("Fit Screen: {} scan {} MB in {} ms, {} views.", full ? "full" : "arena", scannedBytes >> 20, GetTickCount() - start, guis.size());
            }
            for (auto view : scenes) {
                if (const int old = FixSceneView(view)) {
                    LogOnce(("Fit Screen: scene view display type " + std::to_string(old) + " set to Fit.").c_str());
                }
            }
            std::vector<NamedView> named;
            char name[128];
            for (auto view : guis) {
                if (ViewName(view, name, sizeof(name))) named.push_back({ view, name });
            }
            {
                std::lock_guard lock(knownMutex);
                known = std::move(named);
            }
            Sleep(100);
        }
    }
}

DWORD __stdcall Main(void*)
{
    Logging();
    Configuration();

    if (bFitScreen) {
        if (HANDLE fitHandle = CreateThread(NULL, 0, FitScreen::Thread, 0, NULL, 0)) {
            CloseHandle(fitHandle);
        }
    }

    SPDLOG_INFO("Waiting 15 seconds for DRM initialization...");
    Sleep(15000);

    ApplyMHRAspectRatioFix();
    return true;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        thisModule = hModule;
        if (HANDLE mainHandle = CreateThread(NULL, 0, Main, 0, NULL, 0)) {
            SetThreadPriority(mainHandle, THREAD_PRIORITY_HIGHEST);
            CloseHandle(mainHandle);
        }
        break;
    }
    return TRUE;
}
