#include "stdafx.h"
#include "version.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <safetyhook.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

static_assert(sizeof(void*) == 8, "Build for Windows x64.");

namespace
{
    constexpr const wchar_t* gameExecutable = L"Life is Strange - Before the Storm.exe";
    constexpr const wchar_t* unityWindowClass = L"UnityWndClass";
    constexpr float originalAspect = 16.0f / 9.0f;

    using UnityObject = void*;
    using MethodInfo = void*;

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

    bool WaitUntil(const std::function<bool()>& condition)
    {
        constexpr DWORD pollMilliseconds = 5;
        constexpr int maxPolls = 12000;
        for (int poll = 0; poll < maxPolls; ++poll)
        {
            if (condition())
                return true;
            Sleep(pollMilliseconds);
        }
        return false;
    }

    template <typename Detour>
    bool InstallHook(SafetyHookInline& hook, void* target, Detour detour, const char* name)
    {
        if (!target)
        {
            SPDLOG_ERROR("{} not found.", name);
            return false;
        }
        auto created = SafetyHookInline::create(target, reinterpret_cast<void*>(detour), SafetyHookInline::StartDisabled);
        if (!created)
        {
            SPDLOG_ERROR("Cannot hook {}: error {}.", name, static_cast<int>(created.error().type));
            return false;
        }
        hook = std::move(*created);
        if (const auto enabled = hook.enable(); !enabled)
        {
            SPDLOG_ERROR("Cannot enable {} hook: error {}.", name, static_cast<int>(enabled.error().type));
            return false;
        }
        SPDLOG_INFO("Hooked {}.", name);
        return true;
    }
}

namespace Il2Cpp
{
    namespace
    {
        using Class = void*;

        HMODULE gameAssembly = nullptr;

        struct Exports
        {
            void* (*getDomain)();
            const void** (*getAssemblies)(const void* domain, std::size_t* count);
            const void* (*getImage)(const void* assembly);
            Class (*getClass)(const void* image, const char* space, const char* name);
            Class (*getNestedClass)(Class parent, void** iterator);
            const char* (*getClassName)(Class type);
            MethodInfo (*getMethod)(Class type, const char* name, int parameterCount);
            void* (*getField)(Class type, const char* name);
            std::size_t (*getFieldOffset)(void* field);
            void* (*attachThread)(void* domain);
            void (*detachThread)(void* thread);
            void* (*resolveInternalCall)(const char* name);
        };
        Exports exports{};

        template <typename Function>
        bool LoadExport(Function& function, const char* name)
        {
            function = reinterpret_cast<Function>(GetProcAddress(gameAssembly, name));
            if (!function)
                SPDLOG_ERROR("{} is not exported.", name);
            return function != nullptr;
        }

        bool LoadExports()
        {
            return LoadExport(exports.getDomain, "il2cpp_domain_get") &&
                   LoadExport(exports.getAssemblies, "il2cpp_domain_get_assemblies") &&
                   LoadExport(exports.getImage, "il2cpp_assembly_get_image") &&
                   LoadExport(exports.getClass, "il2cpp_class_from_name") &&
                   LoadExport(exports.getNestedClass, "il2cpp_class_get_nested_types") &&
                   LoadExport(exports.getClassName, "il2cpp_class_get_name") &&
                   LoadExport(exports.getMethod, "il2cpp_class_get_method_from_name") &&
                   LoadExport(exports.getField, "il2cpp_class_get_field_from_name") &&
                   LoadExport(exports.getFieldOffset, "il2cpp_field_get_offset") &&
                   LoadExport(exports.attachThread, "il2cpp_thread_attach") &&
                   LoadExport(exports.detachThread, "il2cpp_thread_detach") &&
                   LoadExport(exports.resolveInternalCall, "il2cpp_resolve_icall");
        }

        bool WaitForGameAssembly()
        {
            return WaitUntil([] { return (gameAssembly = GetModuleHandleW(L"GameAssembly.dll")) != nullptr; });
        }

