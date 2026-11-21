#include "stdafx.h"
#include "version.h"

#if !defined(_M_X64)
#error Ys8_AspectPatch must be built for Windows x64 (64-bit), not x86.
#endif

#ifdef _DEBUG
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#else
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#endif

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <inipp/inipp.h>
#include <safetyhook.hpp>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <vector>
#include <exception>
#include <filesystem>
#include <fstream>
#include <utility>
#include <windows.h>
#include <d3d11.h>

namespace fs = std::filesystem;

namespace
{
    HMODULE exeModule = GetModuleHandleW(nullptr);
    HMODULE thisModule = nullptr;
    fs::path FixPath;
    constexpr char sConfigFile[] = "Ys8_AspectPatch.ini";
    constexpr char sLogFile[] = "Ys8_AspectPatch.log";

    int iTargetWidth = 1280;
    int iTargetHeight = 800;
    float fCustomAR = 1.6f;
    bool bCameraAspect = true;
    bool bRemoveLetterbox = true;
    bool bSharedProjection = false;
    SafetyHookInline drawIndexedHook;
    SafetyHookInline deferredDrawIndexedHook;
    std::atomic<bool> sceneFillObserved = false;

    bool TextureSize(ID3D11Resource* resource, UINT& width, UINT& height)
    {
        D3D11_RESOURCE_DIMENSION dimension{};
        resource->GetType(&dimension);
        if (dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D)
            return false;
        D3D11_TEXTURE2D_DESC desc{};
        static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
        width = desc.Width;
        height = desc.Height;
        return true;
    }

    bool IsSceneCopy(ID3D11DeviceContext* ctx, const D3D11_VIEWPORT& vp, UINT& width, UINT& height)
    {
        ID3D11RenderTargetView* rtv = nullptr;
        ctx->OMGetRenderTargets(1, &rtv, nullptr);
        if (!rtv)
            return false;
        ID3D11Resource* target = nullptr;
        rtv->GetResource(&target);
        rtv->Release();
        const bool sized = TextureSize(target, width, height);
        target->Release();
        if (!sized || std::fabs(vp.TopLeftX * 2.0f + vp.Width - width) > 2.0f ||
            std::fabs(vp.TopLeftY * 2.0f + vp.Height - height) > 2.0f)
            return false;

        ID3D11ShaderResourceView* srv = nullptr;
        ctx->PSGetShaderResources(0, 1, &srv);
        if (!srv)
            return false;
        ID3D11Resource* source = nullptr;
        srv->GetResource(&source);
        srv->Release();
        UINT sourceWidth = 0, sourceHeight = 0;
        const bool sourceSized = TextureSize(source, sourceWidth, sourceHeight);
        source->Release();
        return sourceSized && sourceWidth == width && sourceHeight == height;
    }

