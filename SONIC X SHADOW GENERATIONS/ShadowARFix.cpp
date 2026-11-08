#include "ARFixCommon.hpp"

namespace
{
    using namespace ARFix;
    uintptr_t mainCameraCaller = 0;
    uintptr_t renderCameraCaller = 0;
    uintptr_t fitInputAddress = 0;
    uintptr_t fitOutputAddress = 0;
    std::atomic<bool> fixReady{false};
    std::atomic<uint64_t> outputSize{0};
    std::atomic<uint64_t> correctedSize{0};
    std::atomic<bool> cameraWaitingLogged{false};
    thread_local uint64_t fitSize = 0;
    thread_local uintptr_t fitStack = 0;

    void ProbeCamera(SafetyHookContext& ctx)
    {
        uintptr_t caller = 0;
        if (!Read(ctx.rsp, caller))
        {
            correctedSize.store(0);
            ReadFailure(0);
            return;
        }
        const float originalFov = ctx.xmm1.f32[0];
        const float originalAspect = ctx.xmm2.f32[0];
        if (fixReady.load() && cameraCorrection &&
            (caller == mainCameraCaller || caller == renderCameraCaller))
        {
            const auto size = outputSize.load();
            float fov = 0;
            float aspect = 0;
            if (!size)
            {
                if (!cameraWaitingLogged.exchange(true))
                    logger->debug("FIX-CAMERA waiting for verified output dimensions; camera unchanged.");
            }
            else if (CorrectProjection(originalFov, originalAspect, size, fov, aspect))
            {
                ctx.xmm1.f32[0] = fov;
                ctx.xmm2.f32[0] = aspect;
                correctedSize.store(size);
                if (verboseDiagnostics && ShouldLog(0, caller, 1, { originalFov, originalAspect, fov, aspect, 0, 0 }))
                    logger->debug("FIX-CAMERA caller={} fovy_rad={} -> {} aspect={} -> {} output={}x{}",
                                 Caller(caller), originalFov, fov, originalAspect, aspect,
                                 static_cast<uint32_t>(size >> 32), static_cast<uint32_t>(size));
            }
            else
            {
                correctedSize.store(0);
                if (ShouldLog(0, caller, 2, { originalFov, originalAspect, 0, 0, 0, 0 }))
                    logger->error("FIX-CAMERA unsupported projection fovy_rad={} aspect={}; "
                                  "camera unchanged and full-output correction suspended.",
                                  originalFov, originalAspect);
            }
        }
    }

    void ProbeFitInputs(SafetyHookContext& ctx)
    {
        fitSize = 0;
        if (!fixReady.load() || (!cameraCorrection && !removeLetterbox))
            return;
        const auto sourceWidth = static_cast<uint32_t>(ctx.r10);
        const auto sourceHeight = static_cast<uint32_t>(ctx.r8);
        const auto width = static_cast<uint32_t>(ctx.rax);
        const auto height = static_cast<uint32_t>(ctx.rdx);
        const bool supported = sourceWidth && sourceHeight && width && height &&
            sourceWidth <= 16384 && sourceHeight <= 16384 && width <= 16384 && height <= 16384 &&
            std::abs(static_cast<float>(sourceWidth) / sourceHeight - baselineAspect) <= 0.005f;
        if (!supported)
        {
            outputSize.store(0);
            correctedSize.store(0);
            if (ShouldLog(7, fitInputAddress, 1,
                          { static_cast<float>(sourceWidth), static_cast<float>(sourceHeight),
                            static_cast<float>(width), static_cast<float>(height), 0, 0 }))
                logger->error("FIX-OUTPUT unsupported source={}x{} destination={}x{}; corrections suspended.",
                              sourceWidth, sourceHeight, width, height);
            return;
        }
        fitSize = PackSize(width, height);
        fitStack = ctx.rsp;
        if (outputSize.exchange(fitSize) != fitSize)
            correctedSize.store(0);
        if (verboseDiagnostics && ShouldLog(7, fitInputAddress, 0,
                      { static_cast<float>(sourceWidth), static_cast<float>(sourceHeight),
                        static_cast<float>(width), static_cast<float>(height), 0, 0 }))
            logger->debug("FIT-INPUT source={}x{} destination={}x{}", sourceWidth, sourceHeight, width, height);
    }

