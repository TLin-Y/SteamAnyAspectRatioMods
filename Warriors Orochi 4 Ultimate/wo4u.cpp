#include "stdafx.h"
#include "version.h"
#include "helper.hpp"

#ifdef _DEBUG
    #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#else
    #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#endif

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <safetyhook.hpp>
#include <inipp/inipp.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <mutex>
#include <numeric>

namespace Stretch { extern bool bEnabled; }

HMODULE exeModule = GetModuleHandle(NULL);
HMODULE thisModule;
HMODULE wo4uModule = NULL;

std::shared_ptr<spdlog::logger> logger;
std::string sLogFile = "WO4U_ARFix.log";
std::filesystem::path ExePath;
std::string sExeName;

inipp::Ini<char> ini;
std::string sConfigFile = "WO4U_ARFix.ini";
std::filesystem::path FixPath;

float fTargetWidth = 4.0f;
float fTargetHeight = 3.0f;
float fTargetRatio = 1.3333333f;
float fFOVScale = 1.0f;

bool bCustomResolution = true;
int iCustomResX = 0;
int iCustomResY = 0;

template<typename T>
T GetConfigAndLog(inipp::Ini<char>& ini, const std::string& section, const std::string& key, T defaultValue) {
    T value = defaultValue;
    inipp::get_value(ini.sections[section], key, value);
    SPDLOG_INFO("Config Parse: [{}]: {} = {}", section, key, value);
    return value;
}

void Logging() {
    WCHAR dllPath[_MAX_PATH] = {0};
    GetModuleFileNameW(thisModule, dllPath, MAX_PATH);
    FixPath = dllPath;
    FixPath = FixPath.remove_filename();
    
    WCHAR gameExePath[_MAX_PATH] = {0};
    GetModuleFileNameW(exeModule, gameExePath, MAX_PATH);
    ExePath = gameExePath;
    sExeName = ExePath.filename().string();
    ExePath = ExePath.remove_filename();

    try {
        std::ofstream file(ExePath / sLogFile, std::ios::trunc);
        if (file.is_open()) file.close();
        logger = std::make_shared<spdlog::logger>("WO4U_ARFix", std::make_shared<spdlog::sinks::rotating_file_sink_st>((ExePath / sLogFile).string(), 10 * 1024 * 1024, 1));
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
        spdlog::flush_on(spdlog::level::debug);
        spdlog::set_level(spdlog::level::debug); 
        SPDLOG_INFO("----------");
        SPDLOG_INFO("Warriors Orochi 4 Ultimate AR & FOV Fix Initialized");
        SPDLOG_INFO("----------");
    }
    catch (const spdlog::spdlog_ex &ex) {}
}

void Configuration() {
    std::ifstream iniFile(FixPath / sConfigFile);
    if (!iniFile) {
        SPDLOG_WARN("Could not locate {}. Using default 4:3.", sConfigFile);
        fTargetRatio = 4.0f / 3.0f;
        return;
    }
    ini.parse(iniFile);
    ini.strip_trailing_comments();
    
    fTargetWidth = GetConfigAndLog(ini, "AspectRatio", "TargetWidth", 4.0f);
    fTargetHeight = GetConfigAndLog(ini, "AspectRatio", "TargetHeight", 3.0f);

    if (fTargetHeight > 0.001f) {
        fTargetRatio = fTargetWidth / fTargetHeight;
    }
    SPDLOG_INFO("Calculated Target Ratio: {}", fTargetRatio);

    fFOVScale = GetConfigAndLog(ini, "FOV", "FOVScale", 1.0f);
    if (!(fFOVScale > 0.0f && fFOVScale <= 2.0f)) {
        SPDLOG_ERROR("FOVScale {} out of range (0 - 2.0); using 1.0.", fFOVScale);
        fFOVScale = 1.0f;
    }

    bCustomResolution = GetConfigAndLog(ini, "Custom Resolution", "Enabled", true);
    iCustomResX = GetConfigAndLog(ini, "Custom Resolution", "Width", 0);
    iCustomResY = GetConfigAndLog(ini, "Custom Resolution", "Height", 0);
    Stretch::bEnabled = GetConfigAndLog(ini, "Stretch", "Enabled", true);
}

