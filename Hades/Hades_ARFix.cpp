#include "stdafx.h"
#include "version.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <safetyhook.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

static_assert(sizeof(void*) == 8, "Build for Windows x64.");

namespace
{

namespace Engine
{
    using Bytes = std::span<std::uint8_t>;
    using Pattern = std::vector<int>;

    constexpr int any = -1;
    constexpr const wchar_t* moduleNames[] = { L"EngineWin64s.dll", L"EngineWin64sv.dll" };

    Bytes image;
    std::vector<Bytes> code;

    bool Load()
    {
        for (const wchar_t* name : moduleNames)
            if (const auto base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(name)))
            {
                const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
                const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
                image = { base, nt->OptionalHeader.SizeOfImage };
                code.clear();
                for (const auto& section : std::span(IMAGE_FIRST_SECTION(nt), nt->FileHeader.NumberOfSections))
                    if (section.Characteristics & IMAGE_SCN_MEM_EXECUTE)
                        code.push_back({ base + section.VirtualAddress, section.Misc.VirtualSize });
                return true;
            }
        return false;
    }

    std::vector<std::uint8_t*> FindAll(const Pattern& pattern)
    {
        const auto isSame = [](std::uint8_t actual, int wanted) { return wanted == any || wanted == actual; };
        std::vector<std::uint8_t*> found;
        for (const Bytes area : code)
        {
            const auto next = [&](auto from) {
                return std::search(from, area.end(), pattern.begin(), pattern.end(), isSame);
            };
            for (auto match = next(area.begin()); match != area.end(); match = next(match + 1))
                found.push_back(&*match);
        }
        return found;
    }

    std::uint8_t* FindOne(const Pattern& pattern, std::string_view what)
    {
        const auto found = FindAll(pattern);
        if (found.size() == 1)
            return found[0];
        SPDLOG_ERROR("{}: {} matches, expected 1.", what, found.size());
        return nullptr;
    }

    template <typename Value>
    Value Read(const void* address)
    {
        Value value;
        std::memcpy(&value, address, sizeof(value));
        return value;
    }

    std::uint8_t* RipTarget(std::uint8_t* instructionEnd, std::size_t trailingBytes = 0)
    {
        const auto displacement = instructionEnd - trailingBytes - sizeof(std::int32_t);
        return instructionEnd + Read<std::int32_t>(displacement);
    }

    bool Contains(const void* address, std::size_t size)
    {
        const auto byte = static_cast<const std::uint8_t*>(address);
        return byte >= image.data() && byte + size <= image.data() + image.size();
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

    void Write(std::uint8_t* address, const void* bytes, std::size_t size)
    {
        DWORD protection = 0;
        if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &protection))
            return;
        std::memcpy(address, bytes, size);
        VirtualProtect(address, size, protection, &protection);
        FlushInstructionCache(GetCurrentProcess(), address, size);
    }

    template <typename Function>
    bool Hook(SafetyHookInline& hook, std::uint8_t* target, Function detour, std::string_view what)
    {
        if (!target)
            return false;
        hook = safetyhook::create_inline(target, detour, safetyhook::InlineHook::StartDisabled);
        if (!hook || !hook.enable())
        {
            SPDLOG_ERROR("Could not hook {}.", what);
            return false;
        }
        SPDLOG_INFO("{} at +0x{:X}.", what, Offset(target));
        return true;
    }
}

namespace Config
{
    double hudAspect = 0;

    void Load(const std::filesystem::path& ini)
    {
        const int width = GetPrivateProfileIntW(L"HUD", L"Width", 0, ini.c_str());
        const int height = GetPrivateProfileIntW(L"HUD", L"Height", 0, ini.c_str());
        hudAspect = width > 0 && height > 0 ? static_cast<double>(width) / height : 0;
        SPDLOG_INFO("HUD aspect ratio {}.", hudAspect > 0 ? std::format("{}:{}", width, height) : "automatic");
    }
}

struct Size
{
    int width;
    int height;
};