    void FixOutputRectangle(SafetyHookContext& ctx)
    {
        const auto size = std::exchange(fitSize, 0);
        if (!fixReady.load() || !removeLetterbox || !size)
            return;
        if (fitStack != ctx.rsp)
        {
            if (!errors[8].exchange(true))
                logger->error("FIX-OUTPUT fit stack does not match input hook; rectangle unchanged.");
            return;
        }
        // Mixed testing mode intentionally allows output expansion without camera compensation.
        if (cameraCorrection && correctedSize.load() != size)
        {
            if (verboseDiagnostics && ShouldLog(8, fitOutputAddress, 1))
                logger->debug("FIX-OUTPUT waiting for a matching camera update; fitted rectangle unchanged.");
            return;
        }
        const auto width = static_cast<uint32_t>(size >> 32);
        const auto height = static_cast<uint32_t>(size);
        ctx.r9 = width;
        ctx.r8 = height;
        ctx.r11 = width / 2;
        ctx.rbx = height / 2;
        if (verboseDiagnostics && ShouldLog(8, fitOutputAddress, 0,
                      { static_cast<float>(width), static_cast<float>(height), 0, 0, 0, 0 }))
            logger->debug("FIX-OUTPUT edges=(0, 0, {}, {})", width, height);
    }

    constexpr char cameraSignature[] =
        "40 53 48 83 EC 70 F3 0F 10 84 24 A0 00 00 00 48 8B D9 "
        "F3 0F 11 81 0C 01 00 00 F3 0F 11 89 00 01 00 00 "
        "F3 0F 11 91 04 01 00 00 F3 0F 11 99 08 01 00 00 "
        "C7 81 E0 00 00 00 00 00 00 00 48 8D 4C 24 30 F3 0F 11 44 24 20 E8 ?? ?? ?? ??";
    constexpr char mainCallerSignature[] =
        "40 53 48 83 EC 70 0F 29 74 24 60 48 8B D9 0F 29 7C 24 50 "
        "48 83 C1 30 44 0F 29 44 24 40 0F 28 F3 44 0F 29 4C 24 30 "
        "0F 28 FA F3 44 0F 10 8C 24 A0 00 00 00 44 0F 28 C1 "
        "F3 44 0F 11 4C 24 20 E8 ?? ?? ?? ?? 48 8B 4B 20 48 85 C9 74 1C "
        "F3 44 0F 11 4C 24 28 0F 28 DF 41 0F 28 D0 F3 0F 11 74 24 20 "
        "48 8B D3 E8 ?? ?? ?? ??";
    constexpr char renderCallerSignature[] =
        "F3 0F 10 84 24 88 00 00 00 0F 28 D6 F3 0F 10 9C 24 80 00 00 00 "
        "0F 28 CF 48 8B C8 F3 0F 11 44 24 20 E8 ?? ?? ?? ?? "
        "0F 28 74 24 40 0F 28 7C 24 30 48 83 C4 58 C3";
    constexpr char compositorSignature[] =
        "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 57 48 81 EC 10 01 00 00 "
        "0F 29 78 D8 48 8B F1 48 8B 49 08 33 D2 0F 29 70 E8 48 8B 81 90 26 00 00 "
        "48 8B 49 28 0F 10 70 10 0F 10 78 20 48 8B 01 FF 90 88 00 00 00 "
        "66 0F 73 DE 0C 66 41 0F 7E F2 0F 28 B4 24 00 01 00 00 0F 10 50 10 "
        "0F 10 48 20 66 0F 6F C2 66 41 0F 7E F8 66 0F 73 D8 08 66 48 0F 7E C0 "
        "66 0F 7E CA 48 C1 E8 20 44 3B D0 75 66 44 3B C2 75 61 "
        "66 0F 73 DF 08 66 0F 73 D9 08 66 0F 7E F9 66 0F 7E C8 3A C8 75 4B "
        "48 8B 4E 08 33 D2 48 8B 71 10 48 8B 99 90 26 00 00 48 8B 49 28 "
        "48 8B 06 48 8B B8 08 03 00 00 48 8B 01 FF 90 88 00 00 00 33 ED 45 33 C9 "
        "48 89 6C 24 30 45 33 C0 89 6C 24 28 48 8B D0 48 8B CE 48 89 5C 24 20 "
        "FF D7 E9 ?? ?? ?? ?? "
        "66 0F 73 DA 0C 0F 57 C0 66 41 0F 7E D1 0F 57 C9 41 8B C2 0F 57 D2 "
        "33 ED 44 89 44 24 5C 45 8B D9 48 89 6C 24 50 8B DA 41 D1 EB "
        "F3 48 0F 2A C8 41 8B C0 D1 EB 44 89 54 24 58 F3 48 0F 2A D0 "
        "41 8B C1 F3 48 0F 2A C0 F3 0F 59 C2 F3 0F 5E C1 F3 4C 0F 2C C0 "
        "44 3B C2 76 18 0F 57 C0 44 8B C2 F3 49 0F 2A C0 F3 0F 59 C1 "
        "F3 0F 5E C2 F3 4C 0F 2C C8 "
        "41 8B D1 41 8B C8 D1 EA 41 8B C3 2B C2 D1 E9 89 44 24 40 44 2B CA "
        "48 8B 56 08 8B C3 2B C1 44 2B C1 89 44 24 44 44 03 C3 "
        "44 89 44 24 4C 45 03 CB 44 89 4C 24 48 "
        "0F 57 C0 0F 11 44 24 60 48 8B 7A 10 48 8B 4A 28 33 D2 48 8B 07 "
        "48 8B 98 C8 02 00 00 48 8B 01 FF 90 88 00 00 00 41 B9 FF FF FF FF "
        "4C 8D 44 24 60 48 8B D0 48 8B CF FF D3 48 8B 46 08 33 D2 "
        "48 8B 48 10 48 8B 01 FF 90 38 02 00 00 48 8B 46 08 33 D2 "
        "48 8B 48 10 48 8B 01 FF 90 40 02 00 00 48 8B 46 08 33 D2 "
        "89 6C 24 20 48 8B 48 10 44 8D 4A 02 44 8D 42 04 48 8B 01 "
        "FF 90 78 02 00 00 48 8B 46 08 33 D2 48 8B 48 28 48 8B 98 F0 25 00 00 "
        "48 8B 01 FF 90 88 00 00 00 48 8B 56 08 48 8D 4C 24 40 48 89 4C 24 28 "
        "4C 8B C8 48 8D 4C 24 50 48 89 4C 24 20 48 8B CB "
        "4C 8B 82 98 26 00 00 48 8B 52 10 E8 ?? ?? ?? ??";
    constexpr char perspectiveSignature[] =
        "40 53 48 83 EC 40 F3 0F 59 0D ?? ?? ?? ?? 48 8B D9 "
        "0F 29 74 24 30 0F 28 F2 0F 29 7C 24 20 0F 28 FB 0F 28 C1 E8 ?? ?? ?? ?? "
        "F3 0F 10 25 ?? ?? ?? ?? 33 C0 F3 0F 10 54 24 70 0F 28 CC "
        "F3 0F 5E C8 0F 28 C2 F3 0F 5C C7 F3 0F 59 FA F3 0F 5E E0 "
        "0F 28 C1 F3 0F 5E C6 0F 28 74 24 30 F3 0F 11 03";
    constexpr char passSignature[] =
        "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 57 41 56 41 57 "
        "48 81 EC 20 01 00 00 48 8B F2 0F 29 70 D8 4C 8B F1 B2 01 "
        "48 8D 4C 24 68 4D 8B F9 49 8B F8 E8 ?? ?? ?? ??";

