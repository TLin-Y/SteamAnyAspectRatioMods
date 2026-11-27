#include "stdafx.h"
#include "version.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <numbers>
#include <set>
#include <span>
#include <string_view>
#include <utility>
#include <vector>
#include <safetyhook.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

static_assert(sizeof(void*) == 8, "Build for Windows x64.");

namespace
{

template <typename Value>
class Temporary
{
public:
    Temporary(Value& target, Value value) : target(target), original(std::exchange(target, value)) {}
    ~Temporary() { target = original; }
    Temporary(const Temporary&) = delete;

private:
    Value& target;
    Value original;
};

template <typename Value>
Value& Field(void* object, std::size_t offset)
{
    return *reinterpret_cast<Value*>(static_cast<std::uint8_t*>(object) + offset);
}

namespace Config
{
    float fovMultiplier = 1.0f;

    void Load(const std::filesystem::path& ini)
    {
        constexpr float minMultiplier = 0.5f;
        constexpr float maxMultiplier = 2.0f;
        wchar_t text[32]{};
        GetPrivateProfileStringW(L"FOV", L"Multiplier", L"1.0", text, static_cast<DWORD>(std::size(text)), ini.c_str());
        const float multiplier = std::wcstof(text, nullptr);
        if (multiplier >= minMultiplier && multiplier <= maxMultiplier)
            fovMultiplier = multiplier;
        SPDLOG_INFO("FOV multiplier {:.2f}.", fovMultiplier);
    }
}

namespace Game
{
    using Bytes = std::span<std::uint8_t>;
    using Pattern = std::vector<int>;

    constexpr int any = -1;

    Bytes image;
    std::vector<Bytes> code;
    std::vector<Bytes> data;

    void Load()
    {
        const auto base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        image = { base, nt->OptionalHeader.SizeOfImage };
        for (const auto& section : std::span(IMAGE_FIRST_SECTION(nt), nt->FileHeader.NumberOfSections))
        {
            const bool isCode = (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
            (isCode ? code : data).push_back({ base + section.VirtualAddress, section.Misc.VirtualSize });
        }
    }

    bool Contains(Bytes area, const void* address, std::size_t size)
    {
        const auto byte = static_cast<const std::uint8_t*>(address);
        return byte >= area.data() && byte + size <= area.data() + area.size();
    }

    bool IsCode(const void* address, std::size_t size)
    {
        return std::ranges::any_of(code, [&](Bytes section) { return Contains(section, address, size); });
    }

    Pattern PatternOf(const void* value, std::size_t size)
    {
        const auto bytes = static_cast<const std::uint8_t*>(value);
        return { bytes, bytes + size };
    }

    std::vector<std::uint8_t*> FindAll(const std::vector<Bytes>& areas, const Pattern& pattern)
    {
        const auto isSame = [](std::uint8_t actual, int wanted) { return wanted == any || wanted == actual; };
        std::vector<std::uint8_t*> found;
        for (const Bytes area : areas)
        {
            const auto next = [&](auto from) {
                return std::search(from, area.end(), pattern.begin(), pattern.end(), isSame);
            };
            for (auto match = next(area.begin()); match != area.end(); match = next(match + 1))
                found.push_back(&*match);
        }
        return found;
    }

    std::uint8_t* FunctionStart(std::uint8_t* address)
    {
        constexpr std::uint8_t chained = UNW_FLAG_CHAININFO << 3;
        constexpr std::size_t headerSize = 4;
        DWORD64 base = 0;
        auto entry = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(address), &base, nullptr);
        while (entry)
        {
            const auto unwind = reinterpret_cast<std::uint8_t*>(base + entry->UnwindData);
            if (!(unwind[0] & chained))
                return reinterpret_cast<std::uint8_t*>(base + entry->BeginAddress);
            const std::size_t evenCodeCount = (unwind[2] + 1) & ~1;
            entry = reinterpret_cast<RUNTIME_FUNCTION*>(unwind + headerSize + evenCodeCount * sizeof(std::uint16_t));
        }
        return nullptr;
    }

