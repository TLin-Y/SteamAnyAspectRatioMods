#include "stdafx.h"
#include "version.h"
#include "helper.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <safetyhook.hpp>
#include <inipp/inipp.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <string_view>

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
    spdlog::set_default_logger(spdlog::basic_logger_mt("SWSanada_ARFix", (modFolder / "SWSanada_ARFix.log").string(), true));
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
    std::ifstream file(modFolder / "SWSanada_ARFix.ini");
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

template<typename Detour>
void Replace(SafetyHookInline& original, std::uint8_t* code, Detour detour) {
    original = safetyhook::create_inline(code, detour, safetyhook::InlineHook::StartDisabled);
    (void)original.enable();
}

namespace Resolution {

struct Row { std::uint16_t width, pictureHeight, height; };
struct Mode { std::uint32_t width, height; };

constexpr int kRows = 15;
constexpr std::uint32_t kMostModes = 0x100;

struct ModeList {
    std::uint8_t before[0xB0];
    Mode modes[kMostModes];
    std::uint32_t count;
};
static_assert(offsetof(ModeList, count) == 0x8B0);

SafetyHookInline fillModeList;

void AddCustomMode(ModeList& list) {
    std::span modes(list.modes, list.count);
    auto isCustom = [](const Mode& mode) { return mode.width == config.width && mode.height == config.height; };
    if (list.count >= kMostModes || std::ranges::any_of(modes, isCustom)) return;
    list.modes[list.count++] = { static_cast<std::uint32_t>(config.width), static_cast<std::uint32_t>(config.height) };
}

std::int64_t __fastcall FillModeList(ModeList* list) {
    auto result = fillModeList.call<std::int64_t>(list);
    AddCustomMode(*list);
    return result;
}

void Apply() {
    auto* table = FindOne("Resolution table", "80 02 68 01 E0 01 20 03 C2 01 58 02 00 04 40 02 00 03");
    auto* modeList = FindOne("Display modes", "40 55 53 41 55 41 56 48 8B EC 48 83 EC 68 48 8B D9 45 33 ED 33 D2 44 89 A9 B0 08 00 00");
    if (!table || !modeList) return;
    Row custom{ static_cast<std::uint16_t>(config.width), static_cast<std::uint16_t>(config.height), static_cast<std::uint16_t>(config.height) };
    for (int i = 0; i < kRows; i++) Memory::Write(table + i * sizeof(Row), custom);
    Replace(fillModeList, modeList, &FillModeList);
    SPDLOG_INFO("Resolution: every entry is now {}x{}.", config.width, config.height);
}

}

namespace Fullscreen {

SafetyHookMid pictureSize;

void FitPicture(SafetyHookContext& ctx) {
    auto& widthShare = ctx.xmm3.f32[0];
    auto& pictureHeight = ctx.xmm2.f32[0];
    float screenWidth = static_cast<float>(GetSystemMetrics(SM_CXSCREEN));
    float screenHeight = static_cast<float>(GetSystemMetrics(SM_CYSCREEN));
    float scale = std::min(screenWidth / config.width, screenHeight / config.height);
    widthShare = config.width * scale / screenWidth;
    pictureHeight = config.height * (config.height * scale / screenHeight);
}

void Apply() {
    auto* code = FindOne("Fullscreen", "F3 0F 59 D1 0F 28 BC 24 A0 00 00 00 F3 0F 5C CA");
    if (!code) return;
    pictureSize = safetyhook::create_mid(code + 4, FitPicture);
    SPDLOG_INFO("Fullscreen: fits the picture to the screen.");
}

}

namespace Camera {

constexpr std::uint32_t kOrthographic = 1;
constexpr float kWidestFov = 2.96f;

struct GameCamera {
    std::uint8_t before[0x148];
    float fov;
    float aspect;
    std::uint8_t between[8];
    std::uint32_t flags;
};
static_assert(offsetof(GameCamera, aspect) == 0x14C && offsetof(GameCamera, flags) == 0x158);

SafetyHookInline buildProjection;

bool ShowsWholeScreen(const GameCamera& camera) {
    return !(camera.flags & kOrthographic) && std::abs(camera.aspect - kNativeRatio) < 0.01f;
}

float WidenedFov(float fov) {
    float keepWidth = std::max(1.0f, kNativeRatio / config.Ratio());
    return std::min(2.0f * std::atan(std::tan(fov / 2.0f) * keepWidth) * config.fovMultiplier, kWidestFov);
}

void __fastcall BuildProjection(GameCamera* camera) {
    float gameFov = camera->fov, gameAspect = camera->aspect;
    if (ShowsWholeScreen(*camera)) {
        camera->fov = WidenedFov(gameFov);
        camera->aspect = config.Ratio();
    }
    buildProjection.call<void>(camera);
    camera->fov = gameFov;
    camera->aspect = gameAspect;
}

void Apply() {
    auto* code = FindOne("Camera", "40 53 48 83 EC 50 F6 81 58 01 00 00 01 48 8B D9 0F 84");
    if (!code) return;
    Replace(buildProjection, code, &BuildProjection);
    SPDLOG_INFO("Camera: aspect ratio {:.4f}.", config.Ratio());
}

}

