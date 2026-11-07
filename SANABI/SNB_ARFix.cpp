#include "stdafx.h"
#include "version.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <inipp/inipp.h>
#include <safetyhook.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>

namespace
{
    HMODULE thisModule = nullptr;
    SafetyHookMid propertiesEntryHook;
    SafetyHookMid propertiesExitHook;
    std::atomic<bool> hooksReady{ false };
    std::atomic<bool> invalidCameraLogged{ false };
    bool enabled = true;
    bool maintainFOV = true;

    struct PixelPerfectCamera
    {
        std::byte header[0x18];
        float zoomScale;
        int assetsPPU;
        int refResolutionX;
        int refResolutionY;
        bool upscaleRT;
        bool cropFrameX;
        bool cropFrameY;
        bool stretchFill;
    };

    struct Rect
    {
        float x, y, width, height;
    };

    struct CameraProperties
    {
        std::byte header[0x10];
        PixelPerfectCamera* component;
        PixelPerfectCamera* serializableComponent;
        float originalOrthoSize;
        bool hasPostProcessLayer;
        bool cropFrameXAndY;
        bool cropFrameXOrY;
        bool useStretchFill;
        int zoom;
        bool useOffscreenRT;
        std::byte padding[3];
        int offscreenRTWidth;
        int offscreenRTHeight;
        Rect pixelRect;
        float orthoSize;
        float unitsPerPixel;
    };

    static_assert(sizeof(void*) == 8, "SANABI requires an x64 build.");
    static_assert(sizeof(bool) == 1);
    static_assert(sizeof(Rect) == 0x10);
    static_assert(offsetof(PixelPerfectCamera, assetsPPU) == 0x1C);
    static_assert(offsetof(PixelPerfectCamera, upscaleRT) == 0x28);
    static_assert(offsetof(CameraProperties, component) == 0x10);
    static_assert(offsetof(CameraProperties, useOffscreenRT) == 0x2C);
    static_assert(offsetof(CameraProperties, pixelRect) == 0x38);
    static_assert(offsetof(CameraProperties, orthoSize) == 0x48);

    void LogInvalidCamera()
    {
        if (!invalidCameraLogged.exchange(true))
            SPDLOG_ERROR("Invalid camera or render dimensions; leaving this camera calculation unchanged.");
    }

    bool ValidCamera(const CameraProperties* properties)
    {
        return properties && properties->component &&
            properties->component->assetsPPU > 0 &&
            properties->component->refResolutionX > 0 &&
            properties->component->refResolutionY > 0;
    }

    void BeforeCalculateProperties(SafetyHookContext& ctx)
    {
        if (!hooksReady.load())
            return;

        auto* properties = reinterpret_cast<CameraProperties*>(ctx.rcx);
        const int width = static_cast<int>(ctx.rdx);
        const int height = static_cast<int>(ctx.r8);
        if (!ValidCamera(properties) || width <= 0 || height <= 0)
        {
            LogInvalidCamera();
            return;
        }

        auto* camera = properties->component;
        camera->cropFrameX = false;
        camera->cropFrameY = false;
        camera->stretchFill = false;
        camera->upscaleRT = false;
    }

    void AfterCalculateProperties(SafetyHookContext& ctx)
    {
        if (!hooksReady.load())
            return;

        auto* properties = reinterpret_cast<CameraProperties*>(ctx.rbx);
        // At this epilogue, r14 has been reused. Width remains in the saved argument.
        int width = 0;
        std::memcpy(&width, reinterpret_cast<const void*>(ctx.rsp + 0xB8), sizeof(width));
        const int height = static_cast<int>(ctx.r13);
        if (!ValidCamera(properties) || width <= 0 || height <= 0)
        {
            LogInvalidCamera();
            return;
        }

        const auto* camera = properties->component;
        const float nativeAspect = static_cast<float>(camera->refResolutionX) / camera->refResolutionY;
        const float aspect = static_cast<float>(width) / height;
        const float framingScale = maintainFOV ? std::max(1.0f, nativeAspect / aspect) : 1.0f;
        properties->orthoSize = 0.5f * camera->refResolutionY / camera->assetsPPU * framingScale;
        properties->cropFrameXAndY = false;
        properties->cropFrameXOrY = false;
        properties->useStretchFill = false;
        properties->useOffscreenRT = false;
        properties->offscreenRTWidth = 0;
        properties->offscreenRTHeight = 0;
        properties->pixelRect = {};

        thread_local int lastWidth = 0;
        thread_local int lastHeight = 0;
        thread_local float lastOrthoSize = 0.0f;
        if (width != lastWidth || height != lastHeight || properties->orthoSize != lastOrthoSize)
        {
            SPDLOG_INFO("Camera: {}x{}, reference {}x{}, PPU {}, base ortho {}. Full viewport, no crop/upscale RT.",
                width, height, camera->refResolutionX, camera->refResolutionY,
                camera->assetsPPU, properties->orthoSize);
            lastWidth = width;
            lastHeight = height;
            lastOrthoSize = properties->orthoSize;
        }
    }

