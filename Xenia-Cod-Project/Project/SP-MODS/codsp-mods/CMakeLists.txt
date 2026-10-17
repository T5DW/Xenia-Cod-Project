cmake_minimum_required(VERSION 3.20)
project(codsp_mod LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The CoD single-player executables (iw6sp64_ship.exe, s1_sp64_ship.exe) are
# 64-bit, so the mod DLL must be x64. Configure with:  cmake -B build -A x64
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Build must target x64. Configure with:  cmake -B build -A x64")
endif()

include(FetchContent)

# ---- MinHook (function hooking) --------------------------------------------
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(minhook)

# ---- Dear ImGui (has no CMakeLists, so populate + build the sources here) ---
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.90.9
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

# ---- The mod DLL -----------------------------------------------------------
add_library(codsp_mod SHARED
    src/dllmain.cpp
    src/renderer.cpp
    src/signature.cpp
    src/dvar.cpp
    src/features.cpp
    src/console.cpp
    src/splash.cpp
    src/uidump.cpp
    src/game.cpp
)
target_include_directories(codsp_mod PRIVATE src)
target_link_libraries(codsp_mod PRIVATE imgui minhook d3d11 dxgi d3dcompiler)
target_compile_definitions(codsp_mod PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# ---- Output: d3d11.dll proxy (self-loading) or a plain injectable DLL -------
# ON  -> builds "d3d11.dll": drop next to the game exe, no injector needed.
# OFF -> builds "codsp-mod.dll": load it with a DLL injector instead.
option(CODSP_BUILD_PROXY "Build as a self-loading d3d11.dll proxy" ON)

if(CODSP_BUILD_PROXY)
    target_sources(codsp_mod PRIVATE src/d3d11_proxy.cpp)
    set_target_properties(codsp_mod PROPERTIES OUTPUT_NAME "d3d11")
    target_link_options(codsp_mod PRIVATE "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/d3d11_proxy.def")
else()
    set_target_properties(codsp_mod PROPERTIES OUTPUT_NAME "codsp-mod")
endif()