        template <typename Function>
        bool FindInternalCall(Function& function, const char* name)
        {
            function = reinterpret_cast<Function>(exports.resolveInternalCall(name));
            return function != nullptr;
        }

        Class FindClass(const char* space, const char* name)
        {
            std::size_t count = 0;
            const void** const assemblies = exports.getAssemblies(exports.getDomain(), &count);
            for (std::size_t i = 0; assemblies && i < count; ++i)
            {
                const void* const image = exports.getImage(assemblies[i]);
                if (const Class found = image ? exports.getClass(image, space, name) : nullptr)
                    return found;
            }
            return nullptr;
        }

        Class FindNestedClass(Class parent, std::string_view namePrefix)
        {
            if (!parent)
                return nullptr;
            void* iterator = nullptr;
            while (const Class nested = exports.getNestedClass(parent, &iterator))
                if (std::string_view(exports.getClassName(nested)).starts_with(namePrefix))
                    return nested;
            return nullptr;
        }

        void* FindMethodCode(Class type, const char* name, int parameterCount)
        {
            const MethodInfo method = type ? exports.getMethod(type, name, parameterCount) : nullptr;
            return method ? *static_cast<void**>(method) : nullptr;
        }

        std::optional<std::size_t> FindFieldOffset(Class type, const char* name)
        {
            void* const field = type ? exports.getField(type, name) : nullptr;
            if (!field)
                return std::nullopt;
            return exports.getFieldOffset(field);
        }

        class AttachedThread
        {
        public:
            AttachedThread() : thread(exports.attachThread(exports.getDomain())) {}
            AttachedThread(const AttachedThread&) = delete;
            AttachedThread& operator=(const AttachedThread&) = delete;

            ~AttachedThread()
            {
                if (thread)
                    exports.detachThread(thread);
            }

        private:
            void* thread;
        };
    }
}

namespace Unity
{
    namespace
    {
        enum class FullScreenMode : int
        {
            FullScreenWindow = 1,
            Windowed = 3
        };

        struct CameraCalls
        {
            float (*getFieldOfView)(UnityObject camera);
            void (*setFieldOfView)(UnityObject camera, float value);
            float (*getAspect)(UnityObject camera);
            UnityObject (*getTargetTexture)(UnityObject camera);
            bool (*isOrthographic)(UnityObject camera);
        };
        CameraCalls camera{};

        struct ScreenCalls
        {
            int (*getWidth)();
            int (*getHeight)();
            FullScreenMode (*getFullScreenMode)();
            void (*setResolution)(int width, int height, FullScreenMode mode, int refreshRate);
            void (*setFullScreen)(bool fullScreen);
        };
        ScreenCalls screen{};

        bool ResolveCalls()
        {
            using Il2Cpp::FindInternalCall;
            return FindInternalCall(camera.getFieldOfView, "UnityEngine.Camera::get_fieldOfView") &&
                   FindInternalCall(camera.setFieldOfView, "UnityEngine.Camera::set_fieldOfView") &&
                   FindInternalCall(camera.getAspect, "UnityEngine.Camera::get_aspect") &&
                   FindInternalCall(camera.getTargetTexture, "UnityEngine.Camera::get_targetTexture") &&
                   FindInternalCall(camera.isOrthographic, "UnityEngine.Camera::get_orthographic") &&
                   FindInternalCall(screen.getWidth, "UnityEngine.Screen::get_width") &&
                   FindInternalCall(screen.getHeight, "UnityEngine.Screen::get_height") &&
                   FindInternalCall(screen.getFullScreenMode, "UnityEngine.Screen::get_fullScreenMode") &&
                   FindInternalCall(screen.setResolution, "UnityEngine.Screen::SetResolution") &&
                   FindInternalCall(screen.setFullScreen, "UnityEngine.Screen::set_fullScreen");
        }

        Size RenderSize()
        {
            return { screen.getWidth(), screen.getHeight() };
        }
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
                std::wcscmp(className, unityWindowClass) != 0)
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

