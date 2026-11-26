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
#include <d3d11.h>
#include <atomic>
#include <vector>

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
    spdlog::set_default_logger(spdlog::basic_logger_mt("SW4DX_ARFix", (modFolder / "SW4DX_ARFix.log").string(), true));
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
    std::ifstream file(modFolder / "SW4DX_ARFix.ini");
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

struct ModeListLayout {
    std::int32_t modes = 0;
    std::int32_t count = 0;
    std::uint32_t capacity = 0;
} layout;

SafetyHookInline fillModeList;

void AddCustomMode(std::uint8_t* list) {
    auto* modes = reinterpret_cast<Mode*>(list + layout.modes);
    auto& count = *reinterpret_cast<std::uint32_t*>(list + layout.count);
    auto isCustom = [](const Mode& mode) { return mode.width == config.width && mode.height == config.height; };
    if (count >= layout.capacity || std::ranges::any_of(std::span(modes, count), isCustom)) return;
    modes[count++] = { static_cast<std::uint32_t>(config.width), static_cast<std::uint32_t>(config.height) };
}

std::int64_t __fastcall FillModeList(std::uint8_t* list) {
    auto result = fillModeList.call<std::int64_t>(list);
    AddCustomMode(list);
    return result;
}

bool ReadLayout(std::uint8_t* code) {
    constexpr int kCountAt = 23, kModesAt = 32, kCapacityAt = 38;
    layout = { *reinterpret_cast<std::int32_t*>(code + kModesAt), *reinterpret_cast<std::int32_t*>(code + kCountAt),
        *reinterpret_cast<std::uint32_t*>(code + kCapacityAt) / static_cast<std::uint32_t>(sizeof(Mode)) };
    if (layout.count == layout.modes + static_cast<std::int32_t>(layout.capacity * sizeof(Mode))) return true;
    SPDLOG_ERROR("Display modes: unexpected list layout; not applied.");
    return false;
}

void FillTable() {
    auto* table = FindOne("Resolution table", "80 02 68 01 E0 01 20 03 C2 01 58 02 00 04 40 02 00 03");
    if (!table) return;
    Row custom{ static_cast<std::uint16_t>(config.width), static_cast<std::uint16_t>(config.height), static_cast<std::uint16_t>(config.height) };
    for (int i = 0; i < kRows; i++) Memory::Write(table + i * sizeof(Row), custom);
    SPDLOG_INFO("Resolution: every entry is now {}x{}.", config.width, config.height);
}

void AcceptCustomMode() {
    auto* modeList = FindOne("Display modes", "40 55 53 41 55 41 56 48 8B EC 48 83 EC ?? 48 8B D9 45 33 ED 44 89 A9 ?? ?? ?? ?? 33 D2 48 81 C1 ?? ?? ?? ?? 41 B8 ?? ?? ?? ??");
    if (!modeList || !ReadLayout(modeList)) return;
    Replace(fillModeList, modeList, &FillModeList);
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
    auto* code = FindOne("Camera", "40 53 48 83 EC ?? F6 81 58 01 00 00 01 48 8B D9 0F 84");
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

namespace Image {

constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
constexpr DWORD kExecutable = PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;

std::vector<std::span<std::uint8_t>> Sections(bool executable) {
    auto* base = reinterpret_cast<std::uint8_t*>(gameModule);
    auto size = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew)->OptionalHeader.SizeOfImage;
    std::vector<std::span<std::uint8_t>> found;
    MEMORY_BASIC_INFORMATION region{};
    for (auto* at = base; at < base + size && VirtualQuery(at, &region, sizeof(region)); at += region.RegionSize) {
        bool readable = region.State == MEM_COMMIT && (region.Protect & kReadable) && !(region.Protect & PAGE_GUARD);
        bool runs = region.Protect & kExecutable;
        if (readable && runs == executable)
            found.push_back({ at, std::min<std::size_t>(region.RegionSize, base + size - at) });
    }
    return found;
}

bool InCode(const std::uint8_t* at) {
    static auto code = Sections(true);
    return std::ranges::any_of(code, [&](auto& section) { return at >= section.data() && at + 5 <= section.data() + section.size(); });
}

}