namespace VirtualScreen
{
    constexpr Size base{ 1920, 1080 };
    constexpr std::size_t storeLength = 10;
    constexpr std::size_t immediateLength = 4;

    struct SizeStore
    {
        std::uint8_t* code;
        std::int32_t* width;
        std::int32_t* height;
    };

    std::int32_t* width = nullptr;
    std::int32_t* height = nullptr;
    std::optional<SizeStore> camera;
    SafetyHookInline resizeHook;
    SafetyHookMid cursorHook;

    Size Current()
    {
        return { *width, *height };
    }

    Size For(int screenWidth, int screenHeight)
    {
        if (screenWidth <= 0 || screenHeight <= 0)
            return base;
        const double aspect = static_cast<double>(screenWidth) / screenHeight;
        if (aspect >= static_cast<double>(base.width) / base.height)
            return { static_cast<int>(std::lround(base.height * aspect)), base.height };
        return { base.width, static_cast<int>(std::lround(base.width / aspect)) };
    }

    void ResizeCamera(Size size)
    {
        Engine::Write(camera->code + storeLength - immediateLength, &size.width, sizeof(size.width));
        Engine::Write(camera->code + 2 * storeLength - immediateLength, &size.height, sizeof(size.height));
        if (*camera->width > 0)
        {
            *camera->width = size.width;
            *camera->height = size.height;
        }
    }

    void Resize(int screenWidth, int screenHeight, bool keepAspect)
    {
        const Size size = For(screenWidth, screenHeight);
        *width = size.width;
        *height = size.height;
        if (camera)
            ResizeCamera(size);
        SPDLOG_INFO("Screen {}x{}: virtual screen {}x{}.", screenWidth, screenHeight, size.width, size.height);
        resizeHook.call(screenWidth, screenHeight, keepAspect);
    }

    std::vector<SizeStore> FindBaseSizeStores()
    {
        const Engine::Pattern storeBaseSize{
            0xC7, 0x05, Engine::any, Engine::any, Engine::any, Engine::any, 0x80, 0x07, 0x00, 0x00,
            0xC7, 0x05, Engine::any, Engine::any, Engine::any, Engine::any, 0x38, 0x04, 0x00, 0x00,
        };
        std::vector<SizeStore> stores;
        for (std::uint8_t* const store : Engine::FindAll(storeBaseSize))
            stores.push_back({ store,
                               reinterpret_cast<std::int32_t*>(Engine::RipTarget(store + storeLength, immediateLength)),
                               reinterpret_cast<std::int32_t*>(Engine::RipTarget(store + 2 * storeLength, immediateLength)) });
        return stores;
    }

    bool StartsRectangleAtZero(const SizeStore& store)
    {
        std::uint8_t* const previous = store.code - storeLength;
        return store.height == store.width + 1 && previous[0] == 0xC7 && previous[1] == 0x05 &&
               Engine::Read<std::int32_t>(previous + storeLength - immediateLength) == 0 &&
               Engine::RipTarget(previous + storeLength, immediateLength) ==
                   reinterpret_cast<std::uint8_t*>(store.width - 1);
    }

    std::optional<std::uint8_t*> LoadTarget(std::span<std::uint8_t> bytes, std::uint8_t modrm)
    {
        constexpr std::size_t loadLength = 8;
        if (bytes.size() < loadLength || bytes[0] != 0x66 || bytes[1] != 0x0F || bytes[2] != 0x6E || bytes[3] != modrm)
            return std::nullopt;
        return Engine::RipTarget(bytes.data() + loadLength);
    }

    bool DividesWidthByHeight(std::span<std::uint8_t> window, const SizeStore& store)
    {
        constexpr std::uint8_t loadXmm0 = 0x05;
        constexpr std::uint8_t divideXmm1ByXmm0[] = { 0xF3, 0x0F, 0x5E, 0xC8 };
        bool loadsHeight = false;
        for (std::size_t i = 0; i < window.size(); ++i)
            loadsHeight = loadsHeight ||
                          LoadTarget(window.subspan(i), loadXmm0) == reinterpret_cast<std::uint8_t*>(store.height);
        return loadsHeight && std::ranges::search(window, divideXmm1ByXmm0).begin() != window.end();
    }

