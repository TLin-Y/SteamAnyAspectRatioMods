#include "stdafx.h"
#include "version.h"
#include "helper.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <safetyhook.hpp>
#include <inipp/inipp.h>
#include <d3d9.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>

#if !defined(_M_IX86)
#error Dynasty Warriors 6 is a 32-bit game. Compile this project as x86.
#endif

constexpr float kNativeRatio = 16.0f / 9.0f;
constexpr int kSmallestSize = 240;
constexpr int kLargestSize = 16384;

HMODULE thisModule = nullptr;
HMODULE gameModule = GetModuleHandleW(nullptr);
std::filesystem::path modFolder;

struct Config {
    int width = 0;
    int height = 0;
    float fovMultiplier = 1.0f;
    float Ratio() const { return static_cast<float>(width) / height; }
} config;

void StartLog() {
    WCHAR path[MAX_PATH]{};
    GetModuleFileNameW(thisModule, path, MAX_PATH);
    modFolder = std::filesystem::path(path).parent_path();
    spdlog::set_default_logger(spdlog::basic_logger_mt("DW6_ARFix", (modFolder / "DW6_ARFix.log").string(), true));
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
    spdlog::flush_on(spdlog::level::info);
    SPDLOG_INFO("{} v{}", FIX_NAME, VERSION_STRING);
}

template<typename T>
T Setting(inipp::Ini<char>& ini, const char* section, const char* key, T fallback) {
    T value = fallback;
    inipp::get_value(ini.sections[section], key, value);
    SPDLOG_INFO("[{}] {} = {}", section, key, value);
    return value;
}

void LoadConfig() {
    std::ifstream file(modFolder / "DW6_ARFix.ini");
    inipp::Ini<char> ini;
    ini.parse(file);
    ini.strip_trailing_comments();

    config.width = Setting(ini, "Resolution", "Width", 0);
    config.height = Setting(ini, "Resolution", "Height", 0);
    if (config.width <= 0 || config.height <= 0)
        std::tie(config.width, config.height) = Util::GetPhysicalDesktopDimensions();

    float multiplier = Setting(ini, "FOV", "Multiplier", 1.0f);
    if (multiplier >= 0.5f && multiplier <= 2.0f) config.fovMultiplier = multiplier;
    else SPDLOG_ERROR("FOV Multiplier must be 0.5 to 2; using 1.");
}

std::uint8_t* FindOne(const char* feature, const char* pattern) {
    auto hits = Memory::PatternScanAll(gameModule, pattern);
    if (hits.size() == 1) return hits[0];
    SPDLOG_ERROR("{}: code found {} times (expected 1); not applied.", feature, hits.size());
    return nullptr;
}

namespace Resolution {

constexpr std::uint16_t kReturnTrue = 0x01B0;
constexpr std::array kWidths{ 640, 800, 1280, 1600, 1920, 1024 };
constexpr std::array kHeights{ 480, 600, 720, 960, 1024, 900, 1200, 1080, 768 };
constexpr std::array kPictureHeights{ 360, 450, 720, 900, 1080, 576 };

std::uint8_t* FindChoice(std::uint8_t* from, std::uint8_t* to, int choice) {
    for (auto* at = from; at + sizeof(int) <= to; at++)
        if (*reinterpret_cast<int*>(at) == choice) return at;
    return nullptr;
}

std::vector<std::uint8_t*> FindLists(std::span<const int> choices) {
    constexpr std::size_t kLongestStep = 18;
    auto* base = reinterpret_cast<std::uint8_t*>(gameModule);
    auto& header = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew)->OptionalHeader;
    auto* code = base + header.BaseOfCode;
    auto* end = code + header.SizeOfCode - choices.size() * (kLongestStep + sizeof(int));

