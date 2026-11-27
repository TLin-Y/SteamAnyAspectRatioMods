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
#include <optional>
#include <span>

#if !defined(_M_IX86)
#error One Piece Pirate Warriors 3 is a 32-bit game. Compile this project as x86.
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
    spdlog::set_default_logger(spdlog::basic_logger_mt("OPPW3_ARFix", (modFolder / "OPPW3_ARFix.log").string(), true));
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
    std::ifstream file(modFolder / "OPPW3_ARFix.ini");
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

struct Row {
    std::uint16_t width, pictureHeight, height;
    bool IsChoice() const { return width >= 640 && pictureHeight == width * 9 / 16 && height >= pictureHeight; }
};

std::vector<std::uint8_t*> FindModeRejections() {
    constexpr std::ptrdiff_t kReturnFalse = 5;
    auto hits = Memory::PatternScanAll(gameModule, "83 ?? 0B 72 ?? 33 C0");
    for (auto*& at : hits) at += kReturnFalse;
    return hits;
}

bool Apply() {
    auto* table = FindOne("Resolution table", "80 02 68 01 E0 01 20 03 C2 01 58 02 00 04 40 02 00 03");
    auto rejections = FindModeRejections();
    if (!table || rejections.empty()) {
        SPDLOG_ERROR("Resolution: {} display mode checks found; nothing applied.", rejections.size());
        return false;
    }
    Row custom{ static_cast<std::uint16_t>(config.width), static_cast<std::uint16_t>(config.height), static_cast<std::uint16_t>(config.height) };
    int rows = 0;
    for (auto* row = table; reinterpret_cast<Row*>(row)->IsChoice(); row += sizeof(Row), rows++) Memory::Write(row, custom);
    for (auto* at : rejections) Memory::Write(at, kReturnTrue);
    SPDLOG_INFO("Resolution: all {} entries are now {}x{}.", rows, config.width, config.height);
    return true;
}

}

