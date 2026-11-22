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
    constexpr char fixName[] = "CCFF7R_ARFix";
    constexpr char curtainPattern[] = "41 0F 11 46 20 41 83 BE ?? ?? ?? ?? 00 41 0F 10 46 20";
    constexpr size_t curtainHookOffset = 0x0D;
    constexpr wchar_t setResName[] = L"r.SetRes";
    constexpr size_t setResSlot = 16;
    constexpr uint32_t setByConsole = 0x09000000;
    constexpr float halfDegToRad = 3.14159265358979f / 360.0f;
    constexpr float referenceAspect = 16.0f / 9.0f;

    constexpr size_t viewFov = 0x18;
    constexpr size_t viewConstrain = 0x30;
    constexpr size_t viewProjectionMode = 0x34;
    constexpr size_t dataProjection = 0x50;
    constexpr size_t dataViewRect = 0x90;

    struct Section
    {
        uintptr_t begin;
        uintptr_t end;
        bool executable;
    };

    HMODULE ownModule = nullptr;
    std::shared_ptr<spdlog::logger> logFile;
    SafetyHookInline projectionHook;
    SafetyHookMid curtainHook;
    uintptr_t imageBase = 0;
    size_t imageSize = 0;
    std::vector<Section> sections;
    float fovMultiplier = 1.0f;
    LONG targetWidth = 0;
    LONG targetHeight = 0;
    void** setResVariable = nullptr;
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
        imageSize = nt->OptionalHeader.SizeOfImage;
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

    bool ReadsPiOver360(uintptr_t begin, size_t length)
    {
        const auto* bytes = reinterpret_cast<const uint8_t*>(begin);
        for (size_t i = 0; i + 8 <= length; ++i)
        {
            size_t at = i;
            if (bytes[at] != 0xF3)
                continue;
            ++at;
            if ((bytes[at] & 0xF0) == 0x40)
                ++at;
            if (bytes[at] != 0x0F || bytes[at + 1] != 0x59 || (bytes[at + 2] & 0xC7) != 0x05)
                continue;
            const auto constant = RipTarget(begin + at + 3, 0);
            float value = 0;
            if (!InImage(constant, sizeof(value)))
                continue;
            std::memcpy(&value, reinterpret_cast<const void*>(constant), sizeof(value));
            if (std::fabs(value - halfDegToRad) < 1e-7f)
                return true;
        }
        return false;
    }

    uintptr_t FindProjectionFunction()
    {
        std::vector<uintptr_t> found;
        ForEachMatch(Memory::PatternToBytes("F6"), true, [&](uintptr_t test) {
            const auto* code = reinterpret_cast<const uint8_t*>(test);
            if ((code[1] & 0xC0) != 0x40 || (code[1] & 0x07) == 0x04 || code[2] != 0x30 || code[3] != 0x01)
                return false;
            uintptr_t start = 0;
            for (uintptr_t back = test; back + 0x40 > test && InCode(back - 1); --back)
                if ((back & 0xF) == 0 && *reinterpret_cast<const uint8_t*>(back - 1) == 0xCC)
                {
                    start = back;
                    break;
                }
            if (!start)
                return false;
            if (test - start > 0x30)
                return false;
            bool loadsAspect = false;
            for (size_t i = 0; i + 6 <= 0x80 && !loadsAspect; ++i)
            {
                const size_t rex = (code[i + 1] & 0xF0) == 0x40 ? 1 : 0;
                const auto* op = code + i + 1 + rex;
                loadsAspect = code[i] == 0xF3 && op[0] == 0x0F && op[1] == 0x10 && (op[2] & 0xC0) == 0x40 &&
                    (op[2] & 0x07) != 0x04 && op[3] == 0x2C;
            }
            if (loadsAspect && InCode(start + 0x800) && ReadsPiOver360(start, 0x800))
                found.push_back(start);
            return false;
        });
        if (found.size() != 1)
        {
            logFile->error("Projection function: {} candidates", found.size());
            return 0;
        }
        logFile->info("Projection function: game+0x{:X}", found[0] - imageBase);
        return found[0];
    }

    bool FindSetRes()
    {
        std::vector<int> name;
        for (size_t i = 0; i < sizeof(setResName); ++i)
            name.push_back(reinterpret_cast<const uint8_t*>(setResName)[i]);
        uintptr_t text = 0;
        ForEachMatch(name, false, [&](uintptr_t match) {
            text = match;
            return true;
        });
        if (!text)
        {
            logFile->error("r.SetRes name not found");
            return false;
        }
        uintptr_t storage = 0;
        ForEachMatch(Memory::PatternToBytes("8D"), true, [&](uintptr_t lea) {
            const auto* bytes = reinterpret_cast<const uint8_t*>(lea);
            if ((bytes[-1] & 0xF8) != 0x48 || (bytes[1] & 0xC7) != 0x05 || RipTarget(lea + 2, 0) != text)
                return false;
            for (size_t i = 6; i < 0x30; ++i)
                if (bytes[i] == 0x48 && bytes[i + 1] == 0x89 && bytes[i + 2] == 0x05)
                {
                    storage = RipTarget(lea + i + 3, 0);
                    return InImage(storage, sizeof(void*));
                }
            return false;
        });
        if (!storage)
        {
            logFile->error("r.SetRes variable not found");
            return false;
        }
        setResVariable = reinterpret_cast<void**>(storage);
        logFile->info("r.SetRes at game+0x{:X}", storage - imageBase);
        return true;
    }

    bool IsStringVariable(void* variable)
    {
        const auto* table = *reinterpret_cast<uintptr_t* const*>(variable);
        if (!InImage(reinterpret_cast<uintptr_t>(table), (setResSlot + 1) * sizeof(void*)))
            return false;
        const auto* asVariable = reinterpret_cast<const uint8_t*>(table[5]);
        const auto* isString = reinterpret_cast<const uint8_t*>(table[9]);
        return InCode(table[5]) && InCode(table[9]) && InCode(table[setResSlot]) &&
            std::memcmp(asVariable, "\x48\x8B\xC1\xC3", 4) == 0 && std::memcmp(isString, "\xB0\x01\xC3", 3) == 0;
    }

    void RequestResolution(int32_t width, int32_t height)
    {
        const LONG wantW = wantWidth.load(std::memory_order_relaxed);
        const LONG wantH = wantHeight.load(std::memory_order_relaxed);
        if (!setResVariable || wantW <= 0 || wantH <= 0 || (width == wantW && height == wantH))
            return;
        const ULONGLONG now = GetTickCount64();
        if (now - lastRequest < 2000)
            return;
        lastRequest = now;
        void* variable = *setResVariable;
        if (!variable)
            return;
        if (!IsStringVariable(variable))
        {
            logFile->error("r.SetRes has an unexpected layout");
            setResVariable = nullptr;
            return;
        }
        const bool fullscreen = wantFullscreen.load();
        wchar_t value[48]{};
        swprintf_s(value, L"%ldx%ld%s", wantW, wantH, fullscreen ? L"wf" : L"w");
        using SetFn = void(__fastcall*)(void*, const wchar_t*, uint32_t);
        (*reinterpret_cast<SetFn**>(variable))[setResSlot](variable, value, setByConsole);
        logFile->info("Viewport {}x{}: r.SetRes {}x{}{}", width, height, wantW, wantH, fullscreen ? "wf" : "w");
    }

    void __fastcall CalculateProjection(uint8_t* view, uint8_t axis, void* viewport, uint8_t* data)
    {
        const uint8_t constrain = view[viewConstrain];
        view[viewConstrain] = constrain & ~1;
        projectionHook.call<void>(view, axis, viewport, data);
        view[viewConstrain] = constrain;

        const auto* rect = reinterpret_cast<const int32_t*>(data + dataViewRect);
        const int32_t width = rect[2] - rect[0];
        const int32_t height = rect[3] - rect[1];
        RequestResolution(width, height);
        if (view[viewProjectionMode] != 0 || width <= 0 || height <= 0)
            return;
        float fov = 0;
        std::memcpy(&fov, view + viewFov, sizeof(fov));
        if (!(fov > 0.0f && fov < 180.0f))
            return;
        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        const float tanHorizontal = std::tan((std::min)(fov * fovMultiplier, 170.0f) * halfDegToRad) *
            (std::max)(1.0f, aspect / referenceAspect);
        auto* matrix = reinterpret_cast<float*>(data + dataProjection);
        matrix[0] = 1.0f / tanHorizontal;
        matrix[5] = aspect / tanHorizontal;
    }

    bool Matches(const float* rect, float left, float top, float right, float bottom)
    {
        return rect[0] == left && rect[1] == top && rect[2] == right && rect[3] == bottom;
    }

    void AdjustCurtains(SafetyHookContext& ctx)
    {
        auto* rect = reinterpret_cast<float*>(ctx.r14 + 0x20);
        if (Matches(rect, -5760.0f, -3240.0f, 11520.0f, 2160.0f) ||
            Matches(rect, -5760.0f, 1080.0f, 11520.0f, 2160.0f) ||
            Matches(rect, 1920.0f, -1080.0f, 3840.0f, 2160.0f) ||
            Matches(rect, -5760.0f, -1080.0f, 3840.0f, 2160.0f))
            std::memset(rect, 0, sizeof(float) * 4);
        else if (Matches(rect, -2170.0f, -1330.0f, 4340.0f, 2660.0f))
            for (int i = 0; i < 4; ++i)
                rect[i] *= 4.0f;
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
        SetWindowPos(window, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
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

    bool InstallCurtainHook()
    {
        std::vector<uintptr_t> found;
        ForEachMatch(Memory::PatternToBytes(curtainPattern), true, [&](uintptr_t site) {
            found.push_back(site);
            return false;
        });
        if (found.size() != 1)
        {
            logFile->warn("Curtain site: {} candidates; UI curtains stay", found.size());
            return false;
        }
        auto hook = SafetyHookMid::create(reinterpret_cast<void*>(found[0] + curtainHookOffset), AdjustCurtains);
        if (!hook)
        {
            logFile->warn("Curtain hook failed: {}", static_cast<unsigned>(hook.error().type));
            return false;
        }
        curtainHook = std::move(*hook);
        logFile->info("Curtain hook at game+0x{:X}", found[0] + curtainHookOffset - imageBase);
        return true;
    }

    DWORD WINAPI Setup(void*)
    {
        wchar_t executable[32768]{};
        const auto exeLength = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        if (!exeLength || exeLength >= std::size(executable) ||
            _wcsicmp(std::filesystem::path(executable).filename().c_str(), L"CCFF7R-Win64-Shipping.exe") != 0)
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
            logFile = spdlog::rotating_logger_mt(fixName, (folder / L"CCFF7R_ARFix.log").string(), 1024 * 1024, 1, true);
            logFile->flush_on(spdlog::level::info);
        }
        catch (const spdlog::spdlog_ex& error)
        {
            Notify(error.what());
            return 0;
        }
        const auto ini = (folder / L"CCFF7R_ARFix.ini").wstring();
        char value[64]{};
        const auto valueLength = GetPrivateProfileStringA("FOV", "Multiplier", "1.0", value,
                                                          static_cast<DWORD>(std::size(value)),
                                                          (folder / L"CCFF7R_ARFix.ini").string().c_str());
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
        logFile->info("{} {}.{}.{}: Resolution={}x{}, FOV Multiplier={:.3f}", fixName, VERSION_MAJOR, VERSION_MINOR,
                      VERSION_PATCH, targetWidth, targetHeight, fovMultiplier);

        imageBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        LoadSections();
        uintptr_t projection = 0;
        for (int attempt = 0; attempt < 120 && !projection; ++attempt)
        {
            projection = FindProjectionFunction();
            if (!projection)
                Sleep(250);
        }
        if (projection)
        {
            auto hook = SafetyHookInline::create(reinterpret_cast<void*>(projection), CalculateProjection);
            if (hook)
                projectionHook = std::move(*hook);
            else
                logFile->error("Projection hook failed: {}", static_cast<unsigned>(hook.error().type));
        }
        if (!projectionHook)
        {
            Notify("CCFF7R_ARFix: unsupported game version. See CCFF7R_ARFix.log.");
            return 0;
        }
        FindSetRes();
        InstallCurtainHook();
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