    bool ResolveRendering(uintptr_t& camera, uintptr_t& perspective, uintptr_t& pass)
    {
        camera = FindUnique("Camera setter", cameraSignature);
        const auto main = FindUnique("Main camera caller", mainCallerSignature);
        const auto render = FindUnique("Renderer camera caller", renderCallerSignature);
        const auto compositor = FindUnique("Final compositor", compositorSignature);
        if (!camera || !main || !render || !compositor)
            return false;
        perspective = CallTarget(camera + 0x47);
        pass = CallTarget(compositor + 0x249);
        if (CallTarget(main + 0x3E) != camera || CallTarget(render + 0x21) != camera ||
            !perspective || !Matches(perspective, Memory::PatternToBytes(perspectiveSignature)) ||
            !FloatOperand(perspective + 0xA, 0.5f) || !FloatOperand(perspective + 0x2D, 1.0f) ||
            !pass || !Matches(pass, Memory::PatternToBytes(passSignature)) ||
            !Matches(pass + 0x53, Memory::PatternToBytes("4C 8B 84 24 60 01 00 00")) ||
            !Matches(pass + 0x1AE, Memory::PatternToBytes(
                "48 8D 4C 24 50 44 8B 43 0C 8B 53 08 44 2B 43 04 2B 13 "
                "E8 ?? ?? ?? ?? 8B 03 48 8D 54 24 50 89 44 24 50 "
                "41 B8 01 00 00 00 8B 43 04 48 8B CE 89 44 24 54 "
                "48 8B 06 FF 90 80 02 00 00")))
        {
            logger->error("Scanned camera/projection/compositor call relationships failed validation; fixes disabled.");
            return false;
        }
        const auto forward = CallTarget(main + 0x63);
        if (!forward || forward > render || render - forward != 0xAB ||
            !Matches(forward, Memory::PatternToBytes(
                "48 83 EC 58 48 0F BE 42 18 4C 8B D2 0F 29 74 24 40 "
                "0F 28 F3 0F 29 7C 24 30 0F 28 FA")))
        {
            logger->error("Scanned main-camera forwarding path changed; fixes disabled.");
            return false;
        }
        mainCameraCaller = main + 0x43;
        renderCameraCaller = render + 0x26;
        fitInputAddress = compositor + 0x79;
        fitOutputAddress = compositor + 0x154;
        logger->debug("Validated rendering graph: camera={} perspective={} mainCaller={} renderCaller={} "
                     "fitInput={} fitOutput={} finalPass={}.",
                     Caller(camera), Caller(perspective), Caller(mainCameraCaller), Caller(renderCameraCaller),
                     Caller(fitInputAddress), Caller(fitOutputAddress), Caller(pass));
        return true;
    }


