#pragma once
#include <string>

// An in-overlay console styled after the IW6-Mod game console:
//   * blue "iw6-mod: <ver>>" prompt over a dark input box
//   * autocomplete hint box listing matching dvars / commands (+ values)
//   * scrollback output window with CoD "^n" color codes and a styled scrollbar
//   * command history (up/down) and tab-complete
//
// Toggled with the tilde (`) key; Shift+tilde toggles the output window.
namespace Console
{
    // Seed the autocomplete registry and built-in commands. Call once.
    void Setup();

    void Toggle();        // open/close the console (compact input view)
    void ToggleOutput();  // open (if needed) and toggle the big output window
    bool IsOpen();

    // Virtual-key code of the current toggle key (con_togglekey). The renderer
    // polls this so the bind is live. Defaults to VK_OEM_3 (the ` / ~ key).
    int ToggleVk();

    // Register a name (dvar or command) so it shows up in autocomplete.
    void AddName(const std::string& name);

    // Push text into the output scrollback (supports "^n" color codes and \n).
    void Print(const std::string& line);
    void Printf(const char* fmt, ...);

    // Call once per frame BEFORE ImGui's NewFrame (rebuilds the font atlas
    // when con_fontface/size/weight changed).
    void BeginFrame();

    // Draw the console. Call every frame from the Present hook.
    void Draw();
}
