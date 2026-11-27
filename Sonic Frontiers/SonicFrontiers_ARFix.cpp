#include "stdafx.h"
#include "version.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <safetyhook.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

static_assert(sizeof(void*) == 8, "Build for Windows x64.");

namespace
{
    constexpr const wchar_t* gameExecutable = L"SonicFrontiers.exe";
    constexpr float originalAspect = 16.0f / 9.0f;

    HMODULE thisModule = nullptr;

    void ReportFailure(const char* message)
    {
        SPDLOG_ERROR("{}", message);
        MessageBoxA(nullptr, message, sFixName.c_str(), MB_OK | MB_ICONERROR);
    }

    std::optional<std::filesystem::path> ModulePath(HMODULE module)
    {
        wchar_t path[32768]{};
        const DWORD length = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
        if (length == 0 || length >= std::size(path))
            return std::nullopt;
        return std::filesystem::path(path);
    }
}

namespace Config
{
    namespace
    {
        constexpr float minFovMultiplier = 0.5f;
        constexpr float maxFovMultiplier = 2.0f;

        float fovMultiplier = 1.0f;

        std::string ReadAsciiText(const std::filesystem::path& ini, const wchar_t* section, const wchar_t* key)
        {
            wchar_t wide[64]{};
            GetPrivateProfileStringW(section, key, L"", wide, static_cast<DWORD>(std::size(wide)), ini.c_str());
            std::string ascii;
            for (const wchar_t* character = wide; *character && *character < 0x80; ++character)
                ascii.push_back(static_cast<char>(*character));
            return ascii;
        }

        float ReadFovMultiplier(const std::filesystem::path& ini)
        {
            const std::string text = ReadAsciiText(ini, L"FOV", L"Multiplier");
            if (text.empty())
                return 1.0f;
            float value = 0.0f;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            if (parsed.ec == std::errc{} && value >= minFovMultiplier && value <= maxFovMultiplier)
                return value;
            SPDLOG_WARN("FOV Multiplier must be {}-{}; using 1.0.", minFovMultiplier, maxFovMultiplier);
            return 1.0f;
        }

        void Load(const std::filesystem::path& ini)
        {
            fovMultiplier = ReadFovMultiplier(ini);
            SPDLOG_INFO("FOV Multiplier={:.3f}.", fovMultiplier);
        }
    }
}

namespace GameImage
{
    namespace
    {
        using Bytes = std::span<const std::uint8_t>;

        std::uintptr_t base = 0;
        std::size_t size = 0;

        void Load()
        {
            base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            size = nt->OptionalHeader.SizeOfImage;
        }

        std::uintptr_t Offset(std::uintptr_t address)
        {
            return address - base;
        }

        bool IsExecutable(const MEMORY_BASIC_INFORMATION& region)
        {
            constexpr DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            return region.State == MEM_COMMIT && (region.Protect & executable) && !(region.Protect & PAGE_GUARD);
        }

        std::vector<Bytes> CodeRegions()
        {
            std::vector<Bytes> regions;
            MEMORY_BASIC_INFORMATION region{};
            for (std::uintptr_t address = base; address < base + size;
                 address = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize)
            {
                if (!VirtualQuery(reinterpret_cast<const void*>(address), &region, sizeof(region)))
                    break;
                if (IsExecutable(region))
                    regions.emplace_back(static_cast<const std::uint8_t*>(region.BaseAddress), region.RegionSize);
            }
            return regions;
        }
    }
}

namespace
{
    class CodePattern
    {
    public:
        explicit CodePattern(std::string_view text)
        {
            constexpr int hexadecimal = 16;
            for (const auto word : std::views::split(text, ' '))
            {
                const std::string byte(word.begin(), word.end());
                bytes.push_back(byte == "?" ? anyByte : std::stoi(byte, nullptr, hexadecimal));
            }
        }

        std::vector<std::uintptr_t> FindIn(const std::vector<GameImage::Bytes>& regions) const
        {
            std::vector<std::uintptr_t> matches;
            for (GameImage::Bytes code : regions)
            {
                while (const void* const candidate = std::memchr(code.data(), bytes.front(), code.size()))
                {
                    code = code.subspan(static_cast<const std::uint8_t*>(candidate) - code.data());
                    if (MatchesStartOf(code))
                        matches.push_back(reinterpret_cast<std::uintptr_t>(code.data()));
                    code = code.subspan(1);
                }
            }
            return matches;
        }

    private:
        static constexpr int anyByte = -1;

        std::vector<int> bytes;