    DWORD WINAPI Setup(void*)
    {
        if (!Initialize(L"SONIC_X_SHADOW_GENERATIONS.exe", "Shadow"))
            return 0;
        if (!cameraCorrection && !removeLetterbox)
        {
            logger->info("Both fixes disabled; no hooks installed.");
            return 0;
        }
        uintptr_t camera = 0, perspective = 0, pass = 0;
        if (!ResolveRendering(camera, perspective, pass))
        {
            logger->error("Rendering graph not verified; no hooks installed.");
            return 0;
        }
        const bool cameraInstalled = !cameraCorrection ||
            Install(0, "Camera", reinterpret_cast<void*>(camera), ProbeCamera);
        const bool inputInstalled = Install(7, "Final fit inputs",
            reinterpret_cast<void*>(fitInputAddress), ProbeFitInputs);
        const bool outputInstalled = !removeLetterbox || Install(8, "Final fit rectangle",
            reinterpret_cast<void*>(fitOutputAddress), FixOutputRectangle);
        fixReady.store(cameraInstalled && inputInstalled && outputInstalled);
        if (fixReady.load())
            logger->info("Fix enabled: fresh camera inputs and final compositor.");
        else
            logger->error("Required hook failed; all rendering corrections disabled.");
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    return ARFix::Attach(module, reason, Setup);
}
