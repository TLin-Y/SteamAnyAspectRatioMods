#include "stdafx.h"
#include "version.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
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
    constexpr const wchar_t* gameExecutable = L"LIS2-Win64-Shipping.exe";
    constexpr const wchar_t* unrealWindowClass = L"UnrealWindow";
    constexpr float originalAspect = 16.0f / 9.0f;

    struct Size
    {
        int width = 0;
        int height = 0;

        bool operator==(const Size&) const = default;

        bool IsValid() const
        {
            return width > 0 && height > 0;
        }

        float Aspect() const
        {
            return static_cast<float>(width) / static_cast<float>(height);
        }
    };

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

    template <typename Value>
    Value Read(std::uintptr_t address)
    {
        Value value{};
        std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
        return value;
    }
}

namespace Config
{
    namespace
    {
        constexpr float minFovMultiplier = 0.5f;
        constexpr float maxFovMultiplier = 2.0f;
        constexpr int maxDimension = 16384;

        float fovMultiplier = 1.0f;
        std::optional<Size> customResolution;

        bool RunningOnMacOS()
        {
            using GetHostVersion = void(CDECL*)(const char** systemName, const char** release);
            const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
            const auto getHostVersion = ntdll
                ? reinterpret_cast<GetHostVersion>(GetProcAddress(ntdll, "wine_get_host_version"))
                : nullptr;
            if (!getHostVersion)
                return false;
            const char* systemName = nullptr;
            const char* release = nullptr;
            getHostVersion(&systemName, &release);
            return systemName && std::strcmp(systemName, "Darwin") == 0;
        }

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

        std::optional<Size> ReadCustomResolution(const std::filesystem::path& ini)
        {
            const Size size{ static_cast<int>(GetPrivateProfileIntW(L"Resolution", L"Width", 0, ini.c_str())),
                             static_cast<int>(GetPrivateProfileIntW(L"Resolution", L"Height", 0, ini.c_str())) };
            if (!size.IsValid() || size.width > maxDimension || size.height > maxDimension)
                return std::nullopt;
            if (RunningOnMacOS())
            {
                SPDLOG_INFO("macOS host: using the screen size instead of {}x{}.", size.width, size.height);
                return std::nullopt;
            }
            return size;
        }

        void Load(const std::filesystem::path& ini)
        {
            fovMultiplier = ReadFovMultiplier(ini);
            customResolution = ReadCustomResolution(ini);
            const Size logged = customResolution.value_or(Size{});
            SPDLOG_INFO("Resolution={}x{} (0 = screen), FOV Multiplier={:.3f}.", logged.width, logged.height,
                        fovMultiplier);
        }
    }
}

namespace GameImage
{
    namespace
    {
        using Bytes = std::span<const std::uint8_t>;

        struct Section
        {
            Bytes content;
            bool isCode;
        };

        std::uintptr_t base = 0;
        std::vector<Section> sections;

        std::uintptr_t AddressOf(const std::uint8_t* byte)
        {
            return reinterpret_cast<std::uintptr_t>(byte);
        }

        const std::uint8_t* PointerTo(std::uintptr_t address)
        {
            return reinterpret_cast<const std::uint8_t*>(address);
        }