    std::size_t Offset(const void* address)
    {
        return static_cast<const std::uint8_t*>(address) - image.data();
    }
}

namespace Camera
{
    struct ReflectedProperty
    {
        std::uint8_t* entry;

        bool IsNamed(std::string_view name) const
        {
            const auto text = Field<const char*>(entry, 0);
            return Game::Contains(Game::image, text, name.size() + 1) && name == std::string_view(text);
        }

        std::size_t Offset() const { return Field<std::uint16_t>(entry, 0x32); }
        std::uint8_t* BitSetter() const { return Field<std::uint8_t*>(entry, 0x38); }
    };

    struct Layout
    {
        std::size_t fov = 0;
        std::size_t aspect = 0;
        std::size_t flags = 0;
        std::uint8_t constrainAspectBit = 0;

        std::size_t HasAxisOverride() const { return aspect + 8; }
        bool IsSupported() const { return fov && aspect && constrainAspectBit && flags == aspect + 0xC; }
    };

    Layout layout;
    SafetyHookInline hook;

    void Read(ReflectedProperty property)
    {
        constexpr std::size_t orInstructionLength = 4;
        if (property.IsNamed("FOV"))
            layout.fov = property.Offset();
        else if (property.IsNamed("AspectRatio"))
            layout.aspect = property.Offset();
        else if (property.IsNamed("bConstrainAspectRatio") && Game::IsCode(property.BitSetter(), orInstructionLength))
        {
            layout.flags = property.BitSetter()[2];
            layout.constrainAspectBit = property.BitSetter()[3];
        }
    }

    Layout FindLayout()
    {
        constexpr std::string_view lastProperty("\0OffCenterProjectionOffset\0", 27);
        constexpr std::size_t tableLength = 0x800;
        layout = {};
        const Game::Pattern lastName = Game::PatternOf(lastProperty.data(), lastProperty.size());
        for (std::uint8_t* const match : Game::FindAll(Game::data, lastName))
        {
            const std::uint8_t* const name = match + 1;
            for (std::uint8_t* const last : Game::FindAll(Game::data, Game::PatternOf(&name, sizeof(name))))
                for (std::uint8_t* entry = last - tableLength; entry < last; entry += sizeof(void*))
                    Read({ entry });
        }
        return layout;
    }

    std::set<std::uint8_t*> FindProjectionFunctions()
    {
        constexpr std::size_t maxStartDistance = 0x100;
        constexpr std::size_t searchedLength = 0x500;
        const Game::Pattern testConstrainAspect{ 0xF6, Game::any, static_cast<int>(layout.flags),
                                                 layout.constrainAspectBit };
        const Game::Pattern loadAndLimitFov{ 0xF3, 0x0F, 0x10, Game::any, static_cast<int>(layout.fov),
                                             0xF3, 0x0F, 0x5F };
        std::set<std::uint8_t*> functions;
        for (std::uint8_t* const test : Game::FindAll(Game::code, testConstrainAspect))
        {
            std::uint8_t* const start = Game::FunctionStart(test);
            if (!start || start > test || test - start > maxStartDistance || !Game::IsCode(start, searchedLength))
                continue;
            if (!Game::FindAll({ { start, searchedLength } }, loadAndLimitFov).empty())
                functions.insert(start);
        }
        return functions;
    }

    float AdjustedFov(float fov, float cameraAspect, float screenAspect)
    {
        constexpr float maxFov = 170.0f;
        constexpr float halfRadians = std::numbers::pi_v<float> / 360.0f;
        const float scaled = (std::min)(fov * Config::fovMultiplier, maxFov);
        const float widening = (std::max)(1.0f, screenAspect / cameraAspect);
        return std::atan(std::tan(scaled * halfRadians) * widening) / halfRadians;
    }

