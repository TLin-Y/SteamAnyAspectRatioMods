set_project("AnyAspectRatioBuilder")
add_rules("mode.debug", "mode.release")
set_languages("cxxlatest", "clatest")
set_optimize("fastest")

if is_mode("release") then
    set_symbols("hidden")
    set_strip("all")
else
    set_symbols("debug")
    set_strip("none")
end

target("zydis")
    set_kind("static")
    set_policy("build.fence", true)

    before_build(function ()
        local arch = is_arch("x64") and "x64" or "Win32"
        local builddir = ".build_zydis/build"
        local hashfile = path.join(builddir, ".zydis_build_hash")
        local zydisdir = "external/zydis"

        if not os.isdir(zydisdir) then
            raise("Missing external dependencies. Run setup-dependencies.ps1 first.")
        end

        local zy_hash = os.iorun("git -C " .. zydisdir .. " rev-parse HEAD"):gsub("%s+$", "")
        if not zy_hash or #zy_hash == 0 then
            raise("Could not determine Zydis git hash. Re-run setup-dependencies.ps1.")
        end

        local old_hash = nil
        if os.isfile(hashfile) then
            local file = io.open(hashfile, "r")
            if file then
                old_hash = file:read("*l")
                file:close()
            end
        end

        local function build_config(configuration)
            cprint("[Zydis] configuring (%s)...", configuration)
            os.exec("cmake -S " .. zydisdir .. " -B " .. builddir ..
                " -A " .. arch .. " -DZYDIS_BUILD_SHARED_LIB=OFF")
            cprint("[Zydis] building (%s)...", configuration)
            os.exec("cmake --build " .. builddir .. " --config " .. configuration)
        end

        local rebuild = old_hash ~= zy_hash
        if rebuild or not os.isfile(path.join(builddir, "Release/Zydis.lib")) then
            build_config("Release")
            local file = io.open(hashfile, "w")
            if not file then
                raise("Could not write Zydis build hash.")
            end
            file:write(zy_hash)
            file:close()
        else
            cprint("[Zydis] Release build is up to date.")
        end

        if os.getenv("MGD_RELEASE_BUILD") ~= "true" then
            if rebuild or not os.isfile(path.join(builddir, "Debug/Zydis.lib")) then
                build_config("Debug")
            else
                cprint("[Zydis] Debug build is up to date.")
            end
        else
            cprint("[Zydis] Skipping Debug build.")
        end
    end)

    on_load(function (target)
        local builddir = ".build_zydis/build"
        local configuration = is_mode("debug") and "Debug" or "Release"

        target:add("includedirs",
            "external/zydis/include",
            "external/zydis/dependencies/zycore/include",
            {public = true})
        target:add("links",
            path.join(builddir, configuration, "Zydis.lib"),
            path.join(builddir, "zycore", configuration, "Zycore.lib"),
            {public = true})
        target:add("defines", "ZYDIS_STATIC_BUILD", "ZYCORE_STATIC_BUILD", {public = true})
    end)

target("AnyAspectRatioBuilder")
    set_kind("shared")
    set_prefixname("")
    set_extension(".asi")

    add_includedirs(".", "external/spdlog/include", "external/inipp",
        "external/safetyhook/include")
    add_headerfiles("*.h", "*.hpp")
    add_files("dllmain.cpp", "version.rc", "external/safetyhook/src/**.cpp")
    add_deps("zydis")
    add_syslinks("user32")
    on_load(function (target)
        local config = os.iorun('powershell -NoProfile -Command "Get-Content -Raw -LiteralPath build_config.json"')
        local project_name = config:match('"project_name"%s*:%s*"([%w_%-]+)"')
        local version_major = config:match('"major"%s*:%s*(%d+)')
        local version_minor = config:match('"minor"%s*:%s*(%d+)')
        local version_patch = config:match('"patch"%s*:%s*(%d+)')

        target:set("basename", project_name)
        target:add("defines",
            'FIX_NAME="' .. project_name .. '"',
            "VERSION_MAJOR=" .. version_major,
            "VERSION_MINOR=" .. version_minor,
            "VERSION_PATCH=" .. version_patch)
    end)

    if is_plat("windows") then
        set_toolchains("msvc")
        if is_mode("release") then
            set_runtimes("MT")
            add_cxflags("/utf-8")
            add_ldflags("/LTCG", "/GL", "/OPT:REF", "/OPT:ICF")
        else
            set_runtimes("MDd")
            add_cxflags("/utf-8", "/Zi")
            add_defines("_DEBUG")
        end
    end