namespace Screen {

struct Point { float x, y; };

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

namespace Rtti {

struct Image {
    std::uint8_t* base = reinterpret_cast<std::uint8_t*>(gameModule);
    std::size_t size = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew)->OptionalHeader.SizeOfImage;

    template<typename T, typename Accept>
    std::uint8_t* Find(T value, Accept accept) const {
        for (auto* at = base; at + sizeof(T) <= base + size; at += 4)
            if (*reinterpret_cast<T*>(at) == value && accept(at)) return at;
        return nullptr;
    }

    std::uint8_t* FindText(std::string_view text) const {
        std::string_view bytes(reinterpret_cast<char*>(base), size);
        auto at = bytes.find({ text.data(), text.size() + 1 });
        return at == bytes.npos ? nullptr : base + at;
    }
};

void** FindVtable(std::string_view className) {
    constexpr std::ptrdiff_t kNameInType = 16, kTypeInLocator = 12, kSelfInLocator = 20;
    Image image;
    auto* name = image.FindText(className);
    auto isLocator = [&](std::uint8_t* at) {
        auto* locator = at - kTypeInLocator;
        return *reinterpret_cast<std::uint32_t*>(locator + kSelfInLocator) == static_cast<std::uint32_t>(locator - image.base);
    };
    auto* typeInLocator = name ? image.Find(static_cast<std::uint32_t>(name - kNameInType - image.base), isLocator) : nullptr;
    auto* locatorInVtable = typeInLocator ? image.Find(typeInLocator - kTypeInLocator, [](std::uint8_t*) { return true; }) : nullptr;
    if (locatorInVtable) return reinterpret_cast<void**>(locatorInVtable + sizeof(void*));
    SPDLOG_ERROR("Class {} not found; not applied.", className);
    return nullptr;
}

}

