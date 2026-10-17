# iw6sp-mod

A C++ / Direct3D 11 **single-player mod menu** for Call of Duty: Ghosts (IW6).
It builds an injectable DLL that hooks the game's D3D11 `Present` call and
draws a [Dear ImGui](https://github.com/ocornut/imgui) overlay (toggle with
`INSERT`).

This is a **starting skeleton**: the overlay, input, and hook plumbing all work
out of the box. The feature toggles in the menu are stubs — wiring them to the
actual game is the part you build on top.

> **Scope:** single-player only, on a copy of the game you own. Don't use this
> in multiplayer — injecting into MP affects other players and gets you banned.

## Layout

```
iw6sp-mod/
├─ xmake.lua             # build (xrepo pulls MinHook + Dear ImGui)
├─ CMakeLists.txt        # alternative build (CMake + FetchContent)
└─ src/
   ├─ dllmain.cpp        # DLL entry; spawns the worker thread
   ├─ renderer.h/.cpp    # D3D11 Present/ResizeBuffers + WndProc hooks, ImGui
   ├─ menu.h/.cpp        # the ImGui menu (UI only - calls into Features)
   ├─ console.h/.cpp     # IW6-Mod-styled console (tilde to toggle)
   ├─ features.h/.cpp    # high-level SP features the menu drives
   ├─ dvar.h/.cpp        # IW6 dvar (console variable) get/set wrapper
   ├─ signature.h/.cpp   # IDA-style byte pattern scanner
   ├─ d3d11_proxy.cpp    # d3d11.dll export forwarding (self-load, no injector)
   └─ d3d11_proxy.def    # the proxied exports
```

## Build (xmake)

Requires: [xmake](https://xmake.io), the MSVC toolchain (Visual Studio 2022
Build Tools / Desktop C++), and the Windows SDK. xmake pulls MinHook and Dear
ImGui (with the Win32 + DX11 backends) from xrepo on first build.

```bash
xmake f -m release          # configure (defaults to x64 on 64-bit Windows)
xmake                        # build
```

Output: `build/windows/x64/release/d3d11.dll`

- Builds a self-loading **`d3d11.dll` proxy** by default. For a plain injectable
  `iw6sp-mod.dll` instead, configure with `xmake f -m release --proxy=n`.
- Ghosts' SP executable is 64-bit (`iw6sp64_ship.exe`); the build errors if you
  configure x86 (`xmake f -a x64` to force it).
- `xmake project -k vsxmake2022` generates a Visual Studio solution if you'd
  rather build/debug in the IDE.

> A CMake build is still provided as an alternative — see `CMakeLists.txt`
> (`cmake -B build -A x64 && cmake --build build --config Release`).

## Run

Two ways to load it:

**Proxy (default, no injector):**
1. Copy `d3d11.dll` next to `iw6sp64_ship.exe`.
2. Launch single-player — the game loads it automatically.

**Injector:** build with `--proxy=n`, then inject `iw6sp-mod.dll` into the game
with any standard DLL injector once you're at the menu / in a mission.

Then press `INSERT` for the feature menu, or `` ` `` (tilde) for the console
(Shift+`` ` `` toggles the big output window).

## Console

An ImGui recreation of the IW6-Mod in-game console — same colors (taken from
its `con_*` dvar defaults), the blue `iw6-mod: <ver>>` prompt, an autocomplete
hint box that lists matching dvars/commands with values, scrollback with CoD
`^n` color codes and a styled scrollbar, up/down history and Tab completion.

- Autocomplete covers **every registered dvar**: `Console::Setup()` seeds a few
  commands, then pulls the whole engine table (`sortedDvars` / `dvarCount`) the
  way the real console does — lazily, once the table is populated. Values in the
  hint box come from the engine's own `Dvar_ValueToString`, so they're
  type-aware (bools read `0/1`, colors as vec4, etc.). Call `Console::AddName()`
  to add your own commands.
- `Enter` on `<dvar> <value>` sets it via the dvar layer; a bare dvar name
  prints its current value; `clear` clears output; `diag` reports which
  signatures resolved (also printed once on startup) — run it first when a
  feature seems to do nothing.
- The original renders through the game (`R_AddCmdDrawText`); this draws through
  the D3D11 overlay instead, so it needs no engine render hooks.

### Signatures to fill in (`src/dvar.cpp`)

Everything no-ops safely until these are set for your `iw6sp64_ship.exe`:

| Constant | What it locates |
| --- | --- |
| `SIG_DVAR_FINDVAR` | `Dvar_FindVar(name)` |
| `SIG_DVAR_SETFROMSTRINGNAME` | `Dvar_SetFromStringByName(name, value)` |
| `SIG_DVAR_VALUETOSTRING` | `Dvar_ValueToString(dvar, value)` — type-aware values |
| `SIG_SORTEDDVARS` | reference to the `sortedDvars[]` array — full autocomplete |
| `SIG_DVARCOUNT` | reference to the `dvarCount` int |

The last two land on an instruction that *references* the global (rip-relative),
so the scanner resolves the operand with `ResolveRelative`. Adjust the
`*_OFF` / `*_LEN` constants next to them if your match isn't the assumed
`lea`/`mov` form. Verify the `dvar_t` layout (the offset of `current`) for your
build too, since `ValueToString` and the readers dereference it.

## How it works

- `ResolveSwapchainMethods()` creates a throwaway D3D11 device + swapchain to
  read the `IDXGISwapChain` vtable, grabbing the addresses of `Present`
  (index 8) and `ResizeBuffers` (index 13).
- MinHook detours those. The `Present` hook lazily grabs the game's device,
  context and window on the first frame, initializes ImGui's Win32 + DX11
  backends, then draws the menu every frame.
- `WndProc` is hooked so input goes to ImGui while the menu is open.

## Adding real features

The features layer is now in place. The **Timescale** slider and **Developer
mode** toggle in the "World" section already route through
`Features` → `Dvar` → the engine's dvar setter. They just need the engine
addresses resolved:

1. Open `src/dvar.cpp` and fill in the two signature constants
   (`SIG_DVAR_FINDVAR`, `SIG_DVAR_SETFROMSTRINGNAME`) with byte patterns for
   **your exact `iw6sp64_ship.exe`**, found with IDA / Ghidra / x64dbg.
2. The menu header shows `dvar layer: resolved` (green) once both are found.
3. Add more features in `src/features.cpp` and expose them in `src/menu.cpp` —
   the menu should only call `Features::...`, never touch the engine directly.

The signature scanner (`src/signature.h`) also gives you `ResolveRelative()`
for turning a `call`/`lea` rel32 into an absolute address when a pattern lands
on a reference rather than the target itself.

> Until the signatures are filled in, `Scan()` returns 0 and every dvar helper
> safely no-ops — the overlay still runs, the toggles just don't bite yet.
