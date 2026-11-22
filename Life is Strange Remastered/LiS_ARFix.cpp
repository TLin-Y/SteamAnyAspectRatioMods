#include "stdafx.h"
#include "helper.hpp"
#include <safetyhook.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <memory>
#include <vector>

namespace
{
    static_assert(sizeof(void*) == 8, "Build for Windows x64.");
    constexpr char fixName[] = "LiS_ARFix";
    constexpr uint32_t setByConsole = 0x09000000;
    constexpr float halfDegToRad = 3.14159265358979f / 360.0f;
    constexpr float referenceAspect = 16.0f / 9.0f;
    constexpr size_t dataProjection = 0x50;
    constexpr size_t dataViewRect = 0x90;

    struct Section
    {
        uintptr_t begin;
        uintptr_t end;
        bool executable;
    };

    struct ViewLayout
    {
        uintptr_t function;
        size_t fov;
        size_t constrain;
        size_t projectionMode;
    };

    struct ConsoleVariable
    {
        const wchar_t* name;
        const char* label;
        std::vector<void**> storage;
        size_t slot = 0;
    };

    HMODULE ownModule = nullptr;
    std::shared_ptr<spdlog::logger> logFile;
    SafetyHookInline projectionHook;
    uintptr_t imageBase = 0;
    std::vector<Section> sections;
    ViewLayout layout{};
    float fovMultiplier = 1.0f;
    LONG targetWidth = 0;
    LONG targetHeight = 0;
    ConsoleVariable setRes{L"r.SetRes", "r.SetRes"};
    ConsoleVariable screenPercentageVariable{L"r.ScreenPercentage", "r.ScreenPercentage"};
    int screenPercentage = 0;
    bool screenPercentageApplied = false;
    std::atomic<LONG> wantWidth{0};
    std::atomic<LONG> wantHeight{0};
    std::atomic<bool> wantFullscreen{false};
    ULONGLONG lastRequest = 0;

    void Notify(const char* message)
    {
        OutputDebugStringA(message);
        MessageBoxA(nullptr, message, fixName, MB_OK | MB_ICONERROR);
    }