        bool MatchesStartOf(GameImage::Bytes code) const
        {
            const auto isSame = [](int expected, std::uint8_t actual) {
                return expected == anyByte || expected == actual;
            };
            return code.size() >= bytes.size() && std::ranges::equal(bytes, code.first(bytes.size()), isSame);
        }
    };

    struct Site
    {
        const char* name;
        CodePattern pattern;
        std::uintptr_t address = 0;
    };

    bool FindAll(std::span<Site> sites)
    {
        constexpr int attempts = 300;
        constexpr DWORD retryDelayMs = 100;
        for (int attempt = 0; attempt < attempts; ++attempt)
        {
            const auto regions = GameImage::CodeRegions();
            bool complete = true;
            for (Site& site : sites)
            {
                if (site.address)
                    continue;
                const auto matches = site.pattern.FindIn(regions);
                if (matches.size() == 1)
                    site.address = matches.front();
                else
                    complete = false;
            }
            if (complete)
                return true;
            Sleep(retryDelayMs);
        }
        for (const Site& site : sites)
            if (!site.address)
                SPDLOG_ERROR("Cannot find {}.", site.name);
        return false;
    }

    bool WriteCode(std::uintptr_t address, std::span<const std::uint8_t> bytes)
    {
        DWORD oldProtection = 0;
        void* const target = reinterpret_cast<void*>(address);
        if (!VirtualProtect(target, bytes.size(), PAGE_EXECUTE_READWRITE, &oldProtection))
            return false;
        std::memcpy(target, bytes.data(), bytes.size());
        VirtualProtect(target, bytes.size(), oldProtection, &oldProtection);
        FlushInstructionCache(GetCurrentProcess(), target, bytes.size());
        return true;
    }

    template <typename Hook>
    bool Enable(Hook& hook, const char* name, std::uintptr_t address)
    {
        if (const auto enabled = hook.enable(); !enabled)
        {
            SPDLOG_ERROR("Cannot enable {} hook.", name);
            return false;
        }
        SPDLOG_INFO("Hooked {} at game+0x{:X}.", name, GameImage::Offset(address));
        return true;
    }

    float WidenedFov(float verticalFov, float aspect, float multiplier)
    {
        constexpr float maxFov = 3.0f;
        const float scale = std::max(1.0f, originalAspect / aspect) * multiplier;
        return std::min(maxFov, 2.0f * std::atan(std::tan(verticalFov * 0.5f) * scale));
    }

    bool IsScreenPerspective(float verticalFov, float aspect)
    {
        constexpr float minScreenAspect = 1.01f;
        constexpr float maxScreenAspect = 10.0f;
        return aspect > minScreenAspect && aspect < maxScreenAspect && verticalFov > 0.0f && verticalFov < 3.0f;
    }
}

namespace RenderSize
{
    namespace
    {
        constexpr std::uint8_t jumpShort = 0xEB;
        constexpr std::uint8_t useWindowHeight[] = { 0x44, 0x8B, 0xC6, 0x90, 0x90 };
        constexpr std::size_t fitBranchOffset = 18;
        constexpr std::size_t heightOffset = 49;

        bool Apply(std::uintptr_t fit)
        {
            const std::uint8_t branch[] = { jumpShort };
            if (!WriteCode(fit + fitBranchOffset, branch) || !WriteCode(fit + heightOffset, useWindowHeight))
                return false;
            SPDLOG_INFO("Render size follows the window (game+0x{:X}).", GameImage::Offset(fit));
            return true;
        }
    }
}

namespace Camera
{
    namespace
    {
        SafetyHookInline setPerspectiveHook;
        SafetyHookInline buildPerspectiveHook;
        thread_local bool insideSetPerspective = false;
        std::atomic<float> screenAspect = originalAspect;

        void OnSetPerspective(void* camera, float verticalFov, float aspect, float nearClip, float farClip)
        {
            if (IsScreenPerspective(verticalFov, aspect))
            {
                screenAspect = aspect;
                verticalFov = WidenedFov(verticalFov, aspect, Config::fovMultiplier);
            }
            insideSetPerspective = true;
            setPerspectiveHook.call<void>(camera, verticalFov, aspect, nearClip, farClip);
            insideSetPerspective = false;
        }

        void* OnBuildPerspective(void* matrix, float verticalFov, float aspect, float nearClip, float farClip)
        {
            if (!insideSetPerspective && IsScreenPerspective(verticalFov, aspect))
                verticalFov = WidenedFov(verticalFov, aspect, 1.0f);
            return buildPerspectiveHook.call<void*>(matrix, verticalFov, aspect, nearClip, farClip);
        }