static const unsigned char kStretchVS11[] = {
0x44,0x58,0x42,0x43,0xD4,0xAE,0xD6,0x4B,0xA5,0x6E,0x25,0x02,0x0D,0x16,0x94,0x85,0xD7,0xA9,0x9D,0x26,0x01,0x00,0x00,0x00,
0xD0,0x01,0x00,0x00,0x03,0x00,0x00,0x00,0x2C,0x00,0x00,0x00,0x60,0x00,0x00,0x00,0xB8,0x00,0x00,0x00,0x49,0x53,0x47,0x4E,
0x2C,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x00,0x00,
0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x00,0x00,0x53,0x56,0x5F,0x56,0x65,0x72,0x74,0x65,0x78,0x49,0x44,0x00,
0x4F,0x53,0x47,0x4E,0x50,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x38,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x01,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0F,0x00,0x00,0x00,0x44,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x03,0x0C,0x00,0x00,0x53,0x56,0x5F,0x50,0x6F,0x73,0x69,0x74,
0x69,0x6F,0x6E,0x00,0x54,0x45,0x58,0x43,0x4F,0x4F,0x52,0x44,0x00,0xAB,0xAB,0xAB,0x53,0x48,0x45,0x58,0x10,0x01,0x00,0x00,
0x50,0x00,0x01,0x00,0x44,0x00,0x00,0x00,0x6A,0x08,0x00,0x01,0x60,0x00,0x00,0x04,0x12,0x10,0x10,0x00,0x00,0x00,0x00,0x00,
0x06,0x00,0x00,0x00,0x67,0x00,0x00,0x04,0xF2,0x20,0x10,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x65,0x00,0x00,0x03,
0x32,0x20,0x10,0x00,0x01,0x00,0x00,0x00,0x68,0x00,0x00,0x02,0x01,0x00,0x00,0x00,0x8C,0x00,0x00,0x0B,0x12,0x00,0x10,0x00,
0x00,0x00,0x00,0x00,0x01,0x40,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x40,0x00,0x00,0x01,0x00,0x00,0x00,0x0A,0x10,0x10,0x00,
0x00,0x00,0x00,0x00,0x01,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x07,0x42,0x00,0x10,0x00,0x00,0x00,0x00,0x00,
0x0A,0x10,0x10,0x00,0x00,0x00,0x00,0x00,0x01,0x40,0x00,0x00,0x02,0x00,0x00,0x00,0x56,0x00,0x00,0x05,0x32,0x00,0x10,0x00,
0x00,0x00,0x00,0x00,0x86,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x32,0x00,0x00,0x0F,0x32,0x20,0x10,0x00,0x00,0x00,0x00,0x00,
0x46,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x02,0x40,0x00,0x00,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0xC0,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x02,0x40,0x00,0x00,0x00,0x00,0x80,0xBF,0x00,0x00,0x80,0x3F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x36,0x00,0x00,0x05,0x32,0x20,0x10,0x00,0x01,0x00,0x00,0x00,0x46,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x36,0x00,0x00,0x08,
0xC2,0x20,0x10,0x00,0x00,0x00,0x00,0x00,0x02,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x80,0x3F,0x3E,0x00,0x00,0x01
};
static const unsigned char kStretchPS11[] = {
0x44,0x58,0x42,0x43,0x47,0x59,0x93,0xB4,0xB5,0x97,0x31,0xBB,0x7E,0x90,0x13,0xE3,0xCC,0xF2,0x56,0xE8,0x01,0x00,0x00,0x00,
0x38,0x01,0x00,0x00,0x03,0x00,0x00,0x00,0x2C,0x00,0x00,0x00,0x84,0x00,0x00,0x00,0xB8,0x00,0x00,0x00,0x49,0x53,0x47,0x4E,
0x50,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x38,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0F,0x00,0x00,0x00,0x44,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x03,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x03,0x03,0x00,0x00,0x53,0x56,0x5F,0x50,0x6F,0x73,0x69,0x74,0x69,0x6F,0x6E,0x00,
0x54,0x45,0x58,0x43,0x4F,0x4F,0x52,0x44,0x00,0xAB,0xAB,0xAB,0x4F,0x53,0x47,0x4E,0x2C,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
0x08,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x0F,0x00,0x00,0x00,0x53,0x56,0x5F,0x54,0x61,0x72,0x67,0x65,0x74,0x00,0xAB,0xAB,0x53,0x48,0x45,0x58,0x78,0x00,0x00,0x00,
0x50,0x00,0x00,0x00,0x1E,0x00,0x00,0x00,0x6A,0x08,0x00,0x01,0x5A,0x00,0x00,0x03,0x00,0x60,0x10,0x00,0x00,0x00,0x00,0x00,
0x58,0x18,0x00,0x04,0x00,0x70,0x10,0x00,0x00,0x00,0x00,0x00,0x55,0x55,0x00,0x00,0x62,0x10,0x00,0x03,0x32,0x10,0x10,0x00,
0x01,0x00,0x00,0x00,0x65,0x00,0x00,0x03,0xF2,0x20,0x10,0x00,0x00,0x00,0x00,0x00,0x48,0x00,0x00,0x8D,0xC2,0x00,0x00,0x80,
0x43,0x55,0x15,0x00,0xF2,0x20,0x10,0x00,0x00,0x00,0x00,0x00,0x46,0x10,0x10,0x00,0x01,0x00,0x00,0x00,0x46,0x7E,0x10,0x00,
0x00,0x00,0x00,0x00,0x00,0x60,0x10,0x00,0x00,0x00,0x00,0x00,0x01,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x3E,0x00,0x00,0x01
};

