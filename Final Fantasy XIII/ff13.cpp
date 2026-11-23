#include "stdafx.h"
#include "helper.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <inipp/inipp.h>
#include <safetyhook.hpp>
#include <windows.h>
#include <cmath>
#include <string>
#include <filesystem>
#include <fstream>
#include <atomic>

HMODULE exeModule = GetModuleHandle(NULL);
HMODULE thisModule;
std::shared_ptr<spdlog::logger> logger;
std::filesystem::path ExePath;

int iReplaceWidth = 1280;
int iReplaceHeight = 720;
int iTargetWidth = 1280;
int iTargetHeight = 800;
float fTargetARValue = 1.3333333f;

// Video state tracking via Bink2
std::atomic<int> g_VideoPlayCount{0};

static safetyhook::InlineHook Hook_BinkOpen;
static safetyhook::InlineHook Hook_BinkClose;

void* __stdcall Hooked_BinkOpen(const char* name, uint32_t flags) {
    g_VideoPlayCount++;
    SPDLOG_INFO("Bink Video Opened! Active videos: {}", g_VideoPlayCount.load());
    return Hook_BinkOpen.call<void*>(name, flags);
}

void __stdcall Hooked_BinkClose(void* bink) {
    g_VideoPlayCount--;
    if (g_VideoPlayCount < 0) g_VideoPlayCount = 0;
    SPDLOG_INFO("Bink Video Closed! Active videos: {}", g_VideoPlayCount.load());
    Hook_BinkClose.call<void>(bink);
}

void InitVideoDecoderHooks() {
    HMODULE hBink = GetModuleHandleA("bink2w32.dll");
    if (!hBink) {
        SPDLOG_INFO("bink2w32.dll not loaded yet. Forcing load via LoadLibraryA...");
        hBink = LoadLibraryA("bink2w32.dll");
    }

    if (hBink) {
        void* pBinkOpen = (void*)GetProcAddress(hBink, "BinkOpen");
        if (!pBinkOpen) pBinkOpen = (void*)GetProcAddress(hBink, "_BinkOpen@8");

        void* pBinkClose = (void*)GetProcAddress(hBink, "BinkClose");
        if (!pBinkClose) pBinkClose = (void*)GetProcAddress(hBink, "_BinkClose@4");

        if (pBinkOpen && pBinkClose) {
            Hook_BinkOpen = safetyhook::create_inline(pBinkOpen, reinterpret_cast<void*>(Hooked_BinkOpen));
            Hook_BinkClose = safetyhook::create_inline(pBinkClose, reinterpret_cast<void*>(Hooked_BinkClose));
            SPDLOG_INFO("Universal Bink 2 Video Hooks initialized successfully.");
        } else {
            SPDLOG_ERROR("Failed to find BinkOpen/BinkClose exports in bink2w32.dll!");
        }
    } else {
        SPDLOG_ERROR("CRITICAL: bink2w32.dll could not be loaded at all!");
    }
}

void Logging() {
    WCHAR gameExePath[_MAX_PATH] = {0};
    GetModuleFileNameW(exeModule, gameExePath, MAX_PATH);
    ExePath = gameExePath;
    ExePath = ExePath.remove_filename();

    try {
        std::ofstream file(ExePath / "FF13_ARFix.log", std::ios::trunc);
        if (file.is_open()) file.close();

        logger = std::make_shared<spdlog::logger>("FF13_Fix", std::make_shared<spdlog::sinks::rotating_file_sink_st>((ExePath / "FF13_ARFix.log").string(), 10 * 1024 * 1024, 1));
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
        spdlog::flush_on(spdlog::level::info);

        SPDLOG_INFO("Final Fantasy XIII Ultimate ASI Fix Initialized");
    } catch (...) {}
}

int iBorderless = 1;

void Configuration() {
    std::ifstream iniFile(ExePath / "FF13_ARFix.ini");
    if (iniFile) {
        inipp::Ini<char> ini;
        ini.parse(iniFile);
        inipp::get_value(ini.sections["Resolution"], "ReplaceWidth", iReplaceWidth);
        inipp::get_value(ini.sections["Resolution"], "ReplaceHeight", iReplaceHeight);
        inipp::get_value(ini.sections["Resolution"], "TargetWidth", iTargetWidth);
        inipp::get_value(ini.sections["Resolution"], "TargetHeight", iTargetHeight);
        inipp::get_value(ini.sections["Window"], "Borderless", iBorderless);
    }

    if (iTargetHeight > 0) {
        fTargetARValue = static_cast<float>(iTargetWidth) / static_cast<float>(iTargetHeight);
    }
    SPDLOG_INFO("Config Loaded -> Target AR: {:.5f}", fTargetARValue);
}

