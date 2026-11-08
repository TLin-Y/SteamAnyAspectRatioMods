#pragma once

#include "stdafx.h"
#include "helper.hpp"
#include <safetyhook.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <memory>
#include <mutex>
#include <utility>

namespace ARFix
{
    static_assert(sizeof(void*) == 8, "Build for Windows/x64.");
    inline HMODULE ownModule = nullptr;
    inline uintptr_t imageBase = 0;
    inline size_t imageSize = 0;
    inline std::shared_ptr<spdlog::logger> logger;
    inline std::array<SafetyHookMid, 9> hooks;
    inline std::array<std::atomic<bool>, 9> errors{};
    inline bool cameraCorrection = true;
    inline bool removeLetterbox = true;
    inline bool verboseDiagnostics = false;
    inline constexpr float baselineAspect = 16.0f / 9.0f;
    inline constexpr wchar_t configName[] = L"SonicShadow_ARFix.ini";
    inline constexpr char defaultConfig[] =
        "; Restart both games after editing. Use true/false or 1/0; no inline comments.\r\n"
        "; Both fixes enabled gives correct proportions; mixed modes are for comparison.\r\n"
        "[Shadow]\r\nCameraCorrection = true\r\nRemoveLetterbox = true\r\n\r\n"
        "[Generations]\r\nCameraCorrection = true\r\nRemoveLetterbox = true\r\n\r\n"
        "[Diagnostics]\r\nVerboseDiagnostics = false\r\n";