namespace Stretch {

bool bEnabled = true;

std::mutex mtx;
thread_local bool inHook = false;
bool failed = false;
IDXGISwapChain* swapChain = nullptr;
UINT srcW = 0, srcH = 0;
DXGI_FORMAT srcFormat = DXGI_FORMAT_UNKNOWN;
UINT loggedW = 0, loggedH = 0;

ID3D11Device* d11Device = nullptr;
ID3D11DeviceContext* d11Ctx = nullptr;
ID3D11VertexShader* d11VS = nullptr;
ID3D11PixelShader* d11PS = nullptr;
ID3D11SamplerState* d11Sampler = nullptr;
ID3D11RasterizerState* d11Raster = nullptr;
ID3D11Texture2D* d11Src = nullptr;
ID3D11ShaderResourceView* d11SrcView = nullptr;

SafetyHookInline hkCreateSwapChain, hkCreateSwapChainForHwnd;
SafetyHookInline hkPresent, hkPresent1;

void* VFunc(void* obj, size_t index) { return (*reinterpret_cast<void***>(obj))[index]; }

bool ContentRect(UINT w, UINT h, UINT& x0, UINT& y0, UINT& cw, UINT& ch) {
    if (!w || !h) return false;
    double r = (double)w / h, native = 16.0 / 9.0;
    if (std::abs(r - native) / native < 0.005) return false;
    if (r < native) { cw = w; ch = (UINT)std::lround(w * 9.0 / 16.0); x0 = 0; y0 = (h - ch) / 2; }
    else { ch = h; cw = (UINT)std::lround(h * 16.0 / 9.0); y0 = 0; x0 = (w - cw) / 2; }
    return cw > 0 && ch > 0;
}

void LogScreenRatio(UINT w, UINT h) {
    if (!w || !h || (w == loggedW && h == loggedH)) return;
    UINT g = std::gcd(w, h);
    if ((w / g) * 9 == (h / g) * 16) return;
    loggedW = w;
    loggedH = h;
    SPDLOG_INFO("Adjust aspect ratio to {}:{} for {}x{}.", w / g, h / g, w, h);
}

bool Fail(const char* step, HRESULT hr) {
    SPDLOG_ERROR("Stretch disabled: {} failed (0x{:08X}).", step, (unsigned)hr);
    return false;
}

bool CreateObjects11() {
    if (d11VS) return true;
    d11Device->GetImmediateContext(&d11Ctx);
    HRESULT hr;
    if (FAILED(hr = d11Device->CreateVertexShader(kStretchVS11, sizeof(kStretchVS11), nullptr, &d11VS))) return Fail("CreateVertexShader", hr);
    if (FAILED(hr = d11Device->CreatePixelShader(kStretchPS11, sizeof(kStretchPS11), nullptr, &d11PS))) return Fail("CreatePixelShader", hr);
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(hr = d11Device->CreateSamplerState(&sd, &d11Sampler))) return Fail("CreateSamplerState", hr);
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    if (FAILED(hr = d11Device->CreateRasterizerState(&rd, &d11Raster))) return Fail("CreateRasterizerState", hr);
    return true;
}

