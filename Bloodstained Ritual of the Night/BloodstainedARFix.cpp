#include "stdafx.h"
#include "helper.hpp"
#include <safetyhook.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cwchar>
#include <limits>
#include <memory>

namespace
{
    static_assert(sizeof(void*) == 8, "Build for Windows x64.");
    constexpr float baseAspect = 16.0f / 9.0f;
    constexpr double pi = 3.14159265358979323846;
    HMODULE ownModule = nullptr;
    std::shared_ptr<spdlog::logger> logFile;
    SafetyHookMid barsHook;
    SafetyHookMid fovHook;
    SafetyHookMid equipmentHook;
    SafetyHookMid dialogueHook;
    SafetyHookMid captureProjectionHook;
    using FindNameFunction = uint64_t* (*)(uint64_t*, const char*, int);
    using ResizeCaptureFunction = void (*)(uintptr_t, uint64_t);
    FindNameFunction findCaptureName = nullptr;
    ResizeCaptureFunction resizeCapture = nullptr;
    std::atomic<bool> ready{false};
    std::atomic<bool> barsReadError{false};
    std::atomic<bool> projectionError{false};
    std::atomic<bool> projectionLogged{false};
    std::atomic<bool> equipmentLogged{false};
    std::atomic<bool> dialogueError{false};
    std::atomic<bool> dialogueDisabled{false};
    std::atomic<bool> captureProjectionError{false};
    std::atomic<bool> captureProjectionLogged{false};
    std::atomic<uint64_t> captureObjectName{0};
    std::atomic<uint64_t> capturePackageName{0};
    int targetWidth = 1280;
    int targetHeight = 800;
    float fovPercent = 100.0f;
    uintptr_t imageBase = 0;
    size_t imageSize = 0;

    void Notify(const char* message)
    {
        OutputDebugStringA(message);
        MessageBoxA(nullptr, message, "BloodstainedARFix", MB_OK | MB_ICONERROR);
    }