    class MutexLock
    {
        HANDLE mutex_;
    public:
        explicit MutexLock(HANDLE mutex) : mutex_(mutex)
        {
            const auto result = WaitForSingleObject(mutex_, INFINITE);
            if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED)
                throw spdlog::spdlog_ex("Shared file lock failed", GetLastError());
        }
        ~MutexLock() { ReleaseMutex(mutex_); }
        MutexLock(const MutexLock&) = delete;
        MutexLock& operator=(const MutexLock&) = delete;
    };

    class SharedLogSink final : public spdlog::sinks::base_sink<std::mutex>
    {
        HANDLE file_ = INVALID_HANDLE_VALUE;
        HANDLE mutex_ = nullptr;
    public:
        explicit SharedLogSink(const std::filesystem::path& path)
        {
            uint64_t hash = 14695981039346656037ULL;
            for (const auto ch : path.wstring())
                hash = (hash ^ static_cast<uint64_t>(std::towlower(ch))) * 1099511628211ULL;
            const auto name = L"Local\\SonicShadow_ARFix_" + std::to_wstring(hash);
            mutex_ = CreateMutexW(nullptr, FALSE, name.c_str());
            if (!mutex_)
                throw spdlog::spdlog_ex("Cannot create shared file mutex", GetLastError());
            file_ = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file_ == INVALID_HANDLE_VALUE)
            {
                const auto error = GetLastError();
                CloseHandle(mutex_);
                mutex_ = nullptr;
                throw spdlog::spdlog_ex("Cannot open shared log", error);
            }
        }
        ~SharedLogSink() override
        {
            if (file_ != INVALID_HANDLE_VALUE)
                CloseHandle(file_);
            if (mutex_)
                CloseHandle(mutex_);
        }
        HANDLE fileMutex() const { return mutex_; }
    protected:
        void sink_it_(const spdlog::details::log_msg& message) override
        {
            spdlog::memory_buf_t text;
            formatter_->format(message, text);
            MutexLock lock(mutex_);
            DWORD written = 0;
            if (text.size() > MAXDWORD ||
                !WriteFile(file_, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) ||
                written != text.size())
                throw spdlog::spdlog_ex("Shared log append failed", GetLastError());
        }
        void flush_() override {}
    };

    inline void Notify(const char* message)
    {
        OutputDebugStringA(message);
        MessageBoxA(nullptr, message, "SonicShadow_ARFix", MB_ICONERROR);
    }

    inline std::mutex sampleMutex;
    struct Sample
    {
        uintptr_t caller = 0;
        unsigned index = 0;
        std::array<float, 6> shape{};
        unsigned count = 0;
        ULONGLONG last = 0;
    };
    inline std::array<std::array<Sample, 128>, 9> samples{};
    inline std::array<bool, 9> sampleLimitLogged{};

    inline bool ParseBoolean(std::wstring text, bool& value)
    {
        const auto first = text.find_first_not_of(L" \t\r\n");
        if (first == std::wstring::npos)
            return false;
        text = text.substr(first, text.find_last_not_of(L" \t\r\n") - first + 1);
        if (_wcsicmp(text.c_str(), L"true") == 0 || text == L"1")
            value = true;
        else if (_wcsicmp(text.c_str(), L"false") == 0 || text == L"0")
            value = false;
        else
            return false;
        return true;
    }

    inline bool ReadBoolean(const std::filesystem::path& path, const char* section,
                     const char* key, bool& value)
    {
        wchar_t text[64]{};
        const std::wstring wideSection(section, section + std::strlen(section));
        const std::wstring wideKey(key, key + std::strlen(key));
        const DWORD length = GetPrivateProfileStringW(wideSection.c_str(), wideKey.c_str(), L"<missing>", text,
                                                     static_cast<DWORD>(std::size(text)), path.c_str());
        if (std::wcscmp(text, L"<missing>") == 0)
        {
            logger->warn("INI [{}] {} missing; using default {}.",
                         section, key, value);
            return true;
        }
        if (length == std::size(text) - 1 || !ParseBoolean(text, value))
        {
            logger->error("Invalid INI [{}] {}: expected true/false or 1/0. No hooks installed.",
                          section, key);
            return false;
        }
        return true;
    }

    inline bool LoadConfiguration(const std::filesystem::path& folder, const char* section)
    {
        const auto path = folder / configName;
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            const bool saved = WriteFile(file, defaultConfig, sizeof(defaultConfig) - 1, &written, nullptr) &&
                               written == sizeof(defaultConfig) - 1;
            const DWORD writeError = saved ? ERROR_SUCCESS : GetLastError();
            const bool closed = CloseHandle(file) != FALSE;
            if (!saved || !closed)
            {
                logger->error("Cannot create complete {}: write Win32={}, close Win32={}. No hooks installed.",
                              path.string(), writeError, closed ? ERROR_SUCCESS : GetLastError());
                return false;
            }
            logger->info("Created default INI: {}", path.string());
        }
        else if (GetLastError() != ERROR_FILE_EXISTS)
        {
            logger->error("Cannot create/open {}: Win32={}. No hooks installed.", path.string(), GetLastError());
            return false;
        }
        const HANDLE readable = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (readable == INVALID_HANDLE_VALUE)
        {
            logger->error("Cannot read {}: Win32={}. No hooks installed.", path.string(), GetLastError());
            return false;
        }
        if (!CloseHandle(readable))
        {
            logger->error("INI handle close failed: Win32={}. No hooks installed.", GetLastError());
            return false;
        }
        wchar_t legacy[16]{};
        GetPrivateProfileStringW(L"Fixes", L"CameraCorrection", L"", legacy,
                                 static_cast<DWORD>(std::size(legacy)), path.c_str());
        if (*legacy)
        {
            logger->error("Legacy [Fixes] INI detected; replace with the shared [Shadow]/[Generations] sample. "
                          "Old settings are not migrated; no hooks installed.");
            return false;
        }
        if (!ReadBoolean(path, section, "CameraCorrection", cameraCorrection) ||
            !ReadBoolean(path, section, "RemoveLetterbox", removeLetterbox) ||
            !ReadBoolean(path, "Diagnostics", "VerboseDiagnostics", verboseDiagnostics))
            return false;
        if (cameraCorrection != removeLetterbox)
            logger->warn("Mixed rendering switches selected: testing mode may distort proportions. "
                         "Both enabled is recommended; both disabled retains original framing.");
        return true;
    }

    inline uint64_t PackSize(uint32_t width, uint32_t height)
    {
        return (static_cast<uint64_t>(width) << 32) | height;
    }

    inline bool CorrectProjection(float fov, float aspect, uint64_t size, float& correctedFov,
                           float& correctedAspect)
    {
        const auto width = static_cast<uint32_t>(size >> 32);
        const auto height = static_cast<uint32_t>(size);
        if (!width || !height || width > 16384 || height > 16384 ||
            !std::isfinite(fov) || fov <= 0 || fov >= std::numbers::pi_v<float> ||
            !std::isfinite(aspect) || std::abs(aspect - baselineAspect) > 0.005f)
            return false;
        correctedAspect = static_cast<float>(width) / height;
        const float scale = (std::max)(1.0f, baselineAspect / correctedAspect);
        correctedFov = scale == 1.0f ? fov : 2.0f * std::atan(std::tan(fov * 0.5f) * scale);
        return std::isfinite(correctedFov) && correctedFov > 0 &&
               correctedFov < std::numbers::pi_v<float>;
    }

    template<typename T>
    inline bool Read(uintptr_t address, T& value)
    {
        SIZE_T count = 0;
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                                 &value, sizeof(value), &count) && count == sizeof(value);
    }

    inline void ReadFailure(unsigned kind)
    {
        if (!errors[kind].exchange(true))
            logger->error("Hook {} input read failed; correction suspended. Win32={}",
                          kind, GetLastError());
    }

    inline bool ShouldLog(unsigned kind, uintptr_t caller, unsigned index = 0,
                   const std::array<float, 6>& shape = {})
    {
        std::lock_guard lock(sampleMutex);
        Sample* available = nullptr;
        for (auto& sample : samples[kind])
        {
            if (sample.count == 0)
            {
                if (!available)
                    available = &sample;
                continue;
            }
            // Temporary stack objects must not consume a stream for every frame.
            if (sample.caller == caller && sample.index == index && sample.shape == shape)
            {
                const auto now = GetTickCount64();
                if (sample.count >= 4 && now - sample.last < 5000)
                    return false;
                ++sample.count;
                sample.last = now;
                return true;
            }
        }
        if (!available)
        {
            if (!sampleLimitLogged[kind])
            {
                sampleLimitLogged[kind] = true;
                logger->warn("Probe {} reached 128 caller/shape streams; new streams for this probe omitted.", kind);
            }
            return false;
        }
        *available = { caller, index, shape, 1, GetTickCount64() };
        return true;
    }

    inline std::string Caller(uintptr_t address)
    {
        if (address >= imageBase && address - imageBase < imageSize)
            return fmt::format("game+0x{:X}", address - imageBase);
        return fmt::format("non-game@0x{:X}", address);
    }

    inline bool Matches(uintptr_t address, const std::vector<int>& pattern)
    {
        if (address < imageBase || address - imageBase > imageSize ||
            pattern.size() > imageSize - (address - imageBase))
            return false;
        std::vector<uint8_t> bytes(pattern.size());
        SIZE_T read = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address),
                               bytes.data(), bytes.size(), &read) || read != bytes.size())
            return false;
        for (size_t index = 0; index < pattern.size(); ++index)
            if (pattern[index] >= 0 && bytes[index] != pattern[index])
                return false;
        return true;
    }

    inline uintptr_t FindUnique(const char* name, const char* signature)
    {
        const auto pattern = Memory::PatternToBytes(signature);
        if (pattern.empty() || pattern.front() < 0)
        {
            logger->error("{} scanner requires a nonempty signature with a fixed first byte.", name);
            return 0;
        }
        constexpr size_t chunkSize = 1024 * 1024;
        std::vector<uint8_t> bytes(chunkSize + pattern.size() - 1);
        uintptr_t match = 0;
        const auto end = imageBase + imageSize;
        for (auto cursor = imageBase; cursor < end;)
        {
            MEMORY_BASIC_INFORMATION memory{};
            if (!VirtualQuery(reinterpret_cast<void*>(cursor), &memory, sizeof(memory)))
            {
                logger->error("{} scan VirtualQuery failed: Win32={}.", name, GetLastError());
                return 0;
            }
            const auto regionEnd = (std::min)(end,
                reinterpret_cast<uintptr_t>(memory.BaseAddress) + memory.RegionSize);
            if (regionEnd <= cursor)
            {
                logger->error("{} scan encountered a non-advancing memory region.", name);
                return 0;
            }
            const DWORD protection = memory.Protect & 0xFF;
            const bool readable = protection == PAGE_READONLY || protection == PAGE_READWRITE ||
                protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
                protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
            if (memory.State == MEM_COMMIT && readable && !(memory.Protect & PAGE_GUARD) &&
                reinterpret_cast<uintptr_t>(memory.AllocationBase) == imageBase)
            {
                // Some game code lives in readable non-executable sections; do not filter by section flags.
                while (cursor < regionEnd)
                {
                    const auto size = (std::min)(bytes.size(), static_cast<size_t>(regionEnd - cursor));
                    SIZE_T read = 0;
                    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(cursor),
                                           bytes.data(), size, &read) || read != size)
                    {
                        logger->error("{} scan read failed: Win32={}.", name, GetLastError());
                        return 0;
                    }
                    const auto candidates = size >= pattern.size() ? size - pattern.size() + 1 : 0;
                    for (size_t index = 0; index < candidates; ++index)
                    {
                        if (bytes[index] != pattern.front())
                            continue;
                        size_t offset = 1;
                        while (offset < pattern.size() &&
                               (pattern[offset] < 0 || bytes[index + offset] == pattern[offset]))
                            ++offset;
                        if (offset != pattern.size())
                            continue;
                        if (match)
                        {
                            logger->error("{} signature is ambiguous: {} and {}; fixes disabled.",
                                          name, Caller(match), Caller(cursor + index));
                            return 0;
                        }
                        match = cursor + index;
                    }
                    cursor += (std::min)(chunkSize, static_cast<size_t>(regionEnd - cursor));
                }
            }
            else
                cursor = regionEnd;
        }
        if (!match)
            logger->error("{} signature not found; fixes disabled.", name);
        else
            logger->debug("SCAN {} uniquely matched {}.", name, Caller(match));
        return match;
    }

    inline uintptr_t CallTarget(uintptr_t call)
    {
        uint8_t opcode = 0;
        int32_t displacement = 0;
        if (!Read(call, opcode) || opcode != 0xE8 || !Read(call + 1, displacement))
            return 0;
        const auto target = call + 5 + displacement;
        return target >= imageBase && target - imageBase < imageSize ? target : 0;
    }

    inline bool FloatOperand(uintptr_t displacementAddress, float expected)
    {
        int32_t displacement = 0;
        float value = 0;
        if (imageSize < sizeof(float) || !Read(displacementAddress, displacement))
            return false;
        const auto target = displacementAddress + 4 + displacement;
        return target >= imageBase && target - imageBase <= imageSize - sizeof(float) &&
               Read(target, value) && value == expected;
    }

    inline bool Install(unsigned kind, const char* name, void* target, safetyhook::MidHookFn callback)
    {
        auto hook = SafetyHookMid::create(target, callback, SafetyHookMid::StartDisabled);
        if (!hook)
        {
            logger->error("{} hook creation failed: type={}", name, static_cast<unsigned>(hook.error().type));
            return false;
        }
        hooks[kind] = std::move(*hook);
        auto enabled = hooks[kind].enable();
        if (!enabled)
        {
            logger->error("{} hook enable failed: type={}", name, static_cast<unsigned>(enabled.error().type));
            hooks[kind].reset();
            return false;
        }
        logger->debug("{} hook installed at 0x{:X}", name, reinterpret_cast<uintptr_t>(target));
        return true;
    }

    inline bool Initialize(const wchar_t* executableName, const char* section)
    {
        wchar_t executable[32768]{};
        const auto exeLength = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        if (!exeLength || exeLength == std::size(executable))
        {
            Notify("Cannot identify game executable; no hooks installed.");
            return false;
        }
        if (_wcsicmp(std::filesystem::path(executable).filename().c_str(), executableName) != 0)
            return false;
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                               reinterpret_cast<LPCWSTR>(ownModule), &pinned))
        {
            Notify("Cannot pin fix module; no hooks installed.");
            return false;
        }
        wchar_t path[32768]{};
        const auto length = GetModuleFileNameW(ownModule, path, static_cast<DWORD>(std::size(path)));
        if (!length || length == std::size(path))
        {
            Notify("Cannot resolve shared INI/log folder; no hooks installed.");
            return false;
        }
        const auto folder = std::filesystem::path(path).parent_path();
        try
        {
            const auto sink = std::make_shared<SharedLogSink>(folder / L"SonicShadow_ARFix.log");
            logger = std::make_shared<spdlog::logger>(section, sink);
            logger->set_pattern("[%Y-%m-%d %H:%M:%S] [pid %P] [%n] [%l] %v");
            logger->set_error_handler([](const std::string& error)
            {
                static std::atomic<bool> reported{false};
                OutputDebugStringA(error.c_str());
                if (!reported.exchange(true))
                    Notify("Shared log write failed. Check disk space/folder permissions.");
            });
            bool loaded = false;
            {
                // The two ASIs can start simultaneously in separate launcher/game processes.
                MutexLock lock(sink->fileMutex());
                loaded = LoadConfiguration(folder, section);
            }
            if (!loaded)
            {
                Notify("Cannot load SonicShadow_ARFix.ini; no hooks installed. See SonicShadow_ARFix.log.");
                return false;
            }
        }
        catch (const spdlog::spdlog_ex& error)
        {
            Notify(error.what());
            return false;
        }
        logger->set_level(verboseDiagnostics ? spdlog::level::debug : spdlog::level::info);
        imageBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(imageBase);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(imageBase + dos->e_lfanew);
        imageSize = nt->OptionalHeader.SizeOfImage;
        logger->info("Validated-signature fix starting; CameraCorrection={} RemoveLetterbox={} VerboseDiagnostics={}.",
                     cameraCorrection, removeLetterbox, verboseDiagnostics);
        return true;
    }

    inline BOOL Attach(HMODULE module, DWORD reason, LPTHREAD_START_ROUTINE setup)
    {
        if (reason == DLL_PROCESS_ATTACH)
        {
            ownModule = module;
            DisableThreadLibraryCalls(module);
            const HANDLE thread = CreateThread(nullptr, 0, setup, nullptr, 0, nullptr);
            if (!thread)
            {
                OutputDebugStringW(L"SonicShadow_ARFix: setup thread creation failed.\n");
                return FALSE;
            }
            CloseHandle(thread);
        }
        return TRUE;
    }
}