DXGI_FORMAT ViewFormat(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return f;
    }
}

bool EnsureSrc11(DXGI_FORMAT fmt, UINT w, UINT h) {
    if (d11Src && srcW == w && srcH == h && srcFormat == fmt) return true;
    if (d11SrcView) { d11SrcView->Release(); d11SrcView = nullptr; }
    if (d11Src) { d11Src->Release(); d11Src = nullptr; }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = fmt;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    HRESULT hr;
    if (FAILED(hr = d11Device->CreateTexture2D(&td, nullptr, &d11Src))) return Fail("CreateTexture2D", hr);
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = ViewFormat(fmt);
    sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sv.Texture2D.MipLevels = 1;
    if (FAILED(hr = d11Device->CreateShaderResourceView(d11Src, &sv, &d11SrcView))) return Fail("CreateShaderResourceView", hr);
    srcW = w; srcH = h; srcFormat = fmt;
    return true;
}

void Present11(IDXGISwapChain* self) {
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(self->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))) return;
    D3D11_TEXTURE2D_DESC bd;
    bb->GetDesc(&bd);
    LogScreenRatio(bd.Width, bd.Height);
    UINT x0, y0, cw, ch;
    if (!ContentRect(bd.Width, bd.Height, x0, y0, cw, ch) || bd.SampleDesc.Count != 1) { bb->Release(); return; }

    std::lock_guard lk(mtx);
    if (!CreateObjects11() || !EnsureSrc11(bd.Format, cw, ch)) {
        failed = true;
        bb->Release();
        return;
    }
    ID3D11RenderTargetView* rtv = nullptr;
    D3D11_RENDER_TARGET_VIEW_DESC rv{};
    rv.Format = ViewFormat(bd.Format);
    rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    if (HRESULT hr = d11Device->CreateRenderTargetView(bb, &rv, &rtv); FAILED(hr)) {
        failed = true;
        Fail("CreateRenderTargetView", hr);
        bb->Release();
        return;
    }
    D3D11_BOX box{ x0, y0, 0, x0 + cw, y0 + ch, 1 };
    d11Ctx->CopySubresourceRegion(d11Src, 0, 0, 0, 0, bb, 0, &box);

    ID3D11RenderTargetView* oldRtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* oldDsv = nullptr;
    d11Ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRtv, &oldDsv);
    ID3D11BlendState* oldBlend = nullptr;
    FLOAT oldFactor[4];
    UINT oldMask;
    d11Ctx->OMGetBlendState(&oldBlend, oldFactor, &oldMask);
    ID3D11DepthStencilState* oldDepth = nullptr;
    UINT oldRef;
    d11Ctx->OMGetDepthStencilState(&oldDepth, &oldRef);
    UINT vpCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_VIEWPORT oldVp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    d11Ctx->RSGetViewports(&vpCount, oldVp);
    ID3D11RasterizerState* oldRaster = nullptr;
    d11Ctx->RSGetState(&oldRaster);
    D3D11_PRIMITIVE_TOPOLOGY oldTopo;
    d11Ctx->IAGetPrimitiveTopology(&oldTopo);
    ID3D11InputLayout* oldLayout = nullptr;
    d11Ctx->IAGetInputLayout(&oldLayout);
    ID3D11VertexShader* oldVS = nullptr;
    d11Ctx->VSGetShader(&oldVS, nullptr, nullptr);
    ID3D11PixelShader* oldPS = nullptr;
    d11Ctx->PSGetShader(&oldPS, nullptr, nullptr);
    ID3D11GeometryShader* oldGS = nullptr;
    d11Ctx->GSGetShader(&oldGS, nullptr, nullptr);
    ID3D11HullShader* oldHS = nullptr;
    d11Ctx->HSGetShader(&oldHS, nullptr, nullptr);
    ID3D11DomainShader* oldDS = nullptr;
    d11Ctx->DSGetShader(&oldDS, nullptr, nullptr);
    ID3D11ShaderResourceView* oldSrv = nullptr;
    d11Ctx->PSGetShaderResources(0, 1, &oldSrv);
    ID3D11SamplerState* oldSampler = nullptr;
    d11Ctx->PSGetSamplers(0, 1, &oldSampler);

    d11Ctx->OMSetRenderTargets(1, &rtv, nullptr);
    d11Ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    d11Ctx->OMSetDepthStencilState(nullptr, 0);
    D3D11_VIEWPORT vp{ 0, 0, (float)bd.Width, (float)bd.Height, 0, 1 };
    d11Ctx->RSSetViewports(1, &vp);
    d11Ctx->RSSetState(d11Raster);
    d11Ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    d11Ctx->IASetInputLayout(nullptr);
    d11Ctx->VSSetShader(d11VS, nullptr, 0);
    d11Ctx->PSSetShader(d11PS, nullptr, 0);
    d11Ctx->GSSetShader(nullptr, nullptr, 0);
    d11Ctx->HSSetShader(nullptr, nullptr, 0);
    d11Ctx->DSSetShader(nullptr, nullptr, 0);
    d11Ctx->PSSetShaderResources(0, 1, &d11SrcView);
    d11Ctx->PSSetSamplers(0, 1, &d11Sampler);
    d11Ctx->Draw(3, 0);
    ID3D11ShaderResourceView* nullSrv = nullptr;
    d11Ctx->PSSetShaderResources(0, 1, &nullSrv);

    d11Ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRtv, oldDsv);
    d11Ctx->OMSetBlendState(oldBlend, oldFactor, oldMask);
    d11Ctx->OMSetDepthStencilState(oldDepth, oldRef);
    d11Ctx->RSSetViewports(vpCount, oldVp);
    d11Ctx->RSSetState(oldRaster);
    d11Ctx->IASetPrimitiveTopology(oldTopo);
    d11Ctx->IASetInputLayout(oldLayout);
    d11Ctx->VSSetShader(oldVS, nullptr, 0);
    d11Ctx->PSSetShader(oldPS, nullptr, 0);
    d11Ctx->GSSetShader(oldGS, nullptr, 0);
    d11Ctx->HSSetShader(oldHS, nullptr, 0);
    d11Ctx->DSSetShader(oldDS, nullptr, 0);
    d11Ctx->PSSetShaderResources(0, 1, &oldSrv);
    d11Ctx->PSSetSamplers(0, 1, &oldSampler);
    for (auto* r : oldRtv) if (r) r->Release();
    IUnknown* olds[] = { oldDsv, oldBlend, oldDepth, oldRaster, oldLayout, oldVS, oldPS, oldGS, oldHS, oldDS, oldSrv, oldSampler };
    for (auto* o : olds) if (o) o->Release();
    rtv->Release();
    bb->Release();
}

