-- Illuminated - Core - SKSE plugin. GPL-3.0-or-later, see LICENSE.
-- One plugin that holds the code of every Illuminated mod; each mod's own download carries only its data.
set_xmakever("3.0.0")
set_project("Illuminated")
set_version("1.0.0")
set_license("GPL-3.0-or-later")
set_arch("x64")
set_languages("c++23")
-- the DLL carries its own Visual C++ runtime, so an older runtime on a player's PC cannot stop it loading
set_runtimes("MT")
add_rules("mode.releasedbg")
set_defaultmode("releasedbg")

-- GLOW_VR=1 (set by PC Runner\pluginbuild.py) builds for Skyrim VR only; anything else is the SE + AE build
local vr_only = os.getenv("GLOW_VR") == "1"
set_config("skyrim_se", not vr_only)
set_config("skyrim_ae", not vr_only)
set_config("skyrim_vr", vr_only)

-- REPRODUCIBLE BUILDS: the same source gives the same DLL, byte for byte, in whatever folder it is built. MSVC only.
add_cxflags("/Brepro", "/experimental:deterministic", "/pathmap:" .. os.projectdir() .. "=.", { force = true, tools = "cl" })
add_shflags("/Brepro", "/PDBALTPATH:%_PDB%", { force = true, tools = "link" })

includes("lib/commonlibsse-ng")

-- a data mod's manifest is JSON (MIT-licensed library; the version CommonLib pins for its own option)
add_requires("nlohmann_json v3.12.0")

target("Illuminated", function()
    add_deps("commonlibsse-ng")
    add_packages("nlohmann_json")
    add_defines("NOMINMAX")  -- windows.h min/max macros break std::min/max and numeric_limits::max
    add_rules("commonlibsse-ng.plugin", {
        name = "Illuminated",
        author = "izzydoingit",
        description = "Illuminated - Core - the code of every Illuminated mod in one plugin",
    })
    add_files("src/*.cpp")
    add_headerfiles("src/*.h")
    add_includedirs("src")
    -- GLOW_FADE_DEBUG=1 (set by PC Runner\pluginbuild.py for a testing build) adds the kernel's test part (src/Probe.cpp): a DevBench
    -- tool that tries the engine routes on the game's own lights. It is in no shipped DLL.
    if os.getenv("GLOW_FADE_DEBUG") == "1" then
        add_defines("CORE_PROBE")
    end
    set_pcxxheader("src/PCH.h")
    -- every warning in OUR files is an error
    set_warnings("allextra", "error")
end)

-- the game-free parts (src/ManifestText.h, src/SwitchesText.h and the shared check sets), tested without the game:
-- xmake build testcore && xmake run testcore (PC Runner\plugin build.py builds and runs it, then again under clang's checkers)
target("testcore", function()
    set_kind("binary")
    set_default(false)
    add_packages("nlohmann_json")
    add_files("tests/testcore.cpp")
    add_includedirs("src")
    set_warnings("allextra", "error")
end)
