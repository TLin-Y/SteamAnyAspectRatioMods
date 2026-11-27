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
#error Samurai Warriors 4-II is a 32-bit game. Compile this project as x86.
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
    spdlog::set_default_logger(spdlog::basic_logger_mt("SW4II_ARFix", (modFolder / "SW4II_ARFix.log").string(), true));
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
    std::ifstream file(modFolder / "SW4II_ARFix.ini");
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
    auto hits = Memory::PatternScanAll(gameModule, "83 F9 0B 72 ?? 33 C0");
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

IDirect3DDevice9* device = nullptr;

bool ShowsPicture(std::span<Point> uvs) {
    constexpr float kSmallestPicture = 640.0f, kTolerance = 0.01f;
    IDirect3DBaseTexture9* bound = nullptr;
    if (!device || FAILED(device->GetTexture(0, &bound)) || !bound) return false;
    IDirect3DTexture9* texture = nullptr;
    D3DSURFACE_DESC desc{};
    bool described = SUCCEEDED(bound->QueryInterface(__uuidof(IDirect3DTexture9), reinterpret_cast<void**>(&texture))) && SUCCEEDED(texture->GetLevelDesc(0, &desc));
    if (texture) texture->Release();
    bound->Release();
    if (!described || (desc.Usage & D3DUSAGE_RENDERTARGET)) return false;
    Span u = SpanOf(uvs, &Point::x), v = SpanOf(uvs, &Point::y);
    float width = (u.high - u.low) * desc.Width, height = (v.high - v.low) * desc.Height;
    return width >= kSmallestPicture && std::abs(width / height - kNativeRatio) < kTolerance;
}

float FitPosition(float value, float length, float scale, bool staysOnEdge) {
    bool onEdge = value <= kEdgeTolerance || value >= length - kEdgeTolerance;
    return staysOnEdge && onEdge ? value : Screen::ScaleFromCenter(value, length, scale);
}

void FitPoints(std::span<Point> points, bool backdrop) {
    for (auto& point : points) {
        point.x = FitPosition(point.x, Screen::size.x, Screen::fit.x, backdrop);
        point.y = FitPosition(point.y, Screen::size.y, Screen::fit.y, backdrop);
    }
}

template<std::size_t Corners>
void FitCorners(SafetyHookContext& ctx) {
    std::span points(*reinterpret_cast<Point**>(ctx.esp + 4), Corners);
    bool backdrop = IsBackdrop(points);
    if constexpr (Corners == 4) {
        backdrop = backdrop && !ShowsPicture({ *reinterpret_cast<Point**>(ctx.esp + 8), Corners });
    }
    FitPoints(points, backdrop);
}

void FitLine(SafetyHookContext& ctx) {
    std::span ends(reinterpret_cast<Point*>(ctx.esp + 4), 2);
    FitPoints(ends, IsBackdrop(ends));
}

std::vector<SafetyHookMid> writers;

void Apply() {
    constexpr std::ptrdiff_t kRoom = 19;
    constexpr int kBufferEnd = 0x400;
    for (auto* entry : Memory::PatternScanAll(gameModule, "55 8B EC 56 8B F1 8B 46 ?? 03 46 ?? 57 BF ?? 00 00 00 3D ?? 03 00 00 7F")) {
        int vertices = kBufferEnd - *reinterpret_cast<int*>(entry + kRoom);
        if (vertices == 6) writers.push_back(safetyhook::create_mid(entry, FitCorners<4>));
        else if (vertices == 3) writers.push_back(safetyhook::create_mid(entry, FitCorners<3>));
        else if (vertices == 2) writers.push_back(safetyhook::create_mid(entry, FitLine));
    }
    if (writers.size() != 3) SPDLOG_ERROR("HUD: {} of 3 vertex writers found.", writers.size());
    else SPDLOG_INFO("HUD: kept 16:9.");
}

}

namespace WorldHud {

struct Matrix { float rows[4][4]; };

struct SceneCamera {
    std::uint8_t before[0xF4];
    Matrix viewProjection;
};

SafetyHookInline drawMarkers;

void __stdcall DrawMarkers(void* markers, SceneCamera* camera, int player) {
    Matrix original = camera->viewProjection;
    for (auto& row : camera->viewProjection.rows) {
        row[0] /= Screen::fit.x;
        row[1] /= Screen::fit.y;
    }
    drawMarkers.stdcall<void>(markers, camera, player);
    camera->viewProjection = original;
}

void Apply() {
    constexpr int kCallAt = 2;
    auto* call = FindOne("World HUD", "52 50 E8 ?? ?? ?? ?? 46 81 C7");
    if (!call) return;
    auto* code = call + kCallAt + 5 + *reinterpret_cast<std::int32_t*>(call + kCallAt + 1);
    drawMarkers = safetyhook::create_inline(code, &DrawMarkers);
    SPDLOG_INFO("World HUD: kept on target.");
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