void OnPresent(IDXGISwapChain* self, UINT flags) {
    if (inHook || (flags & DXGI_PRESENT_TEST) || self != swapChain || failed) return;
    inHook = true;
    if (d11Device) Present11(self);
    inHook = false;
}

HRESULT STDMETHODCALLTYPE Present(IDXGISwapChain* self, UINT sync, UINT flags) {
    OnPresent(self, flags);
    bool prev = inHook;
    inHook = true;
    HRESULT hr = hkPresent.call<HRESULT>(self, sync, flags);
    inHook = prev;
    return hr;
}

HRESULT STDMETHODCALLTYPE Present1(IDXGISwapChain1* self, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* p) {
    OnPresent(self, flags);
    bool prev = inHook;
    inHook = true;
    HRESULT hr = hkPresent1.call<HRESULT>(self, sync, flags, p);
    inHook = prev;
    return hr;
}

void Adopt(IUnknown* dev, IDXGISwapChain* sc) {
    ID3D11Device* d11 = nullptr;
    if (dev && SUCCEEDED(dev->QueryInterface(__uuidof(ID3D11Device), (void**)&d11))) {
        if (d11Device != d11) {
            if (d11Device) d11Device->Release();
            d11Device = d11;
        } else d11->Release();
    } else return;
    swapChain = sc;
    if (hkPresent) return;
    hkPresent = safetyhook::create_inline(VFunc(sc, 8), &Present);
    IDXGISwapChain1* sc1 = nullptr;
    if (SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain1), (void**)&sc1))) {
        hkPresent1 = safetyhook::create_inline(VFunc(sc1, 22), &Present1);
        sc1->Release();
    }
    SPDLOG_INFO("Stretch hooks installed.");
}

HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(IDXGIFactory2* self, IUnknown* dev, HWND w, const DXGI_SWAP_CHAIN_DESC1* desc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fs, IDXGIOutput* out, IDXGISwapChain1** ppSc) {
    bool prev = inHook;
    inHook = true;
    HRESULT hr = hkCreateSwapChainForHwnd.call<HRESULT>(self, dev, w, desc, fs, out, ppSc);
    inHook = prev;
    if (!prev && SUCCEEDED(hr) && ppSc && *ppSc) Adopt(dev, *ppSc);
    return hr;
}

HRESULT STDMETHODCALLTYPE CreateSwapChain(IDXGIFactory* self, IUnknown* dev, DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** ppSc) {
    bool prev = inHook;
    inHook = true;
    HRESULT hr = hkCreateSwapChain.call<HRESULT>(self, dev, desc, ppSc);
    inHook = prev;
    if (!prev && SUCCEEDED(hr) && ppSc && *ppSc) Adopt(dev, *ppSc);
    return hr;
}

void Install() {
    if (!bEnabled) return;
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    auto create = dxgi ? reinterpret_cast<HRESULT(WINAPI*)(REFIID, void**)>(GetProcAddress(dxgi, "CreateDXGIFactory1")) : nullptr;
    IDXGIFactory2* f = nullptr;
    if (!create || FAILED(create(__uuidof(IDXGIFactory2), (void**)&f)) || !f) {
        SPDLOG_ERROR("Stretch disabled: no DXGI factory.");
        return;
    }
    hkCreateSwapChain = safetyhook::create_inline(VFunc(f, 10), &CreateSwapChain);
    hkCreateSwapChainForHwnd = safetyhook::create_inline(VFunc(f, 15), &CreateSwapChainForHwnd);
    f->Release();
}

}

int PatchImmediates(const char* pattern, std::ptrdiff_t offset, int value) {
    auto hits = Memory::PatternScanAll(wo4uModule, pattern);
    for (auto* hit : hits) Memory::Write(hit + offset, value);
    return static_cast<int>(hits.size());
}