    template <typename T>
    bool Read(uintptr_t address, T& value)
    {
        SIZE_T read = 0;
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                                 &value, sizeof(value), &read) && read == sizeof(value);
    }

    bool InImage(uintptr_t address, size_t length)
    {
        return address >= imageBase && address - imageBase <= imageSize &&
               length <= imageSize - (address - imageBase);
    }

    bool Matches(uintptr_t address, const std::vector<int>& pattern)
    {
        if (!InImage(address, pattern.size()))
            return false;
        std::vector<uint8_t> bytes(pattern.size());
        SIZE_T read = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address),
                               bytes.data(), bytes.size(), &read) || read != bytes.size())
            return false;
        for (size_t i = 0; i < pattern.size(); ++i)
            if (pattern[i] >= 0 && bytes[i] != pattern[i])
                return false;
        return true;
    }

    uintptr_t FindUnique(const char* name, const char* signature)
    {
        const auto pattern = Memory::PatternToBytes(signature);
        constexpr size_t chunkSize = 1024 * 1024;
        std::vector<uint8_t> bytes(chunkSize + pattern.size() - 1);
        uintptr_t found = 0;
        const auto end = imageBase + imageSize;
        for (auto cursor = imageBase; cursor < end;)
        {
            MEMORY_BASIC_INFORMATION region{};
            if (!VirtualQuery(reinterpret_cast<void*>(cursor), &region, sizeof(region)))
            {
                logFile->error("{} scan: VirtualQuery failed ({})", name, GetLastError());
                return 0;
            }
            const auto regionEnd = (std::min)(end,
                reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize);
            if (regionEnd <= cursor)
            {
                logFile->error("{} scan: invalid memory region", name);
                return 0;
            }
            const DWORD protection = region.Protect & 0xff;
            const bool readable = protection == PAGE_READONLY || protection == PAGE_READWRITE ||
                protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
                protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
            if (region.State != MEM_COMMIT || !readable || (region.Protect & PAGE_GUARD) ||
                reinterpret_cast<uintptr_t>(region.AllocationBase) != imageBase)
            {
                cursor = regionEnd;
                continue;
            }
            while (cursor < regionEnd)
            {
                const auto size = (std::min)(bytes.size(), static_cast<size_t>(regionEnd - cursor));
                SIZE_T read = 0;
                if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(cursor),
                                       bytes.data(), size, &read) || read != size)
                {
                    logFile->error("{} scan: memory read failed ({})", name, GetLastError());
                    return 0;
                }
                const auto count = size >= pattern.size() ? size - pattern.size() + 1 : 0;
                for (size_t i = 0; i < count; ++i)
                {
                    if (bytes[i] != pattern[0])
                        continue;
                    size_t j = 1;
                    while (j < pattern.size() && (pattern[j] < 0 || bytes[i + j] == pattern[j]))
                        ++j;
                    if (j == pattern.size())
                    {
                        if (found)
                        {
                            logFile->error("{} signature is ambiguous", name);
                            return 0;
                        }
                        found = cursor + i;
                    }
                }
                cursor += (std::min)(chunkSize, static_cast<size_t>(regionEnd - cursor));
            }
        }
        if (!found)
            logFile->error("{} signature not found", name);
        else
            logFile->info("{} signature: game+0x{:X}", name, found - imageBase);
        return found;
    }

    uintptr_t CallTarget(uintptr_t address)
    {
        uint8_t opcode = 0;
        int32_t displacement = 0;
        if (!InImage(address, 5) || !Read(address, opcode) || opcode != 0xe8 ||
            !Read(address + 1, displacement))
            return 0;
        const auto target = address + 5 + displacement;
        return InImage(target, 1) ? target : 0;
    }

    bool ReadNumber(const std::filesystem::path& path, const wchar_t* section,
                    const wchar_t* key, double& value)
    {
        wchar_t text[64]{};
        const auto length = GetPrivateProfileStringW(section, key, L"", text,
                                                     static_cast<DWORD>(std::size(text)), path.c_str());
        if (!length || length == std::size(text) - 1)
            return false;
        wchar_t* end = nullptr;
        value = std::wcstod(text, &end);
        if (end == text)
            return false;
        while (*end == L' ' || *end == L'\t')
            ++end;
        return *end == L'\0' && std::isfinite(value);
    }

    bool LoadConfig(const std::filesystem::path& folder)
    {
        const auto path = folder / L"BloodstainedARFix.ini";
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            logFile->error("Cannot open INI {}: Win32={}", path.string(), GetLastError());
            return false;
        }
        CloseHandle(file);
        double width = 0, height = 0, percent = 0;
        if (!ReadNumber(path, L"Resolution", L"Width", width) ||
            !ReadNumber(path, L"Resolution", L"Height", height) ||
            !ReadNumber(path, L"Camera", L"FOVPercent", percent) ||
            width < 320 || width > 16384 || height < 240 || height > 16384 ||
            std::floor(width) != width || std::floor(height) != height ||
            percent < 25 || percent > 200)
        {
            logFile->error("Invalid INI: Width 320-16384 and Height 240-16384 must be integers; "
                           "FOVPercent must be 25-200");
            return false;
        }
        targetWidth = static_cast<int>(width);
        targetHeight = static_cast<int>(height);
        fovPercent = static_cast<float>(percent);
        logFile->info("Target client={}x{}, FOVPercent={}, letterbox removal enabled",
                      targetWidth, targetHeight, fovPercent);
        return true;
    }

    // The engine builds its matrix using either horizontal or vertical axis scaling.
    bool CorrectFov(float input, float aspect, bool horizontalAxis, float percent, float& output)
    {
        if (!std::isfinite(input) || input <= 0.0f || input >= 179.0f ||
            !std::isfinite(aspect) || aspect <= 0.0f ||
            !std::isfinite(percent) || percent < 25.0f || percent > 200.0f)
            return false;
        const double scaled = input * static_cast<double>(percent) / 100.0;
        if (scaled <= 0.0 || scaled >= 179.0)
            return false;
        const double factor = horizontalAxis
            ? (std::max)(1.0, static_cast<double>(aspect) / baseAspect)
            : 1.0 / (std::min)(static_cast<double>(aspect), static_cast<double>(baseAspect));
        const double result = 2.0 * std::atan(std::tan(scaled * pi / 360.0) * factor) * 180.0 / pi;
        if (!std::isfinite(result) || result <= 0.0 || result >= 179.0)
            return false;
        output = static_cast<float>(result);
        return true;
    }

    void RemoveBars(SafetyHookContext& ctx)
    {
        if (!ready.load(std::memory_order_acquire))
            return;
        uint32_t flags = 0;
        if (!Read(ctx.rcx + 0x30, flags))
        {
            if (!barsReadError.exchange(true))
                logFile->error("Cannot read camera flags; letterbox removal skipped");
            return;
        }
        // The next instruction XORs EAX into the destination flags.
        ctx.rax = flags & 1;
    }

    void CorrectCamera(SafetyHookContext& ctx)
    {
        if (!ready.load(std::memory_order_acquire))
            return;
        struct Rect { int32_t left, top, right, bottom; } rect{};
        uint8_t projectionMode = 0;
        if (!Read(ctx.r9, rect) || !Read(ctx.rbx + 0x34, projectionMode))
        {
            if (!projectionError.exchange(true))
                logFile->error("Cannot read projection inputs; FOV correction skipped");
            return;
        }
        if (projectionMode == 1)
            return;
        const int64_t width = static_cast<int64_t>(rect.right) - rect.left;
        const int64_t height = static_cast<int64_t>(rect.bottom) - rect.top;
        const uint8_t axis = static_cast<uint8_t>(ctx.rdx);
        const bool horizontalAxis = (width > height && axis == 2) || axis == 1;
        float corrected = 0;
        if (width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
            !CorrectFov(ctx.xmm1.f32[0], static_cast<float>(width) / static_cast<float>(height),
                        horizontalAxis, fovPercent, corrected))
        {
            if (!projectionError.exchange(true))
                logFile->error("Unsupported projection input: viewport={}x{}, axis={}, FOV={}; "
                               "FOV correction skipped", width, height, axis, ctx.xmm1.f32[0]);
            return;
        }
        if (!projectionLogged.exchange(true))
            logFile->info("Projection sample: viewport={}x{}, axis={}, horizontalAxis={}, FOV={} -> {}",
                          width, height, axis, horizontalAxis, ctx.xmm1.f32[0], corrected);
        ctx.xmm1.f32[0] = corrected;
    }

    float TargetAspect()
    {
        return static_cast<float>(targetWidth) / static_cast<float>(targetHeight);
    }

    bool CaptureWidth(float height, float& width)
    {
        width = height * TargetAspect();
        return std::isfinite(height) && height > 0.0f && std::isfinite(width) &&
               width >= 1.0f && width <= 16384.0f;
    }

    bool CaptureAspectMatches(int32_t width, int32_t height)
    {
        // The game truncates both scaled dimensions independently.
        return width > 0 && height > 0 &&
               std::abs(static_cast<float>(width) - static_cast<float>(height) * TargetAspect()) <=
                   1.0f + TargetAspect();
    }

    void CorrectEquipmentCapture(SafetyHookContext& ctx)
    {
        if (!ready.load(std::memory_order_acquire))
            return;
        const float width = ctx.xmm0.f32[0];
        const float height = ctx.xmm0.f32[1];
        float correctedWidth = 0;
        if (!std::isfinite(width) || width <= 0.0f || !CaptureWidth(height, correctedWidth))
        {
            if (!equipmentLogged.exchange(true))
                logFile->error("Unsupported equipment capture input {}x{}, corrected width {}; unchanged",
                               width, height, correctedWidth);
            return;
        }
        ctx.xmm0.f32[0] = correctedWidth;
        if (!equipmentLogged.exchange(true))
            logFile->info("Equipment capture base {}x{} -> {}x{}", width, height, correctedWidth, height);
    }

    bool IsCharacterCapture(uintptr_t texture)
    {
        if (!texture)
            return false;
        uint64_t objectName = 0;
        if (!Read(texture + 0x18, objectName))
        {
            if (!dialogueError.exchange(true))
                logFile->error("Dialogue capture target name could not be read");
            return false;
        }
        auto expectedObject = captureObjectName.load();
        if (!expectedObject)
        {
            findCaptureName(&expectedObject, "CharaCaptureRT", 1);
            if (expectedObject)
                captureObjectName.store(expectedObject);
        }
        if (!expectedObject || objectName != expectedObject)
            return false;

        uintptr_t package = 0;
        uint64_t packageName = 0;
        if (!Read(texture + 0x20, package) || !package || !Read(package + 0x18, packageName))
        {
            if (!dialogueError.exchange(true))
                logFile->error("Dialogue capture package could not be read");
            return false;
        }
        auto expectedPackage = capturePackageName.load();
        if (!expectedPackage)
        {
            findCaptureName(&expectedPackage, "/Game/Core/UI/Dialog/Asset/CharaCaptureRT", 1);
            if (expectedPackage)
                capturePackageName.store(expectedPackage);
        }
        if (!expectedPackage)
        {
            if (!dialogueError.exchange(true))
                logFile->error("Dialogue capture package name could not be resolved");
            return false;
        }
        return packageName == expectedPackage;
    }

    void CorrectDialogueCapture(SafetyHookContext& ctx)
    {
        if (!ready.load(std::memory_order_acquire) || dialogueDisabled.load() ||
            !IsCharacterCapture(ctx.rbx))
            return;
        int32_t width = 0, height = 0;
        float correctedWidth = 0;
        if (!Read(ctx.rbx + 0xc0, width) || !Read(ctx.rbx + 0xc4, height) ||
            width <= 0 || height <= 0 || !CaptureWidth(1080.0f, correctedWidth))
        {
            if (!dialogueError.exchange(true))
                logFile->error("Invalid dialogue capture dimensions {}x{}, base width {}",
                               width, height, correctedWidth);
            return;
        }
        if (CaptureAspectMatches(width, height))
            return;

        const float baseSize[2] = {correctedWidth, 1080.0f};
        uint64_t packedSize = 0;
        std::memcpy(&packedSize, baseSize, sizeof(packedSize));
        resizeCapture(ctx.rbx, packedSize);
        int32_t resizedWidth = 0, resizedHeight = 0;
        if (!Read(ctx.rbx + 0xc0, resizedWidth) || !Read(ctx.rbx + 0xc4, resizedHeight) ||
            !CaptureAspectMatches(resizedWidth, resizedHeight))
        {
            dialogueDisabled.store(true);
            logFile->error("Dialogue capture resize failed: {}x{} -> {}x{}; correction disabled",
                           width, height, resizedWidth, resizedHeight);
            return;
        }
        logFile->info("Dialogue capture resized {}x{} -> {}x{}", width, height,
                      resizedWidth, resizedHeight);
    }

    void CorrectCaptureProjection(SafetyHookContext& ctx)
    {
        if (!ready.load(std::memory_order_acquire) || TargetAspect() <= baseAspect)
            return;
        uintptr_t texture = 0;
        uint8_t mode = 0;
        if (!Read(ctx.rdi + 0x440, texture) || !Read(ctx.rdi + 0x430, mode))
        {
            if (!captureProjectionError.exchange(true))
                logFile->error("Cannot read character capture projection inputs");
            return;
        }
        if (mode != 0 || !IsCharacterCapture(texture))
            return;
        const auto width = static_cast<int32_t>(ctx.rbx);
        const auto height = static_cast<int32_t>(ctx.rbx >> 32);
        // This projection path has already converted FOV degrees to half-angle radians.
        const float angle = ctx.xmm6.f32[0] * static_cast<float>(360.0 / pi);
        float corrected = 0;
        if (width <= 0 || height <= 0 ||
            !CorrectFov(angle, static_cast<float>(width) / height, true, 100.0f, corrected))
        {
            if (!captureProjectionError.exchange(true))
                logFile->error("Invalid character capture projection {}x{}, FOV={}", width, height, angle);
            return;
        }
        ctx.xmm6.f32[0] = corrected * static_cast<float>(pi / 360.0);
        if (!captureProjectionLogged.exchange(true))
            logFile->info("Character capture projection {}x{}, FOV={} -> {}", width, height, angle, corrected);
    }

    void InstallDialogueHook()
    {
        constexpr char rendererPattern[] =
            "48 8B 9A 40 04 00 00 48 8B FA 4C 8B F9 48 85 DB 0F 84 ?? ?? ?? ?? "
            "0F 10 A2 C0 02 00 00 0F 10 8A D0 02 00 00";
        constexpr char nameCallerPattern[] =
            "41 B8 01 00 00 00 48 8D 15 ?? ?? ?? ?? 48 8D 4D B0 E8 ?? ?? ?? ?? "
            "48 8B 8E 00 08 00 00 48 8B D3 4C 8B 00 E8 ?? ?? ?? ??";
        constexpr char namePattern[] =
            "40 53 48 83 EC 30 48 8B D9 48 85 D2 74 ?? 45 8B C8";
        constexpr char resizePattern[] =
            "40 53 48 83 EC 20 48 8B 05 ?? ?? ?? ?? 48 8B D9 "
            "48 89 54 24 40 48 8B 90 50 07 00 00 48 8B 8A 98 00 00 00";
        constexpr char captureProjectionPattern[] =
            "0F B6 97 30 04 00 00 48 8D 45 60 F3 0F 10 9F 38 04 00 00 "
            "0F 28 D6 48 8B CB 48 89 44 24 20 E8 ?? ?? ?? ?? "
            "80 BF 48 04 00 00 02";
        const auto renderer = FindUnique("Dialogue capture renderer", rendererPattern);
        const auto caller = FindUnique("Dialogue name caller", nameCallerPattern);
        const auto name = caller ? CallTarget(caller + 17) : 0;
        const auto resize = FindUnique("Dialogue capture resize", resizePattern);
        const auto projection = FindUnique("Character capture projection", captureProjectionPattern);
        if (!renderer || !name || !Matches(name, Memory::PatternToBytes(namePattern)) || !resize)
        {
            logFile->error("Dialogue capture contracts not verified; correction disabled");
            return;
        }
        findCaptureName = reinterpret_cast<FindNameFunction>(name);
        resizeCapture = reinterpret_cast<ResizeCaptureFunction>(resize);
        auto hook = SafetyHookMid::create(reinterpret_cast<void*>(renderer + 13),
                                          CorrectDialogueCapture);
        if (!hook)
        {
            logFile->error("Dialogue capture hook creation failed: {}",
                           static_cast<unsigned>(hook.error().type));
            return;
        }
        dialogueHook = std::move(*hook);
        logFile->info("Dialogue capture hook installed: game+0x{:X}", renderer + 13 - imageBase);
        if (!projection)
        {
            logFile->error("Character capture projection correction disabled");
            return;
        }
        auto projectionResult = SafetyHookMid::create(reinterpret_cast<void*>(projection + 19),
                                                      CorrectCaptureProjection);
        if (!projectionResult)
        {
            logFile->error("Character capture projection hook failed: {}",
                           static_cast<unsigned>(projectionResult.error().type));
            return;
        }
        captureProjectionHook = std::move(*projectionResult);
        logFile->info("Character capture projection hook installed: game+0x{:X}",
                      projection + 19 - imageBase);
    }

    bool InstallHooks()
    {
        constexpr char callerPattern[] =
            "40 53 48 83 EC 20 48 8B DA 48 8D 91 90 1B 00 00 48 8B CB "
            "E8 ?? ?? ?? ?? 48 8B C3 48 83 C4 20 5B C3";
        constexpr char copyPattern[] =
            "48 83 EC 20 F2 0F 10 02 48 8B F9 F2 0F 11 01 48 8B DA "
            "8B 42 08 89 41 08 F2 0F 10 42 0C F2 0F 11 41 0C "
            "8B 42 14 89 41 14 8B 42 18 89 41 18 8B 42 1C 89 41 1C "
            "8B 42 20 89 41 20 8B 42 24 89 41 24 8B 42 28 89 41 28 "
            "8B 42 2C 89 41 2C 8B 41 30 33 42 30 83 E0 01 31 41 30 "
            "8B 4A 30 33 4F 30 83 E1 02 31 4F 30";
        constexpr char projectionPattern[] =
            "F3 0F 10 49 18 F3 0F 5F 0D ?? ?? ?? ?? "
            "41 8B 41 08 41 8B 49 0C 41 2B 01 41 2B 49 04 "
            "F3 0F 59 0D ?? ?? ?? ?? 3B C1 7E 05 80 FA 02 74 31 "
            "80 FA 01 74 2C 0F B6 53 34 80 FA 01 74 23";
        const auto caller = FindUnique("Camera copy caller", callerPattern);
        const auto projection = FindUnique("Projection", projectionPattern);
        const auto copy = caller ? CallTarget(caller + 19) : 0;
        if (!copy || !Matches(copy + 6, Memory::PatternToBytes(copyPattern)) || !projection)
        {
            logFile->error("Camera-copy/projection contracts not verified; no hooks installed");
            return false;
        }
        auto bars = SafetyHookMid::create(reinterpret_cast<void*>(copy + 0x5b), RemoveBars,
                                         SafetyHookMid::StartDisabled);
        if (!bars)
        {
            logFile->error("Letterbox hook creation failed: {}", static_cast<unsigned>(bars.error().type));
            return false;
        }
        barsHook = std::move(*bars);
        auto fov = SafetyHookMid::create(reinterpret_cast<void*>(projection + 5), CorrectCamera,
                                        SafetyHookMid::StartDisabled);
        if (!fov)
        {
            logFile->error("FOV hook creation failed: {}", static_cast<unsigned>(fov.error().type));
            barsHook.reset();
            return false;
        }
        fovHook = std::move(*fov);
        auto b = barsHook.enable();
        auto f = fovHook.enable();
        if (!b || !f)
        {
            logFile->error("Hook enable failed (letterbox={}, FOV={})", bool(b), bool(f));
            fovHook.reset();
            barsHook.reset();
            return false;
        }
        ready.store(true, std::memory_order_release);
        logFile->info("Hooks enabled: letterbox game+0x{:X}, projection game+0x{:X}",
                      copy + 0x5b - imageBase, projection + 5 - imageBase);
        InstallDialogueHook();
        constexpr char equipmentPattern[] =
            "F3 0F 10 0D ?? ?? ?? ?? F3 0F 10 05 ?? ?? ?? ?? "
            "0F 14 C1 C6 80 E1 03 00 00 00 66 48 0F 7E C2 "
            "48 8B 86 00 08 00 00 48 8B 88 98 04 00 00 "
            "44 88 A1 E0 03 00 00";
        const auto equipment = FindUnique("Equipment capture", equipmentPattern);
        if (!equipment)
        {
            logFile->error("Equipment capture correction disabled; gameplay hooks remain active");
            return true;
        }
        auto equipmentResult = SafetyHookMid::create(reinterpret_cast<void*>(equipment + 26),
                                                     CorrectEquipmentCapture);
        if (!equipmentResult)
        {
            logFile->error("Equipment capture hook failed: {}; gameplay hooks remain active",
                           static_cast<unsigned>(equipmentResult.error().type));
            return true;
        }
        equipmentHook = std::move(*equipmentResult);
        logFile->info("Equipment capture hook installed: game+0x{:X}", equipment + 26 - imageBase);
        return true;
    }

    BOOL CALLBACK FindWindowCallback(HWND window, LPARAM parameter)
    {
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        if (process != GetCurrentProcessId() || !IsWindowVisible(window) ||
            GetWindow(window, GW_OWNER))
            return TRUE;
        wchar_t className[128]{};
        if (!GetClassNameW(window, className, static_cast<int>(std::size(className))) ||
            std::wcscmp(className, L"UnrealWindow") != 0)
            return TRUE;
        *reinterpret_cast<HWND*>(parameter) = window;
        return FALSE;
    }

    bool SetBorderless(HWND window)
    {
        SetLastError(ERROR_SUCCESS);
        LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
        if (!style && GetLastError() != ERROR_SUCCESS)
        {
            logFile->error("GetWindowLongPtr failed: {}", GetLastError());
            return false;
        }
        style &= ~(static_cast<LONG_PTR>(WS_CAPTION) | WS_THICKFRAME |
                   WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
        SetLastError(ERROR_SUCCESS);
        if (!SetWindowLongPtrW(window, GWL_STYLE, style) && GetLastError() != ERROR_SUCCESS)
        {
            logFile->error("SetWindowLongPtr failed: {}", GetLastError());
            return false;
        }
        SetLastError(ERROR_SUCCESS);
        LONG_PTR extended = GetWindowLongPtrW(window, GWL_EXSTYLE);
        if (!extended && GetLastError() != ERROR_SUCCESS)
        {
            logFile->error("GetWindowLongPtr extended style failed: {}", GetLastError());
            return false;
        }
        extended &= ~(static_cast<LONG_PTR>(WS_EX_CLIENTEDGE) | WS_EX_WINDOWEDGE | WS_EX_STATICEDGE);
        SetLastError(ERROR_SUCCESS);
        if (!SetWindowLongPtrW(window, GWL_EXSTYLE, extended) && GetLastError() != ERROR_SUCCESS)
        {
            logFile->error("SetWindowLongPtr extended style failed: {}", GetLastError());
            return false;
        }
        MONITORINFO info{sizeof(info)};
        if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info))
        {
            logFile->error("GetMonitorInfo failed: {}", GetLastError());
            return false;
        }
        const int x = info.rcMonitor.left + ((info.rcMonitor.right - info.rcMonitor.left) - targetWidth) / 2;
        const int y = info.rcMonitor.top + ((info.rcMonitor.bottom - info.rcMonitor.top) - targetHeight) / 2;
        if (!SetWindowPos(window, nullptr, x, y, targetWidth, targetHeight,
                          SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED))
        {
            logFile->error("SetWindowPos failed: {}", GetLastError());
            return false;
        }
        return true;
    }

    DWORD WINAPI Setup(void*)
    {
        wchar_t executable[32768]{};
        const auto exeLength = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        if (!exeLength || exeLength >= std::size(executable) ||
            _wcsicmp(std::filesystem::path(executable).filename().c_str(),
                     L"BloodstainedRotN-Win64-Shipping.exe") != 0)
            return 0;
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                reinterpret_cast<LPCWSTR>(ownModule), &pinned))
        {
            Notify("Cannot pin the fix module.");
            return 0;
        }
        wchar_t modulePath[32768]{};
        const auto length = GetModuleFileNameW(ownModule, modulePath, static_cast<DWORD>(std::size(modulePath)));
        if (!length || length >= std::size(modulePath))
        {
            Notify("Cannot locate the fix module.");
            return 0;
        }
        const auto folder = std::filesystem::path(modulePath).parent_path();
        try
        {
            logFile = spdlog::rotating_logger_mt("BloodstainedARFix",
                (folder / L"BloodstainedARFix.log").string(), 1024 * 1024, 1);
            logFile->flush_on(spdlog::level::info);
            logFile->set_error_handler([](const std::string& error) { Notify(error.c_str()); });
        }
        catch (const spdlog::spdlog_ex& error)
        {
            Notify(error.what());
            return 0;
        }
        if (!LoadConfig(folder))
        {
            Notify("Cannot load BloodstainedARFix.ini. See BloodstainedARFix.log.");
            return 0;
        }
        imageBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(imageBase);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(imageBase + dos->e_lfanew);
        imageSize = nt->OptionalHeader.SizeOfImage;
        if (!InstallHooks())
            Notify("Camera hooks could not be installed. See BloodstainedARFix.log.");

        HWND window = nullptr;
        for (int i = 0; i < 120 && !window; ++i)
        {
            EnumWindows(FindWindowCallback, reinterpret_cast<LPARAM>(&window));
            if (!window)
                Sleep(1000);
        }
        if (!window)
        {
            logFile->error("No visible UnrealWindow found within 120 seconds");
            return 0;
        }
        if (!SetBorderless(window))
        {
            logFile->error("Borderless window override stopped");
            return 0;
        }
        bool mismatchLogged = false;
        bool initialLogged = false;
        while (IsWindow(window))
        {
            Sleep(1000);
            if (!IsWindowVisible(window) || IsIconic(window))
                continue;
            RECT client{};
            if (!GetClientRect(window, &client))
            {
                logFile->error("Cannot read client size: {}", GetLastError());
                return 0;
            }
            const auto width = client.right - client.left;
            const auto height = client.bottom - client.top;
            if (width == targetWidth && height == targetHeight)
            {
                if (!initialLogged || mismatchLogged)
                    logFile->info("Window client={}x{}", width, height);
                initialLogged = true;
                mismatchLogged = false;
                continue;
            }
            if (!mismatchLogged)
            {
                logFile->warn("Window client={}x{}; restoring {}x{}",
                              width, height, targetWidth, targetHeight);
                mismatchLogged = true;
            }
            if (!SetBorderless(window))
            {
                logFile->error("Borderless window override stopped");
                return 0;
            }
        }
        logFile->info("Game window closed; borderless window override stopped");
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        ownModule = module;
        DisableThreadLibraryCalls(module);
        const HANDLE thread = CreateThread(nullptr, 0, Setup, nullptr, 0, nullptr);
        if (!thread)
        {
            OutputDebugStringA("BloodstainedARFix: setup thread creation failed.\n");
            return FALSE;
        }
        CloseHandle(thread);
    }
    return TRUE;
}
