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
#error Dynasty Warriors 7 XL is a 32-bit game. Compile this project as x86.
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
    spdlog::set_default_logger(spdlog::basic_logger_mt("DW7XL_ARFix", (modFolder / "DW7XL_ARFix.log").string(), true));
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
    std::ifstream file(modFolder / "DW7XL_ARFix.ini");
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
    auto* modeCheck = FindOne("Display modes", "83 F9 0B 72 D0 32 C0 5B 5D C2 04 00");
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
    bool wholeScreen = Shows(lens, kNativeRatio), splitScreen = Shows(lens, 2.0f * kNativeRatio);
    if (!wholeScreen && !splitScreen) return;
    lens.fov = WidenedFov(lens.fov);
    lens.aspect *= config.Ratio() / kNativeRatio;
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

void FitQuad(SafetyHookContext& ctx) {
    std::span points(*reinterpret_cast<Point**>(ctx.esp), 4);
    if (!IsBackgroundTile(points)) FitPoints(points);
}

struct Rect { int x, y, width, height; };

void FitClip(SafetyHookContext& ctx) {
    auto& rect = *reinterpret_cast<Rect*>(ctx.edi);
    Point corners[]{ { static_cast<float>(rect.x), static_cast<float>(rect.y) },
        { static_cast<float>(rect.x + rect.width), static_cast<float>(rect.y + rect.height) } };
    FitPoints(corners);
    rect.x = std::lround(corners[0].x);
    rect.y = std::lround(corners[0].y);
    rect.width = std::lround(corners[1].x) - rect.x;
    rect.height = std::lround(corners[1].y) - rect.y;
}

SafetyHookMid quads, sprites, scaledSprites, clips;

void Apply() {
    auto* quad = FindOne("Quads", "D8 4F 1C D9 5D F4 E8 ?? ?? ?? ?? 8B 4D F8 5E 33 CD 5B E8");
    auto* sprite = FindOne("Sprites", "D8 4F 0C D9 5D F8 D9 45 F8 D9 5D F0 E8");
    auto* scaledSprite = FindOne("Scaled sprites", "8B 4D AC 52 50 E8 ?? ?? ?? ?? 8B 4D FC 5E 33 CD 5B E8");
    auto* clip = FindOne("Clips", "8D 7C 24 18 89 44 24 24 E8 ?? ?? ?? ?? 8B 4D 08 8B C7 50 E8");
    if (!quad || !sprite || !scaledSprite || !clip) return;
    quads = safetyhook::create_mid(quad + 6, FitQuad);
    sprites = safetyhook::create_mid(sprite + 12, FitQuad);
    scaledSprites = safetyhook::create_mid(scaledSprite + 5, FitQuad);
    clips = safetyhook::create_mid(clip + 18, FitClip);
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

SafetyHookMid buildMatrix, checkBounds;

Point UndoFit(Point at, Point viewport) {
    return { Screen::ScaleFromCenter(viewport.x + at.x, Screen::kCanvas.x, 1.0f / Screen::fit.x) - viewport.x,
        Screen::ScaleFromCenter(viewport.y + at.y, Screen::kCanvas.y, 1.0f / Screen::fit.y) - viewport.y };
}

void UndoFitInMatrix(SafetyHookContext& ctx) {
    auto& camera = *reinterpret_cast<PlayerCamera*>(ctx.esi);
    auto& rows = camera.toViewport.rows;
    auto& center = reinterpret_cast<Point&>(rows[3]);
    rows[0][0] /= Screen::fit.x;
    rows[1][1] /= Screen::fit.y;
    center = UndoFit(center, camera.viewport);
}

void WidenBounds(SafetyHookContext& ctx) {
    auto& camera = *reinterpret_cast<PlayerCamera*>(ctx.eax);
    auto low = UndoFit({}, camera.viewport), high = UndoFit(camera.viewportSize, camera.viewport);
    ctx.xmm0.f32[0] = std::min(low.x, low.y);
    *reinterpret_cast<float*>(ctx.ebp - 0x80) = high.x;
    *reinterpret_cast<float*>(ctx.ebp - 0x78) = high.y;
}

void Apply() {
    auto* matrix = FindOne("World HUD", "8D 86 F4 00 00 00 50 81 C6 D8 01 00 00 56 FF 15");
    auto* bounds = FindOne("World HUD bounds", "0F 2F C2 F2 0F 59 DD 66 0F 5A DB F3 0F 11 55 ?? F3 0F 11 65 ?? F3 0F 11 5D ?? 0F 87 ?? ?? ?? ?? 0F 57 C9 F3 0F 5A CA F3 0F 10 55 80");
    if (!matrix || !bounds) return;
    buildMatrix = safetyhook::create_mid(matrix, UndoFitInMatrix);
    checkBounds = safetyhook::create_mid(bounds, WidenBounds);
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
    auto* code = FindOne("Movies", "8D 4D EC 51 8D 55 DC 52 8B CE E8 ?? ?? ?? ?? 8B 4D FC 5F 5E 33 CD 5B");
    if (!code) return;
    drawMovie = safetyhook::create_mid(code + 10, FitMovie);
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
