set_project("codsp-mod")
set_xmakever("2.8.1")

add_rules("mode.debug", "mode.release")
set_languages("cxx17")

-- Call of Duty single-player executables (iw6sp64_ship.exe, s1_sp64_ship.exe)
-- are 64-bit. One d3d11.dll serves both and auto-detects which it's in.
if is_arch("x86") then
    raise("codsp-mod must be built x64. Run:  xmake f -a x64")
end

-- Dependencies (pulled from xrepo). The imgui package builds the Win32 + DX11
-- backends for us and installs their headers under backends/.
add_requires("imgui v1.90.9", {configs = {dx11 = true, win32 = true}})
add_requires("minhook")

-- ON  -> builds "d3d11.dll": drop next to the game exe, no injector needed.
-- OFF -> builds "codsp-mod.dll": load it with a DLL injector instead.
--        (toggle with:  xmake f --proxy=n)
option("proxy")
    set_default(true)
    set_showmenu(true)
    set_description("Build as a self-loading d3d11.dll proxy")
option_end()

target("codsp-mod")
    set_kind("shared")

    add_files(
        "src/dllmain.cpp",
        "src/renderer.cpp",
        "src/console.cpp",
        "src/features.cpp",
        "src/dvar.cpp",
        "src/signature.cpp",
        "src/splash.cpp",
        "src/uidump.cpp",
        "src/game.cpp")

    add_includedirs("src")
    add_packages("imgui", "minhook")
    add_syslinks("d3d11", "dxgi", "d3dcompiler", "user32", "gdi32")
    add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX")

    if has_config("proxy") then
        add_files("src/d3d11_proxy.cpp")
        set_basename("d3d11")
        add_shflags("/DEF:" .. path.join(os.scriptdir(), "src", "d3d11_proxy.def"), {force = true})
    else
        set_basename("codsp-mod")
    end
target_end()