void CustomResolution() {
    if (!bCustomResolution) return;
    if (iCustomResX <= 0 || iCustomResY <= 0) {
        auto desktop = Util::GetPhysicalDesktopDimensions();
        iCustomResX = desktop.first;
        iCustomResY = desktop.second;
    }
    if (iCustomResX <= 0 || iCustomResY <= 0 || iCustomResX > 9999 || iCustomResY > 9999) {
        SPDLOG_ERROR("Custom Resolution: invalid size {}x{}.", iCustomResX, iCustomResY);
        return;
    }

    int widths = PatchImmediates("C7 44 24 ?? 00 0A 00 00 C7 44 24 ?? 00 0F 00 00", 12, iCustomResX)
        + PatchImmediates("C7 45 ?? 00 0A 00 00 C7 45 ?? 00 0F 00 00", 10, iCustomResX)
        + PatchImmediates("C7 40 ?? 00 0A 00 00 C7 40 ?? 00 0F 00 00", 10, iCustomResX);
    int heights = PatchImmediates("C7 44 24 ?? A0 05 00 00 C7 44 24 ?? 70 08 00 00", 12, iCustomResY)
        + PatchImmediates("C7 45 ?? A0 05 00 00 C7 45 ?? 70 08 00 00", 10, iCustomResY)
        + PatchImmediates("C7 40 ?? A0 05 00 00 C7 40 ?? 70 08 00 00", 10, iCustomResY);
    SPDLOG_INFO("Custom Resolution: replaced 3840x2160 with {}x{} ({} width, {} height sites).", iCustomResX, iCustomResY, widths, heights);

    if (std::uint8_t* label = Memory::PatternScan(wo4uModule, "00 33 38 34 30 C3 97 32 31 36 30 00")) {
        std::string text = std::to_string(iCustomResX) + "\xC3\x97" + std::to_string(iCustomResY);
        char buffer[11] = {};
        std::memcpy(buffer, text.data(), text.size());
        Memory::PatchBytes(label + 1, buffer, sizeof(buffer));
        SPDLOG_INFO("Custom Resolution: menu entry renamed.");
    } else {
        SPDLOG_ERROR("Custom Resolution: menu entry not found.");
    }
}

void ApplyHooks() {
    std::uint8_t* ARScanResult = Memory::PatternScan(wo4uModule, "F3 0F 11 AB 4C 01 00 00 F3 0F 11 8B 48 01 00 00 F3 0F 11 93 40 01 00 00");
    
    if (ARScanResult) {
        SPDLOG_INFO("WO4U AR Fix: Found instruction pattern! Hooking...");
        
        static SafetyHookMid ARMidHook{};
        ARMidHook = safetyhook::create_mid(ARScanResult,
            [](SafetyHookContext& ctx) {
                ctx.xmm5.f32[0] = fTargetRatio;
            });
    }
    else {
        SPDLOG_ERROR("WO4U AR Fix: Pattern scan failed.");
    }

    if (fFOVScale == 1.0f) return;
    auto fovHits = Memory::PatternScanAll(wo4uModule, "F3 0F 11 ?? E8 00 00 00 F3 0F 11 ?? D4 00 00 00");
    if (fovHits.size() != 1) {
        SPDLOG_ERROR("WO4U FOV Fix: FOV code found {} times (expected 1); FOV not changed.", fovHits.size());
        return;
    }
    static SafetyHookMid FOVMidHook{};
    FOVMidHook = safetyhook::create_mid(fovHits[0],
        [](SafetyHookContext& ctx) {
            ctx.xmm2.f32[0] *= fFOVScale;
            ctx.xmm7.f32[0] *= fFOVScale;
        });
    SPDLOG_INFO("WO4U FOV Fix: hooked with scale {:.4f}.", fFOVScale);
}

DWORD __stdcall Main(void*) {
    Logging();
    Configuration();
    Stretch::Install();
    
    SPDLOG_INFO("Waiting for WO4U.dll...");
    while (!wo4uModule) {
        wo4uModule = GetModuleHandleA("WO4U.dll");
        if (!wo4uModule) Sleep(1);
    }

    CustomResolution();
    Sleep(2000);
    
    ApplyHooks();
    return 1;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        thisModule = hModule;
        if (HANDLE mainHandle = CreateThread(NULL, 0, Main, 0, NULL, 0)) {
            SetThreadPriority(mainHandle, THREAD_PRIORITY_HIGHEST);
            CloseHandle(mainHandle);
        }
    }
    return TRUE;
}