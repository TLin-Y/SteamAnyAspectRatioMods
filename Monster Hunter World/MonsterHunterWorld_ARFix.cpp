#include "stdafx.h"
#include "version.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <tuple>
#include <vector>
#include <safetyhook.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

static_assert(sizeof(void*) == 8, "Build for Windows x64.");

namespace
{

namespace Config
{
    bool centerHud = true;

    void Load(const std::filesystem::path& ini)
    {
        centerHud = GetPrivateProfileIntW(L"HUD", L"Center16x9", 1, ini.c_str()) != 0;
        SPDLOG_INFO("HUD kept in a centred 16:9 area: {}.", centerHud);
    }
}

namespace Game
{
    using Bytes = std::span<std::uint8_t>;
    using Pattern = std::vector<int>;

    constexpr int any = -1;

    Bytes image;
    std::vector<Bytes> code;

    void Load()
    {
        const auto base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        image = { base, nt->OptionalHeader.SizeOfImage };
        code.clear();
        for (const auto& section : std::span(IMAGE_FIRST_SECTION(nt), nt->FileHeader.NumberOfSections))
            if (section.Characteristics & IMAGE_SCN_MEM_EXECUTE)
                code.push_back({ base + section.VirtualAddress, section.Misc.VirtualSize });
    }

    bool Contains(Bytes area, const void* address, std::size_t size)
    {
        const auto byte = static_cast<const std::uint8_t*>(address);
        return byte >= area.data() && byte + size <= area.data() + area.size();
    }

    bool IsCode(const void* address, std::size_t size)
    {
        return std::ranges::any_of(code, [&](Bytes section) { return Contains(section, address, size); });
    }

    std::vector<std::uint8_t*> FindAll(const Pattern& pattern, std::span<const Bytes> areas = code)
    {
        const auto isSame = [](std::uint8_t actual, int wanted) { return wanted == any || wanted == actual; };
        std::vector<std::uint8_t*> found;
        for (const Bytes area : areas)
        {
            const auto next = [&](auto from) {
                return std::search(from, area.end(), pattern.begin(), pattern.end(), isSame);
            };
            for (auto match = next(area.begin()); match != area.end(); match = next(match + 1))
                found.push_back(&*match);
        }
        return found;
    }

    std::vector<std::uint8_t*> FindIn(std::uint8_t* from, std::size_t length, const Pattern& pattern)
    {
        if (!IsCode(from, length))
            return {};
        const Bytes area{ from, length };
        return FindAll(pattern, { &area, 1 });
    }

    template <typename Value>
    Value Read(const void* address)
    {
        Value value;
        std::memcpy(&value, address, sizeof(value));
        return value;
    }

    template <typename Value>
    Value& Field(void* object, std::ptrdiff_t offset)
    {
        return *reinterpret_cast<Value*>(static_cast<std::uint8_t*>(object) + offset);
    }

    Pattern BytesOf(std::int32_t value)
    {
        const auto bytes = reinterpret_cast<const std::uint8_t*>(&value);
        return { bytes[0], bytes[1], bytes[2], bytes[3] };
    }

    Pattern Join(std::initializer_list<Pattern> parts)
    {
        Pattern joined;
        for (const Pattern& part : parts)
            joined.insert(joined.end(), part.begin(), part.end());
        return joined;
    }

    std::uint8_t* RelativeTarget(std::uint8_t* instructionEnd)
    {
        return instructionEnd + Read<std::int32_t>(instructionEnd - sizeof(std::int32_t));
    }

    const float* RipTarget(std::uint8_t* instructionEnd)
    {
        const auto target = RelativeTarget(instructionEnd);
        return Contains(image, target, sizeof(float)) ? reinterpret_cast<const float*>(target) : nullptr;
    }