    std::uint8_t* FindResize(const std::vector<SizeStore>& stores)
    {
        constexpr std::uint8_t loadXmm1 = 0x0D;
        constexpr std::size_t searchLength = 0x60;
        std::vector<std::pair<std::uint8_t*, const SizeStore*>> found;
        for (std::uint8_t* const load : Engine::FindAll({ 0x66, 0x0F, 0x6E, loadXmm1 }))
        {
            if (!Engine::Contains(load, searchLength))
                continue;
            const auto window = std::span(load, searchLength);
            const auto loaded = LoadTarget(window, loadXmm1);
            const auto store = std::ranges::find_if(
                stores, [&](const SizeStore& s) { return loaded == reinterpret_cast<std::uint8_t*>(s.width); });
            std::uint8_t* const start = Engine::FunctionStart(load);
            if (store == stores.end() || !start || !DividesWidthByHeight(window, *store))
                continue;
            if (std::ranges::none_of(found, [&](const auto& f) { return f.first == start; }))
                found.push_back({ start, &*store });
        }
        if (found.size() != 1)
        {
            SPDLOG_ERROR("Resolution resize function: {} candidates, expected 1.", found.size());
            return nullptr;
        }
        width = found[0].second->width;
        height = found[0].second->height;
        return found[0].first;
    }

    std::optional<SizeStore> FindCamera(const std::vector<SizeStore>& stores)
    {
        std::vector<SizeStore> found;
        for (const SizeStore& store : stores)
            if (store.width != width && StartsRectangleAtZero(store))
                found.push_back(store);
        if (found.size() != 1)
        {
            SPDLOG_WARN("Camera rectangle: {} candidates, expected 1. Screen edges may be culled.", found.size());
            return std::nullopt;
        }
        return found[0];
    }

    float Recentred(float offset, float baseHalf, int virtualSize)
    {
        const float half = virtualSize / 2.0f;
        return (offset + baseHalf - half) * baseHalf / half;
    }

    void RecentreCursor(safetyhook::Context& context)
    {
        auto& x = *reinterpret_cast<float*>(&context.xmm1);
        auto& y = *reinterpret_cast<float*>(&context.xmm0);
        x = Recentred(x, base.width / 2.0f, *width);
        y = Recentred(y, base.height / 2.0f, *height);
    }

    std::uint8_t* FindCursorScale()
    {
        constexpr std::size_t instructionLength = 8;
        const Engine::Pattern centreAndScale{
            0xF3, 0x0F, 0x5C, 0x0D, Engine::any, Engine::any, Engine::any, Engine::any,
            0xF3, 0x0F, 0x5C, 0x05, Engine::any, Engine::any, Engine::any, Engine::any,
            0xF3, 0x0F, 0x5E, 0x0D, Engine::any, Engine::any, Engine::any, Engine::any,
            0xF3, 0x0F, 0x5E, 0x05, Engine::any, Engine::any, Engine::any, Engine::any,
        };
        std::uint8_t* const match = Engine::FindOne(centreAndScale, "Virtual cursor offset");
        if (!match)
            return nullptr;
        const auto* const centre = reinterpret_cast<const float*>(Engine::RipTarget(match + instructionLength));
        const bool isBaseCentre = Engine::Contains(centre, 2 * sizeof(float)) &&
                                  centre[0] == base.width / 2.0f && centre[1] == base.height / 2.0f;
        return isBaseCentre ? match + 2 * instructionLength : nullptr;
    }

    void HookCursor()
    {
        std::uint8_t* const scale = FindCursorScale();
        if (scale)
            cursorHook = safetyhook::create_mid(scale, RecentreCursor, safetyhook::MidHook::StartDisabled);
        if (!cursorHook || !cursorHook.enable())
            SPDLOG_WARN("Mouse camera lead stays centred on the 16:9 area.");
    }