    std::vector<std::uint8_t*> found;
    for (auto* start = code; (start = FindChoice(start, end, choices[0])); start++) {
        std::vector<std::uint8_t*> list{ start };
        for (int choice : choices.subspan(1))
            if (auto* next = FindChoice(list.back() + sizeof(int), list.back() + sizeof(int) + kLongestStep, choice)) list.push_back(next);
            else break;
        if (list.size() == choices.size()) found.insert(found.end(), list.begin(), list.end());
    }
    return found;
}

bool Apply() {
    auto* modeCheck = FindOne("Display modes", "83 F8 0B 72 E2 32 C0 5B C2 04 00");
    auto widths = FindLists(kWidths), heights = FindLists(kHeights), pictureHeights = FindLists(kPictureHeights);
    if (!modeCheck || widths.empty() || heights.empty() || pictureHeights.empty()) {
        SPDLOG_ERROR("Resolution: {} widths, {} heights and {} picture heights found; nothing applied.", widths.size(), heights.size(), pictureHeights.size());
        return false;
    }
    for (auto* at : widths) Memory::Write(at, config.width);
    for (auto* at : heights) Memory::Write(at, config.height);
    for (auto* at : pictureHeights) Memory::Write(at, config.height);
    Memory::Write(modeCheck + 5, kReturnTrue);
    SPDLOG_INFO("Resolution: every entry is now {}x{}.", config.width, config.height);
    return true;
}

}

namespace Camera {

constexpr float kWidestFov = 2.96f;

struct Lens { float fov, aspect; };

SafetyHookMid buildPerspective;

bool Shows(const Lens& lens, float ratio) { return std::abs(lens.aspect - ratio) < 0.01f; }

float WidenedFov(float fov) {
    float keepWidth = std::max(1.0f, kNativeRatio / config.Ratio());
    return std::min(2.0f * std::atan(std::tan(fov / 2.0f) * keepWidth) * config.fovMultiplier, kWidestFov);
}

void FitLens(SafetyHookContext& ctx) {
    auto& lens = *reinterpret_cast<Lens*>(ctx.esp + 4);
    bool showsBattle = Shows(lens, config.Ratio()) || Shows(lens, 2.0f * config.Ratio());
    bool showsMenuScene = Shows(lens, kNativeRatio);
    if (!showsBattle && !showsMenuScene) return;
    lens.fov = WidenedFov(lens.fov);
    if (!showsBattle) lens.aspect = config.Ratio();
}

void Apply() {
    auto* code = FindOne("Camera", "D9 86 8C 00 00 00 D9 5C 24 04 D9 86 88 00 00 00 D9 1C 24 56 E8");
    if (!code) return;
    buildPerspective = safetyhook::create_mid(code + 20, FitLens);
    SPDLOG_INFO("Camera: aspect ratio {:.4f}.", config.Ratio());
}

}

namespace Screen {

struct Point { float x, y; };

constexpr Point kCanvas{ 1280.0f, 720.0f };

Point size, fit{ 1.0f, 1.0f };

void ChooseFit() {
    size = { static_cast<float>(config.width), static_cast<float>(config.height) };
    if (config.Ratio() < kNativeRatio) fit.y = config.Ratio() / kNativeRatio;
    else fit.x = kNativeRatio / config.Ratio();
}

struct Target {
    std::uint8_t before[0x18];
    int width, height;
};

struct Renderer {
    std::uint8_t before[8];
    Target* target;
    bool DrawsToScreen() const { return !target || (target->width == config.width && target->height == config.height); }
};

float ScaleFromCenter(float value, float length, float scale) {
    return (value - length / 2) * scale + length / 2;
}

}

