#include "ARFixCommon.hpp"
namespace
{
    using namespace ARFix;
    std::atomic<bool> fixReady{false};
    std::atomic<uint64_t> outputSize{0};
    std::array<std::atomic<uint64_t>, 2> correctedSizes{};

    struct BlitInput
    {
        uintptr_t caller = 0;
        uintptr_t stack = 0;
        std::array<uint32_t, 2> source{};
        std::array<uint32_t, 2> destination{};
        uint8_t fit = 0;
    };
    thread_local BlitInput blitInput{};

    std::array<uintptr_t, 2> projectionCallers{};
    uintptr_t blitCaller = 0;

    bool SupportedOutput(const BlitInput& input)
    {
        return input.caller == blitCaller && input.fit == 1 &&
            input.source[0] && input.source[1] && input.destination[0] && input.destination[1] &&
            input.source[0] <= 16384 && input.source[1] <= 16384 &&
            input.destination[0] <= 16384 && input.destination[1] <= 16384 &&
            std::abs(static_cast<float>(input.source[0]) / input.source[1] - baselineAspect) <= 0.005f;
    }

    void ProbePerspective(SafetyHookContext& ctx)
    {
        uintptr_t caller = 0;
        if (!Read(ctx.rsp, caller))
        {
            correctedSizes[0].store(0);
            correctedSizes[1].store(0);
            ReadFailure(1);
            return;
        }
        const unsigned path = caller == projectionCallers[0] ? 0 : 1;
        if (fixReady.load() && cameraCorrection &&
            (caller == projectionCallers[0] || caller == projectionCallers[1]))
        {
            const auto size = outputSize.load();
            const float originalFov = ctx.xmm1.f32[0];
            const float originalAspect = ctx.xmm2.f32[0];
            float fov = 0;
            float aspect = 0;
            if (!size)
            {
                if (verboseDiagnostics && ShouldLog(1, caller, 1))
                    logger->debug("FIX-PROJECTION waiting for final scene dimensions; inputs unchanged.");
            }
            else if (CorrectProjection(originalFov, originalAspect, size, fov, aspect))
            {
                ctx.xmm1.f32[0] = fov;
                ctx.xmm2.f32[0] = aspect;
                correctedSizes[path].store(size);
                if (verboseDiagnostics && ShouldLog(1, caller, 2, { originalFov, originalAspect, fov, aspect }))
                    logger->debug("FIX-PROJECTION caller={} fovy_rad={} -> {} aspect={} -> {} output={}x{}",
                                 Caller(caller), originalFov, fov, originalAspect, aspect,
                                 static_cast<uint32_t>(size >> 32), static_cast<uint32_t>(size));
            }
            else
            {
                correctedSizes[path].store(0);
                if (ShouldLog(1, caller, 3, { originalFov, originalAspect, 0, 0 }))
                    logger->error("FIX-PROJECTION unsupported fovy_rad={} aspect={}; inputs unchanged, quad expansion suspended.",
                                  originalFov, originalAspect);
            }
        }
    }

    void ProbeBlit(SafetyHookContext& ctx)
    {
        blitInput = {};
        BlitInput input{};
        if (!fixReady.load())
            return;
        if (!Read(ctx.rsp, input.caller))
        {
            outputSize.store(0);
            correctedSizes[0].store(0);
            correctedSizes[1].store(0);
            ReadFailure(4);
            return;
        }
        if (input.caller != blitCaller)
            return;
        if (!Read(ctx.rsp + 0x28, input.fit) ||
            !Read(ctx.rdx + 0x1C, input.source) || !Read(ctx.r8 + 0x1C, input.destination))
        {
            outputSize.store(0);
            correctedSizes[0].store(0);
            correctedSizes[1].store(0);
            ReadFailure(4);
            return;
        }
        input.stack = ctx.rsp;
        blitInput = input;
        if (fixReady.load() && input.caller == blitCaller)
        {
            const auto size = SupportedOutput(input) ? PackSize(input.destination[0], input.destination[1]) : 0;
            if (outputSize.exchange(size) != size)
            {
                correctedSizes[0].store(0);
                correctedSizes[1].store(0);
            }
            if (!size && ShouldLog(4, input.caller, 2))
                logger->error("FIX-QUAD unsupported final source/destination/fit; projection and quad corrections suspended.");
        }
        if (verboseDiagnostics && ShouldLog(4, input.caller, input.fit,
                      { static_cast<float>(input.source[0]), static_cast<float>(input.source[1]),
                        static_cast<float>(input.destination[0]), static_cast<float>(input.destination[1]) }))
        {
            logger->debug("SCENE-BLIT caller={} source={}x{} destination={}x{} fit={} source_object=0x{:X} "
                         "destination_object=0x{:X}", Caller(input.caller), input.source[0], input.source[1],
                         input.destination[0], input.destination[1], input.fit, ctx.rdx, ctx.r8);
        }
    }