        void Load()
        {
            base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            const std::span headers(IMAGE_FIRST_SECTION(nt), nt->FileHeader.NumberOfSections);
            for (const IMAGE_SECTION_HEADER& header : headers)
            {
                const bool isReadable = (header.Characteristics & IMAGE_SCN_MEM_READ) != 0;
                const bool isCode = (header.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
                if (isReadable && header.Misc.VirtualSize > 0)
                    sections.push_back({ { PointerTo(base + header.VirtualAddress), header.Misc.VirtualSize }, isCode });
            }
        }

        std::uintptr_t Offset(std::uintptr_t address)
        {
            return address - base;
        }

        const Section* SectionOf(std::uintptr_t address, std::size_t size)
        {
            for (const Section& section : sections)
            {
                const std::uintptr_t begin = AddressOf(section.content.data());
                const std::uintptr_t end = begin + section.content.size();
                if (address >= begin && address < end && size <= end - address)
                    return &section;
            }
            return nullptr;
        }

        bool Contains(std::uintptr_t address, std::size_t size)
        {
            return SectionOf(address, size) != nullptr;
        }

        std::optional<Bytes> CodeAt(std::uintptr_t address, std::size_t size)
        {
            const Section* const section = SectionOf(address, size);
            if (!section || !section->isCode)
                return std::nullopt;
            return Bytes{ PointerTo(address), size };
        }

        std::uintptr_t FindData(Bytes wanted)
        {
            for (const Section& section : sections)
            {
                if (section.isCode)
                    continue;
                if (const auto found = std::ranges::search(section.content, wanted); !found.empty())
                    return AddressOf(&found.front());
            }
            return 0;
        }

        std::uintptr_t RipRelativeTarget(std::uintptr_t displacement)
        {
            return displacement + sizeof(std::int32_t) + Read<std::int32_t>(displacement);
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

        std::size_t Length() const
        {
            return bytes.size();
        }

        bool MatchesStartOf(GameImage::Bytes code) const
        {
            const auto isSame = [](int expected, std::uint8_t actual) {
                return expected == anyByte || expected == actual;
            };
            return code.size() >= bytes.size() && std::ranges::equal(bytes, code.first(bytes.size()), isSame);
        }

        bool OccursIn(GameImage::Bytes code) const
        {
            for (; code.size() >= bytes.size(); code = code.subspan(1))
                if (MatchesStartOf(code))
                    return true;
            return false;
        }

        template <typename Visit>
        void ForEachMatchInGame(Visit visit) const
        {
            for (const GameImage::Section& section : GameImage::sections)
                if (section.isCode)
                    ForEachMatchIn(section.content, visit);
        }

    private:
        static constexpr int anyByte = -1;

        std::vector<int> bytes;

        template <typename Visit>
        void ForEachMatchIn(GameImage::Bytes code, Visit visit) const
        {
            while (const void* const candidate = std::memchr(code.data(), bytes.front(), code.size()))
            {
                code = code.subspan(static_cast<const std::uint8_t*>(candidate) - code.data());
                if (MatchesStartOf(code))
                    visit(GameImage::AddressOf(code.data()));
                code = code.subspan(1);
            }
        }
    };
}

namespace Window
{
    namespace
    {
        BOOL CALLBACK StoreIfGameWindow(HWND window, LPARAM result)
        {
            DWORD processId = 0;
            GetWindowThreadProcessId(window, &processId);
            const bool isTopLevel = IsWindowVisible(window) && !GetWindow(window, GW_OWNER);
            if (processId != GetCurrentProcessId() || !isTopLevel)
                return TRUE;
            wchar_t className[64]{};
            if (!GetClassNameW(window, className, static_cast<int>(std::size(className))) ||
                std::wcscmp(className, unrealWindowClass) != 0)
                return TRUE;
            *reinterpret_cast<HWND*>(result) = window;
            return FALSE;
        }

        HWND Find()
        {
            HWND window = nullptr;
            EnumWindows(StoreIfGameWindow, reinterpret_cast<LPARAM>(&window));
            return window;
        }

        RECT MonitorArea(HWND window)
        {
            const HMONITOR monitor = window ? MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST)
                                            : MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
            MONITORINFO info{ sizeof(info) };
            return GetMonitorInfoW(monitor, &info) ? info.rcMonitor : RECT{};
        }

        Size SizeOf(const RECT& area)
        {
            return { area.right - area.left, area.bottom - area.top };
        }

        Size ShrinkToFit(Size size, Size bounds)
        {
            const float scale = (std::min)({ 1.0f, static_cast<float>(bounds.width) / size.width,
                                             static_cast<float>(bounds.height) / size.height });
            return { (std::max)(1, static_cast<int>(std::lround(size.width * scale))),
                     (std::max)(1, static_cast<int>(std::lround(size.height * scale))) };
        }

        Size WantedSize(HWND window)
        {
            const Size monitor = SizeOf(MonitorArea(window));
            if (!Config::customResolution || !monitor.IsValid())
                return monitor;
            return ShrinkToFit(*Config::customResolution, monitor);
        }

        RECT CenteredArea(Size size, const RECT& bounds)
        {
            const Size outer = SizeOf(bounds);
            const LONG left = bounds.left + (outer.width - size.width) / 2;
            const LONG top = bounds.top + (outer.height - size.height) / 2;
            return { left, top, left + size.width, top + size.height };
        }

        void RemoveFrame(HWND window)
        {
            constexpr LONG_PTR frameStyles = WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
            constexpr LONG_PTR edgeStyles = WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE | WS_EX_STATICEDGE;
            SetWindowLongPtrW(window, GWL_STYLE, GetWindowLongPtrW(window, GWL_STYLE) & ~frameStyles);
            SetWindowLongPtrW(window, GWL_EXSTYLE, GetWindowLongPtrW(window, GWL_EXSTYLE) & ~edgeStyles);
        }