namespace Hud {

using Screen::Point;

constexpr float kEdgeTolerance = 1.0f;
constexpr float kBackdropShare = 0.2f;
constexpr float kLargestBackdrop = 1.1f;

struct Span {
    float low, high;
    bool MatchesScreen(float length) const { return low <= kEdgeTolerance && high >= length - kEdgeTolerance && high - low <= length * kLargestBackdrop; }
    bool IsBackdropSide(float length) const { return low <= kEdgeTolerance || high >= length - kEdgeTolerance || high - low >= length * kBackdropShare; }
};

Span SpanOf(std::span<Point> points, float Point::* axis) {
    auto [low, high] = std::ranges::minmax(points, {}, axis);
    return { low.*axis, high.*axis };
}

bool IsBackdrop(std::span<Point> points) {
    Span x = SpanOf(points, &Point::x), y = SpanOf(points, &Point::y);
    return (x.MatchesScreen(Screen::size.x) && y.IsBackdropSide(Screen::size.y)) || (y.MatchesScreen(Screen::size.y) && x.IsBackdropSide(Screen::size.x));
}

bool IsTileSide(Span side, float canvasLength, float screenLength) {
    constexpr float kTile = 256.0f, kTolerance = 0.1f;
    float low = side.low * canvasLength / screenLength, high = side.high * canvasLength / screenLength;
    return std::abs(high - low - kTile) < kTolerance && std::abs(std::remainder(low, kTile)) < kTolerance;
}

bool IsBackgroundTile(std::span<Point> points) {
    return IsTileSide(SpanOf(points, &Point::x), Screen::kCanvas.x, Screen::size.x) && IsTileSide(SpanOf(points, &Point::y), Screen::kCanvas.y, Screen::size.y);
}

float FitPosition(float value, float length, float scale, bool staysOnEdge) {
    bool onEdge = value <= kEdgeTolerance || value >= length - kEdgeTolerance;
    return staysOnEdge && onEdge ? value : Screen::ScaleFromCenter(value, length, scale);
}

void FitPoints(std::span<Point> points) {
    bool backdrop = IsBackdrop(points);
    for (auto& point : points) {
        point.x = FitPosition(point.x, Screen::size.x, Screen::fit.x, backdrop);
        point.y = FitPosition(point.y, Screen::size.y, Screen::fit.y, backdrop);
    }
}

void FitCorners(std::uintptr_t corners, std::uintptr_t renderer) {
    std::span points(reinterpret_cast<Point*>(corners), 4);
    if (reinterpret_cast<Screen::Renderer*>(renderer)->DrawsToScreen() && !IsBackgroundTile(points)) FitPoints(points);
}

void FitSprite(SafetyHookContext& ctx) { FitCorners(ctx.eax, ctx.esi); }

void FitQuad(SafetyHookContext& ctx) { FitCorners(ctx.ecx, ctx.esi); }

void FitLine(SafetyHookContext& ctx) { FitPoints({ reinterpret_cast<Point*>(ctx.esp + 4), 2 }); }

struct Rect { int x, y, width, height; };

void FitClip(SafetyHookContext& ctx) {
    auto& rect = *reinterpret_cast<Rect*>(ctx.esi);
    Point corners[]{ { static_cast<float>(rect.x), static_cast<float>(rect.y) },
        { static_cast<float>(rect.x + rect.width), static_cast<float>(rect.y + rect.height) } };
    FitPoints(corners);
    rect.x = std::lround(corners[0].x);
    rect.y = std::lround(corners[0].y);
    rect.width = std::lround(corners[1].x) - rect.x;
    rect.height = std::lround(corners[1].y) - rect.y;
}

SafetyHookMid sprites, quads, lines, clips;

void Apply() {
    auto* sprite = FindOne("Sprites", "83 EC 30 F3 0F 10 00 D9 44 24 3C 0F 5A C8");
    auto* quad = FindOne("Quads", "83 EC 34 F3 0F 10 01 0F 5A C8");
    auto* line = FindOne("Lines", "8B 86 A0 00 00 00 03 86 A4 00 00 00 57 3D FE 03 00 00 BF 02 00 00 00 7F 08");
    auto* clip = FindOne("Clips", "8D 74 24 18 89 44 24 24 E8 ?? ?? ?? ?? 8B D6 52 8B CB E8");
    if (!sprite || !quad || !line || !clip) return;
    sprites = safetyhook::create_mid(sprite, FitSprite);
    quads = safetyhook::create_mid(quad, FitQuad);
    lines = safetyhook::create_mid(line, FitLine);
    clips = safetyhook::create_mid(clip + 15, FitClip);
    SPDLOG_INFO("HUD: kept 16:9.");
}

}