void IntResolutionMemoryPatch() {
    std::uint8_t* moduleBase = reinterpret_cast<std::uint8_t*>(exeModule);
    PIMAGE_DOS_HEADER dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(moduleBase);
    PIMAGE_NT_HEADERS ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(moduleBase + dosHeader->e_lfanew);
    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(ntHeaders);

    for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++, section++) {
        if (section->Characteristics & (IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_CNT_UNINITIALIZED_DATA)) {
            std::uint8_t* sectionBase = moduleBase + section->VirtualAddress;
            DWORD sectionSize = section->Misc.VirtualSize;
            for (DWORD j = 0; j < sectionSize - 8; j += 4) {
                int* pRes = reinterpret_cast<int*>(sectionBase + j);
                if (pRes[0] == iReplaceWidth && pRes[1] == iReplaceHeight) {
                    DWORD oldProtect;
                    if (VirtualProtect(pRes, 8, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                        pRes[0] = iTargetWidth;
                        pRes[1] = iTargetHeight;
                        VirtualProtect(pRes, 8, oldProtect, &oldProtect);
                    }
                }
            }
        }
    }
}

void FloatResolutionMemoryPatch() {
    std::uint8_t* moduleBase = reinterpret_cast<std::uint8_t*>(exeModule);
    PIMAGE_DOS_HEADER dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(moduleBase);
    PIMAGE_NT_HEADERS ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(moduleBase + dosHeader->e_lfanew);
    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(ntHeaders);

    float fReplaceWidth = static_cast<float>(iReplaceWidth);
    float fReplaceHeight = static_cast<float>(iReplaceHeight);
    float fTargetWidth = static_cast<float>(iTargetWidth);
    float fTargetHeight = static_cast<float>(iTargetHeight);

    float fReplaceHalfWidth = fReplaceWidth / 2.0f;   // 1280 / 2 = 640.0f
    float fReplaceHalfHeight = fReplaceHeight / 2.0f; // 720 / 2 = 360.0f
    float fTargetHalfWidth = fTargetWidth / 2.0f;
    float fTargetHalfHeight = fTargetHeight / 2.0f;

    int patchCountW = 0, patchCountH = 0;
    int patchCountHalfW = 0, patchCountHalfH = 0;

    for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++, section++) {
        if (section->Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA) {
            std::uint8_t* sectionBase = moduleBase + section->VirtualAddress;
            DWORD sectionSize = section->Misc.VirtualSize;

            for (DWORD j = 0; j < sectionSize - sizeof(float); j += 4) {
                float* pFloat = reinterpret_cast<float*>(sectionBase + j);

                if (std::abs(*pFloat - fReplaceWidth) < 0.001f) {
                    DWORD oldProtect;
                    if (VirtualProtect(pFloat, sizeof(float), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                        *pFloat = fTargetWidth;
                        VirtualProtect(pFloat, sizeof(float), oldProtect, &oldProtect);
                        patchCountW++;
                    }
                }
                else if (std::abs(*pFloat - fReplaceHeight) < 0.001f) {
                    DWORD oldProtect;
                    if (VirtualProtect(pFloat, sizeof(float), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                        *pFloat = fTargetHeight;
                        VirtualProtect(pFloat, sizeof(float), oldProtect, &oldProtect);
                        patchCountH++;
                    }
                }
                else if (std::abs(*pFloat - fReplaceHalfWidth) < 0.001f) {
                    DWORD oldProtect;
                    if (VirtualProtect(pFloat, sizeof(float), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                        *pFloat = fTargetHalfWidth;
                        VirtualProtect(pFloat, sizeof(float), oldProtect, &oldProtect);
                        patchCountHalfW++;
                    }
                }
                else if (std::abs(*pFloat - fReplaceHalfHeight) < 0.001f) {
                    DWORD oldProtect;
                    if (VirtualProtect(pFloat, sizeof(float), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                        *pFloat = fTargetHalfHeight;
                        VirtualProtect(pFloat, sizeof(float), oldProtect, &oldProtect);
                        patchCountHalfH++;
                    }
                }
            }
        }
    }
    SPDLOG_INFO("Float Patch: W({}), H({}), HalfW({}), HalfH({})", patchCountW, patchCountH, patchCountHalfW, patchCountHalfH);
}

void AspectRatioMemoryPatch() {
    std::uint8_t* moduleBase = reinterpret_cast<std::uint8_t*>(exeModule);
    PIMAGE_DOS_HEADER dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(moduleBase);
    PIMAGE_NT_HEADERS ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(moduleBase + dosHeader->e_lfanew);
    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(ntHeaders);

    float originalAR = 16.0f / 9.0f; // 1.7777778f

    for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++, section++) {
        if (section->Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA) {
            std::uint8_t* sectionBase = moduleBase + section->VirtualAddress;
            DWORD sectionSize = section->Misc.VirtualSize;

            for (DWORD j = 0; j < sectionSize - sizeof(float); j += 4) {
                float* pFloat = reinterpret_cast<float*>(sectionBase + j);

                // Only overwrite positive 16:9 AR constants
                if (std::abs(*pFloat - originalAR) < 0.000001f) {
                    DWORD oldProtect;
                    if (VirtualProtect(pFloat, sizeof(float), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                        *pFloat = fTargetARValue;
                        VirtualProtect(pFloat, sizeof(float), oldProtect, &oldProtect);
                    }
                }
            }
        }
    }
    SPDLOG_INFO("Static AR Patch complete.");
}

void CutsceneCameraARHook() {
    std::uint8_t* cutsceneScan = Memory::PatternScan(exeModule, "89 8D 14 FF FF FF 8B 95 14 FF FF FF F3 0F 10 42 34");
    if (cutsceneScan) {
        std::uint8_t* hookTarget = cutsceneScan + 12;
        static SafetyHookMid CutsceneARWriteHook{};

        CutsceneARWriteHook = safetyhook::create_mid(hookTarget, [](SafetyHookContext& ctx) {
            if (ctx.edx) {
                uintptr_t ptrVal = ctx.edx;
                if (ptrVal > 0x10000000 && ptrVal < 0x3FFFFFFF) {
                    float* pAR = reinterpret_cast<float*>(ptrVal + 0x34);
                    *pAR = 1.0f / fTargetARValue;
                }
            }
        });
        SPDLOG_INFO("Cutscene Camera AR Hook injected.");
    }
}

// Active Enforcement Viewport Hook (The definitive fix)
void CutsceneViewportARHook() {
    std::uint8_t* scan = Memory::PatternScan(exeModule, "89 55 A8 8B 45 A8 F3 0F 10 40 28");
    if (scan) {
        std::uint8_t* target = scan + 6;
        static SafetyHookMid viewportHook;

        viewportHook = safetyhook::create_mid(target, [](SafetyHookContext& ctx) {
            if (ctx.eax > 0x10000000 && ctx.eax < 0x3FFFFFFF) {
                if (g_VideoPlayCount.load() == 0) {
                    // 3D Rendering Mode: Apply custom ultrawide aspect ratio
                    *reinterpret_cast<float*>(ctx.eax + 0x28) = 1.0f / fTargetARValue;
                } else {
                    // Video Playback Mode: Actively FORCE native 16:9 aspect ratio (0.5625)
                    // This overrides any polluted values dynamically allocated by the engine
                    *reinterpret_cast<float*>(ctx.eax + 0x28) = 9.0f / 16.0f;
                }
            }
        });
        SPDLOG_INFO("Ultimate Viewport Hook injected with Active Enforcement Mode.");
    }
}

void FieldCameraARHook() {
    std::uint8_t* fieldScan = Memory::PatternScan(exeModule, "8B 8C 24 74 03 00 00 51 D9 81 FC 03 00 00 D9 1C 24");
    if (fieldScan) {
        std::uint8_t* hookTarget = fieldScan + 8;
        static SafetyHookMid FieldARWriteHook{};
        FieldARWriteHook = safetyhook::create_mid(hookTarget, [](SafetyHookContext& ctx) {
            if (ctx.ecx) {
                float* pAR = reinterpret_cast<float*>(ctx.ecx + 0x3FC);
                *pAR = fTargetARValue;
            }
        });
        SPDLOG_INFO("Field Camera AR Hook injected.");
    }
}

void BattleCameraARHook() {
    std::uint8_t* battleScan = Memory::PatternScan(exeModule, "8B 4D FC 8B 55 08 D9 42 1C D9 59 1C 8B 45 FC");
    if (battleScan) {
        std::uint8_t* hookTarget = battleScan + 6;
        static SafetyHookMid BattleARWriteHook{};
        BattleARWriteHook = safetyhook::create_mid(hookTarget, [](SafetyHookContext& ctx) {
            if (ctx.edx) {
                float* pAR = reinterpret_cast<float*>(ctx.edx + 0x1C);
                *pAR = fTargetARValue;
            }
        });
        SPDLOG_INFO("Battle Camera AR Hook injected.");
    }
}

bool bUltrawide = false;
int iUWWidth = 0;
int iUWHeight = 0;
int iUWUiWidth = 1280;
float fUWAspect = 16.0f / 9.0f;
float fUWInvAspect = 9.0f / 16.0f;
float fUWFovDivisor = 2.0f;
float fUWUiWidth = 1280.0f;
float fUWUiHalfWidth = 640.0f;
float fUWUiOffset = 0.0f;
float fUWMarkerScale = 1.0f;
float fUWQuadEdge = 1.0f;
const float fUWNative = 16.0f / 9.0f;
const float fUWFourThirds = 4.0f / 3.0f;
const float fUWHalfHeight = 360.0f;

std::uint8_t* pTextStart = nullptr;
std::uint8_t* pTextEnd = nullptr;
std::uint8_t* pCaveCursor = nullptr;
std::uint8_t* pCaveEnd = nullptr;

void FindTextSection() {
    std::uint8_t* base = reinterpret_cast<std::uint8_t*>(exeModule);
    auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + reinterpret_cast<PIMAGE_DOS_HEADER>(base)->e_lfanew);
    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, section++) {
        if (memcmp(section->Name, ".text", 5) == 0) {
            pTextStart = base + section->VirtualAddress;
            pTextEnd = pTextStart + section->Misc.VirtualSize;
            return;
        }
    }
}

std::vector<std::uint8_t*> ScanText(const char* signature, std::uint8_t* from = nullptr, std::uint8_t* to = nullptr) {
    std::vector<std::uint8_t*> results;
    std::vector<int> bytes = Memory::PatternToBytes(signature);
    std::uint8_t* start = from ? from : pTextStart;
    std::uint8_t* end = (to ? to : pTextEnd) - bytes.size();
    for (std::uint8_t* p = start; p <= end; ++p) {
        if (p[0] != bytes[0]) continue;
        size_t j = 1;
        while (j < bytes.size() && (bytes[j] == -1 || p[j] == bytes[j])) ++j;
        if (j == bytes.size()) results.push_back(p);
    }
    return results;
}

std::uint8_t* FindUnique(const char* name, const char* signature, ptrdiff_t offset) {
    auto results = ScanText(signature);
    if (results.size() != 1) {
        SPDLOG_ERROR("Ultrawide: {} has {} matches (expected 1).", name, results.size());
        return nullptr;
    }
    SPDLOG_INFO("Ultrawide: {} at exe+{:x}", name, (results[0] + offset) - reinterpret_cast<std::uint8_t*>(exeModule));
    return results[0] + offset;
}

bool PointsToFloat(std::uint8_t* operand, float expected) {
    float* target = *reinterpret_cast<float**>(operand);
    std::uint8_t* base = reinterpret_cast<std::uint8_t*>(exeModule);
    auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + reinterpret_cast<PIMAGE_DOS_HEADER>(base)->e_lfanew);
    std::uint8_t* t = reinterpret_cast<std::uint8_t*>(target);
    if (t < base || t + sizeof(float) > base + nt->OptionalHeader.SizeOfImage) return false;
    return std::abs(*target - expected) < 0.0001f;
}