        bool IsBorderlessAt(HWND window, const RECT& wanted)
        {
            RECT outer{};
            RECT client{};
            const bool hasCaption = (GetWindowLongPtrW(window, GWL_STYLE) & WS_CAPTION) != 0;
            return !hasCaption && GetWindowRect(window, &outer) && EqualRect(&outer, &wanted) &&
                   GetClientRect(window, &client) && SizeOf(client) == SizeOf(wanted);
        }

        void ForceBackBufferResize(HWND window, const RECT& area, Size size)
        {
            SetWindowPos(window, nullptr, area.left, area.top, size.width + 1, size.height,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            SetWindowPos(window, nullptr, area.left, area.top, size.width, size.height, SWP_NOZORDER | SWP_NOACTIVATE);
        }

        void MakeBorderless(HWND window)
        {
            const Size size = WantedSize(window);
            if (!size.IsValid())
                return;
            const RECT wanted = CenteredArea(size, MonitorArea(window));
            if (IsBorderlessAt(window, wanted))
                return;
            if (IsZoomed(window))
                ShowWindow(window, SW_RESTORE);
            RemoveFrame(window);
            ForceBackBufferResize(window, wanted, size);
            SPDLOG_INFO("Borderless window {}x{} at {},{}.", size.width, size.height, wanted.left, wanted.top);
        }

        void KeepBorderlessForever()
        {
            for (;; Sleep(1000))
            {
                const HWND window = Find();
                if (window && !IsIconic(window))
                    MakeBorderless(window);
            }
        }
    }
}

namespace ConsoleVariable
{
    namespace
    {
        using Setter = void (*)(void* variable, const wchar_t* value, std::uint32_t flags);

        void** FindStorageAfter(std::uintptr_t registration)
        {
            const CodePattern storeVariable("48 89 05 ? ? ? ?");
            constexpr std::size_t displacementOffset = 3;
            constexpr std::size_t searchLength = 0x30;
            for (std::uintptr_t store = registration; store < registration + searchLength; ++store)
            {
                const auto code = GameImage::CodeAt(store, storeVariable.Length());
                if (!code || !storeVariable.MatchesStartOf(*code))
                    continue;
                const std::uintptr_t storage = GameImage::RipRelativeTarget(store + displacementOffset);
                return GameImage::Contains(storage, sizeof(void*)) ? reinterpret_cast<void**>(storage) : nullptr;
            }
            return nullptr;
        }

        void** FindStorage(const std::wstring& name)
        {
            const std::size_t sizeWithTerminator = (name.size() + 1) * sizeof(wchar_t);
            const std::uintptr_t text =
                GameImage::FindData({ reinterpret_cast<const std::uint8_t*>(name.c_str()), sizeWithTerminator });
            if (!text)
                return nullptr;
            const CodePattern loadName("48 8D 15 ? ? ? ?");
            constexpr std::size_t displacementOffset = 3;
            void** storage = nullptr;
            loadName.ForEachMatchInGame([&](std::uintptr_t load) {
                if (!storage && GameImage::RipRelativeTarget(load + displacementOffset) == text)
                    storage = FindStorageAfter(load + loadName.Length());
            });
            return storage;
        }

        Setter FindSetter(void* variable)
        {
            const CodePattern releaseMethod("48 85 C9 74 ? 48 8B 01 BA 01 00 00 00");
            constexpr std::size_t methodCount = 22;
            const auto table = Read<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(variable));
            if (!GameImage::Contains(table, methodCount * sizeof(void*)))
                return nullptr;
            const std::span methods(reinterpret_cast<const std::uintptr_t*>(table), methodCount);
            for (std::size_t slot = 0; slot + 1 < methodCount; ++slot)
            {
                const auto method = GameImage::CodeAt(methods[slot], releaseMethod.Length());
                const std::uintptr_t methodAfterRelease = methods[slot + 1];
                if (method && releaseMethod.MatchesStartOf(*method) && GameImage::CodeAt(methodAfterRelease, 1))
                    return reinterpret_cast<Setter>(methodAfterRelease);
            }
            return nullptr;
        }
    }
}

namespace Resolution
{
    namespace
    {
        void** variableStorage = nullptr;

