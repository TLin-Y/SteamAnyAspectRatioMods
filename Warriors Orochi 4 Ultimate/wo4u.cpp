#include "stdafx.h"
#include "version.h"
#include "helper.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <safetyhook.hpp>
#include <inipp/inipp.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

constexpr float kNativeRatio = 16.0f / 9.0f;

HMODULE thisModule = nullptr;
HMODULE gameModule = nullptr;
std::filesystem::path modFolder;

struct Config {
    float ratio = 16.0f / 10.0f;
    float fovScale = 1.0f;
    bool customResolution = true;
    int width = 0;
    int height = 0;
} config;

void StartLog() {
    WCHAR path[MAX_PATH]{};
    GetModuleFileNameW(thisModule, path, MAX_PATH);
    modFolder = std::filesystem::path(path).parent_path();
    spdlog::set_default_logger(spdlog::basic_logger_mt("WO4U_ARFix", (modFolder / "WO4U_ARFix.log").string(), true));
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
    std::ifstream file(modFolder / "WO4U_ARFix.ini");
    inipp::Ini<char> ini;
    ini.parse(file);
    ini.strip_trailing_comments();

    float width = Setting(ini, "AspectRatio", "TargetWidth", 16.0f);
    float height = Setting(ini, "AspectRatio", "TargetHeight", 10.0f);
    if (width > 0.0f && height > 0.0f) config.ratio = width / height;

    float fovScale = Setting(ini, "FOV", "FOVScale", 1.0f);
    if (fovScale > 0.0f && fovScale <= 2.0f) config.fovScale = fovScale;
    else SPDLOG_ERROR("FOVScale must be above 0 and up to 2; using 1.");

    config.customResolution = Setting(ini, "Custom Resolution", "Enabled", true);
    config.width = Setting(ini, "Custom Resolution", "Width", 0);
    config.height = Setting(ini, "Custom Resolution", "Height", 0);
    if (config.width <= 0 || config.height <= 0)
        std::tie(config.width, config.height) = Util::GetPhysicalDesktopDimensions();
}

std::uint8_t* FindOne(const char* feature, const char* pattern) {
    auto hits = Memory::PatternScanAll(gameModule, pattern);
    if (hits.size() == 1) return hits[0];
    SPDLOG_ERROR("{}: code found {} times (expected 1); not applied.", feature, hits.size());
    return nullptr;
}

template<typename Detour>
void Replace(SafetyHookInline& original, std::uint8_t* code, Detour detour) {
    original = safetyhook::create_inline(code, detour, safetyhook::InlineHook::StartDisabled);
    (void)original.enable();
}

namespace Resolution {

constexpr int kLargestSize = 9999;
constexpr int kLabelLength = 11;

int ReplaceAll(const char* pattern, std::ptrdiff_t offset, int size) {
    auto hits = Memory::PatternScanAll(gameModule, pattern);
    for (auto* hit : hits) Memory::Write(hit + offset, size);
    return static_cast<int>(hits.size());
}

int ReplaceWidths() {
    return ReplaceAll("C7 44 24 ?? 00 0A 00 00 C7 44 24 ?? 00 0F 00 00", 12, config.width)
        + ReplaceAll("C7 45 ?? 00 0A 00 00 C7 45 ?? 00 0F 00 00", 10, config.width)
        + ReplaceAll("C7 40 ?? 00 0A 00 00 C7 40 ?? 00 0F 00 00", 10, config.width);
}

int ReplaceHeights() {
    return ReplaceAll("C7 44 24 ?? A0 05 00 00 C7 44 24 ?? 70 08 00 00", 12, config.height)
        + ReplaceAll("C7 45 ?? A0 05 00 00 C7 45 ?? 70 08 00 00", 10, config.height)
        + ReplaceAll("C7 40 ?? A0 05 00 00 C7 40 ?? 70 08 00 00", 10, config.height);
}

void RenameMenuEntry() {
    auto* code = FindOne("Resolution menu entry", "00 33 38 34 30 C3 97 32 31 36 30 00");
    if (!code) return;
    std::string label = std::to_string(config.width) + "\xC3\x97" + std::to_string(config.height);
    label.resize(kLabelLength);
    Memory::PatchBytes(code + 1, label.data(), kLabelLength);
}

void Apply() {
    if (!config.customResolution) return;
    if (std::min(config.width, config.height) <= 0 || std::max(config.width, config.height) > kLargestSize) {
        SPDLOG_ERROR("Custom Resolution: invalid size {}x{}.", config.width, config.height);
        return;
    }
    int widths = ReplaceWidths();
    int heights = ReplaceHeights();
    RenameMenuEntry();
    SPDLOG_INFO("Custom Resolution: 3840x2160 is now {}x{} ({} width, {} height sites).", config.width, config.height, widths, heights);
}

}