    bool Install()
    {
        const auto stores = FindBaseSizeStores();
        if (!Engine::Hook(resizeHook, FindResize(stores), Resize, "Resolution resize"))
            return false;
        camera = FindCamera(stores);
        HookCursor();
        return true;
    }
}

namespace HudScript
{
    constexpr std::string_view script = "UIData";
    SafetyHookInline loadHook;

    struct Box
    {
        Size size;
        int marginX;
        int marginY;
    };

    double AutomaticAspect(double screenAspect)
    {
        constexpr double fullScreenUpTo = 12.0 / 5.0;
        constexpr double singleMonitorUpTo = 4.0;
        constexpr double wide = 64.0 / 27.0;
        constexpr double standard = 16.0 / 9.0;
        if (screenAspect < standard)
            return standard;
        if (screenAspect <= fullScreenUpTo)
            return screenAspect;
        return screenAspect <= singleMonitorUpTo ? wide : standard;
    }

    Box HudBox(Size screen)
    {
        const double screenAspect = static_cast<double>(screen.width) / screen.height;
        const double aspect = Config::hudAspect > 0 ? Config::hudAspect : AutomaticAspect(screenAspect);
        const int boxWidth = aspect < screenAspect ? static_cast<int>(std::lround(screen.height * aspect)) : screen.width;
        const int boxHeight = aspect > screenAspect ? static_cast<int>(std::lround(screen.width / aspect)) : screen.height;
        return { { boxWidth, boxHeight }, (screen.width - boxWidth) / 2, (screen.height - boxHeight) / 2 };
    }

    std::string WithScreenSize(std::string_view source, Size size)
    {
        const std::pair<std::string_view, int> values[] = {
            { "ScreenWidth", size.width },
            { "ScreenHeight", size.height },
            { "ScreenCenterX", size.width / 2 },
            { "ScreenCenterY", size.height / 2 },
        };
        std::string result;
        std::size_t lineStart = 0;
        while (lineStart < source.size())
        {
            const std::size_t lineEnd = std::min(source.find('\n', lineStart), source.size());
            std::string_view line = source.substr(lineStart, lineEnd - lineStart);
            for (const auto& [name, value] : values)
            {
                const auto rest = line.substr(std::min(line.size(), name.size()));
                const auto assignment = rest.find_first_not_of(" \t");
                if (line.starts_with(name) && assignment != std::string_view::npos && rest[assignment] == '=')
                {
                    result += std::format("{} = {}{}", name, value, line.ends_with('\r') ? "\r" : "");                    line = {};
                    break;
                }
            }
            result += line;
            if (lineEnd < source.size())
                result += '\n';
            lineStart = lineEnd + 1;
        }
        return result;
    }