        void MakeBorderless(HWND window)
        {
            constexpr LONG_PTR frameStyles = WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
            const Size size = WantedSize(window);
            if (!size.IsValid())
                return;
            const RECT wanted = CenteredArea(size, MonitorArea(window));
            RECT current{};
            GetWindowRect(window, &current);
            const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
            const bool hasFrame = (style & frameStyles) != 0;
            if (!hasFrame && EqualRect(&current, &wanted))
                return;
            if (hasFrame)
                SetWindowLongPtrW(window, GWL_STYLE, (style & ~frameStyles) | WS_POPUP);
            SetWindowPos(window, nullptr, wanted.left, wanted.top, size.width, size.height,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            SPDLOG_INFO("Borderless window {}x{} at {},{}.", size.width, size.height, wanted.left, wanted.top);
        }

        void KeepBorderlessForever()
        {
            for (;; Sleep(250))
            {
                const HWND window = Find();
                if (window && !IsIconic(window))
                    MakeBorderless(window);
            }
        }
    }
}

namespace Resolution
{
    namespace
    {
        SafetyHookInline setResolutionHook;
        SafetyHookInline setFullScreenHook;

        void OnSetResolution(int width, int height, Unity::FullScreenMode mode, int refreshRate)
        {
            const Size wanted = Window::WantedSize(Window::Find());
            if (!wanted.IsValid())
            {
                setResolutionHook.call<void>(width, height, mode, refreshRate);
                return;
            }
            const auto wantedMode = Config::customResolution ? Unity::FullScreenMode::Windowed
                                                             : Unity::FullScreenMode::FullScreenWindow;
            constexpr int defaultRefreshRate = 0;
            SPDLOG_INFO("Resolution request {}x{} (mode {}) -> {}x{} (mode {}).", width, height,
                        static_cast<int>(mode), wanted.width, wanted.height, static_cast<int>(wantedMode));
            setResolutionHook.call<void>(wanted.width, wanted.height, wantedMode, defaultRefreshRate);
        }

        void Reapply()
        {
            const Size current = Unity::RenderSize();
            OnSetResolution(current.width, current.height, Unity::screen.getFullScreenMode(), 0);
        }

        void OnSetFullScreen(bool)
        {
            Reapply();
        }

        void ReapplyIfChanged()
        {
            constexpr ULONGLONG checkIntervalMilliseconds = 1000;
            constexpr int maxFailedAttempts = 10;
            static ULONGLONG nextCheck = 0;
            static int failedAttempts = 0;

            const ULONGLONG now = GetTickCount64();
            if (now < nextCheck || failedAttempts >= maxFailedAttempts)
                return;
            nextCheck = now + checkIntervalMilliseconds;

            const Size wanted = Window::WantedSize(Window::Find());
            if (!wanted.IsValid() || Unity::RenderSize() == wanted)
            {
                failedAttempts = 0;
                return;
            }
            ++failedAttempts;
            Reapply();
        }

        bool Install()
        {
            return InstallHook(setResolutionHook, reinterpret_cast<void*>(Unity::screen.setResolution),
                               OnSetResolution, "Screen.SetResolution") &&
                   InstallHook(setFullScreenHook, reinterpret_cast<void*>(Unity::screen.setFullScreen),
                               OnSetFullScreen, "Screen.set_fullScreen");
        }
    }
}

namespace Letterbox
{
    namespace
    {
        SafetyHookInline reclipHook;
        std::size_t targetWidthOffset = 0;
        std::size_t targetHeightOffset = 0;

        void WriteFloat(UnityObject object, std::size_t offset, int value)
        {
            *reinterpret_cast<float*>(static_cast<std::uint8_t*>(object) + offset) = static_cast<float>(value);
        }

        void OnReclip(UnityObject clipping, MethodInfo method)
        {
            const Size size = Unity::RenderSize();
            if (clipping && size.IsValid())
            {
                WriteFloat(clipping, targetWidthOffset, size.width);
                WriteFloat(clipping, targetHeightOffset, size.height);
                SPDLOG_INFO("Camera clipping opened to {}x{}.", size.width, size.height);
            }
            reclipHook.call<void>(clipping, method);
        }

