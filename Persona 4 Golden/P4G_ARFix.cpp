#include "stdafx.h"
#include "version.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <inipp/inipp.h>
#include <safetyhook.hpp>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    HMODULE exeModule = GetModuleHandleW(nullptr);
    HMODULE thisModule = nullptr;
    std::filesystem::path fixPath;

    int iWidth = 0;
    int iHeight = 0;

    float* pWinWidth = nullptr;
    float* pWinHeight = nullptr;
    float* pLayout = nullptr;
    uint8_t** pModeLists = nullptr;
    uint8_t** pMonitors = nullptr;

    bool current2D = false;
    std::unordered_map<uintptr_t, uint64_t> adjusted;
    std::vector<SafetyHookMid> hooks;

    struct Section
    {
        uint8_t* start;
        size_t size;
    };

    std::vector<Section> CodeSections()
    {
        std::vector<Section> out;
        auto base = reinterpret_cast<uint8_t*>(exeModule);
        auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        auto sec = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
            if (sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)
                out.push_back({ base + sec->VirtualAddress, sec->Misc.VirtualSize });
        }
        return out;
    }

    std::vector<uint8_t*> Scan(const char* pattern)
    {
        std::vector<int> bytes;
        for (const char* p = pattern; *p;) {
            if (*p == ' ') { ++p; continue; }
            if (*p == '?') { bytes.push_back(-1); while (*p == '?') ++p; continue; }
            char* end = nullptr;
            bytes.push_back(static_cast<int>(strtoul(p, &end, 16)));
            p = end;
        }
        std::vector<uint8_t*> hits;
        for (const auto& s : CodeSections()) {
            if (s.size < bytes.size()) continue;
            for (size_t i = 0; i + bytes.size() <= s.size; ++i) {
                bool ok = true;
                for (size_t k = 0; k < bytes.size(); ++k) {
                    if (bytes[k] >= 0 && s.start[i + k] != bytes[k]) { ok = false; break; }
                }
                if (ok) hits.push_back(s.start + i);
            }
        }
        return hits;
    }

    uint8_t* ScanOne(const std::string& name, const char* pattern)
    {
        auto hits = Scan(pattern);
        if (hits.size() != 1) {
            spdlog::error("{}: expected 1 match, found {}", name, hits.size());
            return nullptr;
        }
        spdlog::info("{}: {:X}", name, reinterpret_cast<uintptr_t>(hits[0]));
        return hits[0];
    }

    template <typename T> T Rip(uint8_t* instruction, size_t dispOffset, size_t length)
    {
        auto disp = *reinterpret_cast<int32_t*>(instruction + dispOffset);
        return reinterpret_cast<T>(instruction + length + disp);
    }

    template <typename T> T RipSame(const char* name, const char* pattern, size_t dispOffset, size_t length)
    {
        auto hits = Scan(pattern);
        if (hits.empty()) {
            spdlog::error("{}: not found", name);
            return nullptr;
        }
        auto first = Rip<T>(hits[0], dispOffset, length);
        for (auto h : hits) {
            if (Rip<T>(h, dispOffset, length) != first) {
                spdlog::error("{}: inconsistent matches", name);
                return nullptr;
            }
        }
        spdlog::info("{}: {:X}", name, reinterpret_cast<uintptr_t>(first));
        return first;
    }

    float Factor()
    {
        if (!pWinWidth || !pWinHeight) return 1.0f;
        float w = *pWinWidth, h = *pWinHeight;
        if (w <= 0.0f || h <= 0.0f) return 1.0f;
        return (h * 960.0f) / (w * 540.0f);
    }

    bool Mid(uint8_t* at, safetyhook::MidHookFn fn)
    {
        auto h = safetyhook::create_mid(at, fn);
        if (!h) {
            spdlog::error("Hook failed at {:X}", reinterpret_cast<uintptr_t>(at));
            return false;
        }
        hooks.emplace_back(std::move(h));
        return true;
    }

    void Logging()
    {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(thisModule, path, MAX_PATH);
        fixPath = std::filesystem::path(path).remove_filename();
        try {
            auto logger = spdlog::basic_logger_mt("P4G_ARFix", (fixPath / "P4G_ARFix.log").string(), true);
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
            spdlog::flush_on(spdlog::level::info);
            spdlog::info("P4G_ARFix {}.{}.{}", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
        } catch (...) {}
    }

    void Configuration()
    {
        inipp::Ini<char> ini;
        std::ifstream file(fixPath / "P4G_ARFix.ini");
        if (file) {
            ini.parse(file);
            ini.strip_trailing_comments();
            inipp::get_value(ini.sections["Resolution"], "Width", iWidth);
            inipp::get_value(ini.sections["Resolution"], "Height", iHeight);
        }
        if (iWidth > 0 && iHeight > 0) spdlog::info("Resolution: {}x{}", iWidth, iHeight);
        else spdlog::info("Resolution: game setting");
    }

    struct Mode
    {
        int32_t width;
        int32_t height;
        int32_t refreshNumerator;
        int32_t refreshDenominator;
    };

    struct ModeList
    {
        Mode* begin;
        Mode* end;
        Mode* capacity;
    };

    void AddMode(ModeList& list)
    {
        if (!list.begin || list.end < list.begin) return;
        size_t count = list.end - list.begin;
        if (count == 0) return;
        for (size_t i = 0; i < count; ++i) {
            if (list.begin[i].width == iWidth && list.begin[i].height == iHeight) return;
        }
        if (list.end >= list.capacity) {
            size_t newCapacity = count + 8;
            auto fresh = static_cast<Mode*>(HeapAlloc(GetProcessHeap(), 0, newCapacity * sizeof(Mode)));
            if (!fresh) return;
            memcpy(fresh, list.begin, count * sizeof(Mode));
            list.begin = fresh;
            list.end = fresh + count;
            list.capacity = fresh + newCapacity;
        }
        Mode mode{ iWidth, iHeight, list.begin[count - 1].refreshNumerator, list.begin[count - 1].refreshDenominator };
        size_t at = count;
        while (at > 0 && (list.begin[at - 1].width > iWidth ||
            (list.begin[at - 1].width == iWidth && list.begin[at - 1].height > iHeight))) --at;
        memmove(list.begin + at + 1, list.begin + at, (count - at) * sizeof(Mode));
        list.begin[at] = mode;
        ++list.end;
    }

    void AddModes()
    {
        if (!pModeLists || !pMonitors || !*pModeLists) return;
        auto monitors = reinterpret_cast<uint8_t**>(pMonitors);
        if (!monitors[0] || monitors[1] < monitors[0]) return;
        size_t count = (monitors[1] - monitors[0]) / sizeof(void*);
        auto lists = reinterpret_cast<ModeList*>(*pModeLists);
        for (size_t i = 0; i < count && i < 16; ++i) AddMode(lists[i]);
    }

    void WindowSize(safetyhook::Context& c)
    {
        if (iWidth <= 0 || iHeight <= 0) return;
        AddModes();
        c.xmm7.f32[0] = static_cast<float>(iWidth);
        c.xmm6.f32[0] = static_cast<float>(iHeight);
    }

    void Composite(safetyhook::Context& c)
    {
        auto stack = reinterpret_cast<float*>(c.rsp);
        c.xmm1.f32[0] = 0.0f;
        c.xmm2.f32[0] = 0.0f;
        c.xmm3.f32[0] = stack[0x30 / 4];
        stack[0x28 / 4] = stack[0x38 / 4];
    }

    void Camera(safetyhook::Context& c)
    {
        float f = Factor();
        if (f > 1.0f) c.xmm0.f32[0] *= f;
        else if (f < 1.0f) c.xmm2.f32[0] /= f;
    }

    void Binder(safetyhook::Context& c)
    {
        auto program = reinterpret_cast<uint8_t*>(c.rbx);
        auto uniforms = *reinterpret_cast<uint8_t**>(program + 8);
        current2D = uniforms && *reinterpret_cast<void**>(uniforms + 8) != nullptr;
    }

    uint64_t Hash(const uint8_t* data, size_t size)
    {
        uint64_t h = 1469598103934665603ull;
        for (size_t i = 0; i < size; ++i) {
            h ^= data[i];
            h *= 1099511628211ull;
        }
        return h;
    }

    bool ScreenSampled(const uint8_t* src, uint64_t count, float minX, float maxX, float spanU, bool covers)
    {
        if (!(maxX - minX > 1.0f) || !std::isfinite(spanU) || spanU <= 0.0f) return false;
        float k = spanU / (maxX - minX);
        if (covers && k < 1.01f) return false;
        for (uint64_t i = 0; i < count; ++i) {
            auto v = reinterpret_cast<const float*>(src + i * 0x18);
            if (std::fabs(v[0] - v[3] * k) > k || std::fabs(v[1] - v[4] * k) > k) return false;
        }
        return true;
    }

    void Draw(safetyhook::Context& c)
    {
        if (!current2D) return;
        float f = Factor();
        if (std::fabs(f - 1.0f) < 0.001f) return;
        uint64_t count = c.r14;
        auto src = *reinterpret_cast<const uint8_t**>(c.rsp + 0x70);
        constexpr size_t stride = 0x18;
        if (!src || count == 0 || count > 65536 || IsBadReadPtr(src, count * stride)) return;

        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        float minU = 1e9f, minV = 1e9f, maxU = -1e9f, maxV = -1e9f;
        for (uint64_t i = 0; i < count; ++i) {
            auto v = reinterpret_cast<const float*>(src + i * stride);
            minU = (std::min)(minU, v[0]);
            maxU = (std::max)(maxU, v[0]);
            minV = (std::min)(minV, v[1]);
            maxV = (std::max)(maxV, v[1]);
            minX = (std::min)(minX, v[3]);
            maxX = (std::max)(maxX, v[3]);
            minY = (std::min)(minY, v[4]);
            maxY = (std::max)(maxY, v[4]);
        }
        if (!std::isfinite(minX) || !std::isfinite(maxX) || !std::isfinite(minY) || !std::isfinite(maxY)) return;

        float w = pLayout[0], h = pLayout[1];
        bool beyond = minX < -1.0f || minY < -1.0f || maxX > w + 1.0f || maxY > h + 5.0f;
        bool covers = minX <= 1.0f && minY <= 1.0f && maxX >= w - 1.0f && maxY >= h - 1.0f;
        bool solid = minU == maxU && minV == maxV;
        if (covers && (beyond || solid)) return;
        float spanU = maxU - minU, spanV = maxV - minV;
        if (ScreenSampled(src, count, minX, maxX, spanU, covers)) return;
        bool artwork = covers && std::isfinite(spanU) && std::isfinite(spanV) && spanU >= 64.0f && spanV >= 64.0f &&
            spanU <= 8192.0f && spanV <= 8192.0f;
        bool spanWidth = !artwork && minX <= 1.0f && maxX >= w - 1.0f;
        bool spanHeight = !artwork && minY <= 1.0f && maxY >= h - 1.0f;

        auto key = reinterpret_cast<uintptr_t>(src) ^ (static_cast<uintptr_t>(count) << 1);
        auto found = adjusted.find(key);
        if (found != adjusted.end() && found->second == Hash(src, count * stride)) return;
        auto dst = const_cast<uint8_t*>(src);
        if (IsBadWritePtr(dst, count * stride)) return;
        float cx = w * 0.5f, cy = h * 0.5f;
        for (uint64_t i = 0; i < count; ++i) {
            auto v = reinterpret_cast<float*>(dst + i * stride);
            if (f > 1.0f) {
                if (spanWidth && (v[4] <= 0.5f || v[4] >= h - 0.5f)) continue;
                v[4] = cy + (v[4] - cy) / f;
            } else {
                if (spanHeight && (v[3] <= 0.5f || v[3] >= w - 0.5f)) continue;
                v[3] = cx + (v[3] - cx) * f;
            }
        }
        if (adjusted.size() > 8192) adjusted.clear();
        adjusted[key] = Hash(dst, count * stride);
    }

    void Install()
    {
        Logging();
        Configuration();
        auto store = ScanOne("Window size store", "F3 0F 11 3D ? ? ? ? F3 0F 11 35 ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? F3 48 0F 2C DF");
        pModeLists = RipSame<uint8_t**>("Display modes", "48 8B 05 ? ? ? ? 48 8B 54 C8 08 48 2B 14 C8 48 C1 FA 04", 3, 7);
        pMonitors = RipSame<uint8_t**>("Monitors", "4C 8B 05 ? ? ? ? 4C 2B 05 ? ? ? ? 49 C1 F8 03", 10, 14);
        auto win = ScanOne("Window size", "F3 0F 10 05 ? ? ? ? F3 0F 11 05 ? ? ? ? F3 0F 10 0D ? ? ? ? F3 0F 11 0D ? ? ? ? E8 ? ? ? ? 90 4C 8B 05");
        auto composite = ScanOne("Composite", "4C 8B DC 55 49 8D AB F8 FE FF FF 48 81 EC 00 02 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 90 00 00 00 49 89 5B 08 49 89 73 10 48 8B F1");
        auto camera = ScanOne("Camera", "F3 0F 10 1D ? ? ? ? 0F 28 D0 F3 0F 59 15 ? ? ? ? 0F 28 CB 48 8B 4B 08");
        auto binder = ScanOne("2D binder", "49 8B F9 49 8B D8 E8 ? ? ? ? 48 8B 43 08 0F 57 F6 F3 0F 10 3D ? ? ? ?");
        auto draw = ScanOne("2D draw", "4C 8B 44 24 70 49 8B C6 4C 8B 74 24 48 41 B9 18 00 00 00 48 89 44 24 20 0F BA E7 17");
        auto layout = ScanOne("2D layout", "F3 0F 2C 05 ? ? ? ? 0F 5B E4 66 0F 6E D8 0F 5B DB");
        if (!win || !composite) {
            spdlog::error("Disabled: a required pattern is missing");
            return;
        }
        pWinWidth = Rip<float*>(win + 8, 4, 8);
        pWinHeight = Rip<float*>(win + 24, 4, 8);

        if (store && Mid(store, WindowSize)) spdlog::info("Window size: enabled");
        if (Mid(composite, Composite)) spdlog::info("Composite: fills the window");
        if (camera && Mid(camera + 11, Camera)) spdlog::info("Camera: enabled");
        if (binder && draw && layout) {
            pLayout = Rip<float*>(layout, 4, 8);
            if (Mid(binder + 11, Binder) && Mid(draw, Draw)) spdlog::info("2D layout: enabled");
        } else {
            spdlog::error("2D layout disabled: a required pattern is missing");
        }
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        thisModule = module;
        DisableThreadLibraryCalls(module);
        Install();
    }
    return TRUE;
}