    std::string ScreenPlacement(const Box& box, Size screen)
    {
        constexpr Size dimSize{ 4800, 2700 };
        const double dimGrowth = std::max({ 1.0, static_cast<double>(screen.width) / dimSize.width,
                                            static_cast<double>(screen.height) / dimSize.height });
        return std::format(R"(
do
  local marginX, marginY = {}, {}
  local vignetteScaleX, vignetteScaleY, dimGrowth = {}, {}, {}
  local screenIds, dimIds = {{}}, {{}}
  local function shift(args)
    args.OffsetX = (args.OffsetX or 0) + marginX
    args.OffsetY = (args.OffsetY or 0) + marginY
  end
  local function isScreenTarget(args)
    return args ~= nil and args.DestinationId == nil and args.Id ~= nil and screenIds[args.Id]
  end
  local spawnObstacle = SpawnObstacle
  SpawnObstacle = function(args)
    if args == nil or args.DestinationId ~= nil or args.LocationX ~= nil or args.LocationY ~= nil
       or (args.OffsetX == nil and args.OffsetY == nil) then
      return spawnObstacle(args)
    end
    shift(args)
    local id = spawnObstacle(args)
    if id ~= nil then
      screenIds[id] = true
      dimIds[id] = args.Name == "rectangle01" or nil
    end
    return id
  end
  local teleport = Teleport
  Teleport = function(args)
    if isScreenTarget(args) then shift(args) end
    return teleport(args)
  end
  local move = Move
  Move = function(args)
    if isScreenTarget(args) and args.OffsetX ~= nil and args.OffsetY ~= nil then shift(args) end
    return move(args)
  end
  local createAnimation = CreateAnimation
  CreateAnimation = function(args)
    if args ~= nil and args.UseScreenLocation then shift(args) end
    return createAnimation(args)
  end
  local setAnimation = SetAnimation
  SetAnimation = function(args)
    local result = setAnimation(args)
    if args ~= nil and args.Name == "VignetteOverlay" and args.DestinationId ~= nil then
      SetScaleX({{ Id = args.DestinationId, Fraction = vignetteScaleX }})
      SetScaleY({{ Id = args.DestinationId, Fraction = vignetteScaleY }})
    end
    return result
  end
  local setScale = SetScale
  SetScale = function(args)
    if args ~= nil and args.Id ~= nil and dimIds[args.Id] and (args.Fraction or 0) >= 10 then
      args.Fraction = args.Fraction * dimGrowth
    end
    return setScale(args)
  end
end
)",
                           box.marginX, box.marginY, static_cast<double>(screen.width) / VirtualScreen::base.width,
                           static_cast<double>(screen.height) / VirtualScreen::base.height, dimGrowth);
    }

    int Load(void* state, const char* buffer, std::size_t size, const char* name, const char* mode)
    {
        if (!name || name != script || !VirtualScreen::width)
            return loadHook.call<int>(state, buffer, size, name, mode);
        const Size screen = VirtualScreen::Current();
        const Box box = HudBox(screen);
        const std::string source = WithScreenSize({ buffer, size }, box.size) + ScreenPlacement(box, screen);
        SPDLOG_INFO("HUD area {}x{} at {},{}.", box.size.width, box.size.height, box.marginX, box.marginY);
        return loadHook.call<int>(state, source.data(), source.size(), name, mode);
    }

    bool Install()
    {
        const Engine::Pattern loadBuffer{
            0x48, 0x83, 0xEC, 0x48,
            0x48, 0x8B, 0x44, 0x24, 0x70,
            0x48, 0x89, 0x54, 0x24, 0x30,
            0x48, 0x8D, 0x15, Engine::any, Engine::any, Engine::any, Engine::any,
            0x4C, 0x89, 0x44, 0x24, 0x38,
            0x4C, 0x8D, 0x44, 0x24, 0x30,
            0x48, 0x89, 0x44, 0x24, 0x20,
            0xE8,
        };
        return Engine::Hook(loadHook, Engine::FindOne(loadBuffer, "Lua buffer loader"), Load, "Lua buffer loader");
    }
}

namespace MenuLayout
{
    struct Location
    {
        float x;
        float y;

        bool operator==(const Location&) const = default;
    };

    using SetLocation = void (*)(std::uint8_t* component, const Location* location);

    struct ComponentLayout
    {
        std::uint32_t owner;
        std::uint8_t location;
        std::uint8_t worldSpace;
        std::uint32_t setLocationSlot;
    };

    ComponentLayout layout{};
    SafetyHookInline setupHook;
    std::unordered_map<const std::uint8_t*, Location> centred;

    void Centre(std::uint8_t* component)
    {
        if (*reinterpret_cast<void**>(component + layout.owner) || component[layout.worldSpace])
            return;
        const Location current = *reinterpret_cast<const Location*>(component + layout.location);
        if (const auto previous = centred.find(component); previous != centred.end() && previous->second == current)
            return;
        const Size screen = VirtualScreen::Current();
        const Location moved{ current.x + (screen.width - VirtualScreen::base.width) / 2.0f,
                              current.y + (screen.height - VirtualScreen::base.height) / 2.0f };
        const auto vtable = *reinterpret_cast<std::uint8_t**>(component);
        Engine::Read<SetLocation>(vtable + layout.setLocationSlot)(component, &moved);
        centred[component] = moved;
    }