    std::uint8_t* FunctionStart(std::uint8_t* address)
    {
        constexpr std::uint8_t chained = UNW_FLAG_CHAININFO << 3;
        constexpr std::size_t headerSize = 4;
        DWORD64 base = 0;
        auto entry = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(address), &base, nullptr);
        while (entry)
        {
            const auto unwind = reinterpret_cast<std::uint8_t*>(base + entry->UnwindData);
            if (!(unwind[0] & chained))
                return reinterpret_cast<std::uint8_t*>(base + entry->BeginAddress);
            const std::size_t evenCodeCount = (unwind[2] + 1) & ~1;
            entry = reinterpret_cast<RUNTIME_FUNCTION*>(unwind + headerSize + evenCodeCount * sizeof(std::uint16_t));
        }
        return nullptr;
    }

    std::size_t Offset(const void* address)
    {
        return static_cast<const std::uint8_t*>(address) - image.data();
    }

    bool Write(std::uint8_t* address, const void* bytes, std::size_t size)
    {
        DWORD protection = 0;
        if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &protection))
            return false;
        std::memcpy(address, bytes, size);
        VirtualProtect(address, size, protection, &protection);
        FlushInstructionCache(GetCurrentProcess(), address, size);
        return true;
    }
}

namespace Renderer
{
    struct Size
    {
        std::int32_t width;
        std::int32_t height;

        bool operator==(const Size&) const = default;
    };

    struct Layout
    {
        void** manager = nullptr;
        std::uint8_t** options = nullptr;
        std::int32_t requestedMode = 0;
        std::int32_t contentSize = 0;
        std::int32_t aspect = 0;
        std::int32_t fixedLayoutFlag = 0;

        std::int32_t ActiveMode() const { return requestedMode - static_cast<std::int32_t>(sizeof(std::int32_t)); }
        void* Manager() const { return *manager; }
        Size Content() const { return Game::Field<Size>(Manager(), contentSize); }
    };

    Layout layout;

    bool FindManager()
    {
        constexpr std::uint8_t storeRequestedMode[] = { 0x89, 0x91 };
        const Game::Pattern selectPresetMode{
            0x48, 0x8B, 0x0D, Game::any, Game::any, Game::any, Game::any,
            0xFF, 0xC2,
            0xE8, Game::any, Game::any, Game::any, Game::any,
        };
        std::vector<std::pair<void**, std::int32_t>> found;
        for (std::uint8_t* const match : Game::FindAll(selectPresetMode))
        {
            std::uint8_t* const setter = Game::RelativeTarget(match + 14);
            const auto manager = reinterpret_cast<void**>(Game::RelativeTarget(match + 7));
            if (Game::IsCode(setter, 6) && std::memcmp(setter, storeRequestedMode, 2) == 0 &&
                Game::Contains(Game::image, manager, sizeof(void*)))
                found.emplace_back(manager, Game::Read<std::int32_t>(setter + 2));
        }
        if (found.size() != 1)
            return false;
        std::tie(layout.manager, layout.requestedMode) = found.front();
        const Game::Pattern compareModes = Game::Join({ { 0x8B, 0x81 }, Game::BytesOf(layout.requestedMode),
                                                        { Game::any, Game::any, Game::any, 0x39, 0x81 },
                                                        Game::BytesOf(layout.ActiveMode()) });
        return Game::FindAll(compareModes).size() == 1;
    }

    bool FindContent(std::uint8_t* applyKeepSize)
    {
        const Game::Pattern storeContent{ 0x66, 0x0F, 0x6E, 0xC9, 0x66, 0x0F, 0x6E, 0xC2, 0x0F, 0x5B, 0xC9, 0x89, 0x8B };
        const Game::Pattern storeAspect{ 0xF3, 0x0F, 0x11, 0x8B };
        const auto content = Game::FindIn(applyKeepSize, storeContent.size(), storeContent);
        if (content.size() != 1)
            return false;
        layout.contentSize = Game::Read<std::int32_t>(applyKeepSize + storeContent.size());
        const auto aspect = Game::FindIn(applyKeepSize, 0x40, storeAspect);
        if (aspect.empty())
            return false;
        layout.aspect = Game::Read<std::int32_t>(aspect.front() + storeAspect.size());
        return true;
    }