    void OnCalculateProjection(void* view, std::uint8_t axisConstraint, void* rectangle, void* data)
    {
        constexpr float originalAspect = 16.0f / 9.0f;
        constexpr std::uint8_t maintainHorizontalFov = 1;
        constexpr std::size_t dataViewRect = 0x120;
        const RECT rect = Field<RECT>(data, dataViewRect);
        const LONG width = rect.right - rect.left;
        const LONG height = rect.bottom - rect.top;
        if (width <= 0 || height <= 0)
            return hook.call<void>(view, axisConstraint, rectangle, data);

        auto& fov = Field<float>(view, layout.fov);
        auto& flags = Field<std::uint8_t>(view, layout.flags);
        const bool hasOwnAspect = (flags & layout.constrainAspectBit) != 0;
        const float cameraAspect = hasOwnAspect ? Field<float>(view, layout.aspect) : originalAspect;
        const float screenAspect = static_cast<float>(width) / static_cast<float>(height);

        const Temporary newFov(fov, AdjustedFov(fov, cameraAspect, screenAspect));
        const Temporary newFlags(flags, static_cast<std::uint8_t>(flags & ~layout.constrainAspectBit));
        const Temporary noAxisOverride(Field<bool>(view, layout.HasAxisOverride()), false);
        hook.call<void>(view, maintainHorizontalFov, rectangle, data);
    }

    std::uint8_t* WaitForProjectionFunction()
    {
        constexpr int attempts = 120;
        constexpr DWORD retryDelayMs = 500;
        std::set<std::uint8_t*> functions;
        for (int attempt = 0; attempt < attempts; ++attempt, Sleep(retryDelayMs))
        {
            functions = FindLayout().IsSupported() ? FindProjectionFunctions() : std::set<std::uint8_t*>{};
            if (functions.size() == 1)
                return *functions.begin();
        }
        SPDLOG_ERROR("Camera FOV +0x{:X}, aspect +0x{:X}, flags +0x{:X}; {} projection candidate(s).", layout.fov,
                     layout.aspect, layout.flags, functions.size());
        return nullptr;
    }

    bool Install()
    {
        std::uint8_t* const function = WaitForProjectionFunction();
        if (!function)
            return false;
        hook = safetyhook::create_inline(function, OnCalculateProjection, SafetyHookInline::StartDisabled);
        if (!hook || !hook.enable())
            return false;
        SPDLOG_INFO("Camera FOV +0x{:X}, aspect +0x{:X}, flags +0x{:X}; hooked projection at game+0x{:X}.", layout.fov,
                    layout.aspect, layout.flags, Game::Offset(function));
        return true;
    }
}

std::filesystem::path ModulePath(HMODULE module)
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(module, path, MAX_PATH);
    return path;
}

DWORD WINAPI Setup(void*)
{
    Game::Load();
    std::error_code error;
    const auto executable = ModulePath(nullptr);
    SPDLOG_INFO("{} ({} bytes).", executable.filename().string(), std::filesystem::file_size(executable, error));
    if (!Camera::Install())
        MessageBoxA(nullptr, "Unsupported game version. See the log.", sFixName.c_str(), MB_OK | MB_ICONERROR);
    return 0;
}

void OpenLog(const std::filesystem::path& file)
{
    try
    {
        spdlog::set_default_logger(spdlog::basic_logger_mt(sFixName, file.string(), true));
        spdlog::flush_on(spdlog::level::info);
    }
    catch (const spdlog::spdlog_ex& error)
    {
        OutputDebugStringA(error.what());
    }
}

bool IsCrashReporter()
{
    return _wcsicmp(ModulePath(nullptr).filename().c_str(), L"crashpad_handler.exe") == 0;
}

}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;
    DisableThreadLibraryCalls(module);
    if (IsCrashReporter())
        return TRUE;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto folder = ModulePath(module).parent_path();
    OpenLog(folder / (sFixName + ".log"));
    Config::Load(folder / (sFixName + ".ini"));
    if (const HANDLE thread = CreateThread(nullptr, 0, Setup, nullptr, 0, nullptr))
        CloseHandle(thread);
    return TRUE;
}