void WriteCode(std::uint8_t* at, const std::vector<std::uint8_t>& code) {
    DWORD oldProtect;
    VirtualProtect(at, code.size(), PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy(at, code.data(), code.size());
    VirtualProtect(at, code.size(), oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), at, code.size());
}

void AppendAddress(std::vector<std::uint8_t>& code, const void* address) {
    std::uint32_t value = static_cast<std::uint32_t>(reinterpret_cast<uintptr_t>(address));
    for (int i = 0; i < 4; i++) code.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}

void AppendRel32(std::vector<std::uint8_t>& code, std::uint8_t* codeBase, std::uint8_t* target) {
    std::int32_t rel = static_cast<std::int32_t>(target - (codeBase + code.size() + 4));
    for (int i = 0; i < 4; i++) code.push_back(static_cast<std::uint8_t>(static_cast<std::uint32_t>(rel) >> (i * 8)));
}

void DivertToCave(std::uint8_t* site, size_t length, std::vector<std::uint8_t> caveCode) {
    if (!pCaveCursor) {
        pCaveCursor = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        pCaveEnd = pCaveCursor + 0x1000;
    }
    std::uint8_t* cave = pCaveCursor;
    caveCode.push_back(0xE9);
    AppendRel32(caveCode, cave, site + length);
    memcpy(cave, caveCode.data(), caveCode.size());
    pCaveCursor += (caveCode.size() + 15) & ~size_t(15);
    FlushInstructionCache(GetCurrentProcess(), cave, caveCode.size());

    std::vector<std::uint8_t> jump = { 0xE9 };
    AppendRel32(jump, site, cave);
    while (jump.size() < length) jump.push_back(0x90);
    WriteCode(site, jump);
}

