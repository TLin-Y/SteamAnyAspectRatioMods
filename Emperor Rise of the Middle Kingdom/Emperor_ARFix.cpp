#include "stdafx.h"
#include "version.h"
#include "helper.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <inipp/inipp.h>
#include <safetyhook.hpp>

namespace
{
    HMODULE exeModule = GetModuleHandleW(nullptr);
    HMODULE thisModule = nullptr;
    std::filesystem::path FixPath;

    struct Settings
    {
        int width = 1280;
        int height = 800;
        bool borderless = true;
    } settings;

    struct Layout
    {
        int width = 1024;
        int height = 768;
        int viewWidthMult = 10;
        int viewHeightMult = 37;
        int fullWidthMult = 17;
        int fullHeightMult = 50;
        int viewWidth = 798;
        int viewBottom = 760;
    } layout;

    int* gScreenWidth = nullptr;
    int* gScreenHeight = nullptr;
    int* gDisplayMode = nullptr;
    int* gFullscreen = nullptr;
    int* gSidebarLeft = nullptr;
    int* gViewBottom = nullptr;
    void* gUiObject = nullptr;
    using GetImageFn = int(__thiscall*)(void*, int);
    using DrawImageFn = void(__cdecl*)(int, int, int, int, int, int);
    GetImageFn GetImage = nullptr;
    DrawImageFn DrawImage = nullptr;
    std::uint8_t* bottomBarResume = nullptr;

    HWND gameWindow = nullptr;
    std::vector<std::string> failures;

    std::vector<safetyhook::MidHook> midHooks;

    constexpr int kMenuBar = 0x27E;
    constexpr int kBottomBorder = 0x2D6;
    constexpr int kSidebarFill = 0x274;
    constexpr int kMenuBarWidth = 798;
    constexpr int kMenuBarStep = 23;
    constexpr int kSidebarWidth = 226;
    constexpr int kSidebarFillHeight = 310;
    constexpr int kSidebarHeight = 768;

    std::uint8_t* Find(const char* name, const char* pattern)
    {
        auto hits = Memory::PatternScanAll(exeModule, pattern);
        if (hits.size() != 1)
        {
            SPDLOG_ERROR("{}: pattern matched {} times", name, hits.size());
            failures.push_back(name);
            return nullptr;
        }
        SPDLOG_INFO("{}: {:x}", name, reinterpret_cast<uintptr_t>(hits.front()));
        return hits.front();
    }

    template <typename T>
    T* ReadAbs(std::uint8_t* at)
    {
        return reinterpret_cast<T*>(*reinterpret_cast<std::uintptr_t*>(at));
    }

    void WriteInt(std::uint8_t* at, int value)
    {
        Memory::Write<int>(at, value);
    }

    void WriteByte(std::uint8_t* at, int value)
    {
        Memory::Write<std::uint8_t>(at, static_cast<std::uint8_t>(value));
    }

    void LoadSettings()
    {
        inipp::Ini<char> ini;
        std::ifstream file(FixPath / (sFixName + ".ini"));
        ini.parse(file);
        ini.strip_trailing_comments();
        inipp::get_value(ini.sections["Window"], "Width", settings.width);
        inipp::get_value(ini.sections["Window"], "Height", settings.height);
        inipp::get_value(ini.sections["Window"], "Borderless", settings.borderless);

        if (settings.width <= 0 || settings.height <= 0)
        {
            settings.width = GetSystemMetrics(SM_CXSCREEN);
            settings.height = GetSystemMetrics(SM_CYSCREEN);
        }
        settings.width = std::clamp(settings.width, 1024, 10380) & ~3;
        settings.height = std::clamp(settings.height, 768, 2560);
        SPDLOG_INFO("Config: {}x{} borderless={}", settings.width, settings.height, settings.borderless);
    }

    void ComputeLayout()
    {
        layout.width = settings.width;
        layout.height = settings.height;
        layout.viewWidthMult = std::min(127, (layout.width - kSidebarWidth + 6) / 80);
        layout.viewHeightMult = std::min(127, (layout.height - 40) / 20 + 1);
        layout.fullWidthMult = std::min(127, (layout.width + 2) / 60);
        layout.fullHeightMult = std::min(127, (layout.height - 40) / 15 + 2);
        layout.viewWidth = layout.viewWidthMult * 80 - 2;
        layout.viewBottom = 40 + (layout.viewHeightMult - 1) * 20;
        SPDLOG_INFO("Layout: view {}x{} (mult {}x{}), full mult {}x{}", layout.viewWidth, layout.viewBottom,
            layout.viewWidthMult, layout.viewHeightMult, layout.fullWidthMult, layout.fullHeightMult);
    }

    void Draw(int id, int x, int y)
    {
        DrawImage(GetImage(gUiObject, id), x, y, 0, 0, 0);
    }

    void DrawBottomBorder()
    {
        const int right = gSidebarLeft ? *gSidebarLeft : layout.viewWidth;
        const int bottom = gViewBottom ? *gViewBottom : layout.viewBottom;
        if (bottom >= layout.height) return;
        for (int x = right - kMenuBarWidth; x > -kMenuBarWidth; x -= kMenuBarWidth)
            Draw(kBottomBorder, x, bottom);
    }