    void ProbeQuad(SafetyHookContext& ctx)
    {
        const auto input = std::exchange(blitInput, {});
        if (!fixReady.load() || !removeLetterbox || input.caller != blitCaller)
            return;
        // The hook is inside the blit function: its entry return address is no longer at rsp.
        if (!input.stack || input.stack < 0x248 || ctx.rsp != input.stack - 0x248)
        {
            if (!errors[5].exchange(true))
                logger->error("SCENE-QUAD input/stack pairing failed; values unchanged.");
            return;
        }
        std::array<float, 2> center{};
        if (!Read(ctx.rsp + 0x40, center))
        {
            ReadFailure(5);
            return;
        }
        const float halfWidth = ctx.xmm7.f32[0];
        const float halfHeight = ctx.xmm6.f32[0];
        if (fixReady.load() && removeLetterbox && SupportedOutput(input))
        {
            const auto size = PackSize(input.destination[0], input.destination[1]);
            if (!std::isfinite(halfWidth) || !std::isfinite(halfHeight) || halfWidth <= 0 ||
                halfHeight <= 0 || halfWidth > 1.001f || halfHeight > 1.001f ||
                center[0] != 0 || center[1] != 0)
            {
                if (ShouldLog(5, input.caller, 2, { halfWidth, halfHeight, center[0], center[1] }))
                    logger->error("FIX-QUAD unexpected fitted geometry; unchanged.");
            }
            else if (cameraCorrection &&
                     (correctedSizes[0].load() != size || correctedSizes[1].load() != size))
            {
                if (verboseDiagnostics && ShouldLog(5, input.caller, 3))
                    logger->debug("FIX-QUAD waiting for both projection paths at matching output size; fitted geometry unchanged.");
            }
            else
            {
                ctx.xmm7.f32[0] = 1;
                ctx.xmm6.f32[0] = 1;
                ctx.rbx &= ~uintptr_t{0xFF};
                if (verboseDiagnostics && ShouldLog(5, input.caller, 4,
                              { halfWidth, halfHeight, static_cast<float>(input.destination[0]),
                                static_cast<float>(input.destination[1]) }))
                    logger->debug("FIX-QUAD output={}x{} half_extent=({}, {}) -> (1, 1) clear_borders=false",
                                 input.destination[0], input.destination[1], halfWidth, halfHeight);
            }
        }
        if (verboseDiagnostics && ShouldLog(5, input.caller, input.fit,
                      { halfWidth, halfHeight, static_cast<float>(input.destination[0]),
                        static_cast<float>(input.destination[1]) }))
            logger->debug("SCENE-QUAD caller={} source={}x{} destination={}x{} fit={} "
                         "center=({}, {}) half_extent=({}, {}) clear_borders={}",
                         Caller(input.caller), input.source[0], input.source[1],
                         input.destination[0], input.destination[1], input.fit,
                         center[0], center[1], ctx.xmm7.f32[0], ctx.xmm6.f32[0],
                         static_cast<uint8_t>(ctx.rbx) != 0);
    }

