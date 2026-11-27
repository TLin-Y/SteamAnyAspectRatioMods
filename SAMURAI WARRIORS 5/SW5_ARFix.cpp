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
    spdlog::set_default_logger(spdlog::basic_logger_mt("SW5_ARFix", (modFolder / "SW5_ARFix.log").string(), true));
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
    std::ifstream file(modFolder / "SW5_ARFix.ini");
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
void Replace(SafetyHookInline& original, void* code, Detour detour) {
    original = safetyhook::create_inline(code, detour, safetyhook::InlineHook::StartDisabled);
    (void)original.enable();
}

namespace Resolution {

constexpr std::array kWidths{ 640, 800, 1024, 1280, 1360, 1600, 1920, 2560, 3840, 7680 };
constexpr std::array kHeights{ 360, 450, 576, 720, 765, 900, 1080, 1440, 2160, 4320 };

std::uint8_t* FindChoice(std::uint8_t* from, std::uint8_t* to, int choice) {
    for (auto* at = from; at + sizeof(int) <= to; at++)
        if (*reinterpret_cast<int*>(at) == choice) return at;
    return nullptr;
}

std::vector<std::uint8_t*> FindList(std::span<const int> choices) {
    constexpr std::size_t kLongestStep = 8;
    auto* base = reinterpret_cast<std::uint8_t*>(gameModule);
    auto& header = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew)->OptionalHeader;
    auto* code = base + header.BaseOfCode;
    auto* end = code + header.SizeOfCode - choices.size() * (kLongestStep + sizeof(int));
    for (auto* start = code; (start = FindChoice(start, end, choices[0])); start++) {
        std::vector<std::uint8_t*> list{ start };
        for (int choice : choices.subspan(1))
            if (auto* next = FindChoice(list.back() + sizeof(int), list.back() + sizeof(int) + kLongestStep, choice)) list.push_back(next);
            else break;
        if (list.size() == choices.size()) return list;
    }
    return {};
}

void Apply() {
    auto widths = FindList(kWidths), heights = FindList(kHeights);
    if (widths.empty() || heights.empty()) {
        SPDLOG_ERROR("Resolution: size lists not found; not applied.");
        return;
    }
    for (auto* at : widths) Memory::Write(at, config.width);
    for (auto* at : heights) Memory::Write(at, config.height);
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

namespace ScreenSpace {

constexpr UINT kConstantsSize = 32;
constexpr UINT kVertexSize = 24;
constexpr int kDrawIndexedSlot = 12;
constexpr int kCreateDeferredContextSlot = 27;
constexpr UINT kQuad = 6;

struct Fit { float x = 1.0f, y = 1.0f; };

Fit box;
SafetyHookMid copyConstants;
SafetyHookInline drawDeferred, createDeferred;
thread_local std::array<float, kConstantsSize / sizeof(float)> boxedConstants;
thread_local bool boxed = false;

bool Near(float value, float expected) { return std::abs(value - expected) < std::abs(expected) * 0.001f; }

void BoxConstants(safetyhook::Context& context) {
    if (context.r8 != kConstantsSize) return;
    auto* constants = reinterpret_cast<const float*>(context.rdx);
    if (constants[2] != -1.0f || constants[3] != 1.0f) return;
    boxed = Near(constants[0], 2.0f / config.width) && Near(constants[1], -2.0f / config.height);
    if (!boxed) return;
    std::copy_n(constants, boxedConstants.size(), boxedConstants.begin());
    for (int i = 0; i < 4; i++) boxedConstants[i] *= box.x;
    context.rdx = reinterpret_cast<std::uintptr_t>(boxedConstants.data());
}

void* Method(void* object, int slot) { return (*reinterpret_cast<void***>(object))[slot]; }

bool UsesScreenSpace(ID3D11DeviceContext* context) {
    ID3D11Buffer* constants = nullptr;
    context->VSGetConstantBuffers(0, 1, &constants);
    if (!constants) return false;
    D3D11_BUFFER_DESC desc{};
    constants->GetDesc(&desc);
    constants->Release();
    if (desc.ByteWidth != kConstantsSize) return false;
    ID3D11Buffer* vertices = nullptr;
    UINT stride = 0, offset = 0;
    context->IAGetVertexBuffers(0, 1, &vertices, &stride, &offset);
    if (vertices) vertices->Release();
    return stride == kVertexSize;
}

bool CopiesScreen(ID3D11DeviceContext* context) {
    ID3D11ShaderResourceView* view = nullptr;
    context->PSGetShaderResources(0, 1, &view);
    if (!view) return false;
    ID3D11Resource* resource = nullptr;
    view->GetResource(&resource);
    view->Release();
    D3D11_TEXTURE2D_DESC desc{};
    ID3D11Texture2D* image = nullptr;
    if (resource && SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&image)))) {
        image->GetDesc(&desc);
        image->Release();
    }
    if (resource) resource->Release();
    return desc.Width == static_cast<UINT>(config.width) && desc.Height == static_cast<UINT>(config.height);
}

