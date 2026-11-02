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

void Configuration() {
    std::ifstream iniFile(ExePath / "FF13_ARFix.ini");
    if (iniFile) {
        inipp::Ini<char> ini;
        ini.parse(iniFile);
        inipp::get_value(ini.sections["Resolution"], "ReplaceWidth", iReplaceWidth);
        inipp::get_value(ini.sections["Resolution"], "ReplaceHeight", iReplaceHeight);
        inipp::get_value(ini.sections["Resolution"], "TargetWidth", iTargetWidth);
        inipp::get_value(ini.sections["Resolution"], "TargetHeight", iTargetHeight);
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

DWORD __stdcall Main(void*) {
    Sleep(1000);
    Logging();
    Configuration();

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
        if (HANDLE mainHandle = CreateThread(NULL, 0, Main, 0, NULL, 0)) {
            SetThreadPriority(mainHandle, THREAD_PRIORITY_NORMAL);
            CloseHandle(mainHandle);
        }
    }
    return TRUE;
}