    bool FindLayoutFlag()
    {
        const Game::Pattern readFlag{ 0x48, 0x8B, 0x05, Game::any, Game::any, Game::any, Game::any,
                                      0x0F, 0xB6, 0x88, Game::any, Game::any, Game::any, Game::any };
        const Game::Pattern storeRequested = Game::Join({ { 0x89, 0x86 }, Game::BytesOf(layout.requestedMode) });
        std::vector<std::uint8_t*> found;
        for (std::uint8_t* const match : Game::FindAll(readFlag))
            if (!Game::FindIn(match, 0x30, storeRequested).empty())
                found.push_back(match);
        if (found.size() != 1)
            return false;
        layout.options = reinterpret_cast<std::uint8_t**>(Game::RelativeTarget(found.front() + 7));
        layout.fixedLayoutFlag = Game::Read<std::int32_t>(found.front() + 10);
        return Game::Contains(Game::image, layout.options, sizeof(void*));
    }
}

namespace Letterbox
{
    constexpr std::size_t expectedSites = 5;

    struct Site
    {
        std::uint8_t* skipJump;
        const float* wide;
        const float* ultrawide;

        std::uint8_t* KeepSize() const { return skipJump + 2 + static_cast<std::int8_t>(skipJump[1]); }

        bool IsFitToPresetAspect() const
        {
            constexpr float wideAspect = 16.0f / 9.0f;
            constexpr float ultrawideAspect = 64.0f / 27.0f;
            constexpr float tolerance = 1e-4f;
            return wide && ultrawide && std::abs(*wide - wideAspect) < tolerance &&
                   std::abs(*ultrawide - ultrawideAspect) < tolerance;
        }
    };

    bool IsRex(std::uint8_t byte)
    {
        return (byte & 0xF0) == 0x40;
    }

    bool IsRipLoad(const std::uint8_t* bytes)
    {
        constexpr std::uint8_t ripMode = 0x05;
        return bytes[0] == 0xF3 && bytes[1] == 0x0F && bytes[2] == 0x10 && (bytes[3] & 0xC7) == ripMode;
    }

    std::optional<Site> ReadSite(std::uint8_t* skipJump)
    {
        constexpr std::size_t jumpLength = 2;
        constexpr std::size_t loadLength = 8;
        std::uint8_t* const wideLoad = skipJump + jumpLength;
        std::uint8_t* compare = wideLoad + loadLength;
        if (!IsRipLoad(wideLoad) || !Game::IsCode(compare, 16))
            return std::nullopt;
        if (IsRex(compare[0]))
            ++compare;
        const bool comparesWithTwo = compare[0] == 0x83 && compare[1] >= 0xF8 && compare[2] == 0x02;
        const bool skipsIfNotTwo = compare[3] == 0x75;
        std::uint8_t* const ultrawideLoad = compare + 5;
        if (!comparesWithTwo || !skipsIfNotTwo || !IsRipLoad(ultrawideLoad))
            return std::nullopt;
        return Site{ skipJump, Game::RipTarget(wideLoad + loadLength), Game::RipTarget(ultrawideLoad + loadLength) };
    }

    std::vector<Site> FindSites()
    {
        const Game::Pattern testThenSkip{ 0x85, Game::any, 0x7E, Game::any, 0xF3, 0x0F, 0x10 };
        std::vector<Site> sites;
        for (std::uint8_t* const match : Game::FindAll(testThenSkip))
            if (const auto site = ReadSite(match + 2))
                sites.push_back(*site);
        return sites;
    }

    void RebuildExistingLayout()
    {
        constexpr std::int32_t noMode = -1;
        void* const manager = Renderer::layout.Manager();
        if (!manager)
            return;
        auto& active = Game::Field<std::int32_t>(manager, Renderer::layout.ActiveMode());
        const auto requested = Game::Field<std::int32_t>(manager, Renderer::layout.requestedMode);
        if (active == requested && requested > 0)
        {
            active = noMode;
            SPDLOG_INFO("Renderer already set up in mode {}; it rebuilds its layout on the next frame.", requested);
        }
    }