void UltrawidePatches() {
    FindTextSection();
    if (!pTextStart) {
        SPDLOG_ERROR("Ultrawide: .text section not found.");
        return;
    }

    std::uint8_t* setRes = nullptr;
    for (int i = 0; i < 200 && !setRes; i++) {
        auto r = ScanText("8B 44 24 04 8B 4C 24 08 A3 ?? ?? ?? ?? 89 0D ?? ?? ?? ?? C3");
        if (r.size() == 1) setRes = r[0];
        else Sleep(50);
    }
    if (!setRes) {
        SPDLOG_ERROR("Ultrawide: resolution setter not found.");
        return;
    }

    std::uint8_t* renderDesc = FindUnique("RenderDesc", "C7 42 28 D0 02 00 00 8B 45 F4 F3 0F 10 05 ?? ?? ?? ?? F3 0F 11 40 2C 8B 4D F4 C7 41 30 00 04 00 00", 0x17);
    std::uint8_t* uiScaleW = FindUnique("UiScaleW", "51 8B 4D D4 E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? F3 0F 2A C0 F3 0F 59 45 E4", 9);
    std::uint8_t* uiScaleH = FindUnique("UiScaleH", "89 4D 8C E8 ?? ?? ?? ?? F3 0F 2A C0 F3 0F 59 45 E4", 3);
    std::uint8_t* camArArg = FindUnique("CamArArg", "8B 55 F8 F3 0F 10 42 70 F3 0F 11 45 FC 51 D9 45 FC D9 1C 24 8B 4D F4 E8", 3);
    std::uint8_t* ar169A = FindUnique("Ar169A", "8B 4D E0 F3 0F 10 05 ?? ?? ?? ?? F3 0F 11 41 1C 8B 4D E0", 7);
    std::uint8_t* ar169B = FindUnique("Ar169B", "F3 0F 10 42 40 F3 0F 59 05 ?? ?? ?? ?? F3 0F 59 05 ?? ?? ?? ?? F3 0F 11 45 E8", 17);
    std::uint8_t* ar169C = FindUnique("Ar169C", "F3 0F 11 86 2C 01 00 00 F3 0F 10 05 ?? ?? ?? ?? F3 0F 11 8E 30 01 00 00", 12);
    std::uint8_t* viewHalf = FindUnique("ViewHalfW", "0F 57 C0 F3 0F 10 0D ?? ?? ?? ?? F3 0F 10 15 ?? ?? ?? ?? F3 0F 11 89 8C 00 00 00", 7);
    std::uint8_t* viewW = FindUnique("ViewW", "8B 42 04 C7 81 BC 00 00 00 00 05 00 00 C7 81 C0 00 00 00 D0 02 00 00", 9);
    std::uint8_t* projDiv = FindUnique("ProjDiv", "D1 F8 89 84 24 88 00 00 00 F3 0F 10 45 10 F3 0F 5E 05 ?? ?? ?? ?? F3 0F 5A C0 E8", 9);
    std::uint8_t* markerScale = FindUnique("MarkerScale", "D9 E8 D9 1C 24 51 D9 84 24 94 02 00 00 D9 1C 24 51 D9 84 24 9C 02 00 00", 6);
    std::uint8_t* markerHalfH = FindUnique("MarkerHalfH", "D9 9C 24 0C 02 00 00 E8 ?? ?? ?? ?? D8 8C 24 0C 02 00 00 D8 8C 24 14 02 00 00", 12);
    std::uint8_t* markerX = FindUnique("MarkerX", "F3 0F 2A 82 20 1F 00 00 F3 0F 11 84 24 34 02 00 00 E8 ?? ?? ?? ?? D1 F8", 17);
    std::uint8_t* arGetter = FindUnique("ArGetter", "F3 0F 10 00 F3 0F 5E 41 04 F3 0F 11 45 FC D9 45 FC 8B E5 5D C3", 9);
    std::uint8_t* scissor = FindUnique("Scissor", "8B 54 24 1C 52 E8 ?? ?? ?? ?? 83 C4 10 EB 54", 5);
    std::uint8_t* movieQuad = FindUnique("MovieQuad", "D9 5D 98 F3 0F 10 05 ?? ?? ?? ?? F3 0F 5E 45 A4 F3 0F 11 45 94 F3 0F 10 05 ?? ?? ?? ?? F3 0F 5E 45 98 F3 0F 11 45 A0", 0x15);
    std::uint8_t* movieQuadR1 = FindUnique("MovieQuadR1", "F3 0F 5C 45 94 F3 0F 11 45 BC", 5);
    std::uint8_t* movieQuadR2 = FindUnique("MovieQuadR2", "F3 0F 5C 45 94 F3 0F 11 45 D0", 5);

    std::uint8_t* uiWidthRead = nullptr;
    std::uint8_t* uiOffsetX = nullptr;
    for (std::uint8_t* m : ScanText("A1 ?? ?? ?? ?? 89 45 C0 8B 4D C0 8B 51 20 89 55 BC 8B 45 BC 89 85 B0 FD FF FF")) {
        if (!ScanText("81 C1 50 07 00 00 E8", m, m + 0x80).empty()) {
            if (uiWidthRead) { uiWidthRead = nullptr; break; }
            uiWidthRead = m + 8;
        }
    }
    if (uiWidthRead) {
        auto offsets = ScanText("8B 4D 08 F3 0F 11 01 BA 01 00 00 00 C1 E2 02 8B 45 08 F3 0F 10 04 10 F3 0F 11 85 BC FD FF FF", uiWidthRead, uiWidthRead + 0x400);
        if (offsets.size() == 1) uiOffsetX = offsets[0] + 7;
    }
    if (!uiWidthRead || !uiOffsetX) SPDLOG_ERROR("Ultrawide: world-to-screen UI sites not found.");

    std::uint8_t* sites[] = { renderDesc, uiScaleW, uiScaleH, camArArg, ar169A, ar169B, ar169C, viewHalf, viewW, projDiv,
        markerScale, markerHalfH, markerX, arGetter, scissor, movieQuad, movieQuadR1, movieQuadR2, uiWidthRead, uiOffsetX };
    for (std::uint8_t* s : sites) {
        if (!s) {
            SPDLOG_ERROR("Ultrawide: a required site is missing; no ultrawide changes applied.");
            return;
        }
    }
    if (!PointsToFloat(ar169A, fUWNative) || !PointsToFloat(ar169B, fUWNative) || !PointsToFloat(ar169C, fUWNative) ||
        !PointsToFloat(viewHalf, 640.0f) || !PointsToFloat(viewHalf + 8, fUWNative) || !PointsToFloat(projDiv + 9, 2.0f) ||
        !PointsToFloat(movieQuad - 0x15 + 7, 1.0f) || movieQuadR1 - movieQuad > 0x200 || movieQuadR2 - movieQuad > 0x200 ||
        movieQuadR1 < movieQuad || movieQuadR2 < movieQuad) {
        SPDLOG_ERROR("Ultrawide: unexpected constants at the patch sites; no ultrawide changes applied.");
        return;
    }

    int* resW = *reinterpret_cast<int**>(setRes + 9);
    int* resH = *reinterpret_cast<int**>(setRes + 15);
    if (resH != resW + 1) {
        SPDLOG_ERROR("Ultrawide: resolution setter has an unexpected layout.");
        return;
    }
    *resW = iUWWidth;
    *resH = iUWHeight;
    static SafetyHookMid setResHook = safetyhook::create_mid(setRes + 8, [](SafetyHookContext& ctx) {
        ctx.eax = static_cast<uintptr_t>(iUWWidth);
        ctx.ecx = static_cast<uintptr_t>(iUWHeight);
    });

    static SafetyHookMid renderDescHook = safetyhook::create_mid(renderDesc, [](SafetyHookContext& ctx) {
        std::uint8_t* desc = reinterpret_cast<std::uint8_t*>(ctx.eax);
        *reinterpret_cast<int*>(desc + 0x18) = iUWWidth;
        *reinterpret_cast<int*>(desc + 0x1C) = iUWHeight;
        *reinterpret_cast<float*>(desc + 0x20) = fUWInvAspect;
        *reinterpret_cast<int*>(desc + 0x24) = iUWWidth;
        *reinterpret_cast<int*>(desc + 0x28) = iUWHeight;
        *reinterpret_cast<float*>(desc + 0x2C) = fUWInvAspect;
    });

    WriteCode(uiScaleW, { 0xB8, 0x00, 0x05, 0x00, 0x00 });
    WriteCode(uiScaleH, { 0xB8, 0xD0, 0x02, 0x00, 0x00 });

    std::vector<std::uint8_t> camArg = { 0x51, 0xD9, 0x05 };
    AppendAddress(camArg, &fUWAspect);
    while (camArg.size() < 14) camArg.push_back(0x90);
    WriteCode(camArArg, camArg);

    for (std::uint8_t* operand : { ar169A, ar169B, ar169C, viewHalf + 8 }) {
        std::vector<std::uint8_t> addr;
        AppendAddress(addr, &fUWAspect);
        WriteCode(operand, addr);
    }
    std::vector<std::uint8_t> halfAddr;
    AppendAddress(halfAddr, &fUWUiHalfWidth);
    WriteCode(viewHalf, halfAddr);

    std::vector<std::uint8_t> widthImm;
    AppendAddress(widthImm, reinterpret_cast<void*>(static_cast<uintptr_t>(iUWUiWidth)));
    WriteCode(viewW, widthImm);

    std::vector<std::uint8_t> divAddr;
    AppendAddress(divAddr, &fUWFovDivisor);
    WriteCode(projDiv + 9, divAddr);
    static SafetyHookMid projHook = safetyhook::create_mid(projDiv, [](SafetyHookContext& ctx) {
        *reinterpret_cast<int*>(ctx.ecx + 0x20) = 1280;
        *reinterpret_cast<int*>(ctx.ecx + 0x24) = 720;
    });

    std::vector<std::uint8_t> scaleCave = { 0xD9, 0x84, 0x24, 0x94, 0x02, 0x00, 0x00, 0xD8, 0x0D };
    AppendAddress(scaleCave, &fUWFourThirds);
    DivertToCave(markerScale, 7, scaleCave);

    std::vector<std::uint8_t> halfHCave = { 0xDD, 0xD8, 0xD9, 0x05 };
    AppendAddress(halfHCave, &fUWHalfHeight);
    DivertToCave(markerHalfH, 7, halfHCave);

    static SafetyHookMid markerXHook = safetyhook::create_mid(markerX, [](SafetyHookContext& ctx) {
        float* x = reinterpret_cast<float*>(ctx.esp + 0x234);
        *x = 640.0f + (*x - 640.0f) * fUWMarkerScale;
    });

    std::vector<std::uint8_t> widthRead = { 0xD9, 0x05 };
    AppendAddress(widthRead, &fUWUiWidth);
    widthRead.insert(widthRead.end(), { 0xDB, 0x5D, 0xBC });
    WriteCode(uiWidthRead, widthRead);
    static SafetyHookMid offsetHook = safetyhook::create_mid(uiOffsetX, [](SafetyHookContext& ctx) {
        *reinterpret_cast<float*>(ctx.ecx) += fUWUiOffset;
    });

    std::vector<std::uint8_t> getter = { 0xD9, 0x05 };
    AppendAddress(getter, &fUWAspect);
    getter.insert(getter.end(), { 0x90, 0x90 });
    WriteCode(arGetter, getter);

    static SafetyHookMid scissorHook = safetyhook::create_mid(scissor, [](SafetyHookContext& ctx) {
        *reinterpret_cast<int*>(ctx.esp) = 1;
        *reinterpret_cast<int*>(ctx.esp + 8) = 0x2000;
    });

    WriteCode(movieQuadR1, { 0x90, 0x90, 0x90, 0x90, 0x90 });
    WriteCode(movieQuadR2, { 0x90, 0x90, 0x90, 0x90, 0x90 });
    static SafetyHookMid movieHook = safetyhook::create_mid(movieQuad, [](SafetyHookContext& ctx) {
        float targetWidth = *reinterpret_cast<float*>(ctx.ebp - 0x5C);
        float height = static_cast<float>(iUWHeight);
        *reinterpret_cast<float*>(ctx.ebp - 0x6C) = (height * fUWNative - height * fUWAspect) / targetWidth;
        *reinterpret_cast<float*>(ctx.ebp - 0x44) = fUWQuadEdge;
        *reinterpret_cast<float*>(ctx.ebp - 0x30) = fUWQuadEdge;
    });

    SPDLOG_INFO("Ultrawide patches applied: {}x{}, UI width {}.", iUWWidth, iUWHeight, iUWUiWidth);
}