namespace Thunk {

constexpr std::uint8_t kJump = 0xE9;

std::uint8_t* JumpTarget(std::uint8_t* code) {
    __try {
        return *code == kJump ? code + 5 + *reinterpret_cast<std::int32_t*>(code + 1) : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

std::uint8_t* Follow(std::uint8_t* code) {
    while (Image::InCode(code))
        if (auto* target = JumpTarget(code)) code = target;
        else break;
    return code;
}

}

namespace Vtable {

constexpr int kDrawSlot = 2;

void** WithDraw(const char* feature, const char* pattern) {
    auto* draw = FindOne(feature, pattern);
    if (!draw) return nullptr;
    auto reaches = [&](void* slot) {
        auto* at = static_cast<std::uint8_t*>(slot);
        return Image::InCode(at) && Thunk::Follow(at) == draw;
    };
    std::vector<void**> users;
    for (auto& section : Image::Sections(false)) {
        std::span slots(reinterpret_cast<void**>(section.data()), section.size() / sizeof(void*));
        for (auto& slot : slots)
            if (reaches(slot)) users.push_back(&slot);
    }
    if (users.size() == 1) return users[0] - kDrawSlot;
    SPDLOG_ERROR("{}: used by {} vtables (expected 1); not applied.", feature, users.size());
    return nullptr;
}

}

namespace Pictures {

D3D11_TEXTURE2D_DESC Describe(ID3D11ShaderResourceView* view) {
    D3D11_TEXTURE2D_DESC desc{};
    ID3D11Resource* resource = nullptr;
    view->GetResource(&resource);
    ID3D11Texture2D* image = nullptr;
    if (resource && SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&image)))) {
        image->GetDesc(&desc);
        image->Release();
    }
    if (resource) resource->Release();
    return desc;
}

bool IsImage(const D3D11_TEXTURE2D_DESC& desc) {
    bool rgba = desc.Format >= DXGI_FORMAT_R8G8B8A8_TYPELESS && desc.Format <= DXGI_FORMAT_R8G8B8A8_SINT;
    bool bgra = desc.Format >= DXGI_FORMAT_B8G8R8A8_UNORM && desc.Format <= DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
    return (rgba || bgra) && !(desc.BindFlags & D3D11_BIND_RENDER_TARGET);
}

}