    bool Remove()
    {
        constexpr std::uint8_t jumpAlways = 0xEB;
        const std::vector<Site> sites = FindSites();
        const bool valid = sites.size() == expectedSites &&
                           std::ranges::all_of(sites, [](const Site& site) { return site.IsFitToPresetAspect(); });
        if (!valid)
        {
            SPDLOG_ERROR("Found {} aspect preset fit(s), expected {}.", sites.size(), expectedSites);
            return false;
        }
        const bool contentFound = std::ranges::any_of(sites, [](const Site& site) {
            return Renderer::FindContent(site.KeepSize());
        });
        if (!Renderer::FindManager() || !contentFound)
        {
            SPDLOG_ERROR("Render manager fields not found.");
            return false;
        }
        for (const Site& site : sites)
        {
            if (!Game::Write(site.skipJump, &jumpAlways, 1))
                return false;
            SPDLOG_INFO("Aspect preset fit at game+0x{:X} now keeps the screen size.", Game::Offset(site.skipJump));
        }
        RebuildExistingLayout();
        return true;
    }
}

namespace Hud
{
    using Rect = std::int32_t[4];

    std::int32_t guiSize = 0;
    std::atomic<std::int32_t> guiWidth = 0;
    std::atomic<std::int32_t> guiHeight = 0;
    std::atomic<std::int32_t> offsetX = 0;
    std::atomic<std::int32_t> offsetY = 0;
    SafetyHookMid effectViewportHook;
    SafetyHookMid scissorHook;
    std::vector<SafetyHookMid> layoutViewportHooks;

    Renderer::Size Fit16x9(Renderer::Size screen)
    {
        const std::int32_t width16x9 = screen.height * 16 / 9;
        const std::int32_t height16x9 = screen.width * 9 / 16;
        return { (std::min)(screen.width, width16x9), (std::min)(screen.height, height16x9) };
    }

    void UpdateGuiSize()
    {
        void* const manager = Renderer::layout.Manager();
        if (!manager)
            return;
        const Renderer::Size screen = Renderer::layout.Content();
        if (screen.width <= 0 || screen.height <= 0)
            return;
        const Renderer::Size gui = Fit16x9(screen);
        auto& stored = Game::Field<Renderer::Size>(manager, guiSize);
        if (stored != gui)
            stored = gui;
        if (gui != screen)
            Game::Field<float>(manager, Renderer::layout.aspect) = static_cast<float>(gui.width) / static_cast<float>(gui.height);
        guiWidth = gui.width;
        guiHeight = gui.height;
        offsetX = (screen.width - gui.width) / 2;
        offsetY = (screen.height - gui.height) / 2;
    }

    bool IsShifted()
    {
        return offsetX > 0 || offsetY > 0;
    }

    void Shift(Rect& rect)
    {
        rect[0] += offsetX;
        rect[1] += offsetY;
        rect[2] += offsetX;
        rect[3] += offsetY;
    }

    void OnLayoutViewport(SafetyHookContext& context)
    {
        UpdateGuiSize();
        auto& rect = *reinterpret_cast<Rect*>(context.rdx);
        if (IsShifted() && rect[0] == 0 && rect[1] == 0 && rect[2] == guiWidth && rect[3] == guiHeight)
            Shift(rect);
    }

    void OnScissor(SafetyHookContext& context)
    {
        const auto& viewport = *reinterpret_cast<const Rect*>(context.rbx + 0xF8);
        Rect scissor;
        std::memcpy(scissor, reinterpret_cast<const void*>(context.rdi + 0x6C), sizeof(scissor));
        if (!IsShifted() || viewport[0] != offsetX || viewport[1] != offsetY)
            return;
        Shift(scissor);
        auto& clipped = *reinterpret_cast<Rect*>(context.rsp + 0x40);
        clipped[0] = (std::max)(viewport[0], scissor[0]);
        clipped[1] = (std::max)(viewport[1], scissor[1]);
        clipped[2] = (std::min)(viewport[2], scissor[2]);
        clipped[3] = (std::min)(viewport[3], scissor[3]);
        context.rsi = static_cast<std::uint32_t>(clipped[0]);
        context.r10 = static_cast<std::uint32_t>(clipped[1]);
        context.r11 = static_cast<std::uint32_t>(clipped[2]);
        context.r9 = static_cast<std::uint32_t>(clipped[3]);
    }

