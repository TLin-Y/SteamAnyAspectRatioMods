#include "stdafx.h"
#include "version.h"
#include "helper.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <safetyhook.hpp>
#include <inipp/inipp.h>
#include <d3d11.h>
#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <atomic>
#include <cmath>
#include <cstddef>

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
    spdlog::set_default_logger(spdlog::basic_logger_mt("DW9E_ARFix", (modFolder / "DW9E_ARFix.log").string(), true));
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
    std::ifstream file(modFolder / "DW9E_ARFix.ini");
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

using Table = std::array<int, 17>;

constexpr Table kWidths{ 640, 800, 1024, 1280, 1366, 1280, 1280, 1600, 1600, 1920, 1920, 1920, 2560, 2560, 3840, 2880, 3200 };
constexpr Table kHeights{ 480, 600, 768, 720, 768, 960, 1024, 900, 1200, 1080, 1200, 1440, 1440, 1600, 2160, 1620, 1800 };
constexpr Table kPictureHeights{ 360, 450, 576, 720, 768, 720, 720, 900, 900, 1080, 1080, 1080, 1440, 1440, 2160, 1620, 1800 };

std::string PatternOf(const Table& table) {
    std::string pattern;
    for (int value : table)
        for (int byte = 0; byte < 4; byte++)
            pattern += std::format("{:02X} ", (value >> (byte * 8)) & 0xFF);
    return pattern;
}

void Fill(const char* name, const Table& table, int value) {
    auto* found = FindOne(name, PatternOf(table).c_str());
    if (!found) return;
    Table filled;
    filled.fill(value);
    Memory::Write(found, filled);
}

void Apply() {
    Fill("Resolution widths", kWidths, config.width);
    Fill("Resolution heights", kHeights, config.height);
    Fill("Resolution picture heights", kPictureHeights, config.height);
    SPDLOG_INFO("Resolution: every entry is now {}x{}.", config.width, config.height);
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

float fitX = 1.0f, fitY = 1.0f;

void ChooseFit() {
    if (config.Ratio() < kNativeRatio) fitY = config.Ratio() / kNativeRatio;
    else fitX = kNativeRatio / config.Ratio();
}

bool IsNative() {
    return fitX == 1.0f && fitY == 1.0f;
}

}

