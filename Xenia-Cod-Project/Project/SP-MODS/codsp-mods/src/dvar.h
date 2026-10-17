#pragma once
#include <cstdint>

// Wrapper over the IW6 (Ghosts) dvar + command systems. Typed console lines
// are run through the engine's command buffer (Cbuf_AddText) exactly like the
// native console, so every command and dvar "just works"; values are read back
// through Dvar_FindVar / Dvar_ValueToString.
//
// Addresses are the single-player (iw6sp64_ship.exe) addresses from the
// AlterWare iw6-mod symbol list, rebased to the module's actual load address.
// They are BUILD-SPECIFIC: a self-test (Verified()) confirms they match the
// running game before anything is called, so a mismatched build fails safe.
struct dvar_t;

namespace Dvar
{
    // Resolve engine addresses (rebased). Does not call into the game.
    bool Init();

    // Function pointers resolved (always true once Init ran on a known layout).
    bool Ready();

    // Engine self-test passed: the addresses point at real dvar code for this
    // build. Runs once, lazily, guarded against access violations.
    bool Verified();

    dvar_t* FindVar(const char* name);

    // Run a full console line through Cbuf_AddText ("cg_fovScale 1.5", etc.).
    void Execute(const char* line);

    // Set a dvar directly (used by feature code). Goes through the engine.
    void SetString(const char* name, const char* value);
    // Set any dvar via the engine's INTERNAL source, bypassing cheat/write
    // protection (sv_cheats, timescale, player_sustainAmmo, cg_drawFPS, ...).
    void Set(const char* name, const char* value);

    bool  GetBool (const char* name, bool  fallback = false);
    int   GetInt  (const char* name, int   fallback = 0);
    float GetFloat(const char* name, float fallback = 0.0f);

    // ---- Engine dvar table -------------------------------------------------
    bool TableReady();
    int  Count();
    const char* NameAt(int index);
    const char* ValueToString(const char* name);

    // ---- Diagnostics -------------------------------------------------------
    struct SigStatus { const char* label; bool resolved; uintptr_t address; };
    int Diagnostics(SigStatus* out, int max);
}