    std::uint8_t* FindScissorClip()
    {
        const Game::Pattern clipScissorToViewport{
            0x44, 0x8B, 0x93, 0xFC, 0x00, 0x00, 0x00, 0x44, 0x8B, 0x9B, 0x00, 0x01, 0x00, 0x00,
            0x44, 0x8B, 0x8B, 0x04, 0x01, 0x00, 0x00, 0x8B, 0xB3, 0xF8, 0x00, 0x00, 0x00,
            0x39, 0x77, 0x6C, 0x0F, 0x4F, 0x77, 0x6C, 0x44, 0x39, 0x57, 0x70, 0x89, 0x74, 0x24, 0x40,
            0x44, 0x0F, 0x4F, 0x57, 0x70, 0x44, 0x39, 0x5F, 0x74, 0x44, 0x89, 0x54, 0x24, 0x44,
            0x44, 0x0F, 0x4C, 0x5F, 0x74, 0x44, 0x39, 0x4F, 0x78, 0x44, 0x89, 0x5C, 0x24, 0x48,
            0x44, 0x0F, 0x4C, 0x4F, 0x78, 0x44, 0x89, 0x4C, 0x24, 0x4C, 0x41, 0x3B, 0xF3 };
        constexpr std::size_t compareLength = 3;
        const auto found = Game::FindAll(clipScissorToViewport);
        return found.size() == 1 ? found.front() + clipScissorToViewport.size() - compareLength : nullptr;
    }

    void OnEffectViewport(SafetyHookContext& context)
    {
        auto& rect = *reinterpret_cast<Rect*>(context.rdx);
        const Renderer::Size screen = Renderer::layout.Content();
        if (IsShifted() && rect[0] == 0 && rect[1] == 0 && rect[2] == screen.width && rect[3] == screen.height)
            Shift(rect);
    }

    struct SizeRead
    {
        std::uint8_t* displacement;
        std::int32_t alternative;
    };

    bool IsQwordFieldRead(const std::uint8_t* bytes)
    {
        constexpr std::uint8_t rexW = 0x48;
        constexpr std::uint8_t rexWithoutIndex = 0xFA;
        constexpr std::uint8_t modDisp32 = 0x80;
        constexpr std::uint8_t sibBase = 0x04;
        return (bytes[0] & rexWithoutIndex) == rexW && bytes[1] == 0x8B && (bytes[2] & 0xC0) == modDisp32 &&
               (bytes[2] & 0x07) != sibBase;
    }

    std::optional<SizeRead> ReadAfterFlagTest(std::uint8_t* flagTestEnd)
    {
        constexpr std::size_t window = 0x30;
        constexpr std::size_t readLength = 7;
        if (!Game::IsCode(flagTestEnd, window * 2 + readLength))
            return std::nullopt;
        for (std::size_t first = 0; first < window; ++first)
        {
            std::uint8_t* const read = flagTestEnd + first;
            if (!IsQwordFieldRead(read) || Game::Read<std::int32_t>(read + 3) != Renderer::layout.contentSize)
                continue;
            for (std::size_t second = readLength; second < window; ++second)
            {
                std::uint8_t* const other = read + second;
                const auto field = Game::Read<std::int32_t>(other + 3);
                if (IsQwordFieldRead(other) && field != Renderer::layout.contentSize && field > 0 && field < 0x1000)
                    return SizeRead{ read + 3, field };
            }
            return std::nullopt;
        }
        return std::nullopt;
    }