    void DrawFillers()
    {
        const int right = gSidebarLeft ? *gSidebarLeft : layout.viewWidth;
        const int bottom = gViewBottom ? *gViewBottom : layout.viewBottom;

        for (int x = right - kMenuBarWidth; x > -kMenuBarWidth; x -= kMenuBarWidth)
            Draw(kMenuBar, x, 0);

        for (int y = layout.height - 39; y > bottom - 8; y -= kMenuBarStep)
            for (int x = right - kMenuBarWidth; x > -kMenuBarWidth; x -= kMenuBarWidth)
                Draw(kMenuBar, x, y);

        for (int x = right; x < layout.width; x += kSidebarWidth)
            for (int y = (x == right ? kSidebarHeight : 0); y < layout.height; y += kSidebarFillHeight)
                Draw(kSidebarFill, x, y);
    }

    void ForceDisplayMode()
    {
        if (gDisplayMode) *gDisplayMode = 3;
        if (gFullscreen) *gFullscreen = 0;
    }

    void PatchResolution()
    {
        const int W = layout.width;
        const int H = layout.height;

        if (auto p = Find("ModeSetter", "C7 05 ?? ?? ?? ?? 58 02 00 00 C3 83 F8 03 75 14 C7 05 ?? ?? ?? ?? 00 04 00 00 C7 05 ?? ?? ?? ?? 00 03 00 00"))
        {
            gScreenWidth = ReadAbs<int>(p + 18);
            gScreenHeight = ReadAbs<int>(p + 28);
            WriteInt(p + 22, W);
            WriteInt(p + 32, H);
        }
        if (auto p = Find("ModeInit", "89 35 ?? ?? ?? ?? EB 27 83 F8 03 75 12 BE 00 04 00 00 B9 00 03 00 00"))
        {
            WriteInt(p + 14, W);
            WriteInt(p + 19, H);
        }

        struct Site { const char* name; const char* pattern; int offset; int value; };
        const Site sites[] = {
            { "WidthCheck1", "83 C4 10 33 C0 81 F9 00 04 00 00 5F 5E", 7, W },
            { "WidthCheck2", "3D 20 03 00 00 74 3D 3D 00 04 00 00 75 5B 6A 00", 8, W },
            { "WidthCheck3", "A1 ?? ?? ?? ?? 83 C4 08 3D 00 04 00 00 75 04 8B 44 24 2C", 9, W },
            { "WidthCheck4", "A1 ?? ?? ?? ?? 83 C4 08 3D 00 04 00 00 75 04 8B 44 24 08", 9, W },
            { "WidthCheck5", "8B 0D ?? ?? ?? ?? 83 C4 08 81 F9 00 04 00 00 8D 78 FF 75 05", 11, W },
            { "WidthCheck6", "A1 ?? ?? ?? ?? 83 C4 08 3D 00 04 00 00 75 04 8B 44 24 18", 9, W },
            { "SidebarButton1", "8B 0D ?? ?? ?? ?? 33 C0 81 F9 00 04 00 00 89 1D ?? ?? ?? ?? 75 05", 10, W },
            { "SidebarButton2", "90 90 8B 0D ?? ?? ?? ?? 33 C0 81 F9 00 04 00 00 75 05 B8 A7 00 00 00 0F BF 0D", 12, W },
            { "SidebarButton3", "00 90 90 90 90 8B 0D ?? ?? ?? ?? 33 C0 81 F9 00 04 00 00 75 05 B8 A7 00 00 00", 15, W },
            { "SidebarButton4", "C3 90 8B 0D ?? ?? ?? ?? 33 C0 81 F9 00 04 00 00 75 05 B8 A7 00 00 00 56", 12, W },
            { "FullViewWidth", "6A 0D EB 15 3D 00 04 00 00 6A 1E 6A 3A", 5, W },
            { "ViewWidth", "3D 20 03 00 00 74 11 3D 00 04 00 00 75 0A 6A 28", 8, W },
            { "SidebarWidth", "C7 05 ?? ?? ?? ?? A6 02 00 00 C3 3D 00 04 00 00 75 14 C7 05 ?? ?? ?? ?? 1E 03 00 00", 12, W },
            { "TopBarCheck1", "A1 ?? ?? ?? ?? 3D 20 03 00 00 75 0A E8 ?? ?? ?? ?? A1 ?? ?? ?? ?? 3D 00 04 00 00 6A 00 75 12 A1", 23, W },
            { "TopBarCheck2", "A1 ?? ?? ?? ?? 83 C4 08 3D 00 04 00 00 89 7C 24 14 75 07", 9, W },
            { "TopBarCheck3", "EB 0C 3D 20 03 00 00 75 0A E8 ?? ?? ?? ?? A1 ?? ?? ?? ?? 3D 00 04 00 00 6A 00 75 12 A1", 20, W },
            { "CollapsedCheck", "3B C3 0F 85 ?? ?? ?? ?? 81 3D ?? ?? ?? ?? 00 04 00 00 75 29 8B 15", 14, W },
            { "TopTextCheck", "A3 ?? ?? ?? ?? EB 2A 3D 00 04 00 00 75 23 8B 0D", 8, W },
            { "SidebarOffset", "83 C4 10 33 F6 3D 00 04 00 00 75 05 BE A7 00 00 00", 6, W },
            { "TopBarCheck4", "3D 20 03 00 00 74 79 3D 00 04 00 00 0F 85 ?? ?? ?? ?? 6A 00", 8, W },
            { "PanelHeight", "81 C2 80 00 00 00 EB 44 81 F9 00 03 00 00 75 42 81 3D ?? ?? ?? ?? 8C 01 00 00", 10, H },
            { "PanelBottom", "EB 10 C1 E0 04 B9 F8 02 00 00 2B C8 89 0D", 6, layout.viewBottom },
            { "BottomBorderAlt", "83 C4 18 6A 00 6A 00 6A 00 68 F8 02 00 00 6A 00 68 D6 02 00 00 EB 12", 10, layout.viewBottom },
        };
        for (const auto& s : sites)
            if (auto p = Find(s.name, s.pattern)) WriteInt(p + s.offset, s.value);

        if (auto p = Find("ViewMult", "3D ?? ?? ?? ?? 75 0A 6A 28 6A 4E 6A 25 6A 0A EB 08"))
        {
            WriteByte(p + 12, layout.viewHeightMult);
            WriteByte(p + 14, layout.viewWidthMult);
        }
        if (auto p = Find("FullViewMult", "3D ?? ?? ?? ?? 6A 1E 6A 3A 75 06 6A 32 6A 11 EB 04"))
        {
            WriteByte(p + 12, layout.fullHeightMult);
            WriteByte(p + 14, layout.fullWidthMult);
        }
        if (auto p = Find("SidebarLeft", "75 14 C7 05 ?? ?? ?? ?? 1E 03 00 00 C7 05 ?? ?? ?? ?? 96 03 00 00 C3"))
        {
            gSidebarLeft = ReadAbs<int>(p + 4);
            WriteInt(p + 8, layout.viewWidth);
            WriteInt(p + 18, layout.width - 106);
        }
        if (auto p = Find("ViewBottom", "03 D1 8B C7 89 15 ?? ?? ?? ?? 89 3D ?? ?? ?? ?? 99"))
            gViewBottom = ReadAbs<int>(p + 6);
        if (auto p = Find("WindowFit", "A1 ?? ?? ?? ?? 3B C1 7C 6F A1 ?? ?? ?? ?? 85 C0"))
        {
            gFullscreen = ReadAbs<int>(p + 10);
            WriteByte(p + 7, 0xEB);
        }
    }