        bool Install()
        {
            const auto clipping = Il2Cpp::FindClass("", "CameraAspectRatioClipping");
            const auto widthOffset = Il2Cpp::FindFieldOffset(clipping, "targetAspectRatioWidth");
            const auto heightOffset = Il2Cpp::FindFieldOffset(clipping, "targetAspectRatioHeight");
            if (!widthOffset || !heightOffset)
            {
                SPDLOG_ERROR("CameraAspectRatioClipping target ratio fields not found.");
                return false;
            }
            targetWidthOffset = *widthOffset;
            targetHeightOffset = *heightOffset;
            return InstallHook(reclipHook, Il2Cpp::FindMethodCode(clipping, "ReClipAspectRatio", 0), OnReclip,
                               "CameraAspectRatioClipping.ReClipAspectRatio");
        }
    }
}

namespace FieldOfView
{
    namespace
    {
        struct Lens
        {
            float gameFov;
            float appliedFov;
        };

        SafetyHookInline getFovHook;
        SafetyHookInline setFovHook;
        SafetyHookInline preCullHook;
        SafetyHookInline reminderCutInHook;
        SafetyHookInline reminderSequenceHook;

        std::unordered_map<UnityObject, Lens> lenses;
        thread_local int gameFovReaders = 0;

        float Widen(float fov, float scale)
        {
            constexpr float maxFov = 170.0f;
            constexpr float radiansPerDegree = std::numbers::pi_v<float> / 180.0f;
            if (scale == 1.0f || !std::isfinite(fov) || fov <= 0.0f || fov >= maxFov)
                return fov;
            const float halfTangent = std::tan(fov * radiansPerDegree * 0.5f) * scale;
            return (std::min)(maxFov, 2.0f * std::atan(halfTangent) / radiansPerDegree);
        }

        float ScaleFor(UnityObject camera)
        {
            const float aspect = Unity::camera.getAspect(camera);
            if (!std::isfinite(aspect) || aspect <= 0.0f)
                return 1.0f;
            const float keepOriginalWidth = (std::max)(1.0f, originalAspect / aspect);
            return keepOriginalWidth * Config::fovMultiplier;
        }

        bool RendersPerspectiveToScreen(UnityObject camera)
        {
            return camera && !Unity::camera.isOrthographic(camera) && !Unity::camera.getTargetTexture(camera);
        }

        const Lens* LensApplying(UnityObject camera, float fov)
        {
            const auto lens = lenses.find(camera);
            return lens != lenses.end() && lens->second.appliedFov == fov ? &lens->second : nullptr;
        }

        float GameFovFrom(UnityObject camera, float fov)
        {
            if (const Lens* const own = LensApplying(camera, fov))
                return own->gameFov;
            const auto copiedFrom = std::ranges::find_if(
                lenses, [fov](const auto& entry) { return entry.second.appliedFov == fov; });
            return copiedFrom != lenses.end() ? copiedFrom->second.gameFov : fov;
        }

        void LogFirstChange(UnityObject camera, const Lens& lens)
        {
            static bool logged = false;
            if (logged || lens.appliedFov == lens.gameFov)
                return;
            logged = true;
            SPDLOG_INFO("Camera aspect {:.4f}: FOV {:.2f} -> {:.2f}.", Unity::camera.getAspect(camera), lens.gameFov,
                        lens.appliedFov);
        }

        void Apply(UnityObject camera, float requestedFov, float currentFov)
        {
            const float gameFov = GameFovFrom(camera, requestedFov);
            const Lens lens{ gameFov, Widen(gameFov, ScaleFor(camera)) };
            LogFirstChange(camera, lens);
            lenses[camera] = lens;
            if (lens.appliedFov != currentFov)
                setFovHook.call<void>(camera, lens.appliedFov);
        }

        float OnGetFov(UnityObject camera)
        {
            const float fov = getFovHook.call<float>(camera);
            const Lens* const lens = gameFovReaders > 0 ? LensApplying(camera, fov) : nullptr;
            return lens ? lens->gameFov : fov;
        }