namespace Fullscreen {

SafetyHookMid pictureSize;

void FillScreen(SafetyHookContext& ctx) {
    auto& pictureWidth = ctx.xmm4.f32[0];
    auto& pictureHeight = ctx.xmm3.f32[0];
    auto screenWidth = ctx.xmm7.f32[0];
    auto screenHeight = ctx.xmm2.f32[0];
    pictureWidth = screenWidth;
    pictureHeight = screenHeight;
}

void Apply() {
    auto* code = FindOne("Fullscreen", "F3 0F 5D D8 0F 28 E3 F3 0F 59 1D ?? ?? ?? ?? F3 0F 59 25 ?? ?? ?? ?? F3 0F 5C D3 F3 0F 5C FC");
    if (!code) return;
    pictureSize = safetyhook::create_mid(code + 0x17, FillScreen);
    SPDLOG_INFO("Fullscreen: fills the screen.");
}

}

namespace Camera {

constexpr std::uint32_t kOrthographic = 1;

struct GameCamera {
    std::uint8_t before[0x148];
    float fov;
    float aspect;
    std::uint8_t between[8];
    std::uint32_t flags;
};
static_assert(offsetof(GameCamera, aspect) == 0x14C && offsetof(GameCamera, flags) == 0x158);

SafetyHookInline buildProjection;
float viewScale = 1.0f;

bool SameRatio(float a, float b) {
    return std::abs(a - b) < 0.01f;
}

bool ShowsWholeScreen(const GameCamera& camera) {
    float customRatio = static_cast<float>(config.width) / config.height;
    return !(camera.flags & kOrthographic)
        && (SameRatio(camera.aspect, kNativeRatio) || SameRatio(camera.aspect, customRatio) || SameRatio(camera.aspect, config.ratio));
}

void __fastcall BuildProjection(GameCamera* camera) {
    float gameFov = camera->fov;
    if (ShowsWholeScreen(*camera)) {
        camera->aspect = config.ratio;
        camera->fov = 2.0f * std::atan(std::tan(gameFov / 2.0f) * viewScale);
    }
    buildProjection.call<void>(camera);
    camera->fov = gameFov;
}

void Apply() {
    auto* code = FindOne("Camera", "40 53 48 83 EC 70 F6 81 58 01 00 00 01 48 8B D9 0F 29 74 24 60 0F 84");
    auto* aspectRead = FindOne("Camera layout", "F3 0F 5E 93 4C 01 00 00 F3 0F 11 93 C0 00 00 00");
    if (!code || !aspectRead) return;
    viewScale = std::max(1.0f, kNativeRatio / config.ratio) / config.fovScale;
    Replace(buildProjection, code, &BuildProjection);
    SPDLOG_INFO("Camera: aspect ratio {:.4f}, view scale {:.4f}.", config.ratio, viewScale);
}

}

namespace Canvas {

constexpr float kWidth = 1920.0f;
constexpr float kHeight = 1080.0f;

struct Point { float x, y; };

Point fit{ 1.0f, 1.0f };

void ChooseFit() {
    if (config.ratio < kNativeRatio) fit.y = config.ratio / kNativeRatio;
    else fit.x = kNativeRatio / config.ratio;
}

float ScaleFromCenter(float value, float canvas, float scale) {
    return (value - canvas / 2) * scale + canvas / 2;
}

void UndoFit(float& x, float& y) {
    x = ScaleFromCenter(x, kWidth, 1.0f / fit.x);
    y = ScaleFromCenter(y, kHeight, 1.0f / fit.y);
}

}