    std::vector<SizeRead> FindSizeReads()
    {
        const Game::Pattern flag = Game::BytesOf(Renderer::layout.fixedLayoutFlag);
        const Game::Pattern compareWithZero = Game::Join({ { 0x80, Game::any }, flag, { 0x00 } });
        const Game::Pattern compareWithRegister = Game::Join({ { 0x38, Game::any }, flag });
        std::vector<SizeRead> reads;
        constexpr std::uint8_t modDisp32 = 0x80;
        constexpr std::uint8_t compareOpcode = 0x38;
        for (const auto& [pattern, length, needsCompareOpcode] :
             { std::tuple{ compareWithZero, 7, true }, std::tuple{ compareWithRegister, 6, false } })
            for (std::uint8_t* const match : Game::FindAll(pattern))
            {
                const std::uint8_t modrm = match[1];
                const bool isCompare = !needsCompareOpcode || (modrm & compareOpcode) == compareOpcode;
                if ((modrm & 0xC0) == modDisp32 && isCompare)
                    if (const auto read = ReadAfterFlagTest(match + length))
                        reads.push_back(*read);
            }
        return reads;
    }

    std::int32_t MostCommonAlternative(const std::vector<SizeRead>& reads)
    {
        std::map<std::int32_t, std::size_t> counts;
        for (const SizeRead& read : reads)
            ++counts[read.alternative];
        return std::ranges::max_element(counts, {}, [](const auto& entry) { return entry.second; })->first;
    }

    std::uint8_t* FindSetViewport()
    {
        const Game::Pattern compareViewport{ 0x8B, 0x41, 0x64, 0x48, 0x8B, 0xD9, 0x39, 0x42, 0x08, 0x75, Game::any,
                                             0x8B, 0x41, 0x60, 0x39, 0x42, 0x04 };
        const auto found = Game::FindAll(compareViewport);
        return found.size() == 1 ? Game::FunctionStart(found.front()) : nullptr;
    }

    std::vector<std::uint8_t*> CallsTo(std::uint8_t* target, std::uint8_t* from, std::size_t length, const Game::Pattern& before)
    {
        std::vector<std::uint8_t*> calls;
        const Game::Pattern call = Game::Join({ before, { 0xE8, Game::any, Game::any, Game::any, Game::any } });
        for (std::uint8_t* const match : Game::FindIn(from, length, call))
        {
            std::uint8_t* const instruction = match + before.size();
            if (Game::RelativeTarget(instruction + 5) == target)
                calls.push_back(instruction);
        }
        return calls;
    }

    bool HookViewports(const std::vector<SizeRead>& reads, std::uint8_t* setViewport)
    {
        constexpr std::size_t layoutWindow = 0x100;
        const Game::Pattern rectOnStack{ 0x48, 0x8D, 0x54, 0x24, Game::any };
        std::set<std::uint8_t*> layoutCalls;
        for (const SizeRead& read : reads)
            for (std::uint8_t* const call : CallsTo(setViewport, read.displacement, layoutWindow, rectOnStack))
                layoutCalls.insert(call);

        const Game::Pattern saveViewport{ 0x8B, 0x41, 0x5C, 0x89, 0x83, Game::any, Game::any, Game::any, Game::any,
                                          0x8B, 0x41, 0x60, 0x89, 0x83, Game::any, Game::any, Game::any, Game::any,
                                          0x8B, 0x41, 0x64, 0x89, 0x83, Game::any, Game::any, Game::any, Game::any,
                                          0x8B, 0x41, 0x68, 0x89, 0x83 };
        const auto saves = Game::FindAll(saveViewport);
        const auto effectCalls = saves.size() == 1 ? CallsTo(setViewport, saves.front(), 0x60, {}) : std::vector<std::uint8_t*>{};
        if (layoutCalls.empty() || effectCalls.size() != 1)
        {
            SPDLOG_ERROR("GUI viewport calls: {} layout, {} effect.", layoutCalls.size(), effectCalls.size());
            return false;
        }

        for (std::uint8_t* const call : layoutCalls)
        {
            auto hook = safetyhook::create_mid(call, OnLayoutViewport, safetyhook::MidHook::StartDisabled);
            if (!hook || !hook.enable())
                return false;
            SPDLOG_INFO("GUI layout viewport at game+0x{:X}.", Game::Offset(call));
            layoutViewportHooks.push_back(std::move(hook));
        }
        effectViewportHook = safetyhook::create_mid(effectCalls.front(), OnEffectViewport, safetyhook::MidHook::StartDisabled);
        if (!effectViewportHook || !effectViewportHook.enable())
            return false;
        SPDLOG_INFO("GUI effect viewport at game+0x{:X}.", Game::Offset(effectCalls.front()));
        return true;
    }