namespace Hud {

using Screen::Point;

constexpr float kEdgeTolerance = 1.0f;
constexpr float kBackdropShare = 0.2f;

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

struct TextureLayout {
    std::int32_t firstView = 0;
    std::int32_t secondView = 0;
    std::int32_t flags = 0;
    std::uint16_t usesSecondView = 0;
} textureLayout;

template<typename T>
T Field(const void* object, std::int32_t offset) {
    return *reinterpret_cast<const T*>(static_cast<const std::uint8_t*>(object) + offset);
}

struct Texture {
    ID3D11ShaderResourceView* View() const {
        bool second = Field<std::uint16_t>(this, textureLayout.flags) & textureLayout.usesSecondView;
        return Field<ID3D11ShaderResourceView*>(this, second ? textureLayout.secondView : textureLayout.firstView);
    }
};

bool ReadTextureLayout(std::uint8_t* bind) {
    constexpr int kMaskAt = 10, kFlagsAt = 18, kSecondViewAt = 20, kFirstViewAt = 28;
    textureLayout = { Field<std::int32_t>(bind, kFirstViewAt), Field<std::int32_t>(bind, kSecondViewAt),
        Field<std::uint8_t>(bind, kFlagsAt), static_cast<std::uint16_t>(Field<std::uint32_t>(bind, kMaskAt)) };
    if (textureLayout.secondView == textureLayout.firstView + 8 && textureLayout.usesSecondView) return true;
    SPDLOG_ERROR("HUD: unexpected texture layout; not applied.");
    return false;
}

bool ShowsPicture(const Texture* texture) {
    constexpr UINT kSmallestPicture = 640;
    if (!texture || !texture->View()) return false;
    auto desc = Pictures::Describe(texture->View());
    return desc.Width >= kSmallestPicture && Pictures::IsImage(desc);
}

void FitPoints(std::span<Point> points, bool backdrop) {
    for (auto& point : points) {
        point.x = FitPosition(point.x, Screen::size.x, Screen::fit.x, backdrop);
        point.y = FitPosition(point.y, Screen::size.y, Screen::fit.y, backdrop);
    }
}

void FitPoints(std::span<Point> points) {
    FitPoints(points, IsBackdrop(points));
}

using DrawFn = void(__fastcall*)(std::uint8_t* primitive, void* device);

struct PrimitiveClass {
    const char* name;
    const char* draw;
    std::ptrdiff_t pointsOffset;
    std::size_t pointCount;
    std::ptrdiff_t textureOffset = 0;
    void** vtable = nullptr;
    DrawFn original = nullptr;
};

PrimitiveClass classes[] = {
    { "C2DTile", "48 83 EC 58 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 40 F3 0F 10 41 3C", 0x28, 2 },
    { "C2DTileG", "48 83 EC 68 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 50 F3 0F 10 41 58 4C 8D 49 48", 0x28, 4 },
    { "C2DSprite", "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30 48 8B 59 58 48 8B F2 48 39", 0x28, 2, 0x58 },
    { "C2DAnimSprite", "48 89 5C 24 18 55 56 57 48 8D 6C 24 B9 48 81 EC A0 00 00 00 0F 29 B4 24 90 00 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 27 48 8B 79 58", 0x28, 2, 0x58 },
    { "C2DQuadF", "48 83 EC 68 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 50 F3 0F 10 41 4C", 0x28, 4 },
    { "C2DQuadFT", "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30 48 8B 59 78 48 8B F2 48 39", 0x28, 4, 0x78 },
    { "C2DQuadG", "48 83 EC 68 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 50 F3 0F 10 41 58 4C 8D 49 28", 0x38, 4 },
    { "C2DQuadGT", "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30 48 8B 99 88 00 00 00 48 8B", 0x38, 4, 0x88 },
    { "C2DScissoring", "48 83 EC 48 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 30 80 BA D9 01 00", 0x28, 2 },
};

void __fastcall DrawFitted(std::uint8_t* primitive, void* device) {
    auto* vtable = *reinterpret_cast<void***>(primitive);
    auto& type = *std::ranges::find(classes, vtable, &PrimitiveClass::vtable);
    std::span points(reinterpret_cast<Point*>(primitive + type.pointsOffset), type.pointCount);
    std::array<Point, 4> original{};
    std::ranges::copy(points, original.begin());
    auto* texture = type.textureOffset ? Field<Texture*>(primitive, static_cast<std::int32_t>(type.textureOffset)) : nullptr;
    FitPoints(points, IsBackdrop(points) && !ShowsPicture(texture));
    type.original(primitive, device);
    std::copy_n(original.begin(), type.pointCount, points.begin());
}

bool Apply() {
    auto* bind = FindOne("HUD texture views", "4D 85 C9 74 ?? 4D 8B 59 ?? B8 ?? ?? 00 00 66 41 85 41 ?? B9 ?? 00 00 00 49 8B 39 B8 ?? 00 00 00 0F 45 C1 4E 8B 04 08");
    if (!bind || !ReadTextureLayout(bind)) return false;
    for (auto& type : classes) {
        type.vtable = Vtable::WithDraw(type.name, type.draw);
        if (!type.vtable) return false;
        type.original = reinterpret_cast<DrawFn>(type.vtable[Vtable::kDrawSlot]);
    }
    for (auto& type : classes)
        Memory::Write(reinterpret_cast<std::uint8_t*>(type.vtable + Vtable::kDrawSlot), &DrawFitted);
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

std::uint8_t* CalledConverter(std::uint8_t* draw) {
    constexpr std::size_t kDrawLength = 0x800;
    constexpr std::uint8_t kCall = 0xE8;
    auto converters = Memory::PatternScanAll(gameModule, "48 89 5C 24 08 57 48 83 EC 20 48 8B DA 48 8B F9 E8 ?? ?? ?? ?? 0F 57 C0 8B C0 F3 48 0F 2A C0 F3 0F 5E 05 ?? ?? ?? ?? F3 0F 59 03 F3 0F 11 07 E8");
    for (auto* at = draw; at < draw + kDrawLength; at++) {
        if (*at != kCall) continue;
        auto* target = Thunk::Follow(at + 5 + *reinterpret_cast<std::int32_t*>(at + 1));
        if (std::ranges::find(converters, target) != converters.end()) return target;
    }
    SPDLOG_ERROR("Minimap: converter not found; not applied.");
    return nullptr;
}

void Apply() {
    auto* vtable = Vtable::WithDraw("Minimap draw", "40 55 53 56 57 48 8B EC 48 83 EC 68 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89");
    auto* code = vtable ? CalledConverter(Thunk::Follow(static_cast<std::uint8_t*>(vtable[Vtable::kDrawSlot]))) : nullptr;
    if (!vtable || !code) return;
    draw = reinterpret_cast<Hud::DrawFn>(vtable[Vtable::kDrawSlot]);
    Replace(canvasToScreen, code, &CanvasToScreen);
    Memory::Write(reinterpret_cast<std::uint8_t*>(vtable + Vtable::kDrawSlot), &Draw);
    SPDLOG_INFO("Minimap: kept in proportion.");
}

}

namespace WorldHud {

struct Matrix { float rows[4][4]; };

struct SceneCamera {
    std::uint8_t before[0x100];
    Matrix viewProjection;
};

SafetyHookInline drawMarkers;

std::uint64_t __fastcall DrawMarkers(void* markers, SceneCamera* camera, int player) {
    Matrix original = camera->viewProjection;
    for (auto& row : camera->viewProjection.rows) {
        row[0] /= Screen::fit.x;
        row[1] /= Screen::fit.y;
    }
    auto result = drawMarkers.call<std::uint64_t>(markers, camera, player);
    camera->viewProjection = original;
    return result;
}

void Apply() {
    constexpr int kCallAt = 10;
    auto* call = FindOne("World HUD", "44 8B C7 48 8D 90 ?? ?? ?? ?? E8 ?? ?? ?? ?? FF C7");
    if (!call) return;
    auto* code = Thunk::Follow(call + kCallAt + 5 + *reinterpret_cast<std::int32_t*>(call + kCallAt + 1));
    Replace(drawMarkers, code, &DrawMarkers);
    SPDLOG_INFO("World HUD: kept on target.");
}

}

namespace Movies {

constexpr UINT kMovieWidth = 1920;
constexpr UINT kMovieHeight = 1080;
constexpr int kDrawIndexedSlot = 12;
constexpr int kCreateDeferredContextSlot = 27;

SafetyHookInline drawImmediate, drawDeferred, createDeferred;
std::atomic<ID3D11ShaderResourceView*> lastView{ nullptr };
std::atomic<bool> lastWasMovie{ false };

bool ShowsMovie(ID3D11DeviceContext* context) {
    ID3D11ShaderResourceView* view = nullptr;
    context->PSGetShaderResources(0, 1, &view);
    if (!view) return false;
    if (view == lastView.load()) {
        view->Release();
        return lastWasMovie.load();
    }
    auto desc = Pictures::Describe(view);
    bool movie = desc.Width == kMovieWidth && desc.Height == kMovieHeight && Pictures::IsImage(desc);
    lastWasMovie = movie;
    lastView = view;
    view->Release();
    return movie;
}

void DrawFitted(SafetyHookInline& draw, ID3D11DeviceContext* context, UINT count, UINT start, INT base) {
    if (!ShowsMovie(context)) return draw.call<void>(context, count, start, base);
    UINT viewports = 1;
    D3D11_VIEWPORT full{};
    context->RSGetViewports(&viewports, &full);
    D3D11_VIEWPORT box = full;
    box.Width = full.Width * Screen::fit.x;
    box.Height = full.Height * Screen::fit.y;
    box.TopLeftX = full.TopLeftX + (full.Width - box.Width) / 2;
    box.TopLeftY = full.TopLeftY + (full.Height - box.Height) / 2;
    context->RSSetViewports(1, &box);
    draw.call<void>(context, count, start, base);
    context->RSSetViewports(1, &full);
}

void STDMETHODCALLTYPE DrawImmediate(ID3D11DeviceContext* context, UINT count, UINT start, INT base) {
    DrawFitted(drawImmediate, context, count, start, base);
}

void STDMETHODCALLTYPE DrawDeferred(ID3D11DeviceContext* context, UINT count, UINT start, INT base) {
    DrawFitted(drawDeferred, context, count, start, base);
}

void* Method(void* object, int slot) {
    return (*reinterpret_cast<void***>(object))[slot];
}

HRESULT STDMETHODCALLTYPE CreateDeferred(ID3D11Device* device, UINT flags, ID3D11DeviceContext** context) {
    HRESULT result = createDeferred.call<HRESULT>(device, flags, context);
    if (SUCCEEDED(result) && context && *context && !drawDeferred && Method(*context, kDrawIndexedSlot) != drawImmediate.target())
        drawDeferred = safetyhook::create_inline(Method(*context, kDrawIndexedSlot), &DrawDeferred);
    return result;
}

void Watch(ID3D11Device* device, ID3D11DeviceContext* context) {
    if (Screen::fit.x == 1.0f && Screen::fit.y == 1.0f || drawImmediate) return;
    drawImmediate = safetyhook::create_inline(Method(context, kDrawIndexedSlot), &DrawImmediate);
    createDeferred = safetyhook::create_inline(Method(device, kCreateDeferredContextSlot), &CreateDeferred);
    SPDLOG_INFO("Movies: kept 16:9.");
}

}

void ApplyToGame() {
    Resolution::AcceptCustomMode();
    Camera::Apply();

    Screen::ChooseFit();
    if (Screen::fit.x == 1.0f && Screen::fit.y == 1.0f) return;
    if (!Hud::Apply()) return;
    Minimap::Apply();
    WorldHud::Apply();
}

namespace Device {

SafetyHookInline createDevice;

HRESULT WINAPI CreateDevice(void* adapter, UINT driverType, HMODULE software, UINT flags, const void* levels, UINT levelCount, UINT sdkVersion, void** device, void* level, void** context) {
    static bool applied = (ApplyToGame(), true);
    HRESULT result = createDevice.call<HRESULT>(adapter, driverType, software, flags, levels, levelCount, sdkVersion, device, level, context);
    if (SUCCEEDED(result) && device && *device && context && *context)
        Movies::Watch(static_cast<ID3D11Device*>(*device), static_cast<ID3D11DeviceContext*>(*context));
    return result;
}

void Apply() {
    auto* code = GetProcAddress(LoadLibraryW(L"d3d11.dll"), "D3D11CreateDevice");
    createDevice = safetyhook::create_inline(reinterpret_cast<void*>(code), &CreateDevice);
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
        else {
            Resolution::FillTable();
            Device::Apply();
        }
    }
    return TRUE;
}