void UltrawideResolutionPatch() {
    std::uint8_t* base = reinterpret_cast<std::uint8_t*>(exeModule);
    auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + reinterpret_cast<PIMAGE_DOS_HEADER>(base)->e_lfanew);
    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(nt);
    const float replaceInv = 0.5625f;
    int pairs = 0, ratios = 0;
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, section++) {
        if (!(section->Characteristics & (IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_CNT_UNINITIALIZED_DATA))) continue;
        int* start = reinterpret_cast<int*>(base + section->VirtualAddress);
        size_t count = section->Misc.VirtualSize / sizeof(int);
        for (size_t j = 0; j + 2 < count; j++) {
            if (start[j] != 1280 || start[j + 1] != 720) continue;
            DWORD oldProtect;
            if (!VirtualProtect(&start[j], 3 * sizeof(int), PAGE_READWRITE, &oldProtect)) continue;
            start[j] = iUWWidth;
            start[j + 1] = iUWHeight;
            pairs++;
            float* next = reinterpret_cast<float*>(&start[j + 2]);
            if (std::abs(*next - replaceInv) < 0.0001f) {
                *next = fUWInvAspect;
                ratios++;
            }
            VirtualProtect(&start[j], 3 * sizeof(int), oldProtect, &oldProtect);
        }
    }
    SPDLOG_INFO("Ultrawide resolution: {} pairs, {} ratios updated.", pairs, ratios);
}