        void FindVariable()
        {
            variableStorage = ConsoleVariable::FindStorage(L"r.SetRes");
            if (!variableStorage)
            {
                SPDLOG_ERROR("r.SetRes not found; the game keeps its own resolution.");
                return;
            }
            SPDLOG_INFO("r.SetRes found at game+0x{:X}.",
                        GameImage::Offset(reinterpret_cast<std::uintptr_t>(variableStorage)));
        }

        void Request(Size size)
        {
            constexpr std::uint32_t setByConsole = 0x09000000;
            void* const variable = variableStorage ? *variableStorage : nullptr;
            if (!variable)
                return;
            const auto setValue = ConsoleVariable::FindSetter(variable);
            if (!setValue)
            {
                SPDLOG_ERROR("r.SetRes has an unexpected layout; the game keeps its own resolution.");
                variableStorage = nullptr;
                return;
            }
            const wchar_t* const windowMode = Config::customResolution ? L"w" : L"wf";
            const std::wstring value = std::to_wstring(size.width) + L"x" + std::to_wstring(size.height) + windowMode;
            setValue(variable, value.c_str(), setByConsole);
            SPDLOG_INFO("Resolution set to {}x{}.", size.width, size.height);
        }

        void ReapplyIfChanged(Size current)
        {
            constexpr ULONGLONG checkIntervalMilliseconds = 2000;
            constexpr int maxFailedAttempts = 10;
            static ULONGLONG nextCheck = 0;
            static int failedAttempts = 0;

            const ULONGLONG now = GetTickCount64();
            if (now < nextCheck)
                return;
            nextCheck = now + checkIntervalMilliseconds;

            const HWND window = Window::Find();
            const Size wanted = Window::WantedSize(window);
            if (!window || IsIconic(window) || !wanted.IsValid())
                return;
            if (current == wanted)
            {
                failedAttempts = 0;
                return;
            }
            if (failedAttempts >= maxFailedAttempts)
                return;
            ++failedAttempts;
            SPDLOG_INFO("Viewport is {}x{}.", current.width, current.height);
            Request(wanted);
        }
    }
}

namespace Projection
{
    namespace
    {
        struct MinimalViewInfo
        {
            enum class ProjectionMode : std::uint8_t
            {
                Perspective,
                Orthographic
            };

            float location[3];
            float rotation[3];
            float fov;
            float orthoWidth;
            float orthoNearPlane;
            float orthoFarPlane;
            float aspectRatio;
            std::uint32_t constrainAspectRatio : 1;
            ProjectionMode projectionMode;
        };
        static_assert(offsetof(MinimalViewInfo, fov) == 0x18);
        static_assert(offsetof(MinimalViewInfo, aspectRatio) == 0x28);
        static_assert(offsetof(MinimalViewInfo, projectionMode) == 0x30);

        struct ViewRect
        {
            std::int32_t left;
            std::int32_t top;
            std::int32_t right;
            std::int32_t bottom;

            Size Extent() const
            {
                return { right - left, bottom - top };
            }
        };

        struct ProjectionData
        {
            std::uint8_t viewTransform[0x50];
            float matrix[4][4];
            ViewRect viewRect;
        };
        static_assert(offsetof(ProjectionData, matrix) == 0x50);
        static_assert(offsetof(ProjectionData, viewRect) == 0x90);

        SafetyHookInline hook;

        std::optional<std::uintptr_t> FunctionStartBefore(std::uintptr_t instruction)
        {
            const CodePattern saveStackPointer("48 89 E0");
            const CodePattern saveStackPointerAlternative("48 8B C4");
            constexpr std::uintptr_t functionAlignment = 0x10;
            constexpr std::uintptr_t maxPrologueLength = 0x40;
            const std::uintptr_t nearest = instruction - instruction % functionAlignment;
            for (std::uintptr_t start = nearest; instruction - start < maxPrologueLength; start -= functionAlignment)
            {
                const auto code = GameImage::CodeAt(start, saveStackPointer.Length());
                if (code && (saveStackPointer.MatchesStartOf(*code) || saveStackPointerAlternative.MatchesStartOf(*code)))
                    return start;
            }
            return std::nullopt;
        }