    void HookModeSelection()
    {
        if (auto p = Find("StartupMode", "7D 06 89 35 ?? ?? ?? ?? A1 ?? ?? ?? ?? 6A 00 A3 ?? ?? ?? ??"))
        {
            gDisplayMode = ReadAbs<int>(p + 4);
            midHooks.push_back(safetyhook::create_mid(p + 8, [](SafetyHookContext&) { ForceDisplayMode(); }));
        }
        if (auto p = Find("DisplayInit", "68 ?? ?? ?? ?? E8 ?? ?? ?? ?? A1 ?? ?? ?? ?? 83 C4 08 83 F8 02"))
            midHooks.push_back(safetyhook::create_mid(p + 10, [](SafetyHookContext&) { ForceDisplayMode(); }));
        if (auto p = Find("ModeSetterEntry", "90 90 8B 44 24 04 83 F8 01 75 15"))
            midHooks.push_back(safetyhook::create_mid(p + 2, [](SafetyHookContext& ctx) {
                *reinterpret_cast<int*>(ctx.esp + 4) = 3;
                ForceDisplayMode();
            }));
        if (auto p = Find("WindowedDepthCheck", "E8 ?? ?? ?? ?? 83 C4 0C A1 ?? ?? ?? ?? 85 C0 75 3A"))
            midHooks.push_back(safetyhook::create_mid(p + 8, [](SafetyHookContext&) { ForceDisplayMode(); }));
        if (auto p = Find("CooperativeLevel", "85 C9 75 4B 8B 0D ?? ?? ?? ?? 85 C9 8B 08"))
            midHooks.push_back(safetyhook::create_mid(p + 4, [](SafetyHookContext&) { ForceDisplayMode(); }));
        if (auto p = Find("WindowStyle", "E8 ?? ?? ?? ?? 83 C4 0C A1 ?? ?? ?? ?? 6A F0 85 C0"))
            midHooks.push_back(safetyhook::create_mid(p + 8, [](SafetyHookContext&) { ForceDisplayMode(); }));
    }

    int clampWidth = 1024;
    int clampHeight = 768;
    int* gPitchBytes = nullptr;
    std::uint8_t** gBackBuffer = nullptr;
    int* gBlitDestExtra = nullptr;

    void ClearOutsideMap()
    {
        if (!gPitchBytes || !gBackBuffer || !*gBackBuffer) return;
        const int pitch = *gPitchBytes;
        std::uint8_t* base = *gBackBuffer;
        for (int y = 0; y < layout.height; ++y)
        {
            const int from = y < clampHeight ? clampWidth * 2 : 0;
            memset(base + static_cast<size_t>(y) * pitch + from, 0, static_cast<size_t>(layout.width * 2 - from));
        }
    }