namespace Hud {

using Screen::Point;

constexpr float kEdgeTolerance = 1.0f;
constexpr float kBackdropShare = 0.2f;
constexpr int kDrawSlot = 2;

struct Span {
    float low, high;
    bool Covers(float length) const { return low <= kEdgeTolerance && high >= length - kEdgeTolerance; }
    bool IsLarge(float length) const { return high - low >= length * kBackdropShare; }
};

Span SpanOf(std::span<Point> points, float Point::* axis) {
    auto [low, high] = std::ranges::minmax(points, {}, axis);
    return { low.*axis, high.*axis };
}

bool IsBackdrop(std::span<Point> points) {
    Span x = SpanOf(points, &Point::x), y = SpanOf(points, &Point::y);
    return (x.Covers(Screen::size.x) && y.IsLarge(Screen::size.y)) || (y.Covers(Screen::size.y) && x.IsLarge(Screen::size.x));
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

using DrawFn = void(__fastcall*)(std::uint8_t* primitive, void* device);

struct PrimitiveClass {
    std::string_view name;
    std::ptrdiff_t pointsOffset;
    std::size_t pointCount;
    void** vtable = nullptr;
    DrawFn draw = nullptr;
};

PrimitiveClass classes[] = {
    { ".?AVC2DTile@@", 0x30, 2 },
    { ".?AVC2DTileG@@", 0x30, 4 },
    { ".?AVC2DSprite@@", 0x30, 2 },
    { ".?AVC2DAnimSprite@@", 0x30, 2 },
    { ".?AVC2DQuadF@@", 0x30, 4 },
    { ".?AVC2DQuadFT@@", 0x30, 4 },
    { ".?AVC2DQuadG@@", 0x40, 4 },
    { ".?AVC2DQuadGT@@", 0x40, 4 },
    { ".?AVC2DScissoring@@", 0x30, 2 },
};

void __fastcall DrawFitted(std::uint8_t* primitive, void* device) {
    auto* vtable = *reinterpret_cast<void***>(primitive);
    auto& type = *std::ranges::find(classes, vtable, &PrimitiveClass::vtable);
    std::span points(reinterpret_cast<Point*>(primitive + type.pointsOffset), type.pointCount);
    std::array<Point, 4> original{};
    std::ranges::copy(points, original.begin());
    FitPoints(points);
    type.draw(primitive, device);
    std::copy_n(original.begin(), type.pointCount, points.begin());
}

bool Apply() {
    for (auto& type : classes) {
        type.vtable = Rtti::FindVtable(type.name);
        if (!type.vtable) return false;
        type.draw = reinterpret_cast<DrawFn>(type.vtable[kDrawSlot]);
    }
    for (auto& type : classes)
        Memory::Write(reinterpret_cast<std::uint8_t*>(type.vtable + kDrawSlot), &DrawFitted);
    SPDLOG_INFO("HUD: kept 16:9.");
    return true;
}

}

namespace Minimap {

struct Rect { Screen::Point corners[2]; };

SafetyHookInline canvasToScreen;
Hud::DrawFn draw = nullptr;
thread_local bool drawing = false;

Rect* __fastcall CanvasToScreen(Rect* screen, const Rect* canvas) {
    auto* result = canvasToScreen.call<Rect*>(screen, canvas);
    if (drawing) Hud::FitPoints(screen->corners);
    return result;
}

void __fastcall Draw(std::uint8_t* primitive, void* device) {
    drawing = true;
    draw(primitive, device);
    drawing = false;
}

void Apply() {
    auto* vtable = Rtti::FindVtable(".?AVC2DMinimapGroupPrim@@");
    auto* code = FindOne("Minimap", "48 89 5C 24 08 57 48 83 EC 20 48 8B DA 48 8B F9 E8 ?? ?? ?? ?? 0F 57 C0 8B C0 F3 48 0F 2A C0 F3 0F 5E 05 ?? ?? ?? ?? F3 0F 59 03 F3 0F 11 07 E8 ?? ?? ?? ?? 0F 57 C0 8B C0 F3 48 0F 2A C0 F3 0F 5E 05 ?? ?? ?? ?? F3 0F 59 43 04 F3 0F 11 47 04 E8");
    if (!vtable || !code) return;
    draw = reinterpret_cast<Hud::DrawFn>(vtable[Hud::kDrawSlot]);
    Replace(canvasToScreen, code, &CanvasToScreen);
    Memory::Write(reinterpret_cast<std::uint8_t*>(vtable + Hud::kDrawSlot), &Draw);
    SPDLOG_INFO("Minimap: kept in proportion.");
}

}

namespace WorldHud {

struct Matrix { float rows[4][4]; };

struct SceneCamera {
    std::uint8_t before[0x100];
    Matrix viewProjection;
};

struct Scene {
    std::uint8_t before[0x210];
    SceneCamera camera;
};

SafetyHookInline drawHud;
Scene** scene = nullptr;

void UndoFit(Matrix& matrix) {
    for (auto& row : matrix.rows) {
        row[0] /= Screen::fit.x;
        row[1] /= Screen::fit.y;
    }
}

void __fastcall DrawHud(void* state) {
    if (!*scene) return drawHud.call<void>(state);
    auto& viewProjection = (*scene)->camera.viewProjection;
    Matrix original = viewProjection;
    UndoFit(viewProjection);
    drawHud.call<void>(state);
    viewProjection = original;
}

void Apply() {
    auto* code = FindOne("World HUD", "48 83 EC 28 48 63 41 68 48 83 B9 18 01 00 00 00 48 8B 4C C1 18 48 8B 01 75 05 FF 50 28");
    auto* sceneRead = FindOne("Scene camera", "48 8B 05 ?? ?? ?? ?? 48 8B 7C 24 20 48 8B 6C 24 38 48 8D 98 10 02 00 00");
    if (!code || !sceneRead) return;
    scene = reinterpret_cast<Scene**>(Memory::GetRelativeAddr(sceneRead + 3));
    Replace(drawHud, code, &DrawHud);
    SPDLOG_INFO("World HUD: kept on target.");
}

}

namespace Movies {

struct Rect { int left, top, right, bottom; };

SafetyHookMid movieRect;

int FitInt(int value, float length, float scale) {
    return static_cast<int>(std::lround(Screen::ScaleFromCenter(static_cast<float>(value), length, scale)));
}

void FitMovie(SafetyHookContext& ctx) {
    auto& rect = *reinterpret_cast<Rect*>(ctx.rdx);
    rect = { FitInt(rect.left, Screen::size.x, Screen::fit.x), FitInt(rect.top, Screen::size.y, Screen::fit.y),
        FitInt(rect.right, Screen::size.x, Screen::fit.x), FitInt(rect.bottom, Screen::size.y, Screen::fit.y) };
}

void Apply() {
    auto* code = FindOne("Movies", "F3 0F 11 74 24 20 44 8B 4C 24 30 4C 8D 44 24 38 48 8D 54 24 48 48 8B CB 89 44 24 54 E8");
    if (!code) return;
    movieRect = safetyhook::create_mid(code + 28, FitMovie);
    SPDLOG_INFO("Movies: kept 16:9.");
}

}

void Apply() {
    if (std::min(config.width, config.height) < kSmallestSize || std::max(config.width, config.height) > kLargestSize) {
        SPDLOG_ERROR("Resolution {}x{} is invalid; nothing applied.", config.width, config.height);
        return;
    }
    Resolution::Apply();
    Fullscreen::Apply();
    Camera::Apply();

    Screen::ChooseFit();
    if (Screen::fit.x == 1.0f && Screen::fit.y == 1.0f) return;
    Movies::Apply();
    if (!Hud::Apply()) return;
    Minimap::Apply();
    WorldHud::Apply();
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        thisModule = module;
        SetProcessDPIAware();
        StartLog();
        LoadConfig();
        Apply();
    }
    return TRUE;
}