    void Setup(std::uint8_t* component, void* data)
    {
        setupHook.call(component, data);
        Centre(component);
    }

    std::optional<std::uint32_t> FindSetLocationSlot(std::uint8_t* setup)
    {
        constexpr std::size_t searchLength = 0x400;
        constexpr std::uint8_t passLocationAndCall[] = { 0x48, 0x8D, 0x55, 0x00, 0x48, 0x8B, 0xCF, 0xFF, 0x90 };
        std::vector<std::uint32_t> slots;
        for (std::uint8_t* at = setup; at + sizeof(passLocationAndCall) + 4 <= setup + searchLength; ++at)
            if (std::equal(at, at + 3, passLocationAndCall) && std::equal(at + 4, at + 9, passLocationAndCall + 4))
                slots.push_back(Engine::Read<std::uint32_t>(at + 9));
        if (slots.empty() || std::ranges::any_of(slots, [&](std::uint32_t slot) { return slot != slots[0]; }))
        {
            SPDLOG_ERROR("GUI SetLocation slot: {} calls, expected matching slots.", slots.size());
            return std::nullopt;
        }
        return slots[0];
    }

    bool FindLayout(std::uint8_t* setup)
    {
        const Engine::Pattern screenLocation{
            0x48, 0x83, 0xB9, Engine::any, Engine::any, Engine::any, Engine::any, 0x00,
            0x48, 0x8B, 0xDA,
            0x48, 0x8B, 0xF9,
            0x74, Engine::any,
            0xE8, Engine::any, Engine::any, Engine::any, Engine::any,
            0xEB, Engine::any,
            0xF2, 0x0F, 0x10, 0x41, Engine::any,
            0xF2, 0x0F, 0x11, 0x02,
            0x80, 0x7F, Engine::any, 0x00,
        };
        std::uint8_t* const match = Engine::FindOne(screenLocation, "GUI screen location");
        const auto slot = FindSetLocationSlot(setup);
        if (!match || !slot)
            return false;
        layout = { Engine::Read<std::uint32_t>(match + 3), match[27], match[34], *slot };
        return true;
    }

    bool Install()
    {
        const Engine::Pattern setupStart{
            0x48, 0x8B, 0xF2,
            0x48, 0x8B, 0xF9,
            0x48, 0x8B, 0x01,
            0xFF, 0x90, Engine::any, Engine::any, Engine::any, Engine::any,
            0x48, 0x8B, 0xD6,
            0x48, 0x8B, 0xCF,
            0xE8,
        };
        std::uint8_t* const match = Engine::FindOne(setupStart, "GUI component setup");
        std::uint8_t* const setup = match ? Engine::FunctionStart(match) : nullptr;
        return setup && FindLayout(setup) && Engine::Hook(setupHook, setup, Setup, "GUI component setup");
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

void Install()
{
    if (!VirtualScreen::Install())
    {
        ReportUnsupported();
        return;
    }
    if (!HudScript::Install())
        SPDLOG_WARN("HUD stays in the 16:9 layout.");
    if (!MenuLayout::Install())
        SPDLOG_WARN("Engine menus stay in the top-left 16:9 area.");
}

DWORD WINAPI InstallWhenEngineIsLoaded(void*)
{
    constexpr int attempts = 300;
    constexpr DWORD retryDelayMs = 10;
    for (int attempt = 0; attempt < attempts; ++attempt, Sleep(retryDelayMs))
        if (Engine::Load())
        {
            Install();
            return 0;
        }
    SPDLOG_ERROR("Engine module not loaded.");
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
    SPDLOG_INFO("{} v{}.{}.{}.", sFixName, VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
    Config::Load(folder / (sFixName + ".ini"));
    if (Engine::Load())
        Install();
    else if (const HANDLE thread = CreateThread(nullptr, 0, InstallWhenEngineIsLoaded, nullptr, 0, nullptr))
        CloseHandle(thread);
    return TRUE;
}