namespace Camera {

constexpr float kWidestFov = 2.96f;
constexpr std::uint32_t kOrtho = 1;
constexpr std::uint32_t kLensBeforeFlags = 0x10;

struct Lens { float fov, aspect; };

SafetyHookInline buildProjection;
std::uint32_t flagsAt = 0;

Lens& LensOf(std::uint8_t* camera) { return *reinterpret_cast<Lens*>(camera + flagsAt - kLensBeforeFlags); }
std::uint32_t FlagsOf(std::uint8_t* camera) { return *reinterpret_cast<std::uint32_t*>(camera + flagsAt); }

float WidenedFov(float fov) {
    float keepWidth = std::max(1.0f, kNativeRatio / config.Ratio());
    return std::min(2.0f * std::atan(std::tan(fov / 2.0f) * keepWidth) * config.fovMultiplier, kWidestFov);
}

void __fastcall BuildProjection(std::uint8_t* camera, void*) {
    Lens& lens = LensOf(camera);
    if ((FlagsOf(camera) & kOrtho) || std::abs(lens.aspect - kNativeRatio) > 0.01f) {
        buildProjection.thiscall<void>(camera);
        return;
    }
    Lens original = lens;
    lens = { WidenedFov(lens.fov), config.Ratio() };
    buildProjection.thiscall<void>(camera);
    lens = original;
}

void Apply() {
    constexpr std::ptrdiff_t kFlagsOffsetAt = 12;
    auto* code = FindOne("Camera", "55 8B EC 83 E4 F8 51 56 8B F1 F6 86 ?? ?? ?? ?? 01 0F 84");
    if (!code) return;
    flagsAt = *reinterpret_cast<std::uint32_t*>(code + kFlagsOffsetAt);
    buildProjection = safetyhook::create_inline(code, &BuildProjection);
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

thread_local std::optional<Point> worldAnchor;

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
    return x.MatchesScreen(Screen::size.x) && y.IsBackdropSide(Screen::size.y);
}

enum Corner { kTopLeft, kTopRight, kBottomLeft, kBottomRight };

bool IsUpright(std::span<Point> q) {
    return q[kTopLeft].y == q[kTopRight].y && q[kBottomLeft].y == q[kBottomRight].y && q[kTopLeft].x == q[kBottomLeft].x && q[kTopRight].x == q[kBottomRight].x;
}

void ClipSide(float& low, float& high, float& lowUv, float& highUv, float length) {
    float from = low, to = high, fromUv = lowUv, toUv = highUv;
    if (to <= from) return;
    auto uvAt = [&](float at) { return fromUv + (toUv - fromUv) * (at - from) / (to - from); };
    if (from < 0.0f) { low = 0.0f; lowUv = uvAt(0.0f); }
    if (to > length) { high = length; highUv = uvAt(length); }
    if (high < low) high = low;
}

void ClipToScreen(std::span<Point> q, std::span<Point> uv) {
    if (!IsUpright(q) || !IsUpright(uv)) return;
    ClipSide(q[kTopLeft].x, q[kTopRight].x, uv[kTopLeft].x, uv[kTopRight].x, Screen::size.x);
    ClipSide(q[kTopLeft].y, q[kBottomLeft].y, uv[kTopLeft].y, uv[kBottomLeft].y, Screen::size.y);
    q[kBottomLeft].x = q[kTopLeft].x; q[kBottomRight].x = q[kTopRight].x; q[kTopRight].y = q[kTopLeft].y; q[kBottomRight].y = q[kBottomLeft].y;
    uv[kBottomLeft].x = uv[kTopLeft].x; uv[kBottomRight].x = uv[kTopRight].x; uv[kTopRight].y = uv[kTopLeft].y; uv[kBottomRight].y = uv[kBottomLeft].y;
}

IDirect3DDevice9* device = nullptr;

std::optional<D3DSURFACE_DESC> BoundTexture() {
    IDirect3DBaseTexture9* bound = nullptr;
    if (!device || FAILED(device->GetTexture(0, &bound)) || !bound) return std::nullopt;
    IDirect3DTexture9* texture = nullptr;
    D3DSURFACE_DESC desc{};
    bool described = SUCCEEDED(bound->QueryInterface(__uuidof(IDirect3DTexture9), reinterpret_cast<void**>(&texture))) && SUCCEEDED(texture->GetLevelDesc(0, &desc));
    if (texture) texture->Release();
    bound->Release();
    if (!described) return std::nullopt;
    return desc;
}

bool IsScreenSized(const D3DSURFACE_DESC& desc) { return desc.Width == Screen::size.x && desc.Height == Screen::size.y; }

bool ShowsPicture(const D3DSURFACE_DESC& desc, std::span<Point> uvs) {
    constexpr float kSmallestPicture = 640.0f, kTolerance = 0.01f;
    if (desc.Usage & D3DUSAGE_RENDERTARGET) return false;
    Span u = SpanOf(uvs, &Point::x), v = SpanOf(uvs, &Point::y);
    float width = (u.high - u.low) * desc.Width, height = (v.high - v.low) * desc.Height;
    return width >= kSmallestPicture && std::abs(width / height - kNativeRatio) < kTolerance;
}

bool CopiesScreenLayer(const D3DSURFACE_DESC& desc, std::span<Point> points, std::span<Point> uvs) {
    constexpr float kTolerance = 2.0f;
    if (!(desc.Usage & D3DUSAGE_RENDERTARGET) || !IsScreenSized(desc)) return false;
    Span x = SpanOf(points, &Point::x), y = SpanOf(points, &Point::y), u = SpanOf(uvs, &Point::x), v = SpanOf(uvs, &Point::y);
    return std::abs((x.high - x.low) - (u.high - u.low) * desc.Width) < kTolerance && std::abs((y.high - y.low) - (v.high - v.low) * desc.Height) < kTolerance;
}

bool DrawsToScreenSize() {
    IDirect3DSurface9* target = nullptr;
    D3DSURFACE_DESC desc{};
    if (!device || FAILED(device->GetRenderTarget(0, &target))) return true;
    bool described = SUCCEEDED(target->GetDesc(&desc));
    target->Release();
    return !described || IsScreenSized(desc);
}

float FitPosition(float value, float length, float scale, bool staysOnEdge) {
    bool onEdge = value <= kEdgeTolerance || value >= length - kEdgeTolerance;
    return staysOnEdge && onEdge ? value : Screen::ScaleFromCenter(value, length, scale);
}

void KeepOnTarget(std::span<Point> points, Point anchor) {
    Point shift{ anchor.x - Screen::ScaleFromCenter(anchor.x, Screen::size.x, Screen::fit.x), anchor.y - Screen::ScaleFromCenter(anchor.y, Screen::size.y, Screen::fit.y) };
    for (auto& point : points) {
        point.x += shift.x;
        point.y += shift.y;
    }
}

void FitPoints(std::span<Point> points, bool backdrop) {
    for (auto& point : points) {
        point.x = FitPosition(point.x, Screen::size.x, Screen::fit.x, backdrop);
        point.y = FitPosition(point.y, Screen::size.y, Screen::fit.y, backdrop);
    }
}

template<std::size_t Corners>
void FitCorners(SafetyHookContext& ctx) {
    if (!DrawsToScreenSize()) return;
    std::span points(*reinterpret_cast<Point**>(ctx.esp + 4), Corners);
    bool backdrop = IsBackdrop(points);
    if constexpr (Corners == 4) {
        std::span<Point> uv(*reinterpret_cast<Point**>(ctx.esp + 8), Corners);
        if (auto texture = BoundTexture()) {
            if (CopiesScreenLayer(*texture, points, uv)) return;
            backdrop = backdrop && !ShowsPicture(*texture, uv);
        }
        thread_local std::array<Point, 4> clippedUv;
        if (!backdrop) {
            std::ranges::copy(uv, clippedUv.begin());
            ClipToScreen(points, clippedUv);
            *reinterpret_cast<Point**>(ctx.esp + 8) = clippedUv.data();
        }
    }
    FitPoints(points, backdrop);
    if (Screen::worldAnchor) KeepOnTarget(points, *Screen::worldAnchor);
}

std::vector<SafetyHookMid> writers;

std::uint8_t* EntryBefore(std::uint8_t* code) {
    constexpr int kFurthest = 0x40;
    for (auto* at = code; at > code - kFurthest; at--)
        if (at[-1] == 0xCC && at[0] == 0x55 && at[1] == 0x8B && at[2] == 0xEC) return at;
    return nullptr;
}

void Apply() {
    constexpr std::ptrdiff_t kRoom = 7;
    constexpr int kBufferEnd = 0x400;
    for (auto* check : Memory::PatternScanAll(gameModule, "8B 47 6C 03 47 70 3D ?? 03 00 00 7F")) {
        auto* entry = EntryBefore(check);
        int vertices = kBufferEnd - *reinterpret_cast<int*>(check + kRoom);
        if (!entry) continue;
        if (vertices == 6) writers.push_back(safetyhook::create_mid(entry, FitCorners<4>));
        else if (vertices == 3) writers.push_back(safetyhook::create_mid(entry, FitCorners<3>));
    }
    if (writers.size() != 2) SPDLOG_ERROR("HUD: {} of 2 vertex writers found.", writers.size());
    else SPDLOG_INFO("HUD: kept 16:9.");
}

}