D3D11_TEXTURE2D_DESC DescribeTexture(ID3D11ShaderResourceView* view) {
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

namespace Hud {

using Screen::fitX;
using Screen::fitY;

constexpr float kCanvasWidth = 1920.0f;
constexpr float kCanvasHeight = 1080.0f;
constexpr float kEdgeTolerance = 0.5f;
constexpr float kBackdropShare = 0.2f;
constexpr UINT kSmallestPicture = 640;
constexpr std::uint32_t kColorRgb = 0xFFFFFF;

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

struct Transform {
    float scale;
    float offsetX;
    float offsetY;
    bool enabled;
};

struct RendererLayout {
    std::int32_t texture = 0;
    std::int32_t transform = 0;
} layout;

struct Renderer {
    Texture* GetTexture() { return *reinterpret_cast<Texture**>(reinterpret_cast<std::uint8_t*>(this) + layout.texture); }
    Transform& GetTransform() { return *reinterpret_cast<Transform*>(reinterpret_cast<std::uint8_t*>(this) + layout.transform); }
};

struct Rect {
    float left, top, right, bottom;
    float Width() const { return std::abs(right - left); }
    float Height() const { return std::abs(bottom - top); }
};

thread_local bool keepingFullScreen = false;

bool Covers(float low, float high, float length) {
    return low <= kEdgeTolerance && high >= length - kEdgeTolerance;
}

bool FillsScreen(const Rect& rect) {
    return (Covers(rect.left, rect.right, kCanvasWidth) && rect.Height() >= kCanvasHeight * kBackdropShare)
        || (Covers(rect.top, rect.bottom, kCanvasHeight) && rect.Width() >= kCanvasWidth * kBackdropShare);
}

bool ShowsPicture(const Texture* texture) {
    if (!texture || !texture->View()) return false;
    auto desc = DescribeTexture(texture->View());
    return desc.Width >= kSmallestPicture && !(desc.BindFlags & D3D11_BIND_RENDER_TARGET);
}

float Fit(float value, float length, float scale) {
    return (value - length / 2) * scale + length / 2;
}

SafetyHookInline toScreen, drawRect;

void __fastcall ToScreen(Renderer* renderer, float* x, float* y, std::uint8_t* alpha) {
    if (keepingFullScreen) return toScreen.call<void>(renderer, x, y, alpha);
    *x = Fit(*x, kCanvasWidth, fitX);
    *y = Fit(*y, kCanvasHeight, fitY);
    auto& transform = renderer->GetTransform();
    if (!transform.enabled) return toScreen.call<void>(renderer, x, y, alpha);
    Transform original = transform;
    transform.offsetX *= fitX;
    transform.offsetY *= fitY;
    toScreen.call<void>(renderer, x, y, alpha);
    transform = original;
}

bool IsShade(std::uint32_t color) {
    return (color & kColorRgb) == 0;
}

void __fastcall DrawRect(Renderer* renderer, void* context, const Rect* rect, const Rect* uv, std::uint32_t color, std::uint64_t depth) {
    bool outer = keepingFullScreen;
    keepingFullScreen = FillsScreen(*rect) && (IsShade(color) || !ShowsPicture(renderer->GetTexture()));
    drawRect.call<void>(renderer, context, rect, uv, color, depth);
    keepingFullScreen = outer;
}

bool ReadTextureLayout(std::uint8_t* bind) {
    constexpr int kFlagsAt = 4, kShiftAt = 15, kFirstViewAt = 20;
    std::int32_t firstView = Field<std::uint8_t>(bind, kFirstViewAt);
    textureLayout = { firstView, firstView + 8, Field<std::uint8_t>(bind, kFlagsAt),
        static_cast<std::uint16_t>(1u << Field<std::uint8_t>(bind, kShiftAt)) };
    if (textureLayout.usesSecondView) return true;
    SPDLOG_ERROR("HUD: unexpected texture layout; not applied.");
    return false;
}

bool ReadLayout(std::uint8_t* convert, std::uint8_t* textureCheck) {
    constexpr int kEnabledAt = 23, kScaleAt = 50, kTextureAt = 3;
    auto enabled = *reinterpret_cast<std::int32_t*>(convert + kEnabledAt);
    auto scale = *reinterpret_cast<std::int32_t*>(convert + kScaleAt);
    layout.texture = *reinterpret_cast<std::int32_t*>(textureCheck + kTextureAt);
    layout.transform = scale;
    if (enabled == scale + static_cast<std::int32_t>(offsetof(Transform, enabled)) && layout.texture > 0 && layout.texture < scale) return true;
    SPDLOG_ERROR("HUD: unexpected renderer layout (texture {:#x}, scale {:#x}, flag {:#x}); not applied.", layout.texture, scale, enabled);
    return false;
}

void Apply() {
    auto* convert = FindOne("HUD", "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 41 56 48 83 EC ?? 80 B9 ?? ?? ?? ?? 00 4D 8B F1 49 8B D8 48 8B FA 48 8B F1 0F 84 ?? ?? ?? ?? F3 0F 10 91 ?? ?? ?? ??");
    auto* textureCheck = FindOne("HUD texture", "4C 39 81 ?? ?? ?? ?? 74 ?? 48 85 DB 74 ?? 45 33 C0 4C 8B CB");
    auto* bind = FindOne("HUD texture views", "41 0F B7 41 ?? 4D 8B 59 ?? 49 8B 39 48 C1 E8 ?? 4D 8B 54 C1 ??");
    auto* rect = FindOne("Full-screen pictures", "48 89 5C 24 08 48 89 7C 24 10 55 48 8D 6C 24 ?? 48 81 EC ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 ?? 8B 45 ?? 48 8B DA");
    if (!convert || !textureCheck || !bind || !rect || !ReadLayout(convert, textureCheck) || !ReadTextureLayout(bind)) return;
    Replace(toScreen, convert, &ToScreen);
    Replace(drawRect, rect, &DrawRect);
    SPDLOG_INFO("HUD: kept 16:9.");
}

}

namespace WorldHud {

struct CanvasPoint { float x, y; };

SafetyHookInline project;

float UndoFit(float value, float length, float scale) {
    return (value - length / 2) / scale + length / 2;
}

bool __fastcall Project(int camera, const float* world, CanvasPoint* point) {
    bool visible = project.call<bool>(camera, world, point);
    point->x = UndoFit(point->x, Hud::kCanvasWidth, Screen::fitX);
    point->y = UndoFit(point->y, Hud::kCanvasHeight, Screen::fitY);
    return visible;
}

void Apply() {
    auto* code = FindOne("World HUD", "48 89 5C 24 08 57 48 83 EC ?? 48 63 F9 49 8B D8 ?? 8B ?? 83 FF 01 0F 87");
    if (!code) return;
    Replace(project, code, &Project);
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

bool IsVideoFrame(const D3D11_TEXTURE2D_DESC& desc) {
    bool opaque = desc.Format == DXGI_FORMAT_B8G8R8X8_TYPELESS || desc.Format == DXGI_FORMAT_B8G8R8X8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
    return desc.Width == kMovieWidth && desc.Height == kMovieHeight && (desc.Usage == D3D11_USAGE_DYNAMIC || opaque);
}

bool ShowsMovie(ID3D11DeviceContext* context) {
    ID3D11ShaderResourceView* view = nullptr;
    context->PSGetShaderResources(0, 1, &view);
    if (!view) return false;
    if (view == lastView.load()) {
        view->Release();
        return lastWasMovie.load();
    }
    bool movie = IsVideoFrame(DescribeTexture(view));
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
    box.Width = full.Width * Screen::fitX;
    box.Height = full.Height * Screen::fitY;
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
    if (Screen::IsNative() || drawImmediate) return;
    drawImmediate = safetyhook::create_inline(Method(context, kDrawIndexedSlot), &DrawImmediate);
    createDeferred = safetyhook::create_inline(Method(device, kCreateDeferredContextSlot), &CreateDeferred);
    SPDLOG_INFO("Movies: kept 16:9.");
}

}

void ApplyToGame() {
    Camera::Apply();
    if (Screen::IsNative()) return;
    Hud::Apply();
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
            Resolution::Apply();
            Screen::ChooseFit();
            Device::Apply();
        }
    }
    return TRUE;
}