namespace Hud {

using Canvas::Point;
using Canvas::fit;

constexpr float kEdgeTolerance = 1.0f;
constexpr float kBackdropShare = 0.2f;

struct Quad { Point corners[4]; };

struct Span {
    float low, high;
    bool Covers(float canvas) const { return low <= kEdgeTolerance && high >= canvas - kEdgeTolerance; }
    bool IsLarge(float canvas) const { return high - low >= canvas * kBackdropShare; }
};

SafetyHookInline canvasToScreen, sizeToCanvas;

Span SpanOf(const Quad& quad, float Point::* axis) {
    auto& c = quad.corners;
    auto [low, high] = std::minmax({ c[0].*axis, c[1].*axis, c[2].*axis, c[3].*axis });
    return { low, high };
}

bool IsBackdrop(const Quad& quad) {
    Span x = SpanOf(quad, &Point::x), y = SpanOf(quad, &Point::y);
    return (x.Covers(Canvas::kWidth) && y.IsLarge(Canvas::kHeight)) || (y.Covers(Canvas::kHeight) && x.IsLarge(Canvas::kWidth));
}

float FitPosition(float value, float canvas, float scale, bool staysOnEdge) {
    bool onEdge = value <= kEdgeTolerance || value >= canvas - kEdgeTolerance;
    return staysOnEdge && onEdge ? value : Canvas::ScaleFromCenter(value, canvas, scale);
}

void FitPositions(Quad& quad) {
    bool backdrop = IsBackdrop(quad);
    for (auto& corner : quad.corners) {
        corner.x = FitPosition(corner.x, Canvas::kWidth, fit.x, backdrop);
        corner.y = FitPosition(corner.y, Canvas::kHeight, fit.y, backdrop);
    }
}

void FitSizes(Quad& quad) {
    for (auto& size : quad.corners) {
        size.x *= fit.x;
        size.y *= fit.y;
    }
}

void __fastcall CanvasToScreen(Quad* quad, bool holdsPositions) {
    if (holdsPositions) FitPositions(*quad);
    else FitSizes(*quad);
    canvasToScreen.call<void>(quad, holdsPositions);
}

void __fastcall SizeToCanvas(Point* size) {
    sizeToCanvas.call<void>(size);
    size->x /= fit.x;
    size->y /= fit.y;
}

bool Apply() {
    auto* toScreen = FindOne("HUD", "48 89 5C 24 08 57 48 83 EC 40 0F 29 74 24 30 0F B6 DA 0F 29 7C 24 20 48 8B F9 E8 ?? ?? ?? ?? 8B 48 10 E8");
    auto* toCanvas = FindOne("HUD", "40 53 48 83 EC 30 0F 29 74 24 20 48 8B D9 E8 ?? ?? ?? ?? 8B 48 10 E8 ?? ?? ?? ?? 8B D0 0F 57 F6 F3 48 0F 2A F2 E8");
    if (!toScreen || !toCanvas) return false;
    Replace(canvasToScreen, toScreen, &CanvasToScreen);
    Replace(sizeToCanvas, toCanvas, &SizeToCanvas);
    SPDLOG_INFO("HUD: kept 16:9.");
    return true;
}

}