namespace WorldHud {

SafetyHookInline drawBar;
std::ptrdiff_t xAt = 0, yAt = 0;

void __fastcall DrawBar(std::uint8_t* bar, void*) {
    Screen::worldAnchor = Screen::Point{ static_cast<float>(*reinterpret_cast<std::int16_t*>(bar + xAt)), static_cast<float>(*reinterpret_cast<std::int16_t*>(bar + yAt)) };
    drawBar.thiscall<void>(bar);
    Screen::worldAnchor.reset();
}

void Apply() {
    constexpr std::ptrdiff_t kYOffsetAt = 6, kXOffsetAt = 50;
    auto* code = FindOne("World HUD", "83 EC ?? 0F BF 41 ?? 89 44 24 ?? A1 ?? ?? ?? ?? 66 0F 6E C0 F3 0F E6 C0 C1 E8 1F F3 0F 10 49 ?? F2 0F 58 04 C5 ?? ?? ?? ?? F3 0F 10 61 ?? 53 0F BF 59 ??");
    if (!code) return;
    yAt = code[kYOffsetAt];
    xAt = code[kXOffsetAt];
    drawBar = safetyhook::create_inline(code, &DrawBar);
    SPDLOG_INFO("World HUD: health bars kept on target.");
}

}

void ApplyToGame() {
    if (!Resolution::Apply()) return;
    Camera::Apply();
    Screen::ChooseFit();
    if (Screen::fit.x == 1.0f && Screen::fit.y == 1.0f) return;
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
    if (SUCCEEDED(result)) {
        Hud::device = *device;
        ReplaceReset(*device);
    }
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