void STDMETHODCALLTYPE DrawDeferred(ID3D11DeviceContext* context, UINT count, UINT start, INT base) {
    UINT viewports = 1;
    D3D11_VIEWPORT screen{};
    context->RSGetViewports(&viewports, &screen);
    bool wholeScreen = viewports == 1 && screen.Width == config.width && screen.Height == config.height;
    if (count != kQuad || !wholeScreen || !CopiesScreen(context)) return drawDeferred.call<void>(context, count, start, base);
    Fit fit = box;
    if (UsesScreenSpace(context)) fit = boxed ? Fit{ box.x, box.x } : Fit{};
    D3D11_VIEWPORT unboxed = screen;
    unboxed.Width = screen.Width / fit.x;
    unboxed.Height = screen.Height / fit.y;
    unboxed.TopLeftX = screen.TopLeftX - (unboxed.Width - screen.Width) / 2;
    unboxed.TopLeftY = screen.TopLeftY - (unboxed.Height - screen.Height) / 2;
    context->RSSetViewports(1, &unboxed);
    drawDeferred.call<void>(context, count, start, base);
    context->RSSetViewports(1, &screen);
}

HRESULT STDMETHODCALLTYPE CreateDeferred(ID3D11Device* device, UINT flags, ID3D11DeviceContext** context) {
    HRESULT result = createDeferred.call<HRESULT>(device, flags, context);
    if (SUCCEEDED(result) && context && *context && !drawDeferred)
        Replace(drawDeferred, Method(*context, kDrawIndexedSlot), &DrawDeferred);
    return result;
}

void Watch(ID3D11Device* device) {
    if (createDeferred || Near(config.Ratio(), kNativeRatio)) return;
    Replace(createDeferred, Method(device, kCreateDeferredContextSlot), &CreateDeferred);
}

void Apply() {
    if (config.Ratio() > kNativeRatio) box.x = kNativeRatio / config.Ratio();
    else box.y = config.Ratio() / kNativeRatio;
    if (box.x == 1.0f) return;
    auto* code = FindOne("Screen space", "48 8B 94 C5 ?? ?? ?? ?? E8");
    if (!code) return;
    copyConstants = safetyhook::create_mid(code + 8, &BoxConstants);
    SPDLOG_INFO("Screen space: kept 16:9.");
}

}

namespace Device {

SafetyHookInline createDevice;

HRESULT WINAPI CreateDevice(void* adapter, UINT driverType, HMODULE software, UINT flags, const void* levels, UINT levelCount, UINT sdkVersion, void** device, void* level, void** context) {
    HRESULT result = createDevice.call<HRESULT>(adapter, driverType, software, flags, levels, levelCount, sdkVersion, device, level, context);
    if (SUCCEEDED(result) && device && *device) ScreenSpace::Watch(static_cast<ID3D11Device*>(*device));
    return result;
}

void Apply() {
    auto* code = GetProcAddress(LoadLibraryW(L"d3d11.dll"), "D3D11CreateDevice");
    Replace(createDevice, reinterpret_cast<void*>(code), &CreateDevice);
}

}

void ApplyToGame() {
    Resolution::Apply();
    Camera::Apply();
    ScreenSpace::Apply();
}

namespace Window {

SafetyHookInline createWindow;

HWND WINAPI CreateWindowExW_(DWORD exStyle, LPCWSTR className, LPCWSTR name, DWORD style, int x, int y, int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param) {
    static bool applied = (ApplyToGame(), true);
    return createWindow.stdcall<HWND>(exStyle, className, name, style, x, y, width, height, parent, menu, instance, param);
}

void Apply() {
    auto* code = GetProcAddress(GetModuleHandleW(L"user32.dll"), "CreateWindowExW");
    Replace(createWindow, reinterpret_cast<void*>(code), &CreateWindowExW_);
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
            Window::Apply();
            Device::Apply();
        }
    }
    return TRUE;
}