HWND FindGameWindow() {
    struct Search { DWORD pid; HWND found; } search = { GetCurrentProcessId(), nullptr };
    EnumWindows([](HWND hwnd, LPARAM param) -> BOOL {
        auto* s = reinterpret_cast<Search*>(param);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != s->pid || !IsWindowVisible(hwnd)) return TRUE;
        char title[64] = {};
        GetWindowTextA(hwnd, title, sizeof(title));
        if (strcmp(title, "FINAL FANTASY XIII") != 0) return TRUE;
        s->found = hwnd;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

DWORD __stdcall BorderlessThread(void*) {
    HWND converted = nullptr;
    for (;;) {
        Sleep(500);
        HWND hwnd = FindGameWindow();
        if (!hwnd) continue;
        LONG style = GetWindowLongW(hwnd, GWL_STYLE);
        if (hwnd != converted && !(style & WS_CAPTION)) continue;
        if (IsIconic(hwnd)) continue;

        MONITORINFO mi = { sizeof(mi) };
        if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) continue;
        int monitorW = mi.rcMonitor.right - mi.rcMonitor.left;
        int monitorH = mi.rcMonitor.bottom - mi.rcMonitor.top;
        int w = iTargetWidth > 0 ? iTargetWidth : monitorW;
        int h = iTargetHeight > 0 ? iTargetHeight : monitorH;
        double scale = std::min({ 1.0, static_cast<double>(monitorW) / w, static_cast<double>(monitorH) / h });
        w = static_cast<int>(w * scale + 0.5);
        h = static_cast<int>(h * scale + 0.5);
        int x = mi.rcMonitor.left + (monitorW - w) / 2;
        int y = mi.rcMonitor.top + (monitorH - h) / 2;

        RECT rc;
        GetWindowRect(hwnd, &rc);
        bool styled = !(style & (WS_CAPTION | WS_THICKFRAME));
        if (styled && rc.left == x && rc.top == y && rc.right - rc.left == w && rc.bottom - rc.top == h) continue;

        SetWindowLongW(hwnd, GWL_STYLE, (style & ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) | WS_POPUP);
        LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
        SetWindowLongW(hwnd, GWL_EXSTYLE, exStyle & ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE));
        SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        if (hwnd != converted) SPDLOG_INFO("Borderless window: {}x{} at {},{}.", w, h, x, y);
        converted = hwnd;
    }
}