namespace WorldHud {

using Canvas::Point;
using Context = SafetyHookContext;

struct Position {
    const char* pattern;
    safetyhook::MidHookFn keepOnTarget;
};

struct Size {
    const char* pattern;
    std::ptrdiff_t offset;
    float Point::* axis;
    safetyhook::MidHookFn fit;
};

struct Site {
    std::uint8_t* code;
    safetyhook::MidHookFn detour;
};

void KeepMarkerOnTarget(Context& ctx) {
    Canvas::UndoFit(ctx.xmm12.f32[0], ctx.xmm11.f32[0]);
}

void KeepLabelOnTarget(Context& ctx) {
    auto* position = reinterpret_cast<Point*>(ctx.rsp + 0x78);
    Canvas::UndoFit(position->x, position->y);
}

template<safetyhook::Xmm Context::* scale, float Point::* axis>
void FitSize(Context& ctx) {
    (ctx.*scale).f32[0] *= Canvas::fit.*axis;
}

template<safetyhook::Xmm Context::* scale>
constexpr Size Width(const char* pattern, std::ptrdiff_t offset = 0) {
    return { pattern, offset, &Point::x, FitSize<scale, &Point::x> };
}

template<safetyhook::Xmm Context::* scale>
constexpr Size Height(const char* pattern, std::ptrdiff_t offset = 0) {
    return { pattern, offset, &Point::y, FitSize<scale, &Point::y> };
}

constexpr Position kPositions[] = {
    { "F3 44 0F 11 23 F3 44 0F 11 5B 04 F3 0F 58 DC", KeepMarkerOnTarget },
    { "48 8D 54 24 48 C7 44 24 48 01 00 00 00 48 8D 4D 90 E8", KeepLabelOnTarget },
};

constexpr Size kSizes[] = {
    Width<&Context::xmm6>("F3 0F 5E 35 ?? ?? ?? ?? F3 44 0F 5E 15 ?? ?? ?? ?? 0F 5A C8"),
    Height<&Context::xmm10>("F3 0F 5E 35 ?? ?? ?? ?? F3 44 0F 5E 15 ?? ?? ?? ?? 0F 5A C8", 8),
    Width<&Context::xmm2>("F3 0F 5E 15 ?? ?? ?? ?? F3 48 0F 2A E0 48 8B 45 97"),
    Height<&Context::xmm4>("F3 0F 5E 25 ?? ?? ?? ?? 0F 5A C8 F3 0F 2C D5"),
    Width<&Context::xmm7>("F3 41 0F 5E FA FF 50 30"),
    Height<&Context::xmm8>("F3 44 0F 5E 05 ?? ?? ?? ?? 48 89 45 C0 48 03 CF"),
    Width<&Context::xmm8>("F3 44 0F 5E 05 ?? ?? ?? ?? F3 0F 5E 1D ?? ?? ?? ?? F3 44 0F 59 05"),
    Height<&Context::xmm3>("F3 44 0F 5E 05 ?? ?? ?? ?? F3 0F 5E 1D ?? ?? ?? ?? F3 44 0F 59 05", 9),
    Width<&Context::xmm1>("F3 48 0F 2A C9 F3 0F 5E 0D ?? ?? ?? ?? F3 0F 59 F1 0F 28 C1", 5),
};

std::vector<SafetyHookMid> hooks;

std::vector<Site> FindSites() {
    std::vector<Site> sites;
    for (auto& position : kPositions)
        sites.push_back({ FindOne("World HUD position", position.pattern), position.keepOnTarget });
    for (auto& size : kSizes) {
        if (Canvas::fit.*size.axis == 1.0f) continue;
        auto* code = FindOne("World HUD size", size.pattern);
        sites.push_back({ code ? code + size.offset : nullptr, size.fit });
    }
    return sites;
}

void Apply() {
    auto sites = FindSites();
    if (std::ranges::any_of(sites, [](const Site& site) { return !site.code; })) return;
    for (auto& site : sites) hooks.push_back(safetyhook::create_mid(site.code, site.detour));
    SPDLOG_INFO("World HUD: health bars, names and markers kept in proportion.");
}

}

namespace Movies {

SafetyHookInline drawMovie;

void __fastcall DrawMovie(void* player, std::int64_t top, std::int64_t left, std::int64_t width, std::int64_t height) {
    std::int64_t fittedWidth = std::llround(width * Canvas::fit.x);
    std::int64_t fittedHeight = std::llround(height * Canvas::fit.y);
    drawMovie.call<void>(player, top + (height - fittedHeight) / 2, left + (width - fittedWidth) / 2, fittedWidth, fittedHeight);
}

void Apply() {
    auto* code = FindOne("Movies", "4C 8B DC 49 89 5B 10 49 89 6B 18 49 89 73 20 57 41 56 41 57 48 83 EC 60 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 58 48 8B 59 30");
    if (!code) return;
    Replace(drawMovie, code, &DrawMovie);
    SPDLOG_INFO("Movies: kept 16:9.");
}

}

DWORD WINAPI Main(void*) {
    StartLog();
    LoadConfig();
    while (!(gameModule = GetModuleHandleA("WO4U.dll"))) Sleep(1);

    Resolution::Apply();
    Fullscreen::Apply();
    Camera::Apply();

    if (config.ratio == kNativeRatio) return 0;
    Canvas::ChooseFit();
    Movies::Apply();
    if (Hud::Apply()) WorldHud::Apply();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        thisModule = module;
        SetProcessDPIAware();
        if (HANDLE thread = CreateThread(nullptr, 0, Main, nullptr, 0, nullptr)) CloseHandle(thread);
    }
    return TRUE;
}