    void LoadSections()
    {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(imageBase);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(imageBase + dos->e_lfanew);
        const auto* section = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
        {
            const auto begin = imageBase + section->VirtualAddress;
            const auto size = (std::max)(section->Misc.VirtualSize, section->SizeOfRawData);
            const bool executable = (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
            const bool readable = (section->Characteristics & IMAGE_SCN_MEM_READ) != 0;
            if (readable && size && std::memcmp(section->Name, ".bind", 5) != 0)
                sections.push_back({begin, begin + size, executable});
        }
    }

    bool InImage(uintptr_t address, size_t size)
    {
        for (const auto& section : sections)
            if (address >= section.begin && address + size <= section.end)
                return true;
        return false;
    }

    bool InCode(uintptr_t address)
    {
        for (const auto& section : sections)
            if (section.executable && address >= section.begin && address < section.end)
                return true;
        return false;
    }

    template <typename Visit>
    void ForEachMatch(const std::vector<int>& pattern, bool code, Visit visit)
    {
        for (const auto& section : sections)
        {
            if (section.executable != code)
                continue;
            const auto* bytes = reinterpret_cast<const uint8_t*>(section.begin);
            const size_t size = section.end - section.begin;
            for (size_t i = 0; i + pattern.size() <= size; ++i)
            {
                if (bytes[i] != pattern[0])
                    continue;
                size_t j = 1;
                while (j < pattern.size() && (pattern[j] < 0 || bytes[i + j] == pattern[j]))
                    ++j;
                if (j == pattern.size() && visit(section.begin + i))
                    return;
            }
        }
    }

    uintptr_t RipTarget(uintptr_t displacementAddress, size_t bytesAfterDisplacement)
    {
        int32_t displacement = 0;
        std::memcpy(&displacement, reinterpret_cast<const void*>(displacementAddress), sizeof(displacement));
        return displacementAddress + 4 + bytesAfterDisplacement + displacement;
    }

    bool IsDisp8(uint8_t modrm)
    {
        return (modrm & 0xC0) == 0x40 && (modrm & 0x07) != 0x04;
    }

    size_t SseOpcode(const uint8_t* code, uint8_t opcode)
    {
        if (code[0] != 0xF3)
            return 0;
        const size_t rex = (code[1] & 0xF0) == 0x40 ? 1 : 0;
        return code[1 + rex] == 0x0F && code[2 + rex] == opcode ? 3 + rex : 0;
    }

    bool ReadsPiOver360(uintptr_t begin, size_t length)
    {
        const auto* bytes = reinterpret_cast<const uint8_t*>(begin);
        for (size_t i = 0; i + 8 <= length; ++i)
        {
            const size_t at = SseOpcode(bytes + i, 0x59);
            if (!at || (bytes[i + at] & 0xC7) != 0x05)
                continue;
            const auto constant = RipTarget(begin + i + at + 1, 0);
            float value = 0;
            if (!InImage(constant, sizeof(value)))
                continue;
            std::memcpy(&value, reinterpret_cast<const void*>(constant), sizeof(value));
            if (std::fabs(value - halfDegToRad) < 1e-7f)
                return true;
        }
        return false;
    }

    bool LoadsField(const uint8_t* code, size_t length, size_t offset)
    {
        for (size_t i = 0; i + 6 <= length; ++i)
        {
            const size_t at = SseOpcode(code + i, 0x10);
            if (at && IsDisp8(code[i + at]) && code[i + at + 1] == offset)
                return true;
        }
        return false;
    }

    bool ComparesByteToOne(const uint8_t* code, size_t length, size_t offset)
    {
        for (size_t i = 0; i + 4 <= length; ++i)
            if (code[i] == 0x80 && (code[i + 1] & 0xF8) == 0x78 && (code[i + 1] & 0x07) != 0x04 &&
                code[i + 2] == offset && code[i + 3] == 0x01)
                return true;
        return false;
    }

    size_t FindFovOffset(const uint8_t* code, size_t length)
    {
        size_t offset = 0;
        for (size_t i = 0; i + 12 <= length; ++i)
        {
            const size_t at = SseOpcode(code + i, 0x10);
            if (!at || !IsDisp8(code[i + at]) || !SseOpcode(code + i + at + 2, 0x5F))
                continue;
            const size_t field = code[i + at + 1];
            if (offset && offset != field)
                return 0;
            offset = field;
        }
        return offset;
    }

    bool FindProjectionFunction()
    {
        std::vector<ViewLayout> found;
        ForEachMatch(Memory::PatternToBytes("F6"), true, [&](uintptr_t test) {
            const auto* code = reinterpret_cast<const uint8_t*>(test);
            if ((code[1] & 0x38) != 0 || !IsDisp8(code[1]) || code[3] != 0x01 || code[2] < 4 || code[2] > 0x7B)
                return false;
            uintptr_t start = 0;
            for (uintptr_t back = test; back + 0x40 > test && InCode(back - 1); --back)
                if ((back & 0xF) == 0 && *reinterpret_cast<const uint8_t*>(back - 1) == 0xCC)
                {
                    start = back;
                    break;
                }
            if (!start || test - start > 0x30 || !InCode(start + 0x800))
                return false;
            const size_t constrain = code[2];
            const auto* function = reinterpret_cast<const uint8_t*>(start);
            if (!LoadsField(code, 0x80, constrain - 4) || !ComparesByteToOne(function, 0x100, constrain + 4))
                return false;
            const size_t fov = FindFovOffset(function, 0x800);
            if (fov && fov < constrain && ReadsPiOver360(start, 0x800))
                found.push_back({start, fov, constrain, constrain + 4});
            return false;
        });
        if (found.size() != 1)
        {
            logFile->error("Projection function: {} candidates", found.size());
            return false;
        }
        layout = found[0];
        logFile->info("Projection function: game+0x{:X} (FOV +0x{:X}, constrain +0x{:X}, mode +0x{:X})",
                      layout.function - imageBase, layout.fov, layout.constrain, layout.projectionMode);
        return true;
    }

    bool FindVariable(ConsoleVariable& variable)
    {
        std::vector<int> name;
        const auto* chars = reinterpret_cast<const uint8_t*>(variable.name);
        for (size_t i = 0; i < (std::wcslen(variable.name) + 1) * sizeof(wchar_t); ++i)
            name.push_back(chars[i]);
        uintptr_t text = 0;
        ForEachMatch(name, false, [&](uintptr_t match) {
            text = match;
            return true;
        });
        if (!text)
        {
            logFile->error("{} name not found", variable.label);
            return false;
        }
        ForEachMatch(Memory::PatternToBytes("8D"), true, [&](uintptr_t lea) {
            const auto* bytes = reinterpret_cast<const uint8_t*>(lea);
            if ((bytes[-1] & 0xF8) != 0x48 || (bytes[1] & 0xC7) != 0x05 || RipTarget(lea + 2, 0) != text)
                return false;
            for (size_t i = 6; i < 0x30; ++i)
                if (bytes[i] == 0x48 && bytes[i + 1] == 0x89 && bytes[i + 2] == 0x05)
                {
                    const auto storage = RipTarget(lea + i + 3, 0);
                    if (InImage(storage, sizeof(void*)))
                        variable.storage.push_back(reinterpret_cast<void**>(storage));
                    break;
                }
            return false;
        });
        if (variable.storage.empty())
        {
            logFile->error("{} variable not found", variable.label);
            return false;
        }
        logFile->info("{} at game+0x{:X} ({} references)", variable.label,
                      reinterpret_cast<uintptr_t>(variable.storage[0]) - imageBase, variable.storage.size());
        return true;
    }

    size_t FindSetSlot(void* variable)
    {
        const auto* table = *reinterpret_cast<uintptr_t* const*>(variable);
        if (!InImage(reinterpret_cast<uintptr_t>(table), 24 * sizeof(void*)))
            return 0;
        for (size_t i = 5; i < 24; ++i)
            if (!InCode(table[i]))
                return 0;
        const auto code = [&](size_t slot) { return reinterpret_cast<const uint8_t*>(table[slot]); };
        if (std::memcmp(code(5), "\x48\x8B\xC1\xC3", 4) != 0)
            return 0;
        size_t release = 0;
        for (size_t i = 6; i < 22 && !release; ++i)
            if (std::memcmp(code(i), "\x48\x85\xC9\x74", 4) == 0 && std::memcmp(code(i) + 8, "\xBA\x01\x00\x00\x00", 5) == 0)
                release = i;
        if (!release)
            return 0;
        size_t typeChecks = 0;
        for (size_t i = 6; i < release; ++i)
            if (std::memcmp(code(i), "\xB0\x01\xC3", 3) == 0)
                ++typeChecks;
        return typeChecks <= 1 ? release + 1 : 0;
    }

    bool SetVariable(ConsoleVariable& variable, const wchar_t* value)
    {
        void* object = nullptr;
        for (auto* storage : variable.storage)
            if ((object = *storage) != nullptr)
                break;
        if (!object)
            return false;
        if (!variable.slot)
            variable.slot = FindSetSlot(object);
        if (!variable.slot)
        {
            logFile->error("{} has an unexpected layout", variable.label);
            variable.storage.clear();
            return false;
        }
        using SetFn = void(__fastcall*)(void*, const wchar_t*, uint32_t);
        (*reinterpret_cast<SetFn**>(object))[variable.slot](object, value, setByConsole);
        return true;
    }

    void RequestResolution(int32_t width, int32_t height)
    {
        const LONG wantW = wantWidth.load(std::memory_order_relaxed);
        const LONG wantH = wantHeight.load(std::memory_order_relaxed);
        if (wantW <= 0 || wantH <= 0 || (width == wantW && height == wantH))
            return;
        const ULONGLONG now = GetTickCount64();
        if (now - lastRequest < 2000)
            return;
        lastRequest = now;
        const bool fullscreen = wantFullscreen.load();
        wchar_t value[48]{};
        swprintf_s(value, L"%ldx%ld%s", wantW, wantH, fullscreen ? L"wf" : L"w");
        if (SetVariable(setRes, value))
            logFile->info("Viewport {}x{}: r.SetRes {}x{}{}", width, height, wantW, wantH, fullscreen ? "wf" : "w");
    }

    void ApplyScreenPercentage()
    {
        if (screenPercentageApplied || screenPercentage <= 0)
            return;
        wchar_t value[32]{};
        swprintf_s(value, L"%d", screenPercentage);
        if (SetVariable(screenPercentageVariable, value))
        {
            screenPercentageApplied = true;
            logFile->info("r.ScreenPercentage {}", screenPercentage);
        }
    }

    void __fastcall CalculateProjection(uint8_t* view, uint8_t axis, void* viewport, uint8_t* data)
    {
        const uint8_t constrain = view[layout.constrain];
        view[layout.constrain] = constrain & ~1;
        projectionHook.call<void>(view, axis, viewport, data);
        view[layout.constrain] = constrain;

        const auto* rect = reinterpret_cast<const int32_t*>(data + dataViewRect);
        const int32_t width = rect[2] - rect[0];
        const int32_t height = rect[3] - rect[1];
        RequestResolution(width, height);
        ApplyScreenPercentage();
        if (view[layout.projectionMode] != 0 || width <= 0 || height <= 0)
            return;
        float fov = 0;
        std::memcpy(&fov, view + layout.fov, sizeof(fov));
        if (!(fov > 0.0f && fov < 180.0f))
            return;
        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        const float tanHorizontal = std::tan((std::min)(fov * fovMultiplier, 170.0f) * halfDegToRad) *
            (std::max)(1.0f, aspect / referenceAspect);
        auto* matrix = reinterpret_cast<float*>(data + dataProjection);
        matrix[0] = 1.0f / tanHorizontal;
        matrix[5] = aspect / tanHorizontal;
    }

    BOOL CALLBACK FindWindowCallback(HWND window, LPARAM parameter)
    {
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        if (process != GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window, GW_OWNER))
            return TRUE;
        wchar_t className[64]{};
        GetClassNameW(window, className, static_cast<int>(std::size(className)));
        if (std::wcscmp(className, L"UnrealWindow") != 0)
            return TRUE;
        *reinterpret_cast<HWND*>(parameter) = window;
        return FALSE;
    }