    bool Center()
    {
        constexpr std::size_t minimumReads = 40;
        if (!Renderer::FindLayoutFlag())
        {
            SPDLOG_ERROR("GUI layout flag not found.");
            return false;
        }
        std::vector<SizeRead> reads = FindSizeReads();
        if (reads.size() < minimumReads)
        {
            SPDLOG_ERROR("Found {} GUI size reads.", reads.size());
            return false;
        }
        guiSize = MostCommonAlternative(reads);
        std::erase_if(reads, [](const SizeRead& read) { return read.alternative != guiSize; });
        std::uint8_t* const setViewport = FindSetViewport();
        std::uint8_t* const scissorClip = FindScissorClip();
        if (!scissorClip)
        {
            SPDLOG_ERROR("GUI scissor clip not found.");
            return false;
        }
        if (!setViewport || !HookViewports(reads, setViewport))
            return false;
        scissorHook = safetyhook::create_mid(scissorClip, OnScissor, safetyhook::MidHook::StartDisabled);
        if (!scissorHook || !scissorHook.enable())
            return false;
        SPDLOG_INFO("GUI scissor clip at game+0x{:X}.", Game::Offset(scissorClip));
        UpdateGuiSize();
        for (const SizeRead& read : reads)
            if (!Game::Write(read.displacement, &guiSize, sizeof(guiSize)))
                return false;
        SPDLOG_INFO("{} GUI size reads use +0x{:X} instead of the screen size +0x{:X}.", reads.size(), guiSize,
                    Renderer::layout.contentSize);
        return true;
    }
}

std::filesystem::path ModulePath(HMODULE module)
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(module, path, MAX_PATH);
    return path;
}

void ReportUnsupported()
{
    MessageBoxA(nullptr, "Unsupported game version. See the log.", sFixName.c_str(), MB_OK | MB_ICONERROR);
}

bool Install()
{
    if (!Letterbox::Remove())
        return false;
    if (Config::centerHud && !Hud::Center())
        SPDLOG_WARN("HUD stays at the game's own position.");
    return true;
}

DWORD WINAPI InstallWhenCodeIsReady(void*)
{
    constexpr int attempts = 100;
    constexpr DWORD retryDelayMs = 100;
    for (int attempt = 0; attempt < attempts && Letterbox::FindSites().size() != Letterbox::expectedSites;
         ++attempt, Sleep(retryDelayMs))
        Game::Load();
    if (!Install())
        ReportUnsupported();
    return 0;
}

void OpenLog(const std::filesystem::path& file)
{
    try
    {
        spdlog::set_default_logger(spdlog::basic_logger_mt(sFixName, file.string(), true));
        spdlog::flush_on(spdlog::level::info);
    }
    catch (const spdlog::spdlog_ex& error)
    {
        OutputDebugStringA(error.what());
    }
}

}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;
    DisableThreadLibraryCalls(module);
    const auto folder = ModulePath(module).parent_path();
    OpenLog(folder / (sFixName + ".log"));
    std::error_code error;
    const auto executable = ModulePath(nullptr);
    SPDLOG_INFO("{} v{}.{}.{}, {} ({} bytes).", sFixName, VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH,
                executable.filename().string(), std::filesystem::file_size(executable, error));
    Config::Load(folder / (sFixName + ".ini"));
    Game::Load();
    if (Letterbox::FindSites().size() == Letterbox::expectedSites)
    {
        if (!Install())
            ReportUnsupported();
    }
    else if (const HANDLE thread = CreateThread(nullptr, 0, InstallWhenCodeIsReady, nullptr, 0, nullptr))
        CloseHandle(thread);
    return TRUE;
}