        bool UsesCameraLens(std::uintptr_t function)
        {
            const CodePattern loadAspectRatio("F3 0F 10 ? 28");
            const CodePattern loadAndClampFov("F3 0F 10 ? 18 F3 0F 5F");
            const CodePattern checkProjectionMode("80 ? 30 01");
            constexpr std::size_t functionLength = 0x600;
            const auto code = GameImage::CodeAt(function, functionLength);
            return code && loadAspectRatio.OccursIn(*code) && loadAndClampFov.OccursIn(*code) &&
                   checkProjectionMode.OccursIn(*code);
        }

        std::optional<std::uintptr_t> FindFunction()
        {
            const CodePattern testConstrainAspectRatio("F6 41 2C 01");
            std::vector<std::uintptr_t> candidates;
            testConstrainAspectRatio.ForEachMatchInGame([&](std::uintptr_t test) {
                const auto start = FunctionStartBefore(test);
                if (start && UsesCameraLens(*start))
                    candidates.push_back(*start);
            });
            if (candidates.size() != 1)
            {
                SPDLOG_ERROR("Projection function: {} candidates.", candidates.size());
                return std::nullopt;
            }
            return candidates.front();
        }

        float ReferenceAspect(const MinimalViewInfo& view)
        {
            constexpr float minAspect = 1.0f;
            constexpr float maxAspect = 3.0f;
            const bool isUsable = view.aspectRatio >= minAspect && view.aspectRatio <= maxAspect;
            return view.constrainAspectRatio && isUsable ? view.aspectRatio : originalAspect;
        }

        float HorizontalHalfTangent(const MinimalViewInfo& view, float screenAspect)
        {
            constexpr float maxFov = 170.0f;
            constexpr float halfRadiansPerDegree = std::numbers::pi_v<float> / 360.0f;
            const float fov = (std::min)(view.fov * Config::fovMultiplier, maxFov);
            const float widening = (std::max)(1.0f, screenAspect / ReferenceAspect(view));
            return std::tan(fov * halfRadiansPerDegree) * widening;
        }

        void LogFirstUse(const MinimalViewInfo& view, Size screen)
        {
            static bool logged = false;
            if (logged)
                return;
            logged = true;
            SPDLOG_INFO("Camera FOV {:.2f}, reference aspect {:.4f}; view {}x{}.", view.fov, ReferenceAspect(view),
                        screen.width, screen.height);
        }

        void FillScreen(const MinimalViewInfo& view, ProjectionData& data)
        {
            const Size screen = data.viewRect.Extent();
            const bool hasValidFov = view.fov > 0.0f && view.fov < 180.0f;
            if (!screen.IsValid() || !hasValidFov)
                return;
            LogFirstUse(view, screen);
            const float halfTangent = HorizontalHalfTangent(view, screen.Aspect());
            data.matrix[0][0] = 1.0f / halfTangent;
            data.matrix[1][1] = screen.Aspect() / halfTangent;
        }

        void OnCalculateProjection(MinimalViewInfo* view, const std::uint8_t* axisConstraint, void* viewport,
                                   ProjectionData* data)
        {
            const auto gameConstraint = view->constrainAspectRatio;
            view->constrainAspectRatio = 0;
            hook.call<void>(view, axisConstraint, viewport, data);
            view->constrainAspectRatio = gameConstraint;

            Resolution::ReapplyIfChanged(data->viewRect.Extent());
            if (view->projectionMode == MinimalViewInfo::ProjectionMode::Perspective)
                FillScreen(*view, *data);
        }

        bool Install()
        {
            const auto function = FindFunction();
            if (!function)
                return false;
            auto created = SafetyHookInline::create(reinterpret_cast<void*>(*function),
                                                    reinterpret_cast<void*>(OnCalculateProjection),
                                                    SafetyHookInline::StartDisabled);
            if (!created)
            {
                SPDLOG_ERROR("Cannot hook projection: error {}.", static_cast<int>(created.error().type));
                return false;
            }
            hook = std::move(*created);
            if (const auto enabled = hook.enable(); !enabled)
            {
                SPDLOG_ERROR("Cannot enable projection hook: error {}.", static_cast<int>(enabled.error().type));
                return false;
            }
            SPDLOG_INFO("Hooked projection at game+0x{:X}.", GameImage::Offset(*function));
            return true;
        }
    }
}

namespace
{
    DWORD WINAPI Setup(void*)
    {
        GameImage::Load();
        Resolution::FindVariable();
        if (!Projection::Install())
        {
            ReportFailure("LIS2_ARFix: unsupported game version. See LIS2_ARFix.log.");
            return 1;
        }
        if (Config::customResolution)
            Window::KeepBorderlessForever();
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