    void FixWindow(HWND window)
    {
        constexpr LONG_PTR frame = WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
        constexpr LONG_PTR frameEx = WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE | WS_EX_STATICEDGE;
        MONITORINFO info{sizeof(info)};
        if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info))
            return;
        const LONG monitorWidth = info.rcMonitor.right - info.rcMonitor.left;
        const LONG monitorHeight = info.rcMonitor.bottom - info.rcMonitor.top;
        const bool forceSize = targetWidth > 0 && targetHeight > 0;
        const LONG width = forceSize ? targetWidth : monitorWidth;
        const LONG height = forceSize ? targetHeight : monitorHeight;
        wantWidth.store(width);
        wantHeight.store(height);
        wantFullscreen.store(!forceSize);
        if (!forceSize)
            return;
        const LONG x = info.rcMonitor.left + (std::max)(0L, (monitorWidth - width) / 2);
        const LONG y = info.rcMonitor.top + (std::max)(0L, (monitorHeight - height) / 2);
        LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
        RECT client{};
        RECT current{};
        if (!(style & frame) && GetClientRect(window, &client) && client.right == width && client.bottom == height &&
            GetWindowRect(window, &current) && current.left == x && current.top == y)
            return;
        if (style & WS_MAXIMIZE)
            ShowWindow(window, SW_RESTORE);
        style = GetWindowLongPtrW(window, GWL_STYLE);
        SetWindowLongPtrW(window, GWL_STYLE, style & ~frame);
        SetWindowLongPtrW(window, GWL_EXSTYLE, GetWindowLongPtrW(window, GWL_EXSTYLE) & ~frameEx);
        SetWindowPos(window, nullptr, x, y, width + 1, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        SetWindowPos(window, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        logFile->info("Window {}x{} at {},{}", width, height, x, y);
    }

    DWORD WINAPI WindowLoop(void*)
    {
        for (;;)
        {
            HWND window = nullptr;
            EnumWindows(FindWindowCallback, reinterpret_cast<LPARAM>(&window));
            if (window && !IsIconic(window))
                FixWindow(window);
            Sleep(1000);
        }
    }

    DWORD WINAPI Setup(void*)
    {
        wchar_t executable[32768]{};
        const auto exeLength = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        if (!exeLength || exeLength >= std::size(executable) ||
            _wcsicmp(std::filesystem::path(executable).filename().c_str(), L"LiS-Win64-Shipping.exe") != 0)
            return 0;
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
            logFile = spdlog::rotating_logger_mt(fixName, (folder / L"LiS_ARFix.log").string(), 1024 * 1024, 1, true);
            logFile->flush_on(spdlog::level::info);
        }
        catch (const spdlog::spdlog_ex& error)
        {
            Notify(error.what());
            return 0;
        }
        const auto ini = (folder / L"LiS_ARFix.ini").wstring();
        char value[64]{};
        const auto valueLength = GetPrivateProfileStringA("FOV", "Multiplier", "1.0", value,
                                                          static_cast<DWORD>(std::size(value)),
                                                          (folder / L"LiS_ARFix.ini").string().c_str());
        const auto parsed = std::from_chars(value, value + valueLength, fovMultiplier);
        if (parsed.ec != std::errc{} || !(fovMultiplier >= 0.5f && fovMultiplier <= 2.0f))
        {
            logFile->warn("FOV Multiplier must be 0.5-2.0; using 1.0");
            fovMultiplier = 1.0f;
        }
        targetWidth = static_cast<LONG>(GetPrivateProfileIntW(L"Resolution", L"Width", 0, ini.c_str()));
        targetHeight = static_cast<LONG>(GetPrivateProfileIntW(L"Resolution", L"Height", 0, ini.c_str()));
        if (targetWidth <= 0 || targetHeight <= 0 || targetWidth > 16384 || targetHeight > 16384)
            targetWidth = targetHeight = 0;
        screenPercentage = static_cast<int>(GetPrivateProfileIntW(L"Render", L"OverrideResolutionQuality", 0, ini.c_str()));
        if (screenPercentage < 0 || screenPercentage > 200)
            screenPercentage = 0;
        logFile->info("{} {}.{}.{}: Resolution={}x{}, FOV Multiplier={:.3f}, OverrideResolutionQuality={}", fixName,
                      VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, targetWidth, targetHeight, fovMultiplier,
                      screenPercentage);

        imageBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        LoadSections();
        bool found = false;
        for (int attempt = 0; attempt < 120 && !found; ++attempt)
        {
            found = FindProjectionFunction();
            if (!found)
                Sleep(250);
        }
        if (found)
        {
            auto hook = SafetyHookInline::create(reinterpret_cast<void*>(layout.function), CalculateProjection);
            if (hook)
                projectionHook = std::move(*hook);
            else
                logFile->error("Projection hook failed: {}", static_cast<unsigned>(hook.error().type));
        }
        if (!projectionHook)
        {
            Notify("LiS_ARFix: unsupported game version. See LiS_ARFix.log.");
            return 0;
        }
        FindVariable(setRes);
        if (screenPercentage > 0)
            FindVariable(screenPercentageVariable);
        if (const HANDLE thread = CreateThread(nullptr, 0, WindowLoop, nullptr, 0, nullptr))
            CloseHandle(thread);
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
            return FALSE;
        CloseHandle(thread);
    }
    return TRUE;
}