namespace WorldHud {

using Screen::Point;

struct Matrix { float rows[4][4]; };

struct PlayerCamera {
    std::uint8_t before[0x198];
    Matrix toViewport;
    Matrix worldToViewport;
    Point viewport, viewportSize;
};
static_assert(offsetof(PlayerCamera, worldToViewport) == 0x1D8 && offsetof(PlayerCamera, viewport) == 0x218);

SafetyHookMid buildMatrix, soldierBounds, officerBounds;

Point UndoFit(Point at, Point viewport) {
    return { Screen::ScaleFromCenter(viewport.x + at.x, Screen::size.x, 1.0f / Screen::fit.x) - viewport.x,
        Screen::ScaleFromCenter(viewport.y + at.y, Screen::size.y, 1.0f / Screen::fit.y) - viewport.y };
}

void UndoFitInMatrix(SafetyHookContext& ctx) {
    auto& camera = *reinterpret_cast<PlayerCamera*>(ctx.eax - offsetof(PlayerCamera, toViewport));
    auto& rows = camera.toViewport.rows;
    auto& center = reinterpret_cast<Point&>(rows[3]);
    rows[0][0] /= Screen::fit.x;
    rows[1][1] /= Screen::fit.y;
    center = UndoFit(center, camera.viewport);
}

void WidenBounds(SafetyHookContext& ctx) {
    auto& camera = **reinterpret_cast<PlayerCamera**>(ctx.esp + 0x18);
    auto low = UndoFit({}, camera.viewport), high = UndoFit(camera.viewportSize, camera.viewport);
    ctx.xmm7.f32[0] = std::min(low.x, low.y);
    *reinterpret_cast<float*>(ctx.esp + 0xAC) = high.x;
    *reinterpret_cast<float*>(ctx.esp + 0xB4) = high.y;
}

void Apply() {
    auto* matrix = FindOne("World HUD", "F3 0F 11 40 10 E8 ?? ?? ?? ?? 83 C4 14 5E 8B E5 5D C3");
    auto* soldier = FindOne("Soldier bounds", "0F 2F FA F3 0F 5A C9 F2 0F 59 E5 F2 0F 59 C1 66 0F 5A DB F2 0F 5A E4 66 0F 5A C0 F3 0F 11 54 24 78");
    auto* officer = FindOne("Officer bounds", "0F 2F F9 F3 0F 5A DB F2 0F 59 D4 F2 0F 59 D8 F2 0F 5A D2 F2 0F 5A C3 0F 87 ?? ?? ?? ?? F3 0F 5A 9C 24 AC 00 00 00");
    if (!matrix || !soldier || !officer) return;
    buildMatrix = safetyhook::create_mid(matrix, UndoFitInMatrix);
    soldierBounds = safetyhook::create_mid(soldier, WidenBounds);
    officerBounds = safetyhook::create_mid(officer, WidenBounds);
    SPDLOG_INFO("World HUD: kept on target.");
}

}

namespace Movies {

struct Pixel { int x, y; };

SafetyHookMid drawMovie;

void FitMovie(SafetyHookContext& ctx) {
    for (auto& corner : std::span(reinterpret_cast<Pixel*>(ctx.edx), 2)) {
        corner.x = std::lround(Screen::ScaleFromCenter(static_cast<float>(corner.x), Screen::size.x, Screen::fit.x));
        corner.y = std::lround(Screen::ScaleFromCenter(static_cast<float>(corner.y), Screen::size.y, Screen::fit.y));
    }
}

void Apply() {
    auto* code = FindOne("Movies", "8D 4C 24 18 51 8D 54 24 2C 52 8B CE E8 ?? ?? ?? ?? 5F 5E 5B 8B E5 5D C2 0C 00");
    if (!code) return;
    drawMovie = safetyhook::create_mid(code + 12, FitMovie);
    SPDLOG_INFO("Movies: kept 16:9.");
}

}