    bool Initialize()
    {
        wchar_t modulePath[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(thisModule, modulePath, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
        {
            OutputDebugStringA("SANABI ARFix: Cannot resolve ASI path.\n");
            MessageBoxW(nullptr, L"Cannot resolve ASI path.", L"SANABI ARFix", MB_OK | MB_ICONERROR);
            return false;
        }
        const auto directory = std::filesystem::path(modulePath).parent_path();
        try
        {
            auto logger = spdlog::rotating_logger_mt(sFixName, (directory / (sFixName + ".log")).string(),
                10 * 1024 * 1024, 1);
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::info);
        }
        catch (const spdlog::spdlog_ex& error)
        {
            OutputDebugStringA(error.what());
            MessageBoxA(nullptr, error.what(), "SANABI ARFix: Log initialization failed", MB_OK | MB_ICONERROR);
            return false;
        }
        SPDLOG_INFO("{} v{}: SANABI 1.3.53 aspect-ratio fix.", sFixName, sFixVersion);

        const auto configPath = directory / (sFixName + ".ini");
        std::error_code configError;
        const bool configExists = std::filesystem::exists(configPath, configError);
        if (configError)
        {
            SPDLOG_ERROR("Cannot inspect configuration {}: {}", configPath.string(), configError.message());
            return false;
        }
        if (configExists)
        {
            std::ifstream file(configPath);
            if (!file)
            {
                SPDLOG_ERROR("Cannot open configuration: {}", configPath.string());
                return false;
            }
            inipp::Ini<char> ini;
            ini.parse(file);
            ini.strip_trailing_comments();
            if (file.bad() || !ini.errors.empty())
            {
                SPDLOG_ERROR("Malformed configuration: {}", configPath.string());
                return false;
            }
            for (const auto& key : { "Enabled", "MaintainFOV" })
            {
                const auto& section = ini.sections["AspectRatio"];
                const auto found = section.find(key);
                bool& value = std::strcmp(key, "Enabled") == 0 ? enabled : maintainFOV;
                if (found != section.end() && !inipp::get_value(section, key, value))
                {
                    SPDLOG_ERROR("Invalid [AspectRatio] {}: {}", key, found->second);
                    return false;
                }
            }
        }
        else
            SPDLOG_INFO("No INI found: using Enabled=true, MaintainFOV=true; automatic window aspect.");

        SPDLOG_INFO("Enabled={}, MaintainFOV={}", enabled, maintainFOV);
        if (!enabled)
            return true;

        HMODULE gameAssembly = nullptr;
        for (int attempt = 0; attempt < 600 && !gameAssembly; ++attempt)
        {
            gameAssembly = GetModuleHandleW(L"GameAssembly.dll");
            if (!gameAssembly)
                Sleep(100);
        }
        if (!gameAssembly)
        {
            SPDLOG_ERROR("GameAssembly.dll not loaded after 60 seconds. No hooks installed.");
            return false;
        }

        auto* base = reinterpret_cast<std::uint8_t*>(gameAssembly);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        SPDLOG_INFO("GameAssembly: timestamp 0x{:X}, image size 0x{:X}",
            nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
        if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            nt->FileHeader.TimeDateStamp != 0x69783BD1 ||
            nt->OptionalHeader.SizeOfImage != 0x45C0000)
        {
            SPDLOG_ERROR("Unsupported GameAssembly build. Expected SANABI 1.3.53; no hooks installed.");
            return false;
        }

        constexpr std::array<std::uint8_t, 16> entryBytes{
            0x89, 0x54, 0x24, 0x10, 0x53, 0x41, 0x55, 0x41,
            0x56, 0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00 };
        constexpr std::array<std::uint8_t, 18> exitBytes{
            0xF3, 0x0F, 0x10, 0x0D, 0x02, 0xE0, 0x18, 0x01,
            0x0F, 0x28, 0x74, 0x24, 0x60, 0x4C, 0x8B, 0x7C, 0x24, 0x70 };
        auto* entry = base + 0x1A27630;
        auto* exit = base + 0x1A27A9E;
        if (std::memcmp(entry, entryBytes.data(), entryBytes.size()) != 0 ||
            std::memcmp(exit, exitBytes.data(), exitBytes.size()) != 0)
        {
            SPDLOG_ERROR("Camera instruction signatures do not match. No hooks installed.");
            return false;
        }

        auto entryResult = SafetyHookMid::create(entry, BeforeCalculateProperties);
        if (!entryResult)
        {
            SPDLOG_ERROR("Cannot install camera entry hook: error {}.", static_cast<int>(entryResult.error().type));
            return false;
        }
        propertiesEntryHook = std::move(*entryResult);
        auto exitResult = SafetyHookMid::create(exit, AfterCalculateProperties);
        if (!exitResult)
        {
            SPDLOG_ERROR("Cannot install camera exit hook: error {}. Removing entry hook.",
                static_cast<int>(exitResult.error().type));
            propertiesEntryHook.reset();
            return false;
        }
        propertiesExitHook = std::move(*exitResult);
        hooksReady.store(true);
        SPDLOG_INFO("Camera hooks installed at GameAssembly+1A27630 and +1A27A9E.");
        return true;
    }

    DWORD WINAPI Main(void*)
    {
        return Initialize() ? 0 : 1;
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        thisModule = module;
        DisableThreadLibraryCalls(module);
        const HANDLE thread = CreateThread(nullptr, 0, Main, nullptr, 0, nullptr);
        if (!thread)
        {
            OutputDebugStringA("SANABI ARFix: Cannot create initialization thread.\n");
            return FALSE;
        }
        CloseHandle(thread);
    }
    return TRUE;
}