    int* gMapImageHeight = nullptr;
    int* gMapOriginX = nullptr;
    int* gMapOriginY = nullptr;

    void ClearAround(int x0, int y0, int x1, int y1, int mx, int my, int mw, int mh)
    {
        if (!gPitchBytes || !gBackBuffer || !*gBackBuffer) return;
        const int pitch = *gPitchBytes;
        std::uint8_t* base = *gBackBuffer;
        x1 = std::min(x1, layout.width);
        y1 = std::min(y1, layout.height);
        for (int y = std::max(y0, 0); y < y1; ++y)
        {
            std::uint16_t* row = reinterpret_cast<std::uint16_t*>(base + static_cast<size_t>(y) * pitch);
            if (y < my || y >= my + mh)
            {
                std::fill(row + x0, row + x1, std::uint16_t{ 0 });
                continue;
            }
            if (mx > x0) std::fill(row + x0, row + std::min(mx, x1), std::uint16_t{ 0 });
            if (mx + mw < x1) std::fill(row + std::max(mx + mw, x0), row + x1, std::uint16_t{ 0 });
        }
    }

    void HookWorldMap()
    {
        if (auto p = Find("CityMapHeight", "8D 72 D8 3B C8 89 35 ?? ?? ?? ?? 7C 09 8D 48 FF 89 0D ?? ?? ?? ?? 8B 15"))
        {
            gMapImageHeight = ReadAbs<int>(p + 24);
            midHooks.push_back(safetyhook::create_mid(p + 5, [](SafetyHookContext& ctx) {
                const int visible = static_cast<int>(ctx.esi);
                if (gMapImageHeight && *gMapImageHeight > 0 && visible > *gMapImageHeight)
                    ctx.esi = static_cast<uintptr_t>(*gMapImageHeight);
                if (gMapOriginX && gMapOriginY && gBlitDestExtra)
                {
                    const int areaRight = gSidebarLeft ? *gSidebarLeft : layout.viewWidth;
                    const int areaBottom = gViewBottom ? *gViewBottom : layout.viewBottom;
                    const int width = *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(gBlitDestExtra) + 0x38);
                    const int height = static_cast<int>(ctx.esi);
                    *gMapOriginX = std::max(0, (areaRight - width) / 2);
                    *gMapOriginY = 40 + std::max(0, (areaBottom - 40 - height) / 2);
                }
            }));
        }
        if (auto p = Find("CityMapOrigin", "C7 05 ?? ?? ?? ?? 28 00 00 00 89 3D ?? ?? ?? ?? 89 3D ?? ?? ?? ?? 8D 48 10"))
        {
            gMapOriginY = ReadAbs<int>(p + 2);
            gMapOriginX = ReadAbs<int>(p + 12);
        }
        for (auto* p : Memory::PatternScanAll(exeModule, "8B 15 ?? ?? ?? ?? 03 C2 8D 14 41 52 E8"))
            midHooks.push_back(safetyhook::create_mid(p + 8, [](SafetyHookContext&) {
                if (!gBlitDestExtra) return;
                auto at = [](int off) { return *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(gBlitDestExtra) + off); };
                const int width = at(0x08), rows = at(0x98);
                int oy = at(0xA0), ox = at(0xA4);
                if (oy > 0 && gMapOriginX && gMapOriginY)
                {
                    ox = *gMapOriginX;
                    oy = *gMapOriginY;
                    *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(gBlitDestExtra) + 0xA4) = ox;
                    *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(gBlitDestExtra) + 0xA0) = oy;
                    *gBlitDestExtra = (*gPitchBytes / 2 - width) * 2;
                }
                if (oy > 0)
                {
                    const int right = gSidebarLeft ? *gSidebarLeft + 16 : layout.width;
                    const int bottom = gViewBottom ? *gViewBottom : layout.height;
                    ClearAround(0, 40, right, bottom, ox, oy, width, rows);
                }
                else
                    ClearAround(0, 0, layout.width, layout.height, ox, oy, width, rows);
            }));
        clampWidth = std::min(layout.width, 1024);
        clampHeight = std::min(layout.height, 768);
        auto setups = Memory::PatternScanAll(exeModule, "8B 1D ?? ?? ?? ?? B8 00 04 00 00 2B C3 8B 35 ?? ?? ?? ?? 99");
        auto blits = Memory::PatternScanAll(exeModule, "89 35 ?? ?? ?? ?? 89 35 ?? ?? ?? ?? 8B 85 88 C7 20 00 50 E8");
        auto blitters = Memory::PatternScanAll(exeModule, "A1 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? 0F AF 05 ?? ?? ?? ?? 8B 15");
        if (setups.size() != 2 || blits.size() != 2 || blitters.empty())
        {
            SPDLOG_ERROR("WorldMap: patterns matched {}/{}/{}", setups.size(), blits.size(), blitters.size());
            failures.push_back("WorldMap");
            return;
        }
        gPitchBytes = ReadAbs<int>(blitters[0] + 1);
        gBackBuffer = ReadAbs<std::uint8_t*>(blitters[0] + 7);
        for (auto* b : blitters)
            if (ReadAbs<int>(b + 1) != gPitchBytes || ReadAbs<std::uint8_t*>(b + 7) != gBackBuffer)
            {
                SPDLOG_ERROR("WorldMap: blitters disagree on back buffer globals");
                failures.push_back("WorldMap");
                return;
            }
        for (auto* p : setups)
        {
            WriteInt(p + 2, static_cast<int>(reinterpret_cast<uintptr_t>(&clampWidth)));
            WriteInt(p + 15, static_cast<int>(reinterpret_cast<uintptr_t>(&clampHeight)));
        }
        gBlitDestExtra = ReadAbs<int>(blits[0] + 8);
        for (auto* p : blits)
            midHooks.push_back(safetyhook::create_mid(p + 12, [](SafetyHookContext&) {
                *gBlitDestExtra = (layout.width - clampWidth) * 2;
                ClearOutsideMap();
            }));
        SPDLOG_INFO("WorldMap: clamped to {}x{}", clampWidth, clampHeight);
    }

    void HookUi()
    {
        HookWorldMap();
        if (auto p = Find("MenuBackground", "75 0B C7 84 24 A8 00 00 00 00 00 00 00 81 FB 00 03 00 00 75 09 83 F8 03 75 04 33 ED EB 07 8B AC 24 AC 00 00 00 8B 15"))
            midHooks.push_back(safetyhook::create_mid(p + 37, [](SafetyHookContext& ctx) {
                if (ctx.esi == 1024 && ctx.ebx == 768)
                {
                    *reinterpret_cast<int*>(ctx.esp + 0xA8) = (layout.width - 1024) / 2;
                    ctx.ebp = static_cast<uintptr_t>((layout.height - 768) / 2);
                }
            }));

        auto bottom = Find("BottomBorder", "68 7E 02 00 00 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 50 E8 ?? ?? ?? ?? 83 C4 18 B9 ?? ?? ?? ?? 6A 00 6A 00 6A 00 68 F8 02 00 00");
        auto resume = Find("BottomBorderEnd", "83 C4 18 83 C4 08 C3");
        auto filler = Find("SidebarFill", "75 05 BE 01 00 00 00 8B 0D ?? ?? ?? ?? 6A 00 6A 00");
        if (!bottom || !resume || !filler) return;

        gUiObject = ReadAbs<void>(bottom + 6);
        GetImage = reinterpret_cast<GetImageFn>(Memory::GetRelativeAddr(bottom + 11));
        DrawImage = reinterpret_cast<DrawImageFn>(Memory::GetRelativeAddr(bottom + 17));
        bottomBarResume = resume + 3;
        if (bottomBarResume < bottom || bottomBarResume - bottom > 0x100)
        {
            SPDLOG_ERROR("BottomBorderEnd is not after BottomBorder");
            failures.push_back("BottomBorderEnd");
            return;
        }

        midHooks.push_back(safetyhook::create_mid(bottom + 24, [](SafetyHookContext& ctx) {
            DrawBottomBorder();
            ctx.eip = reinterpret_cast<uintptr_t>(bottomBarResume);
        }));
        midHooks.push_back(safetyhook::create_mid(filler + 7, [](SafetyHookContext&) { DrawFillers(); }));
    }

    bool IsGameWindow(HWND hwnd)
    {
        return hwnd && hwnd == gameWindow;
    }

    constexpr LONG kFrameStyles = WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_BORDER | WS_DLGFRAME;
    constexpr LONG kFrameExStyles = WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE | WS_EX_TOPMOST;
    constexpr UINT kReinitMessage = WM_APP + 0x3A7;

    decltype(&CreateWindowExA) PrevCreateWindowExA = nullptr;
    decltype(&SetWindowLongA) PrevSetWindowLongA = nullptr;
    decltype(&AdjustWindowRectEx) PrevAdjustWindowRectEx = nullptr;
    decltype(&SetWindowPos) PrevSetWindowPos = nullptr;
    decltype(&MoveWindow) PrevMoveWindow = nullptr;
    using DirectDrawCreateFn = HRESULT(WINAPI*)(GUID*, void**, void*);
    DirectDrawCreateFn PrevDirectDrawCreate = nullptr;
    std::vector<safetyhook::InlineHook> inlineHooks;
    void* realSetCooperativeLevel = nullptr;
    WNDPROC originalWndProc = nullptr;
    using ChangeModeFn = void(__cdecl*)(int);
    ChangeModeFn ChangeMode = nullptr;
    std::atomic<bool> reinitPending{ false };
    std::uint8_t* gScreenState = nullptr;
    constexpr std::uint8_t kMovieScreen = 0x4A;

    LONG StyleFor(LONG style)
    {
        if (settings.borderless) return (style & ~kFrameStyles) | WS_POPUP;
        return (style & ~(WS_POPUP | WS_THICKFRAME | WS_MAXIMIZEBOX)) | WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    }

    void FramedSize(int& w, int& h, DWORD style, DWORD exStyle)
    {
        w = layout.width;
        h = layout.height;
        if (settings.borderless) return;
        RECT r{ 0, 0, layout.width, layout.height };
        PrevAdjustWindowRectEx(&r, style, FALSE, exStyle);
        w = r.right - r.left;
        h = r.bottom - r.top;
    }

    void Centre(HMONITOR monitor, int w, int h, int& x, int& y)
    {
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(monitor, &mi);
        x = mi.rcMonitor.left + ((mi.rcMonitor.right - mi.rcMonitor.left) - w) / 2;
        y = mi.rcMonitor.top + ((mi.rcMonitor.bottom - mi.rcMonitor.top) - h) / 2;
    }

    void PlaceWindow(int& x, int& y, int& w, int& h)
    {
        FramedSize(w, h, static_cast<DWORD>(GetWindowLongA(gameWindow, GWL_STYLE)), static_cast<DWORD>(GetWindowLongA(gameWindow, GWL_EXSTYLE)));
        Centre(MonitorFromWindow(gameWindow, MONITOR_DEFAULTTOPRIMARY), w, h, x, y);
    }

    decltype(&SetWindowLongA) TrueSetWindowLongA = nullptr;
    decltype(&SetWindowPos) TrueSetWindowPos = nullptr;

    void ApplyWindow()
    {
        if (!gameWindow) return;
        auto setLong = TrueSetWindowLongA ? TrueSetWindowLongA : PrevSetWindowLongA;
        auto setPos = TrueSetWindowPos ? TrueSetWindowPos : PrevSetWindowPos;
        setLong(gameWindow, GWL_STYLE, StyleFor(GetWindowLongA(gameWindow, GWL_STYLE)) | WS_VISIBLE);
        setLong(gameWindow, GWL_EXSTYLE, GetWindowLongA(gameWindow, GWL_EXSTYLE) & ~kFrameExStyles);
        int x, y, w, h;
        PlaceWindow(x, y, w, h);
        setPos(gameWindow, HWND_NOTOPMOST, x, y, w, h, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        SPDLOG_INFO("Window placed: {}x{} at {},{}", w, h, x, y);
    }

    HWND WINAPI HookCreateWindowExA(DWORD exStyle, LPCSTR cls, LPCSTR name, DWORD style, int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
    {
        const bool main = !gameWindow && !parent && !(style & WS_CHILD) && name && strstr(name, "Emperor");
        if (main)
        {
            exStyle &= ~kFrameExStyles;
            style = static_cast<DWORD>(StyleFor(static_cast<LONG>(style)));
            FramedSize(w, h, style, exStyle);
            Centre(MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), w, h, x, y);
        }
        HWND hwnd = PrevCreateWindowExA(exStyle, cls, name, style, x, y, w, h, parent, menu, inst, param);
        if (main && hwnd)
        {
            gameWindow = hwnd;
            SPDLOG_INFO("Game window {:x} created: {}x{} at {},{}", reinterpret_cast<uintptr_t>(hwnd), w, h, x, y);
        }
        return hwnd;
    }

    LONG WINAPI HookSetWindowLongA(HWND hwnd, int index, LONG value)
    {
        if (IsGameWindow(hwnd))
        {
            if (index == GWL_STYLE) value = StyleFor(value);
            else if (index == GWL_EXSTYLE) value &= ~kFrameExStyles;
            if ((index == GWL_STYLE || index == GWL_EXSTYLE) && TrueSetWindowLongA) return TrueSetWindowLongA(hwnd, index, value);
        }
        return PrevSetWindowLongA(hwnd, index, value);
    }

    BOOL WINAPI HookAdjustWindowRectEx(LPRECT rect, DWORD style, BOOL menu, DWORD exStyle)
    {
        if (settings.borderless && gameWindow && !(style & WS_CHILD)) return TRUE;
        return PrevAdjustWindowRectEx(rect, style, menu, exStyle);
    }

    BOOL WINAPI HookSetWindowPos(HWND hwnd, HWND after, int x, int y, int w, int h, UINT flags)
    {
        if (IsGameWindow(hwnd))
        {
            int px, py, pw, ph;
            PlaceWindow(px, py, pw, ph);
            if (!(flags & SWP_NOMOVE)) { x = px; y = py; }
            if (!(flags & SWP_NOSIZE)) { w = pw; h = ph; }
            if (after == HWND_TOPMOST) after = HWND_NOTOPMOST;
            if (TrueSetWindowPos) return TrueSetWindowPos(hwnd, after, x, y, w, h, flags);
        }
        return PrevSetWindowPos(hwnd, after, x, y, w, h, flags);
    }

    BOOL WINAPI HookMoveWindow(HWND hwnd, int x, int y, int w, int h, BOOL repaint)
    {
        if (IsGameWindow(hwnd)) PlaceWindow(x, y, w, h);
        return PrevMoveWindow(hwnd, x, y, w, h, repaint);
    }

    LONG WINAPI HookChangeDisplaySettings(void*, DWORD)
    {
        SPDLOG_INFO("Display mode change blocked");
        return DISP_CHANGE_SUCCESSFUL;
    }

    LONG WINAPI HookChangeDisplaySettingsEx(void*, void*, HWND, DWORD, void*)
    {
        SPDLOG_INFO("Display mode change blocked");
        return DISP_CHANGE_SUCCESSFUL;
    }

    constexpr DWORD kDdsclFullscreen = 0x01;
    constexpr DWORD kDdsclAllowReboot = 0x02;
    constexpr DWORD kDdsclNormal = 0x08;
    constexpr DWORD kDdsclExclusive = 0x10;
    constexpr DWORD kDdsclAllowModeX = 0x40;

    HRESULT WINAPI HookSetCooperativeLevel(void* self, HWND hwnd, DWORD flags)
    {
        const DWORD windowed = (flags & ~(kDdsclFullscreen | kDdsclExclusive | kDdsclAllowModeX | kDdsclAllowReboot)) | kDdsclNormal;
        SPDLOG_INFO("SetCooperativeLevel 0x{:x} -> 0x{:x}", flags, windowed);
        return reinterpret_cast<HRESULT(WINAPI*)(void*, HWND, DWORD)>(realSetCooperativeLevel)(self, hwnd, windowed);
    }

    HRESULT WINAPI HookSetDisplayMode(void*, DWORD w, DWORD h, DWORD bpp)
    {
        SPDLOG_INFO("SetDisplayMode {}x{}x{} blocked", w, h, bpp);
        return 0;
    }

    void PatchVtable(void** vtable, int index, void* detour, void** original)
    {
        if (vtable[index] == detour) return;
        if (original) *original = vtable[index];
        DWORD old;
        VirtualProtect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
        vtable[index] = detour;
        VirtualProtect(&vtable[index], sizeof(void*), old, &old);
    }

    HRESULT WINAPI HookDirectDrawCreate(GUID* guid, void** out, void* outer)
    {
        HRESULT hr = PrevDirectDrawCreate(guid, out, outer);
        if (SUCCEEDED(hr) && out && *out)
        {
            void** vtable = *reinterpret_cast<void***>(*out);
            PatchVtable(vtable, 20, reinterpret_cast<void*>(&HookSetCooperativeLevel), &realSetCooperativeLevel);
            PatchVtable(vtable, 21, reinterpret_cast<void*>(&HookSetDisplayMode), nullptr);
            SPDLOG_INFO("DirectDraw {:x}: windowed mode enforced", reinterpret_cast<uintptr_t>(*out));
        }
        return hr;
    }

    template <typename T>
    void HookImport(const char* module, const char* function, T detour, T& prev)
    {
        auto* base = reinterpret_cast<std::uint8_t*>(exeModule);
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
        auto* imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
        for (; imports->Name; ++imports)
        {
            if (lstrcmpiA(reinterpret_cast<const char*>(base + imports->Name), module) != 0) continue;
            auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imports->OriginalFirstThunk);
            auto* slots = reinterpret_cast<void**>(base + imports->FirstThunk);
            for (; names->u1.AddressOfData; ++names, ++slots)
            {
                if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
                auto* byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
                if (strcmp(reinterpret_cast<const char*>(byName->Name), function) != 0) continue;
                if (*slots == reinterpret_cast<void*>(detour)) return;
                prev = reinterpret_cast<T>(*slots);
                DWORD old;
                VirtualProtect(slots, sizeof(void*), PAGE_READWRITE, &old);
                *slots = reinterpret_cast<void*>(detour);
                VirtualProtect(slots, sizeof(void*), old, &old);
                SPDLOG_INFO("Hooked import {} (was {:x})", function, reinterpret_cast<uintptr_t>(prev));
                return;
            }
        }
        SPDLOG_ERROR("Import {} not found", function);
        failures.push_back(function);
    }

    void* ExportAddress(const char* module, const char* function)
    {
        auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleA(module));
        if (!base) return nullptr;
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
        const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        auto* exports = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
        auto* names = reinterpret_cast<DWORD*>(base + exports->AddressOfNames);
        auto* ordinals = reinterpret_cast<WORD*>(base + exports->AddressOfNameOrdinals);
        auto* functions = reinterpret_cast<DWORD*>(base + exports->AddressOfFunctions);
        for (DWORD i = 0; i < exports->NumberOfNames; ++i)
        {
            if (strcmp(reinterpret_cast<const char*>(base + names[i]), function) != 0) continue;
            const DWORD rva = functions[ordinals[i]];
            if (rva >= dir.VirtualAddress && rva < dir.VirtualAddress + dir.Size) return nullptr;
            return base + rva;
        }
        return nullptr;
    }

    void BlockExport(const char* module, const char* function, void* detour)
    {
        void* target = ExportAddress(module, function);
        auto hook = target ? safetyhook::create_inline(target, detour) : safetyhook::InlineHook{};
        if (!hook)
        {
            SPDLOG_ERROR("Could not hook {}!{}", module, function);
            failures.push_back(function);
            return;
        }
        inlineHooks.push_back(std::move(hook));
    }

    LRESULT CALLBACK GameWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == kReinitMessage)
        {
            SPDLOG_INFO("Re-initialising display at {}x{}", layout.width, layout.height);
            ForceDisplayMode();
            ChangeMode(3);
            ApplyWindow();
            CallWindowProcA(originalWndProc, hwnd, WM_ACTIVATEAPP, FALSE, 0);
            CallWindowProcA(originalWndProc, hwnd, WM_ACTIVATEAPP, TRUE, 0);
            CallWindowProcA(originalWndProc, hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(layout.width, layout.height));
            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
            reinitPending = false;
            SPDLOG_INFO("Display now {}x{}", gScreenWidth ? *gScreenWidth : -1, gScreenHeight ? *gScreenHeight : -1);
            return 0;
        }
        return CallWindowProcA(originalWndProc, hwnd, msg, wp, lp);
    }

    BOOL CALLBACK FindGameWindow(HWND hwnd, LPARAM out)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        char title[64]{};
        GetWindowTextA(hwnd, title, sizeof(title));
        if (pid == GetCurrentProcessId() && !GetWindow(hwnd, GW_OWNER) && strstr(title, "Emperor"))
        {
            *reinterpret_cast<HWND*>(out) = hwnd;
            return FALSE;
        }
        return TRUE;
    }

    DWORD WINAPI ReinitWatcher(LPVOID)
    {
        for (int i = 0; i < 2400; ++i)
        {
            Sleep(250);
            HWND hwnd = gameWindow;
            if (!hwnd) EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&hwnd));
            if (!hwnd || !gScreenWidth || !gScreenHeight) continue;
            if (!gameWindow)
            {
                gameWindow = hwnd;
                SPDLOG_INFO("Found existing game window {:x}", reinterpret_cast<uintptr_t>(hwnd));
            }
            if (*gScreenWidth == layout.width && *gScreenHeight == layout.height && !reinitPending) return 0;
            if (!ChangeMode || reinitPending) continue;
            if (gScreenState && *gScreenState == kMovieScreen) continue;
            if (!originalWndProc)
                originalWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&GameWndProc)));
            reinitPending = true;
            PostMessageA(hwnd, kReinitMessage, 0, 0);
            SPDLOG_INFO("Display is {}x{}; re-init requested", *gScreenWidth, *gScreenHeight);
        }
        return 0;
    }

    void HookWindow()
    {
        inlineHooks.reserve(8);
        if (auto p = Find("ChangeMode", "A0 ?? ?? ?? ?? 3C 0F 74 ?? 3C 0E 74 ?? 3C 21 74 ?? 3C 05 74 ?? 6A 01 C7 05"))
        {
            ChangeMode = reinterpret_cast<ChangeModeFn>(p);
            gScreenState = ReadAbs<std::uint8_t>(p + 1);
        }
        HookImport("DDRAW.dll", "DirectDrawCreate", &HookDirectDrawCreate, PrevDirectDrawCreate);
        HookImport("USER32.dll", "AdjustWindowRectEx", &HookAdjustWindowRectEx, PrevAdjustWindowRectEx);
        HookImport("USER32.dll", "CreateWindowExA", &HookCreateWindowExA, PrevCreateWindowExA);
        HookImport("USER32.dll", "SetWindowLongA", &HookSetWindowLongA, PrevSetWindowLongA);
        HookImport("USER32.dll", "SetWindowPos", &HookSetWindowPos, PrevSetWindowPos);
        HookImport("USER32.dll", "MoveWindow", &HookMoveWindow, PrevMoveWindow);
        TrueSetWindowLongA = reinterpret_cast<decltype(&SetWindowLongA)>(ExportAddress("user32.dll", "SetWindowLongA"));
        TrueSetWindowPos = reinterpret_cast<decltype(&SetWindowPos)>(ExportAddress("user32.dll", "SetWindowPos"));
        BlockExport("user32.dll", "ChangeDisplaySettingsA", reinterpret_cast<void*>(&HookChangeDisplaySettings));
        BlockExport("user32.dll", "ChangeDisplaySettingsW", reinterpret_cast<void*>(&HookChangeDisplaySettings));
        BlockExport("user32.dll", "ChangeDisplaySettingsExA", reinterpret_cast<void*>(&HookChangeDisplaySettingsEx));
        BlockExport("user32.dll", "ChangeDisplaySettingsExW", reinterpret_cast<void*>(&HookChangeDisplaySettingsEx));
        CreateThread(nullptr, 0, ReinitWatcher, nullptr, 0, nullptr);
    }

    void Init()
    {
        WCHAR path[MAX_PATH]{};
        GetModuleFileNameW(thisModule, path, MAX_PATH);
        FixPath = std::filesystem::path(path).remove_filename();

        auto logger = spdlog::basic_logger_st(sFixName, (FixPath / (sFixName + ".log")).string(), true);
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%H:%M:%S] [%l] %v");
        spdlog::flush_on(spdlog::level::info);
        SPDLOG_INFO("{} v{}", sFixName, sFixVersion);

        SetProcessDPIAware();
        LoadSettings();
        ComputeLayout();
        PatchResolution();
        HookModeSelection();
        HookUi();
        HookWindow();
        ForceDisplayMode();

        if (failures.empty()) SPDLOG_INFO("All patches applied.");
        else SPDLOG_ERROR("{} patch sites not found; this Emperor.exe may not be the GOG v1.0.1 build.", failures.size());
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        thisModule = module;
        DisableThreadLibraryCalls(module);
        Init();
    }
    return TRUE;
}
