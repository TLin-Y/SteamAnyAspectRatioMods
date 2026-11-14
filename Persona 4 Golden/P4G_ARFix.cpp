#include "stdafx.h"
#include "version.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <inipp/inipp.h>
#include <safetyhook.hpp>
#include <windows.h>
#include <algorithm>
#include <array>
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

    int iWidth = 1280;
    int iHeight = 800;

    float* pWinWidth = nullptr;
    float* pWinHeight = nullptr;
    float* pViewport = nullptr;
    int* pViewportMode = nullptr;
    uint8_t** pCamera = nullptr;

    bool fullIsMain = false;
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
        spdlog::info("{}: {:08X}", name, reinterpret_cast<uintptr_t>(hits[0]));
        return hits[0];
    }

    template <typename T> T ReadAbs(uint8_t* at)
    {
        return reinterpret_cast<T>(static_cast<uintptr_t>(*reinterpret_cast<uint32_t*>(at)));
    }

    float Factor()
    {
        if (!pWinWidth || !pWinHeight) return 1.0f;
        float w = *pWinWidth, h = *pWinHeight;
        if (w <= 0.0f || h <= 0.0f) return 1.0f;
        return (h * 960.0f) / (w * 540.0f);
    }

    void Write(uint8_t* at, const void* data, size_t size)
    {
        DWORD old;
        VirtualProtect(at, size, PAGE_EXECUTE_READWRITE, &old);
        memcpy(at, data, size);
        VirtualProtect(at, size, old, &old);
        FlushInstructionCache(GetCurrentProcess(), at, size);
    }

    bool Mid(uint8_t* at, safetyhook::MidHookFn fn)
    {
        auto h = safetyhook::create_mid(at, fn);
        if (!h) {
            spdlog::error("Hook failed at {:08X}", reinterpret_cast<uintptr_t>(at));
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
        if (iWidth <= 0 || iHeight <= 0) {
            iWidth = GetSystemMetrics(SM_CXSCREEN);
            iHeight = GetSystemMetrics(SM_CYSCREEN);
        }
        spdlog::info("Resolution: {}x{}", iWidth, iHeight);
    }

    void ResolutionList()
    {
        auto at = ScanOne("Resolution table", "66 0F 6E 14 F5 ? ? ? ? 66 0F 6E 0C F5 ? ? ? ? 0F 5B D2 0F 5B C9");
        if (!at) return;
        auto table = ReadAbs<int*>(at + 5);
        std::vector<std::pair<int, int>> modes;
        for (int i = 0; i < 10; ++i) modes.emplace_back(table[i * 2], table[i * 2 + 1]);
        for (const auto& m : modes) {
            if (m.first <= 0 || m.second <= 0 || m.first > 16384 || m.second > 16384) {
                spdlog::error("Resolution table: unexpected entry {}x{}", m.first, m.second);
                return;
            }
        }
        if (std::find(modes.begin(), modes.end(), std::make_pair(iWidth, iHeight)) == modes.end()) {
            auto same = std::find_if(modes.begin(), modes.end(), [](const auto& m) { return m.first == iWidth; });
            if (same != modes.end()) *same = { iWidth, iHeight };
            else modes.back() = { iWidth, iHeight };
            std::sort(modes.begin(), modes.end());
        }
        DWORD old;
        VirtualProtect(table, sizeof(int) * 20, PAGE_READWRITE, &old);
        for (int i = 0; i < 10; ++i) {
            table[i * 2] = modes[i].first;
            table[i * 2 + 1] = modes[i].second;
        }
        VirtualProtect(table, sizeof(int) * 20, old, &old);
        spdlog::info("Resolution table: {}x{} added", iWidth, iHeight);
    }

    void ScaleViewport(safetyhook::Context&)
    {
        int mode = *pViewportMode;
        if (mode != 1 && mode != 2) {
            fullIsMain = false;
            return;
        }
        float f = Factor();
        pViewport[1] *= f;
        pViewport[3] *= f;
        float w = *pWinWidth, h = *pWinHeight;
        fullIsMain = pViewport[0] == 0.0f && pViewport[1] == 0.0f && pViewport[2] >= 960.0f && h > 0.0f &&
            pViewport[3] > 0.0f && std::fabs((pViewport[2] / pViewport[3]) / (w / h) - 1.0f) < 0.02f;
    }

    void ScaleHeight(safetyhook::Context& c)
    {
        c.xmm0.f32[0] *= Factor();
    }

    void ScaleSurfaceHeight(safetyhook::Context& c)
    {
        if (c.xmm0.f32[0] != 1.0f) c.xmm0.f32[0] *= Factor();
    }

    void Binder(safetyhook::Context& c)
    {
        auto uniforms = reinterpret_cast<uint8_t*>(c.ecx);
        current2D = *reinterpret_cast<uint32_t*>(uniforms + 8) != 0;
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

    void Draw(safetyhook::Context& c)
    {
        if (!fullIsMain || !current2D) return;
        if ((c.edi & 0x1800000) == 0) return;
        float f = Factor();
        if (std::fabs(f - 1.0f) < 0.001f) return;
        auto args = reinterpret_cast<uint32_t*>(c.ebp + 8);
        uint32_t count = args[1];
        auto src = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(args[4]));
        constexpr size_t stride = 0x18;
        if (!src || count == 0 || count > 65536 || IsBadReadPtr(src, count * stride)) return;

        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        float minU = 1e9f, minV = 1e9f, maxU = -1e9f, maxV = -1e9f;
        for (uint32_t i = 0; i < count; ++i) {
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

        bool beyond = minX < -1.0f || minY < -1.0f || maxX > 961.0f || maxY > 545.0f;
        bool covers = minX <= 1.0f && minY <= 1.0f && maxX >= 959.0f && maxY >= 539.0f;
        bool solid = minU == maxU && minV == maxV;
        if (covers && (beyond || solid)) return;
        float spanU = maxU - minU, spanV = maxV - minV;
        bool artwork = covers && std::isfinite(spanU) && std::isfinite(spanV) && spanU >= 64.0f && spanV >= 64.0f &&
            spanU <= 8192.0f && spanV <= 8192.0f;
        bool spanWidth = !artwork && minX <= 1.0f && maxX >= 959.0f;
        bool spanHeight = !artwork && minY <= 1.0f && maxY >= 539.0f;

        auto key = reinterpret_cast<uintptr_t>(src) ^ (static_cast<uintptr_t>(count) << 1);
        auto found = adjusted.find(key);
        if (found != adjusted.end() && found->second == Hash(src, count * stride)) return;
        uint8_t* dst = const_cast<uint8_t*>(src);
        if (IsBadWritePtr(dst, count * stride)) return;
        for (uint32_t i = 0; i < count; ++i) {
            auto v = reinterpret_cast<float*>(dst + i * stride);
            if (f > 1.0f) {
                if (spanWidth && (v[4] <= 0.5f || v[4] >= 539.5f)) continue;
                v[4] = 270.0f + (v[4] - 270.0f) / f;
            } else {
                if (spanHeight && (v[3] <= 0.5f || v[3] >= 959.5f)) continue;
                v[3] = 480.0f + (v[3] - 480.0f) * f;
            }
        }
        if (adjusted.size() > 8192) adjusted.clear();
        adjusted[key] = Hash(dst, count * stride);
    }

    void Camera(safetyhook::Context& c, safetyhook::Xmm& horizontal)
    {
        if (!pCamera || !*pCamera) return;
        float f = Factor();
        if (f > 1.0f) *reinterpret_cast<float*>(*pCamera + 0x6c) = c.xmm0.f32[0] * f;
        else if (f < 1.0f) horizontal.f32[0] /= f;
    }

    void Camera1(safetyhook::Context& c) { Camera(c, c.xmm1); }
    void Camera2(safetyhook::Context& c) { Camera(c, c.xmm2); }

    bool RenderScale()
    {
        auto win = ScanOne("Window size", "C7 05 ? ? ? ? 00 00 A0 44 C7 05 ? ? ? ? 00 00 34 44 E8");
        auto vpGlobal = ScanOne("Viewport", "68 ? ? ? ? F3 0F 11 05 ? ? ? ? F3 0F 11 0D ? ? ? ? F3 0F 11 15 ? ? ? ? F3 0F 11 1D ? ? ? ? 8B 01 6A 01 51 FF 90 B0 00 00 00");
        auto mode = ScanOne("Viewport mode", "8B 0D ? ? ? ? F3 0F 10 25 ? ? ? ? 85 C9 74 ? E8 ? ? ? ? 0F 28 E0 A1 ? ? ? ?");
        auto vp1 = ScanOne("Viewport set 1", "F3 0F 11 0D ? ? ? ? F3 0F 11 25 ? ? ? ? 8B 01 FF 90 B0 00 00 00");
        auto vp2 = ScanOne("Viewport set 2", "F3 0F 11 1D ? ? ? ? 8B 01 6A 01 51 FF 90 B0 00 00 00");
        auto vp3 = ScanOne("Viewport set 3", "F3 0F 11 15 ? ? ? ? 8B 01 FF 90 B0 00 00 00");
        auto fill1 = ScanOne("Composite 1", "F3 0F 59 C7 0F 2F C1 0F 28 D0 76 ? 0F 28 D9 0F 28 C1 F3 0F 5E DF");
        auto fill2 = ScanOne("Composite 2", "F3 0F 59 CD 0F 2F CA 76 ? 0F 28 DA 0F 28 CA F3 0F 5E DD F3 0F 59 D8");
        auto targets = Scan("8B D8 66 0F 6E C1 0F 5B C0 F3 0F 59 45 FC E8 ? ? ? ?");
        const std::array<const char*, 5> surfacePatterns = {
            "8B 45 CC 89 B8 0C 01 00 00 66 0F 6E CE 0F 5B C9 F3 0F 59 C1 F3 0F 2C F0",
            "8B 45 14 89 86 0C 01 00 00 66 0F 6E CF 0F 5B C9 F3 0F 59 C1 F3 0F 2C F8",
            "66 0F 6E 4D 08 0F 5B C9 F3 0F 59 C1 F3 0F 2C D8 EB ? 8B 5D 08",
            "89 BE 0C 01 00 00 66 0F 6E 4D 0C 0F 5B C9 F3 0F 59 C1 F3 0F 2C C0",
            "66 0F 6E CF 0F 5B C9 89 9E 0C 01 00 00 F3 0F 59 C1 F3 0F 2C F8",
        };
        std::vector<uint8_t*> surfaces;
        for (size_t i = 0; i < surfacePatterns.size(); ++i) {
            auto at = ScanOne("Surface size " + std::to_string(i + 1), surfacePatterns[i]);
            if (at) surfaces.push_back(at);
        }
        if (targets.size() != 2) spdlog::error("Render target size: expected 2 matches, found {}", targets.size());
        if (!win || !vpGlobal || !mode || !vp1 || !vp2 || !vp3 || !fill1 || !fill2 || targets.size() != 2 ||
            surfaces.size() != surfacePatterns.size()) {
            spdlog::error("Render scaling disabled: a required pattern is missing");
            return false;
        }

        pWinWidth = ReadAbs<float*>(win + 2);
        pWinHeight = ReadAbs<float*>(win + 12);
        pViewport = ReadAbs<float*>(vpGlobal + 1);
        pViewportMode = ReadAbs<int*>(mode + 2);

        bool ok = true;
        for (auto t : targets) ok &= Mid(t + 14, ScaleHeight);
        for (auto s : surfaces) ok &= Mid(s, ScaleSurfaceHeight);
        ok &= Mid(vp1 + 16, ScaleViewport);
        ok &= Mid(vp2 + 8, ScaleViewport);
        ok &= Mid(vp3 + 8, ScaleViewport);
        if (!ok) {
            hooks.clear();
            spdlog::error("Render scaling disabled: hook installation failed");
            return false;
        }

        const uint8_t fillA[] = { 0x0F, 0x28, 0xC1, 0x90 };
        const uint8_t fillB[] = { 0x0F, 0x28, 0xCA, 0x90 };
        Write(fill1, fillA, sizeof(fillA));
        Write(fill2, fillB, sizeof(fillB));
        spdlog::info("Render scaling: enabled");
        return true;
    }

    void Interface()
    {
        auto binder = ScanOne("2D binder", "8B 31 85 F6 0F 84 ? ? ? ? F3 0F 2C 05 ? ? ? ? 8B 5E 1C C7 45 C0 00 00 80 BF");
        auto draw = ScanOne("2D draw", "FF 75 0C F7 C7 00 00 80 00 74 ? 51 FF 75 18 E8 ? ? ? ? EB ? F7 C7 00 00 00 01");
        if (!binder || !draw) {
            spdlog::error("2D layout disabled: a required pattern is missing");
            return;
        }
        if (Mid(binder, Binder) && Mid(draw, Draw)) spdlog::info("2D layout: enabled");
    }

    void CameraFov()
    {
        auto consts = Scan("F3 0F 11 46 6C 0F 28 C8 F3 0F 59 0D ? ? ? ? F3 0F 10 05 ? ? ? ? F3 0F 11 4E 68");
        auto globals = Scan("8B 35 ? ? ? ? F3 0F 5E 05 ? ? ? ? F3 0F 59 05 ? ? ? ? 0F 5A C0 E8");
        if (consts.empty() || globals.empty()) {
            spdlog::error("Camera: pattern missing");
            return;
        }
        uint32_t aspectConst = *reinterpret_cast<uint32_t*>(consts[0] + 12);
        uint32_t camera = *reinterpret_cast<uint32_t*>(globals[0] + 2);
        for (auto c : consts) {
            if (*reinterpret_cast<uint32_t*>(c + 12) != aspectConst) { spdlog::error("Camera: inconsistent aspect constant"); return; }
        }
        for (auto g : globals) {
            if (*reinterpret_cast<uint32_t*>(g + 2) != camera) { spdlog::error("Camera: inconsistent camera global"); return; }
        }
        float aspect = *reinterpret_cast<float*>(static_cast<uintptr_t>(aspectConst));
        if (aspect < 1.77f || aspect > 1.78f) {
            spdlog::error("Camera: unexpected aspect constant {}", aspect);
            return;
        }
        pCamera = reinterpret_cast<uint8_t**>(static_cast<uintptr_t>(camera));

        int count = 0;
        for (const auto& s : CodeSections()) {
            for (size_t j = 0; j + 8 <= s.size; ++j) {
                uint8_t* p = s.start + j;
                if (p[0] != 0xF3 || p[1] != 0x0F || p[2] != 0x59 || (p[3] & 0xC7) != 0x05) continue;
                if (*reinterpret_cast<uint32_t*>(p + 4) != aspectConst) continue;
                int reg = (p[3] >> 3) & 7;
                if (reg != 1 && reg != 2) {
                    spdlog::warn("Camera: site {:08X} uses xmm{}, skipped", reinterpret_cast<uintptr_t>(p), reg);
                    continue;
                }
                if (Mid(p, reg == 1 ? Camera1 : Camera2)) ++count;
            }
        }
        spdlog::info("Camera: {} sites", count);
    }

    void Install()
    {
        Logging();
        Configuration();
        ResolutionList();
        if (RenderScale()) {
            Interface();
            CameraFov();
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
