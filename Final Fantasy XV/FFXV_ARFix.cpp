#include "stdafx.h"
#include "helper.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <tlhelp32.h>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace
{
    static_assert(sizeof(void*) == 8, "Build for Windows x64.");
    constexpr char fixName[] = "FFXV_ARFix";
    constexpr float referenceAspect = 16.0f / 9.0f;
    constexpr size_t uiFlagOffset = 0x1E1;
    constexpr size_t fovOffset = 0x1C;

    struct Section
    {
        uintptr_t begin;
        uintptr_t end;
        bool executable;
    };

    struct Breakpoint
    {
        uintptr_t address;
        uint8_t length;
        int widthReg;
        int heightReg;
        bool moveHeightToRbx;
    };

    HMODULE ownModule = nullptr;
    std::shared_ptr<spdlog::logger> logFile;
    uintptr_t imageBase = 0;
    std::vector<Section> sections;
    Breakpoint breakpoints[3]{};
    size_t breakpointCount = 0;
    std::atomic<uint32_t> renderWidth{0};
    std::atomic<uint32_t> renderHeight{0};
    float fovMultiplier = 1.0f;
    uintptr_t uiSingletonStorage = 0;
    float* fovValue = nullptr;

    enum Reg { RAX, RCX, RDX, RBX, RSI, R12, R15 };

    DWORD64 ReadReg(const CONTEXT* c, int reg)
    {
        switch (reg)
        {
        case RAX: return c->Rax;
        case RCX: return c->Rcx;
        case RDX: return c->Rdx;
        case RBX: return c->Rbx;
        case RSI: return c->Rsi;
        case R12: return c->R12;
        case R15: return c->R15;
        }
        return 0;
    }

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
            const auto size = section->Misc.VirtualSize;
            if (size && (section->Characteristics & IMAGE_SCN_MEM_READ))
                sections.push_back({begin, begin + size, (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0});
        }
    }

    bool InImage(uintptr_t address, size_t size)
    {
        for (const auto& section : sections)
            if (address >= section.begin && address + size <= section.end)
                return true;
        return false;
    }

    size_t ScanRaw(const uint8_t* bytes, size_t size, const int* pattern, size_t count, uintptr_t* out, size_t maxOut)
    {
        size_t found = 0;
        __try
        {
            for (size_t i = 0; i + count <= size && found < maxOut; ++i)
            {
                if (bytes[i] != pattern[0])
                    continue;
                size_t j = 1;
                while (j < count && (pattern[j] < 0 || bytes[i + j] == pattern[j]))
                    ++j;
                if (j == count)
                    out[found++] = reinterpret_cast<uintptr_t>(bytes + i);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return found;
    }

    std::vector<uintptr_t> FindAll(const std::vector<int>& pattern, bool code)
    {
        std::vector<uintptr_t> found;
        uintptr_t matches[16];
        for (const auto& section : sections)
        {
            if (section.executable != code)
                continue;
            const size_t count = ScanRaw(reinterpret_cast<const uint8_t*>(section.begin), section.end - section.begin,
                                         pattern.data(), pattern.size(), matches, std::size(matches));
            found.insert(found.end(), matches, matches + count);
        }
        return found;
    }

    uintptr_t RelTarget(uintptr_t displacementAddress)
    {
        int32_t displacement = 0;
        std::memcpy(&displacement, reinterpret_cast<const void*>(displacementAddress), sizeof(displacement));
        return displacementAddress + 4 + displacement;
    }

    bool AddFit(const char* name, std::initializer_list<const char*> patterns, size_t cmovOffset, uint8_t cmovLength,
                int widthReg, int heightReg, bool moveHeightToRbx = false)
    {
        std::vector<uintptr_t> found;
        for (const char* pattern : patterns)
        {
            auto matches = FindAll(Memory::PatternToBytes(pattern), true);
            found.insert(found.end(), matches.begin(), matches.end());
        }
        if (found.size() != 1)
        {
            logFile->error("{}: {} candidates", name, found.size());
            return false;
        }
        breakpoints[breakpointCount++] = {found[0] + cmovOffset, cmovLength, widthReg, heightReg, moveHeightToRbx};
        logFile->info("{} at game+0x{:X}", name, found[0] + cmovOffset - imageBase);
        return true;
    }

    LONG CALLBACK Handler(EXCEPTION_POINTERS* info)
    {
        if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
            return EXCEPTION_CONTINUE_SEARCH;
        auto* context = info->ContextRecord;
        for (size_t i = 0; i < breakpointCount; ++i)
        {
            const auto& bp = breakpoints[i];
            if (context->Rip != bp.address)
                continue;
            const auto width = static_cast<uint32_t>(ReadReg(context, bp.widthReg));
            const auto height = static_cast<uint32_t>(ReadReg(context, bp.heightReg));
            if (width >= 320 && height >= 200 && width <= 16384 && height <= 16384)
            {
                renderWidth.store(width);
                renderHeight.store(height);
            }
            if (bp.moveHeightToRbx)
                context->Rbx = (context->Rbx & ~0xFFFFFFFFull) | height;
            context->Rip += bp.length;
            context->Dr6 = 0;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    void ArmThread(HANDLE thread)
    {
        if (!thread || !breakpointCount)
            return;
        DWORD64 control = 0;
        for (size_t i = 0; i < breakpointCount; ++i)
            control |= 1ull << (i * 2);
        CONTEXT context{};
        context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (!GetThreadContext(thread, &context))
            return;
        if (context.Dr0 == breakpoints[0].address && context.Dr1 == breakpoints[1].address &&
            context.Dr2 == breakpoints[2].address && (context.Dr7 & 0x3F) == control)
            return;
        if (SuspendThread(thread) == static_cast<DWORD>(-1))
            return;
        context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        context.Dr0 = breakpoints[0].address;
        context.Dr1 = breakpoints[1].address;
        context.Dr2 = breakpoints[2].address;
        context.Dr7 = (context.Dr7 & ~0x0FFF003Full) | control;
        SetThreadContext(thread, &context);
        ResumeThread(thread);
    }

    void ArmAllThreads()
    {
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return;
        THREADENTRY32 entry{sizeof(entry)};
        for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry))
        {
            if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == GetCurrentThreadId())
                continue;
            if (const HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, entry.th32ThreadID))
            {
                ArmThread(thread);
                CloseHandle(thread);
            }
        }
        CloseHandle(snapshot);
    }

    uintptr_t ResolveUiSingleton()
    {
        auto sites = FindAll(Memory::PatternToBytes("E8 ?? ?? ?? ?? 80 B8 E1 01 00 00 00 0F"), true);
        uintptr_t storage = 0;
        for (const auto site : sites)
        {
            uintptr_t getter = RelTarget(site + 1);
            if (!InImage(getter, 16))
                continue;
            if (*reinterpret_cast<const uint8_t*>(getter) == 0xE9)
                getter = RelTarget(getter + 1);
            uintptr_t callee = 0;
            for (size_t i = 0; i < 0x40 && InImage(getter + i, 5); ++i)
                if (*reinterpret_cast<const uint8_t*>(getter + i) == 0xE8)
                {
                    callee = RelTarget(getter + i + 1);
                    break;
                }
            if (!callee || !InImage(callee, 0x100))
                continue;
            uintptr_t found = 0;
            const auto* bytes = reinterpret_cast<const uint8_t*>(callee);
            for (size_t i = 0; i + 12 <= 0x100; ++i)
                if (bytes[i] == 0x48 && bytes[i + 1] == 0x8D && bytes[i + 2] == 0x05 &&
                    std::memcmp(bytes + i + 7, "\x48\x83\xC4\x38\xC3", 5) == 0)
                {
                    found = RelTarget(callee + i + 3);
                    break;
                }
            if (!found || !InImage(found, 8))
                continue;
            if (storage && storage != found)
            {
                logFile->error("UI settings: conflicting candidates");
                return 0;
            }
            storage = found;
        }
        if (storage)
            logFile->info("UI settings at game+0x{:X}", storage - imageBase);
        else
            logFile->error("UI settings not found ({} sites)", sites.size());
        return storage;
    }

    float* FindFov()
    {
        std::vector<int> signature;
        const float values[] = {-80.0f, 55.0f, -9.8f};
        for (const float value : values)
        {
            uint8_t raw[4];
            std::memcpy(raw, &value, 4);
            for (const auto b : raw)
                signature.push_back(b);
        }
        auto found = FindAll(signature, false);
        std::vector<uintptr_t> valid;
        for (const auto match : found)
        {
            const auto address = match + fovOffset;
            float value = 0;
            std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
            if (InImage(address, 4) && value > 10.0f && value < 120.0f)
                valid.push_back(address);
        }
        if (valid.size() != 1)
            return nullptr;
        logFile->info("Camera FOV at game+0x{:X}", valid[0] - imageBase);
        return reinterpret_cast<float*>(valid[0]);
    }

    float Aspect()
    {
        const auto width = renderWidth.load();
        const auto height = renderHeight.load();
        if (!width || !height)
            return 0.0f;
        return static_cast<float>(width) / static_cast<float>(height);
    }

    float TargetFov(float base, float aspect)
    {
        constexpr float degToRad = 3.14159265358979f / 180.0f;
        float tanHalf = std::tan(base * 0.5f * degToRad);
        if (aspect > 0.0f && aspect < referenceAspect)
            tanHalf *= referenceAspect / aspect;
        const float vertical = 2.0f * std::atan(tanHalf) / degToRad;
        return (std::min)(vertical * fovMultiplier, 150.0f);
    }

    DWORD WINAPI Worker(void*)
    {
        float baseFov = 0.0f;
        float writtenFov = 0.0f;
        uint32_t lastWidth = 0;
        uint32_t lastHeight = 0;
        for (int tick = 0;; ++tick)
        {
            ArmAllThreads();
            if (tick < 6000)
            {
                Sleep(20);
                if (tick % 12)
                    continue;
            }
            const float aspect = Aspect();
            const bool narrow = aspect > 0.0f && aspect < referenceAspect - 0.01f;
            if (uiSingletonStorage && narrow)
            {
                auto* settings = *reinterpret_cast<uint8_t* volatile*>(uiSingletonStorage);
                if (settings && settings[uiFlagOffset] != 1)
                    settings[uiFlagOffset] = 1;
            }
            if (!fovValue && tick % 20 == 0)
                fovValue = FindFov();
            if (fovValue && aspect > 0.0f)
            {
                const float current = *fovValue;
                if (current != writtenFov && current > 10.0f && current < 120.0f)
                    baseFov = current;
                if (baseFov > 0.0f)
                {
                    const float target = TargetFov(baseFov, aspect);
                    if (current != target)
                        *fovValue = target;
                    if (writtenFov != target)
                        logFile->info("FOV {:.2f} -> {:.2f} (aspect {:.3f})", baseFov, target, aspect);
                    writtenFov = target;
                }
            }
            if (renderWidth.load() != lastWidth || renderHeight.load() != lastHeight)
            {
                lastWidth = renderWidth.load();
                lastHeight = renderHeight.load();
                logFile->info("Render size {}x{}", lastWidth, lastHeight);
            }

            if (tick >= 6000)
                Sleep(250);
        }
    }

    bool Setup()
    {
        wchar_t modulePath[32768]{};
        const auto length = GetModuleFileNameW(ownModule, modulePath, static_cast<DWORD>(std::size(modulePath)));
        if (!length || length >= std::size(modulePath))
            return false;
        const auto folder = std::filesystem::path(modulePath).parent_path();
        try
        {
            logFile = spdlog::rotating_logger_mt(fixName, (folder / L"FFXV_ARFix.log").string(), 1024 * 1024, 1, true);
            logFile->flush_on(spdlog::level::info);
        }
        catch (const spdlog::spdlog_ex& error)
        {
            Notify(error.what());
            return false;
        }
        char value[64]{};
        const auto valueLength = GetPrivateProfileStringA("FOV", "Multiplier", "1.0", value,
                                                          static_cast<DWORD>(std::size(value)),
                                                          (folder / L"FFXV_ARFix.ini").string().c_str());
        const auto parsed = std::from_chars(value, value + valueLength, fovMultiplier);
        if (parsed.ec != std::errc{} || !(fovMultiplier >= 0.5f && fovMultiplier <= 2.0f))
        {
            logFile->warn("FOV Multiplier must be 0.5-2.0; using 1.0");
            fovMultiplier = 1.0f;
        }
        logFile->info("{} {}.{}.{}: FOV Multiplier={:.3f}", fixName, VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH,
                      fovMultiplier);

        imageBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        LoadSections();
        bool ok = AddFit("Startup fit", {"8D 04 C9 C1 E8 04 39 D0 44 0F 42 C0", "8D 04 C9 C1 E8 04 3B C2 44 0F 42 C0"}, 8, 4,
                         RCX, RDX);
        ok = AddFit("Settings fit", {"43 8D 1C FF C1 EB 04 44 3B E3 41 0F 46 DC"}, 10, 4, R15, R12, true) && ok;
        ok = AddFit("Renderer fit", {"8D 04 F6 C1 E8 04 39 D8 0F 42 D8", "8D 04 F6 C1 E8 04 3B C3 0F 42 D8"}, 8, 3, RSI,
                    RBX) && ok;
        if (!ok)
        {
            Notify("FFXV_ARFix: unsupported game version. See FFXV_ARFix.log.");
            breakpointCount = 0;
            return false;
        }
        uiSingletonStorage = ResolveUiSingleton();
        if (!AddVectoredExceptionHandler(1, Handler))
        {
            logFile->error("Exception handler failed");
            return false;
        }
        ArmAllThreads();
        logFile->info("Breakpoints armed");
        return true;
    }

    DWORD WINAPI Start(void*)
    {
        wchar_t executable[32768]{};
        const auto length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        if (!length || _wcsicmp(std::filesystem::path(executable).filename().c_str(), L"ffxv_s.exe") != 0)
            return 0;
        if (Setup())
            Worker(nullptr);
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        ownModule = module;
        DisableThreadLibraryCalls(module);
        const HANDLE thread = CreateThread(nullptr, 0, Start, nullptr, 0, nullptr);
        if (!thread)
            return FALSE;
        CloseHandle(thread);
    }
    return TRUE;
}