    constexpr char mainCallerSignature[] =
        "F3 0F 10 87 58 02 00 00 48 8D 4C 24 50 F3 0F 10 9F 54 02 00 00 "
        "0F 28 CE F3 0F 10 97 50 02 00 00 F3 0F 11 44 24 20 E8 ?? ?? ?? ?? "
        "48 8D 8F F0 01 00 00 48 8D 54 24 50 E8 ?? ?? ?? ?? "
        "48 8B 9C 24 C0 00 00 00 0F 28 B4 24 90 00 00 00 48 81 C4 A0 00 00 00 5F C3";
    constexpr char renderCallerSignature[] =
        "F3 0F 10 0D ?? ?? ?? ?? 48 8D 4D 40 F3 0F 11 4C 24 20 "
        "0F 28 DE 41 0F 28 C8 0F 28 D7 E8 ?? ?? ?? ??";
    constexpr char blitCallerSignature[] =
        "48 83 EC 38 4C 8B 41 60 48 8B C2 48 8B 51 50 F3 0F 10 59 4C "
        "48 8B C8 C6 44 24 20 01 4D 8B 40 60 48 8B 52 20 E8 ?? ?? ?? ?? "
        "48 83 C4 38 C3";
    constexpr char perspectiveSignature[] =
        "40 53 48 81 EC 80 00 00 00 F3 0F 59 0D ?? ?? ?? ?? 48 8B D9 "
        "0F 29 74 24 70 0F 28 F2 0F 29 7C 24 60 0F 28 FB 0F 28 C1 E8 ?? ?? ?? ?? "
        "F3 0F 10 2D ?? ?? ?? ?? 48 8D 54 24 20 F3 0F 10 94 24 B0 00 00 00 "
        "0F 28 E5 F3 0F 5E E0 48 C7 44 24 24 00 00 00 00 48 8B CB "
        "48 C7 44 24 2C 00 00 00 00 48 C7 44 24 38 00 00 00 00 "
        "48 C7 44 24 40 00 00 00 00 C7 44 24 4C 00 00 80 BF "
        "48 C7 44 24 50 00 00 00 00 C7 44 24 5C 00 00 00 00 "
        "0F 28 CF F3 0F 5C CA F3 0F 11 64 24 34 0F 28 C4 F3 0F 5E C6 "
        "F3 0F 5E E9 F3 0F 11 44 24 20 0F 28 C7 F3 0F 58 C2 "
        "F3 0F 58 D2 F3 0F 59 C5 F3 0F 59 D7 F3 0F 11 44 24 48 "
        "F3 0F 59 D5 F3 0F 11 54 24 58 E8 ?? ?? ?? ?? "
        "0F 28 74 24 70 48 8B C3 0F 28 7C 24 60 48 81 C4 80 00 00 00 5B C3";
    constexpr char blitSignature[] =
        "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 4C 89 60 20 55 41 56 41 57 "
        "48 8D A8 B8 FE FF FF 48 81 EC 30 02 00 00 0F 29 70 D8 4C 8B F9 0F 29 78 "
        "C8 48 83 C1 20 44 0F 29 40 B8 49 8B D8 44 0F 29 48 A8 48 8B FA 44 0F 28 "
        "CB 44 0F 29 50 98 45 0F C6 C9 00 E8 ?? ?? ?? ?? 45 33 C0 49 8D 4F 20 41 "
        "8D 50 07 E8 ?? ?? ?? ?? 41 BC 01 00 00 00 49 8D 4F 20 41 8B D4 45 33 C0 "
        "E8 ?? ?? ?? ?? 45 33 C0 41 8D 54 24 03 49 8D 4F 20 E8 ?? ?? ?? ?? 45 33 "
        "C0 41 8D 54 24 08 49 8D 4F 20 E8 ?? ?? ?? ?? 41 8D 54 24 09 45 8D 44 24 "
        "05 49 8D 4F 20 E8 ?? ?? ?? ?? 45 33 C0 41 8D 54 24 0A 49 8D 4F 20 E8 ?? "
        "?? ?? ?? 48 8D 8D 80 00 00 00 E8 ?? ?? ?? ?? 48 8D 8D 80 00 00 00 E8 ?? "
        "?? ?? ?? 4C 8D 85 80 00 00 00 48 89 BD A0 00 00 00 33 D2 49 8D 4F 20 E8 "
        "?? ?? ?? ?? 45 8D 4C 24 01 45 8B C4 33 D2 49 8D 4F 20 E8 ?? ?? ?? ?? 41 "
        "B9 02 00 00 00 49 8D 4F 20 45 8B C1 33 D2 E8 ?? ?? ?? ?? 45 8B CC 45 8D "
        "44 24 04 33 D2 49 8D 4F 20 E8 ?? ?? ?? ?? 45 8B CC 45 8D 44 24 05 33 D2 "
        "49 8D 4F 20 E8 ?? ?? ?? ?? 45 33 C9 45 8D 44 24 06 33 D2 49 8D 4F 20 E8 "
        "?? ?? ?? ?? BA 44 01 00 00 49 8B CF E8 ?? ?? ?? ?? 41 8D 54 24 03 49 8D "
        "4F 20 E8 ?? ?? ?? ?? 41 8D 54 24 03 49 8D 4F 20 E8 ?? ?? ?? ?? 48 8D 4D "
        "10 E8 ?? ?? ?? ?? 48 8D 4D 10 C7 45 3C 03 00 00 00 48 89 5D 70 E8 ?? ?? "
        "?? ?? 48 8B 43 18 4C 8D 45 10 44 8B 73 20 49 8D 4F 20 0F 10 73 10 48 C1 "
        "E8 20 33 D2 89 45 50 44 89 75 54 E8 ?? ?? ?? ?? 33 D2 49 8D 4F 20 E8 ?? "
        "?? ?? ?? F3 44 0F 10 05 ?? ?? ?? ?? 48 8D 4C 24 40 41 0F 28 D8 F3 44 0F "
        "11 44 24 20 0F 57 D2 0F 57 C9 45 0F 57 D2 E8 ?? ?? ?? ?? 41 0F 28 D8 F3 "
        "44 0F 11 44 24 20 0F 57 D2 48 8D 4C 24 50 0F 57 C9 E8 ?? ?? ?? ?? 0F 10 "
        "47 20 32 DB 0F 10 4F 10 0F 11 45 F0 38 9D 70 01 00 00 0F 84 BD 00 00 00 "
        "8B 4D F0 66 0F 73 D9 08 66 0F 73 DE 0C 66 48 0F 7E C8 66 0F 7E F2 48 C1 "
        "E8 20 3B D0 75 09 44 3B F1 0F 84 96 00 00 00 0F 57 C0 0F 57 C9 F3 48 0F "
        "2A C1 F3 48 0F 2A C8 8B C2 0F 57 E4 0F 57 DB F3 48 0F 2A D8 F3 0F 5E C8 "
        "F3 49 0F 2A E6 0F 28 D3 F3 0F 5E D1 0F 28 C4 F3 0F 59 C1 0F 2F C3 76 2E "
        "0F 28 F2 41 0F 28 F8 F3 0F 5E F4 F3 0F 5C D4 F3 0F 11 7C 24 48 F3 0F 11 "
        "74 24 4C 0F 54 15 ?? ?? ?? ?? 41 0F 2F D0 76 41 41 0F B6 DC EB 3B 0F 28 "
        "F8 0F B6 DB F3 0F 5E FB F3 0F 5C C3 41 0F 28 F0 F3 0F 11 7C 24 48 F3 0F "
        "11 74 24 4C 0F 54 05 ?? ?? ?? ?? 41 0F 2F C0 41 0F 47 DC EB 0C F3 0F 10 "
        "74 24 4C F3 0F 10 7C 24 48 F3 0F 10 44 24 40 BA FF 00 00 00 F3 0F 10 4C "
        "24 44 F3 0F 5C C7 F3 0F 58 CE C7 44 24 68 00 00 00 3F 8B CA C7 44 24 6C "
        "00 00 80 3F ";