DWORD __stdcall Main(void*) {
    Logging();
    Configuration();

    if (iBorderless) {
        if (HANDLE h = CreateThread(NULL, 0, BorderlessThread, 0, 0, 0)) CloseHandle(h);
    }

    if (iTargetWidth > 0 && iTargetHeight > 0 && fTargetARValue > fUWNative + 0.01f) {
        bUltrawide = true;
        iUWWidth = iTargetWidth;
        iUWHeight = iTargetHeight;
        fUWAspect = fTargetARValue;
        fUWInvAspect = 1.0f / fTargetARValue;
        fUWFovDivisor = 2.0f * fTargetARValue / 1.8030614f;
        fUWUiWidth = 720.0f * fTargetARValue;
        fUWUiHalfWidth = fUWUiWidth * 0.5f;
        iUWUiWidth = static_cast<int>(fUWUiWidth);
        fUWUiOffset = (1280.0f - fUWUiWidth) * 0.5f;
        fUWMarkerScale = fUWNative / fTargetARValue;
        fUWQuadEdge = fUWNative / fTargetARValue;
        SPDLOG_INFO("Target is wider than 16:9; using the ultrawide path.");
        UltrawidePatches();
        Sleep(1000);
        UltrawideResolutionPatch();
        return 1;
    }

    Sleep(1000);

    IntResolutionMemoryPatch();
    FloatResolutionMemoryPatch();
    AspectRatioMemoryPatch();

    InitVideoDecoderHooks();

    CutsceneCameraARHook();
    CutsceneViewportARHook();

    FieldCameraARHook();
    BattleCameraARHook();

    return 1;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        thisModule = hModule;
        using SetContextFn = BOOL(WINAPI*)(HANDLE);
        auto setContext = reinterpret_cast<SetContextFn>(GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext"));
        if (!setContext || !setContext(reinterpret_cast<HANDLE>(-4))) SetProcessDPIAware();
        if (HANDLE mainHandle = CreateThread(NULL, 0, Main, 0, NULL, 0)) {
            SetThreadPriority(mainHandle, THREAD_PRIORITY_NORMAL);
            CloseHandle(mainHandle);
        }
    }
    return TRUE;
}