        bool Install(std::uintptr_t setPerspective, std::uintptr_t buildPerspective)
        {
            auto setHook = SafetyHookInline::create(reinterpret_cast<void*>(setPerspective),
                                                    reinterpret_cast<void*>(OnSetPerspective),
                                                    SafetyHookInline::StartDisabled);
            auto buildHook = SafetyHookInline::create(reinterpret_cast<void*>(buildPerspective),
                                                      reinterpret_cast<void*>(OnBuildPerspective),
                                                      SafetyHookInline::StartDisabled);
            if (!setHook || !buildHook)
            {
                SPDLOG_ERROR("Cannot create camera hooks.");
                return false;
            }
            setPerspectiveHook = std::move(*setHook);
            buildPerspectiveHook = std::move(*buildHook);
            return Enable(buildPerspectiveHook, "perspective builder", buildPerspective) &&
                   Enable(setPerspectiveHook, "camera perspective", setPerspective);
        }
    }
}

namespace Vignette
{
    namespace
    {
        constexpr std::size_t divideOffset = 32;

        SafetyHookMid hook;

        void OnCircleScale(SafetyHookContext& context)
        {
            context.xmm1.f32[0] *= Camera::screenAspect / originalAspect;
        }

        bool Install(std::uintptr_t circleScale)
        {
            const std::uintptr_t divide = circleScale + divideOffset;
            auto created = SafetyHookMid::create(reinterpret_cast<void*>(divide), OnCircleScale,
                                                 SafetyHookMid::StartDisabled);
            if (!created)
            {
                SPDLOG_ERROR("Cannot create vignette hook.");
                return false;
            }
            hook = std::move(*created);
            return Enable(hook, "vignette", divide);
        }
    }
}

namespace
{
    DWORD WINAPI Setup(void*)
    {
        GameImage::Load();
        Site sites[] = {
            { "render size fit", CodePattern("66 0F 6E CE F3 0F 59 05 ? ? ? ? 0F 5B C9 0F 2F C1 76 ? F3 0F 59 0D ? ? ? ? "
                                             "44 8B 84 24 ? ? ? ? F3 0F 2C D1 EB ? 8B 94 24 ? ? ? ? F3 44 0F 2C C0") },
            { "camera perspective", CodePattern("40 53 48 83 EC 70 F3 0F 10 84 24 A0 00 00 00 48 8B D9 F3 0F 11 81 ? ? 00 00 "
                                                "F3 0F 11 89 ? ? 00 00 F3 0F 11 91 ? ? 00 00 F3 0F 11 99") },
            { "perspective builder", CodePattern("40 53 48 83 EC 40 F3 0F 59 0D ? ? ? ? 48 8B D9 0F 29 74 24 30 0F 28 F2") },
            { "vignette circle", CodePattern("F3 0F 10 51 44 0F 28 CA F3 44 0F 11 4D ? F3 0F 59 49 3C 48 C7 45 ? 00 00 00 00 "
                                             "F3 0F 59 51 40 F3 0F 5E C1") },
        };
        if (!FindAll(sites) || !RenderSize::Apply(sites[0].address) ||
            !Camera::Install(sites[1].address, sites[2].address) || !Vignette::Install(sites[3].address))
        {
            ReportFailure("SonicFrontiers_ARFix: unsupported game version. See SonicFrontiers_ARFix.log.");
            return 1;
        }
        return 0;
    }

    bool IsGameProcess()
    {
        const auto executable = ModulePath(nullptr);
        return executable && _wcsicmp(executable->filename().c_str(), gameExecutable) == 0;
    }

    bool OpenLog(const std::filesystem::path& file)
    {
        try
        {
            spdlog::set_default_logger(spdlog::basic_logger_mt(sFixName, file.string(), true));
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
            spdlog::flush_on(spdlog::level::info);
            return true;
        }
        catch (const std::exception& error)
        {
            OutputDebugStringA(error.what());
            return false;
        }
    }

    bool Configure()
    {
        const auto module = ModulePath(thisModule);
        if (!module)
            return false;
        const auto folder = module->parent_path();
        if (!OpenLog(folder / (sFixName + ".log")))
            return false;
        SPDLOG_INFO("{} v{}.", sFixName, sFixVersion);
        Config::Load(folder / (sFixName + ".ini"));
        return true;
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;
    thisModule = module;
    DisableThreadLibraryCalls(module);
    if (!IsGameProcess() || !Configure())
        return TRUE;
    if (const HANDLE thread = CreateThread(nullptr, 0, Setup, nullptr, 0, nullptr))
        CloseHandle(thread);
    return TRUE;
}