    template<SafetyHookInline& Hook>
    void STDMETHODCALLTYPE DrawIndexedDetour(ID3D11DeviceContext* ctx, UINT count, UINT start, INT base)
    {
        D3D11_VIEWPORT vp{};
        UINT viewports = 1;
        ctx->RSGetViewports(&viewports, &vp);
        static thread_local bool filledThisPass = false;
        UINT width = 0, height = 0;
        if (viewports != 1 || (vp.TopLeftX < 0.5f && vp.TopLeftY < 0.5f)) {
            filledThisPass = false;
            Hook.stdcall<void>(ctx, count, start, base);
            return;
        }
        if (filledThisPass || !IsSceneCopy(ctx, vp, width, height)) {
            Hook.stdcall<void>(ctx, count, start, base);
            return;
        }
        filledThisPass = true;
        const D3D11_VIEWPORT full{ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), vp.MinDepth, vp.MaxDepth };
        ctx->RSSetViewports(1, &full);
        Hook.stdcall<void>(ctx, count, start, base);
        ctx->RSSetViewports(1, &vp);
        if (!sceneFillObserved.exchange(true))
            SPDLOG_INFO("Scene fill active: {}x{} scene, HUD kept in {}x{} at {},{}.",
                        width, height, vp.Width, vp.Height, vp.TopLeftX, vp.TopLeftY);
    }

    DWORD WINAPI InstallSceneFill(void*)
    {
        HMODULE d3d11 = LoadLibraryW(L"d3d11.dll");
        auto create = d3d11 ? reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(d3d11, "D3D11CreateDevice")) : nullptr;
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
        if (!create || (FAILED(create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)) &&
                        FAILED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)))) {
            SPDLOG_ERROR("Scene fill: D3D11 device creation failed.");
            return 1;
        }
        void** table = *reinterpret_cast<void***>(context);
        drawIndexedHook = safetyhook::create_inline(table[12], reinterpret_cast<void*>(&DrawIndexedDetour<drawIndexedHook>));
        ID3D11DeviceContext* deferred = nullptr;
        if (SUCCEEDED(device->CreateDeferredContext(0, &deferred))) {
            void** deferredTable = *reinterpret_cast<void***>(deferred);
            if (deferredTable[12] != table[12])
                deferredDrawIndexedHook = safetyhook::create_inline(deferredTable[12], reinterpret_cast<void*>(&DrawIndexedDetour<deferredDrawIndexedHook>));
            deferred->Release();
        }
        context->Release();
        device->Release();
        if (drawIndexedHook)
            SPDLOG_INFO("Scene fill hook installed.");
        else
            SPDLOG_ERROR("Scene fill hook failed.");
        return 0;
    }

    bool InitializeLogging()
    {
        WCHAR dllPath[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(thisModule, dllPath, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) {
            OutputDebugStringA("Ys8_AspectPatch: Failed to resolve ASI path.\n");
            return false;
        }
        FixPath = fs::path(dllPath).parent_path();

        std::ofstream clearLog(FixPath / sLogFile, std::ios::trunc);
        if (clearLog.is_open()) clearLog.close();

        try {
            auto logger = std::make_shared<spdlog::logger>(
                "Ys8_AspectPatch",
                std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    (FixPath / sLogFile).string(), 5 * 1024 * 1024, 1));
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::info);
            SPDLOG_INFO("Ys8 x64 AspectPatch initialized.");
            return true;
        } catch (const std::exception& error) {
            OutputDebugStringA(error.what());
            OutputDebugStringA("\nYs8_AspectPatch: Logging failed.\n");
            return false;
        }
    }

    template<typename T>
    bool ReadSetting(inipp::Ini<char>& ini, const char* section, const char* key, T& value)
    {
        const auto& values = ini.sections[section];
        if (values.find(key) == values.end())
            return true;
        if (inipp::get_value(values, key, value))
            return true;
        SPDLOG_ERROR("Invalid [{}] {}.", section, key);
        return false;
    }

    bool LoadConfiguration()
    {
        const fs::path path = FixPath / sConfigFile;
        if (!fs::exists(path)) {
            std::ofstream defaultIni(path);
            defaultIni << "[Resolution]\nWidth = 1280\nHeight = 800\n\n"
                          "[AspectRatio]\n; Optional manual override\n; CustomAR = 1.6\n\n"
                          "[Fixes]\nCameraAspect = true\n"
                          "; 3D fills the screen, HUD stays 16:9\nRemoveLetterbox = true\n"
                          "; Shared camera/UI projection: experimental, may distort UI\n"
                          "SharedProjection = false\n";
            defaultIni.close();
            if (!defaultIni) {
                SPDLOG_ERROR("Failed to create {}.", path.string());
                return false;
            }
            SPDLOG_INFO("Created default {}.", sConfigFile);
        }

        std::ifstream iniFile(path);
        if (!iniFile) {
            SPDLOG_ERROR("Failed to open {}.", path.string());
            return false;
        }

        inipp::Ini<char> ini;
        ini.parse(iniFile);
        ini.strip_trailing_comments();

        if (!ini.errors.empty()) {
            for (const auto& error : ini.errors)
                SPDLOG_ERROR("INI parse error: {}", error);
            return false;
        }
        if (!ReadSetting(ini, "Resolution", "Width", iTargetWidth) ||
            !ReadSetting(ini, "Resolution", "Height", iTargetHeight) ||
            !ReadSetting(ini, "Fixes", "CameraAspect", bCameraAspect) ||
            !ReadSetting(ini, "Fixes", "RemoveLetterbox", bRemoveLetterbox) ||
            !ReadSetting(ini, "Fixes", "SharedProjection", bSharedProjection))
            return false;
        if (iTargetWidth <= 0 || iTargetHeight <= 0) {
            SPDLOG_ERROR("Resolution Width/Height must be positive.");
            return false;
        }
        fCustomAR = static_cast<float>(iTargetWidth) / static_cast<float>(iTargetHeight);
        if (!ReadSetting(ini, "AspectRatio", "CustomAR", fCustomAR))
            return false;
        if (!std::isfinite(fCustomAR) || fCustomAR <= 0.0f) {
            SPDLOG_ERROR("CustomAR must be finite and positive.");
            return false;
        }

        SPDLOG_INFO("Config loaded -> Resolution: {}x{}, Target Aspect Ratio: {:.5f}",
                    iTargetWidth, iTargetHeight, fCustomAR);
        return true;
    }

    std::vector<std::uint8_t*> FindPattern(HMODULE module, std::string_view text)
    {
        std::vector<std::optional<std::uint8_t>> pattern;
        for (std::size_t i = 0; i < text.size();)
        {
            if (text[i] == ' ')
                ++i;
            else if (text[i] == '?')
            {
                pattern.emplace_back(std::nullopt);
                i += (i + 1 < text.size() && text[i + 1] == '?') ? 2 : 1;
            }
            else
            {
                pattern.emplace_back(static_cast<std::uint8_t>(std::stoul(std::string(text.substr(i, 2)), nullptr, 16)));
                i += 2;
            }
        }
        std::vector<std::uint8_t*> matches;
        auto* base = reinterpret_cast<std::uint8_t*>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const auto* section = IMAGE_FIRST_SECTION(nt);
        for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section)
        {
            if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE) || section->Misc.VirtualSize < pattern.size())
                continue;
            auto* start = base + section->VirtualAddress;
            const std::size_t last = section->Misc.VirtualSize - pattern.size();
            for (std::size_t offset = 0; offset <= last; ++offset)
            {
                std::size_t i = 0;
                while (i < pattern.size() && (!pattern[i] || start[offset + i] == *pattern[i]))
                    ++i;
                if (i == pattern.size())
                    matches.push_back(start + offset);
            }
        }
        return matches;
    }

    std::uint8_t* FindUnique(HMODULE module, const char* name, std::string_view pattern, std::ptrdiff_t offset)
    {
        const auto matches = FindPattern(module, pattern);
        if (matches.size() != 1)
        {
            SPDLOG_ERROR("{} pattern: expected 1 match, found {}.", name, matches.size());
            return nullptr;
        }
        auto* address = matches.front() + offset;
        SPDLOG_INFO("{} found at +{:X}.", name, address - reinterpret_cast<std::uint8_t*>(module));
        return address;
    }

    bool WriteFloat(std::uint8_t* address, float value)
    {
        DWORD oldProtect = 0;
        if (!VirtualProtect(address, sizeof(value), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            SPDLOG_ERROR("VirtualProtect failed at 0x{:X}: {}.", reinterpret_cast<std::uintptr_t>(address), GetLastError());
            return false;
        }
        std::memcpy(address, &value, sizeof(value));
        DWORD unused = 0;
        const BOOL restored = VirtualProtect(address, sizeof(value), oldProtect, &unused);
        if (!restored)
            SPDLOG_ERROR("Memory protection restoration failed: {}.", GetLastError());
        const BOOL flushed = FlushInstructionCache(GetCurrentProcess(), address, sizeof(value));
        if (!flushed)
            SPDLOG_ERROR("Instruction-cache flush failed: {}.", GetLastError());
        return restored && flushed;
    }

    bool WriteByte(std::uint8_t* address, std::uint8_t value)
    {
        DWORD oldProtect = 0;
        if (!VirtualProtect(address, 1, PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;
        *address = value;
        VirtualProtect(address, 1, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), address, 1);
        return true;
    }

    void ApplyNativeRender()
    {
        auto* wide = FindUnique(exeModule, "Render size wide clamp",
            "66 0F 2F 0D ?? ?? ?? ?? 76 1A C1 E1 04 B8 39 8E E3 38", 8);
        auto* narrow = FindUnique(exeModule, "Render size narrow clamp",
            "F2 0F 10 05 ?? ?? ?? ?? 66 0F 2F C1 76 10 8D 04 C0 99 83 E2 0F", 12);
        if (wide && narrow && WriteByte(wide, 0xEB) && WriteByte(narrow, 0xEB))
            SPDLOG_INFO("Native render resolution applied.");
        else
            SPDLOG_ERROR("Native render resolution not applied.");
    }

    bool ApplyMemoryPatch()
    {
        constexpr float originalAR = 16.0f / 9.0f;
        std::uint8_t* aspect1 = nullptr;
        std::uint8_t* aspect2 = nullptr;
        std::uint8_t* sharedAR = nullptr;
        if (bCameraAspect) {
            aspect1 = FindUnique(exeModule, "Camera aspect 1", "C7 87 30 08 00 00 39 8E E3 3F", 6);
            aspect2 = FindUnique(exeModule, "Camera aspect 2", "C7 83 30 08 00 00 39 8E E3 3F", 6);
            if (!aspect1 || !aspect2)
                return false;
        }
        if (bSharedProjection) {
            auto* load = FindUnique(exeModule, "Shared projection",
                "F3 0F 10 1D ?? ?? ?? ?? F3 0F 10 15 ?? ?? ?? ?? 0F 11 03 F3 0F 10 05", 8);
            if (!load)
                return false;
            std::int32_t displacement = 0;
            std::memcpy(&displacement, load + 4, sizeof(displacement));
            sharedAR = load + 8 + displacement;
            float value = 0.0f;
            std::memcpy(&value, sharedAR, sizeof(value));
            if (value != originalAR) {
                SPDLOG_ERROR("Shared projection value is {}, expected 16/9; nothing changed.", value);
                return false;
            }
        }

        const auto rollback = [=]() {
            if (bCameraAspect) {
                const bool restored1 = WriteFloat(aspect1, originalAR);
                const bool restored2 = WriteFloat(aspect2, originalAR);
                if (!restored1 || !restored2)
                    SPDLOG_ERROR("Camera patch rollback failed; restart the game.");
            }
            if (bSharedProjection && !WriteFloat(sharedAR, originalAR))
                SPDLOG_ERROR("Shared projection rollback failed; restart the game.");
        };
        if (bCameraAspect && (!WriteFloat(aspect1, fCustomAR) || !WriteFloat(aspect2, fCustomAR))) {
            rollback();
            return false;
        }
        if (bSharedProjection && !WriteFloat(sharedAR, fCustomAR)) {
            rollback();
            return false;
        }
        ApplyNativeRender();
        if (bRemoveLetterbox) {
            if (HANDLE thread = CreateThread(nullptr, 0, InstallSceneFill, nullptr, 0, nullptr))
                CloseHandle(thread);
        }
        SPDLOG_INFO("Applied: CameraAspect={}, RemoveLetterbox={}, SharedProjection={}.",
                    bCameraAspect, bRemoveLetterbox, bSharedProjection);
        return true;
    }

    DWORD WINAPI SetupThread(void*)
    {
        if (!InitializeLogging()) return 1;
        try {
            HMODULE pinned = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                   reinterpret_cast<LPCWSTR>(&SetupThread), &pinned)) {
                SPDLOG_ERROR("Failed to pin ASI for active hooks: {}.", GetLastError());
                return 1;
            }
            if (!LoadConfiguration()) return 1;
            return ApplyMemoryPatch() ? 0 : 1;
        } catch (const std::exception& error) {
            SPDLOG_ERROR("Setup failed: {}.", error.what());
            return 1;
        }
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        thisModule = hModule;
        SetupThread(nullptr);
    }
    return TRUE;
}