    bool ResolveRendering(uintptr_t& perspective, uintptr_t& blit, uintptr_t& quad)
    {
        const auto main = FindUnique("Main projection caller", mainCallerSignature);
        const auto render = FindUnique("Renderer projection caller", renderCallerSignature);
        const auto final = FindUnique("Final scene caller", blitCallerSignature);
        blit = FindUnique("Final scene blit", blitSignature);
        if (!main || !render || !final || !blit)
            return false;
        perspective = CallTarget(main + 0x26);
        if (!perspective || CallTarget(render + 0x1C) != perspective ||
            CallTarget(final + 0x24) != blit ||
            !Matches(perspective, Memory::PatternToBytes(perspectiveSignature)) ||
            !FloatOperand(perspective + 0xD, 0.5f) || !FloatOperand(perspective + 0x30, 1.0f) ||
            !FloatOperand(blit + 0x1D0, 1.0f) ||
            !CallTarget(blit + 0x1EE) || CallTarget(blit + 0x1EE) != CallTarget(blit + 0x209) ||
            !Matches(blit + 0x4BB, Memory::PatternToBytes(
                "84 DB 74 1F 89 7C 24 30 45 8B CC F3 44 0F 11 54 24 28 "
                "45 33 C0 33 D2 89 7C 24 20 49 8B CF E8 ?? ?? ?? ??")))
        {
            logger->error("Projection/blit call, constant or geometry contracts changed; fixes disabled.");
            return false;
        }
        projectionCallers = {main + 0x2B, render + 0x21};
        blitCaller = final + 0x29;
        quad = blit + 0x2F1;
        logger->debug("Validated rendering graph: perspective={} callers=({}, {}) blit={} caller={} quad={}.",
                      Caller(perspective), Caller(projectionCallers[0]), Caller(projectionCallers[1]),
                      Caller(blit), Caller(blitCaller), Caller(quad));
        return true;
    }

    DWORD WINAPI Setup(void*)
    {
        if (!Initialize(L"SONIC_GENERATIONS.exe", "Generations"))
            return 0;
        if (!cameraCorrection && !removeLetterbox)
        {
            logger->info("Both fixes disabled; no hooks installed.");
            return 0;
        }
        uintptr_t perspective = 0, blit = 0, quad = 0;
        if (!ResolveRendering(perspective, blit, quad))
        {
            logger->error("Rendering graph not verified; no hooks installed.");
            return 0;
        }
        const bool projectionInstalled = !cameraCorrection ||
            Install(1, "Perspective", reinterpret_cast<void*>(perspective), ProbePerspective);
        const bool blitInstalled = Install(4, "Final scene inputs", reinterpret_cast<void*>(blit), ProbeBlit);
        const bool quadInstalled = !removeLetterbox ||
            Install(5, "Final scene quad", reinterpret_cast<void*>(quad), ProbeQuad);
        fixReady.store(projectionInstalled && blitInstalled && quadInstalled);
        if (fixReady.load())
            logger->info("Fix enabled: two fresh projection paths and final scene quad.");
        else
            logger->error("Required hook failed; all rendering corrections disabled.");
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    return ARFix::Attach(module, reason, Setup);
}
