#pragma once

// High-level SP features the menu drives. This is where game logic lives;
// the menu (menu.cpp) should only call into here, never touch the engine
// directly.
namespace Features
{
    // Resolve engine addresses. Call once after the module is loaded.
    void Init();

    // True if the dvar layer resolved successfully (menu can show status).
    bool Ready();

    // Example feature: set the game's timescale (slow-mo / speed-up).
    void SetTimescale(float scale);

    // Example feature: toggle the developer console / cheats dvars.
    void SetDeveloper(bool enabled);
}