void ApplyToGame() {
    if (!Resolution::Apply()) return;
    Camera::Apply();
    Screen::ChooseFit();
    if (Screen::fit.x == 1.0f && Screen::fit.y == 1.0f) return;
    Movies::Apply();
    Hud::Apply();
    WorldHud::Apply();
}

namespace Device {

constexpr std::size_t kCreateDeviceSlot = 16;
constexpr std::size_t kResetSlot = 16;

using ResetFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

SafetyHookInline direct3DCreate, createDevice;
ResetFn reset = nullptr;

void** Method(void* object, std::size_t slot) { return *reinterpret_cast<void***>(object) + slot; }

void FillScreen(HWND window) {
    MONITORINFO monitor{ sizeof(monitor) };
    GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &monitor);
    auto& area = monitor.rcMonitor;
    int areaWidth = area.right - area.left, areaHeight = area.bottom - area.top;
    float scale = std::min(static_cast<float>(areaWidth) / config.width, static_cast<float>(areaHeight) / config.height);
    int width = std::lround(config.width * scale), height = std::lround(config.height * scale);
    SetWindowLongW(window, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowLongW(window, GWL_EXSTYLE, 0);
    SetWindowPos(window, HWND_TOP, area.left + (areaWidth - width) / 2, area.top + (areaHeight - height) / 2, width, height, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

void MakeBorderless(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS& present) {
    D3DDEVICE_CREATION_PARAMETERS creation{};
    device->GetCreationParameters(&creation);
    FillScreen(creation.hFocusWindow);
    present.Windowed = TRUE;
    present.FullScreen_RefreshRateInHz = 0;
    SPDLOG_INFO("Fullscreen: borderless window.");
}

HRESULT STDMETHODCALLTYPE Reset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* present) {
    if (!present->Windowed) MakeBorderless(device, *present);
    return reset(device, present);
}

void ReplaceReset(IDirect3DDevice9* device) {
    auto* method = reinterpret_cast<ResetFn*>(Method(device, kResetSlot));
    ResetFn replacement = &Reset;
    if (*method == replacement) return;
    reset = *method;
    Memory::PatchBytes(reinterpret_cast<std::uint8_t*>(method), reinterpret_cast<const char*>(&replacement), sizeof(replacement));
}

HRESULT STDMETHODCALLTYPE CreateDevice(IDirect3D9* direct3D, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags, D3DPRESENT_PARAMETERS* present, IDirect3DDevice9** device) {
    HRESULT result = createDevice.stdcall<HRESULT>(direct3D, adapter, type, focus, flags, present, device);
    if (SUCCEEDED(result)) ReplaceReset(*device);
    return result;
}

IDirect3D9* WINAPI Direct3DCreate(UINT version) {
    static bool applied = (ApplyToGame(), true);
    auto* direct3D = direct3DCreate.stdcall<IDirect3D9*>(version);
    if (direct3D && !createDevice) createDevice = safetyhook::create_inline(*Method(direct3D, kCreateDeviceSlot), &CreateDevice);
    return direct3D;
}

void Apply() {
    auto* code = GetProcAddress(LoadLibraryW(L"d3d9.dll"), "Direct3DCreate9");
    direct3DCreate = safetyhook::create_inline(reinterpret_cast<void*>(code), &Direct3DCreate);
}

}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        thisModule = module;
        SetProcessDPIAware();
        StartLog();
        LoadConfig();
        if (std::min(config.width, config.height) < kSmallestSize || std::max(config.width, config.height) > kLargestSize)
            SPDLOG_ERROR("Resolution {}x{} is invalid; nothing applied.", config.width, config.height);
        else
            Device::Apply();
    }
    return TRUE;
}