        void OnSetFov(UnityObject camera, float fov)
        {
            constexpr float alwaysWrite = std::numeric_limits<float>::quiet_NaN();
            if (RendersPerspectiveToScreen(camera))
                Apply(camera, fov, alwaysWrite);
            else
                setFovHook.call<void>(camera, fov);
        }

        void OnPreCull(UnityObject camera, MethodInfo method)
        {
            Resolution::ReapplyIfChanged();
            if (RendersPerspectiveToScreen(camera))
            {
                const float fov = getFovHook.call<float>(camera);
                Apply(camera, fov, fov);
            }
            preCullHook.call<void>(camera, method);
        }

        template <SafetyHookInline& Hook>
        void* OnMethodReadingGameFov(UnityObject self, MethodInfo method)
        {
            ++gameFovReaders;
            void* const result = Hook.call<void*>(self, method);
            --gameFovReaders;
            return result;
        }

        bool InstallCameraHooks()
        {
            const auto camera = Il2Cpp::FindClass("UnityEngine", "Camera");
            return InstallHook(getFovHook, reinterpret_cast<void*>(Unity::camera.getFieldOfView), OnGetFov,
                               "Camera.get_fieldOfView") &&
                   InstallHook(setFovHook, reinterpret_cast<void*>(Unity::camera.setFieldOfView), OnSetFov,
                               "Camera.set_fieldOfView") &&
                   InstallHook(preCullHook, Il2Cpp::FindMethodCode(camera, "FireOnPreCull", 1), OnPreCull,
                               "Camera.FireOnPreCull");
        }

        void InstallObjectiveReminderHooks()
        {
            const auto reminder = Il2Cpp::FindClass("", "ObjectiveReminder");
            const auto sequence = Il2Cpp::FindNestedClass(reminder, "<CameraInSequence>");
            InstallHook(reminderCutInHook, Il2Cpp::FindMethodCode(reminder, "CameraCutIn", 0),
                        OnMethodReadingGameFov<reminderCutInHook>, "ObjectiveReminder.CameraCutIn");
            InstallHook(reminderSequenceHook, Il2Cpp::FindMethodCode(sequence, "MoveNext", 0),
                        OnMethodReadingGameFov<reminderSequenceHook>, "ObjectiveReminder.CameraInSequence");
        }
    }
}

namespace Interface
{
    namespace
    {
        SafetyHookInline activeHeightHook;

        int OnGetActiveHeight(UnityObject root, MethodInfo method)
        {
            const int height = activeHeightHook.call<int>(root, method);
            const Size size = Unity::RenderSize();
            if (!size.IsValid() || size.Aspect() >= originalAspect)
                return height;
            return static_cast<int>(std::lround(height * originalAspect / size.Aspect()));
        }

        void Install()
        {
            const auto root = Il2Cpp::FindClass("", "UIRoot");
            InstallHook(activeHeightHook, Il2Cpp::FindMethodCode(root, "get_activeHeight", 0), OnGetActiveHeight,
                        "UIRoot.get_activeHeight");
        }
    }
}

namespace
{
    bool InstallAllHooks()
    {
        const Il2Cpp::AttachedThread attached;
        if (!Letterbox::Install() || !Resolution::Install())
            return false;
        Interface::Install();
        FieldOfView::InstallObjectiveReminderHooks();
        return FieldOfView::InstallCameraHooks();
    }

    DWORD WINAPI Setup(void*)
    {
        if (!Il2Cpp::WaitForGameAssembly() || !Il2Cpp::LoadExports())
        {
            ReportFailure("LiSBtS_ARFix: GameAssembly.dll is not usable. The fix is not active.");
            return 1;
        }
        if (!WaitUntil([] { return Window::Find() && Unity::ResolveCalls(); }))
        {
            ReportFailure("LiSBtS_ARFix: Unity functions not found. The fix is not active.");
            return 1;
        }
        if (!InstallAllHooks())
        {
            ReportFailure("LiSBtS_ARFix: unsupported game version. See LiSBtS_ARFix.log.");
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
