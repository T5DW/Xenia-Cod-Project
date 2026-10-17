#include "console.h"
#include "dvar.h"
#include "dvar_list.h"
#include "uidump.h"
#include "game.h"

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace Console
{
    namespace
    {
        // Version + display name come from the detected game profile.
        inline const char* kVersion() { return Game::Current().version; }

        const ImVec4 col_white        (1.00f, 1.00f, 1.00f, 1.00f);
        const ImVec4 col_hintBox      (0.30f, 0.30f, 0.30f, 1.00f);
        const ImVec4 col_outputWindow (0.25f, 0.25f, 0.25f, 0.85f);
        const ImVec4 col_outputBar    (0.50f, 0.50f, 0.50f, 0.60f);
        const ImVec4 col_outputSlider (0.00f, 0.70f, 1.00f, 1.00f);
        const ImVec4 col_dvarMatch    (1.00f, 1.00f, 0.80f, 1.00f);
        const ImVec4 col_dvarValue    (1.00f, 1.00f, 0.80f, 1.00f);
        const ImVec4 col_cmdMatch     (0.80f, 0.80f, 1.00f, 1.00f);
        const ImVec4 col_desc         (0.60f, 0.60f, 0.60f, 1.00f);

        // ---- All user-configurable console settings (persisted to json) ----
        struct Settings
        {
            std::string name        = "IW6SP-MOD";
            ImVec4 titleColor  = ImVec4(0.00f, 0.70f, 1.00f, 1.00f);
            ImVec4 inboxColor  = ImVec4(0.20f, 0.20f, 0.20f, 0.90f);
            ImVec4 outboxColor = ImVec4(0.00f, 0.70f, 1.00f, 1.00f);
            ImVec4 textColor   = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
            ImVec4 hintColor   = ImVec4(0.30f, 0.30f, 0.30f, 1.00f); // dvar/autocomplete box
            bool   inboxVisible = true;

            std::string fontFace = "default";
            float fontSize   = 16.0f;
            int   fontWeight = 400;
            int   textAlign  = 0;      // 0 L, 1 C, 2 R
            float lineHeight = 1.0f;

            float width   = 0.0f;      // 0 = auto / full
            float height  = 0.0f;
            float posx    = 6.0f;
            float posy    = 6.0f;
            int   anchor  = 0;         // 0-8 (TL..BR)
            float padding = 8.0f;

            int   opacity    = 100;    // 0-100
            int   textAlpha  = 100;    // 0-100
            bool  blur       = false;
            float outboxWidth = 1.0f;
            int   corners    = 1;      // 0 sharp, 1 rounded, 2 bevel

            int   cursorStyle = 1;
            bool  cursorBlink = true;
            int   historySize = 10;
            bool  autoComplete = true;
            bool  secureInput = false;

            std::string toggleKey = "`";
            float fadeTime     = 0.0f;
            bool  alwaysOnTop  = true;
            int   logLines     = 512;
            float scrollSpeed  = 1.0f;

            std::string filter = "";
            bool timestamp = false;
            bool echoCmds  = true;
            bool logFile   = false;
            bool notify    = false;
            bool fade      = true;
        };
        Settings g;

        bool g_open          = false;
        bool g_outputVisible = false;
        bool g_focusInput    = false;
        bool g_forceCheats   = false; // keep sv_cheats=1 reasserted each frame

        int  g_toggleVk   = VK_OEM_3; // resolved from g.toggleKey
        char g_toggleChar = '`';      // char to swallow so it isn't typed (0 = none)

        ImFont* g_font      = nullptr; // active console font (null until first BeginFrame)
        float   g_fontBase  = 13.0f;   // base pixel size the font was built at
        bool    g_fontDirty = true;    // rebuild the atlas next BeginFrame

        float       g_fadeAlpha  = 0.0f; // current fade 0..1 (con_fade/con_fadetime)
        std::string g_notifyMsg;         // last error shown as a toast (con_notify)
        double      g_notifyUntil = 0.0; // ImGui time the toast expires

        char g_input[256] = {};

        std::deque<std::string> g_output;
        std::mutex              g_outputMutex;

        std::deque<std::string> g_history;
        int                     g_historyIndex = -1;

        std::vector<std::string> g_matches;
        std::vector<std::string> g_names; // commands + dvars for completion
        std::unordered_set<std::string> g_dvarSet; // engine dvar names already added
        int g_lastDvarCount = -1;                  // re-pull when the table grows

        // ---- Command table: name + one-line description --------------------
        struct ConCmd { const char* name; const char* desc; };
        const ConCmd kCmds[] = {
            {"help",            "Lists every console command with its description."},
            {"clear",           "Clears the output log."},
            {"con_clear",       "Instantly flushes the log buffer, erasing all on-screen text."},
            {"diag",            "Reports which engine signatures resolved."},
            {"con_name",        "Changes the console name/title. Usage: con_name <text>"},
            {"con_titlecolor",  "Title/prompt text color. Usage: con_titlecolor R G B (0-255)"},
            {"con_textcolor",   "Default log text color. Usage: con_textcolor R G B (0-255)"},
            {"con_inboxcolor",  "Inner input box fill color. R G B (0-255)"},
            {"con_outboxcolor", "Outer outline (frame) color. R G B (0-255)"},
            {"con_hintcolor",   "Autocomplete / dvar box background color. R G B (0-255)"},
            {"con_inboxvis",    "0 disables the inner fill so only the outline shows. 0|1"},
            {"openmenu",        "Opens a LUI menu via LUI_OpenMenu. Usage: openmenu <menuname>"},
            {"watermark",       "Toggles the top-left IW6SP-MOD watermark. Usage: watermark on|off"},
            {"quit",            "Quits the game."},
            {"disconnect",      "Ends the session and returns to the main menu."},
            {"dumpui",          "Dump the game's compiled Lua (LUI) assets to the mod's assets/ui_dump."},
            {"dumpweapon",      "Dump every weapon's internal name to the mod's assets/weapons.txt."},
            {"dumpmenu",        "Dump menu (ASSET_TYPE_MENU) names to the mod's assets/menunames.txt."},
            {"give",            "Give a weapon (enables sv_cheats). Usage: give <weapon>  (see dumpweapon for names)."},
            {"demigod",         "Toggle demigod - take damage but never die (enables sv_cheats)."},
            {"infammo",         "Toggle infinite ammo (player_sustainAmmo)."},
            {"fov",             "Set FOV scale; saved to settings.ini and reapplied. Usage: fov <scale>"},
            {"fps",             "Toggle the on-screen FPS counter (saved). Usage: fps on|off"},
            {"fpslabels",       "Toggle the 'FPS:' label on the counter (saved). Usage: fpslabels on|off"},
            {"con_fontface",    "Font family from Windows/Fonts. e.g. con_fontface Consolas (default = built-in)"},
            {"con_fontsize",    "Text size in pixels. Usage: con_fontsize X"},
            {"con_fontweight",  "Text weight 100-900 (>=600 loads the bold font file)."},
            {"con_textalign",   "Aligns log text. 0=Left 1=Center 2=Right"},
            {"con_lineheight",  "Vertical spacing multiplier between lines. Usage: con_lineheight X"},
            {"con_width",       "Total console width in pixels (0 = auto)."},
            {"con_height",      "Total console height in pixels (0 = auto)."},
            {"con_posx",        "Horizontal anchor position/margin in pixels."},
            {"con_posy",        "Vertical anchor position/margin in pixels."},
            {"con_anchor",      "Binds the window to a screen zone. 0-8 (TL..BR)."},
            {"con_padding",     "Internal margin between text and the frame."},
            {"con_opacity",     "Overall window transparency. 0-100."},
            {"con_textalpha",   "Text-only transparency. 0-100."},
            {"con_blur",        "Dims the screen behind the console (approximate blur). 0|1"},
            {"con_outboxwidth", "Thickness of the outer outline in pixels."},
            {"con_corners",     "Corner style. 0=Sharp 1=Rounded 2=Beveled(->rounded)."},
            {"con_cursorstyle", "Input cursor shape. (ImGui draws a line cursor; stored only) 0|1|2"},
            {"con_cursorblink", "Toggles input cursor blinking. 0|1"},
            {"con_historysize", "Number of commands kept in the up/down history."},
            {"con_autocomplete","Toggles the autocomplete hint box. 0|1"},
            {"con_secureinput", "Masks typed characters with asterisks. 0|1"},
            {"con_togglekey",   "Key that opens/hides the console. e.g. con_togglekey F8 (` A-Z 0-9 F1-F12 Insert Home End PgUp PgDn Del Pause Tab)"},
            {"con_fadetime",    "Fade in/out duration in seconds (needs con_fade 1)."},
            {"con_alwaysontop", "Overlay is inherently top-most; stored only. 0|1"},
            {"con_loglines",    "Max lines kept in the viewable buffer."},
            {"con_scrollspeed", "Mouse-wheel scroll multiplier for the log."},
            {"con_filter",      "Shows only lines containing a keyword. Usage: con_filter <text> (empty clears)"},
            {"con_timestamp",   "Prefixes a clock time to every logged line. 0|1"},
            {"con_echocmds",    "Echoes typed commands back into the log. 0|1"},
            {"con_logfile",     "Writes console output to the mod's assets/console/console.log. 0|1"},
            {"con_notify",      "Shows error (^1) lines as an on-screen toast when the console is closed. 0|1"},
            {"con_fade",        "Enables fade in/out animation (see con_fadetime). 0 = instant. 0|1"},
        };

        const char* cmd_desc(const std::string& name)
        {
            for (const auto& c : kCmds)
                if (name == c.name)
                    return c.desc;
            return nullptr;
        }

        // ---- small helpers -------------------------------------------------
        float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
        int   clamp255(int v)                      { return v < 0 ? 0 : (v > 255 ? 255 : v); }
        ImVec4 with_alpha(ImVec4 c, float a)       { c.w *= a; return c; }

        std::string to_lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        std::string strip_codes(const std::string& line)
        {
            std::string out;
            for (size_t i = 0; i < line.size(); ++i)
            {
                if (line[i] == '^' && i + 1 < line.size() && std::isdigit(static_cast<unsigned char>(line[i + 1])))
                    ++i;
                else
                    out += line[i];
            }
            return out;
        }

        std::string stripped(const char* buf)
        {
            std::string s(buf);
            if (s.size() > 1 && (s[0] == '/' || s[0] == '\\'))
                return s.substr(1);
            return s;
        }

        ImVec4 color_for_code(char c)
        {
            switch (c)
            {
            case '0': return ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
            case '1': return ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
            case '2': return ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
            case '3': return ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
            case '4': return ImVec4(0.0f, 0.0f, 1.0f, 1.0f);
            case '5': return ImVec4(0.0f, 1.0f, 1.0f, 1.0f);
            case '6': return ImVec4(1.0f, 0.0f, 1.0f, 1.0f);
            case '7': return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            case '8': return ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
            case '9': return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
            default:  return col_white;
            }
        }

        void render_colored(const std::string& line, float alphaMul)
        {
            ImVec4 cur   = with_alpha(g.textColor, alphaMul);
            bool   first = true;
            std::string seg;

            auto flush = [&]()
            {
                if (seg.empty()) return;
                if (!first) ImGui::SameLine(0.0f, 0.0f);
                ImGui::TextColored(cur, "%s", seg.c_str());
                first = false;
                seg.clear();
            };

            for (size_t i = 0; i < line.size(); ++i)
            {
                if (line[i] == '^' && i + 1 < line.size() && std::isdigit(static_cast<unsigned char>(line[i + 1])))
                {
                    flush();
                    cur = with_alpha(color_for_code(line[i + 1]), alphaMul);
                    ++i;
                }
                else
                {
                    seg += line[i];
                }
            }
            flush();
            if (first)
                ImGui::TextUnformatted(" ");
        }

        void find_matches(const std::string& rawInput)
        {
            g_matches.clear();
            const std::string in = to_lower(rawInput);
            if (in.empty())
                return;

            const auto sp = in.find(' ');
            const std::string key = sp == std::string::npos ? in : in.substr(0, sp);
            if (key.empty())
                return;

            for (const auto& n : g_names)
            {
                if (to_lower(n).find(key) != std::string::npos)
                    g_matches.push_back(n);
                if (g_matches.size() > 48)
                    break;
            }
        }

        void clear_output()
        {
            std::lock_guard<std::mutex> lock(g_outputMutex);
            g_output.clear();
        }

        void print_diagnostics()
        {
            Dvar::SigStatus status[8];
            const int n = Dvar::Diagnostics(status, 8);
            Print("^7--- signature diagnostics ---");
            for (int i = 0; i < n; ++i)
            {
                if (status[i].resolved)
                    Printf("^2 [ok] ^7%-26s ^80x%llX", status[i].label,
                           static_cast<unsigned long long>(status[i].address));
                else
                    Printf("^1 [--] ^7%-26s ^1unresolved (fill its signature)", status[i].label);
            }
            Printf("^7engine: %s", Dvar::Verified()
                   ? "^2verified - commands are live"
                   : "^1NOT verified - addresses don't match this build; commands disabled");
            Printf("^7dvars enumerated: ^5%d", Dvar::Count());
        }

        void print_help()
        {
            Print("^7--- commands ---");
            for (const auto& c : kCmds)
                Printf("^5%-16s^7 %s", c.name, c.desc);
        }

        // ---- Persisted settings --------------------------------------------
        // <game>/<mod folder>  (made on load)  ->  assets/console/console.json
        std::wstring game_dir()
        {
            wchar_t buf[MAX_PATH]{};
            GetModuleFileNameW(nullptr, buf, MAX_PATH);
            std::wstring p(buf);
            const size_t slash = p.find_last_of(L"\\/");
            return (slash == std::wstring::npos) ? L"." : p.substr(0, slash);
        }
        std::wstring base_dir()    { return game_dir() + Game::Current().baseFolder; }
        std::wstring console_dir() { return base_dir() + L"\\assets\\console"; }
        std::wstring config_path() { return console_dir() + L"\\console.json"; }
        std::wstring logfile_path(){ return console_dir() + L"\\console.log"; }

        void ensure_dir(const std::wstring& d) { CreateDirectoryW(d.c_str(), nullptr); }
        void ensure_console_dir()
        {
            ensure_dir(base_dir());
            ensure_dir(base_dir() + L"\\assets");
            ensure_dir(console_dir());
        }

        // ---- Player settings (the engine `name` dvar) ----------------------
        // <game>/the mod's assets/player/settings.ini
        std::wstring player_dir()    { return base_dir() + L"\\assets\\player"; }
        std::wstring settings_path() { return player_dir() + L"\\settings.ini"; }

        std::string g_playerName;
        bool        g_namePending = false; // apply name to the engine once verified
        float       g_fov         = 0.0f;  // cg_fovScale (0 = unset)
        bool        g_fovPending  = false;
        bool        g_pfps        = false; // on-screen FPS counter (persisted)
        bool        g_fpsLabels   = true;  // show the "FPS:" label
        bool        g_watermark   = true;  // persisted here now (moved out of console.json)

        void save_player()
        {
            ensure_dir(base_dir());
            ensure_dir(base_dir() + L"\\assets");
            ensure_dir(player_dir());
            std::ofstream f(settings_path());
            if (!f) return;
            f << "[player]\n";
            f << "name="      << g_playerName << "\n";
            f << "fov="       << g_fov << "\n";
            f << "fps="       << (g_pfps ? 1 : 0) << "\n";
            f << "fpslabels=" << (g_fpsLabels ? 1 : 0) << "\n";
            f << "watermark=" << (g_watermark ? 1 : 0) << "\n";
        }

        void load_player()
        {
            std::ifstream f(settings_path());
            if (!f) return;
            std::string line;
            while (std::getline(f, line))
            {
                while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                    line.pop_back();
                const size_t eq = line.find('=');
                if (eq == std::string::npos) continue;
                const std::string key = line.substr(0, eq);
                const std::string val = line.substr(eq + 1);
                if (key == "name")           { g_playerName = val; if (!val.empty()) g_namePending = true; }
                else if (key == "fov")       { g_fov = static_cast<float>(std::atof(val.c_str())); if (g_fov > 0.0f) g_fovPending = true; }
                else if (key == "fps")       { g_pfps = (std::atoi(val.c_str()) != 0); }
                else if (key == "fpslabels") { g_fpsLabels = (std::atoi(val.c_str()) != 0); }
                else if (key == "watermark") { g_watermark = (std::atoi(val.c_str()) != 0); }
            }
        }

        int to255(float f) { return static_cast<int>(std::lround(f * 255.0f)); }

        void save_config()
        {
            ensure_console_dir();
            std::ofstream f(config_path());
            if (!f) return;
            auto col = [&](const char* k, const ImVec4& c)
            {
                f << "  \"" << k << "\": [" << to255(c.x) << ", " << to255(c.y) << ", " << to255(c.z) << "],\n";
            };
            f << "{\n";
            f << "  \"name\": \"" << g.name << "\",\n";
            col("titlecolor",  g.titleColor);
            col("textcolor",   g.textColor);
            col("inboxcolor",  g.inboxColor);
            col("outboxcolor", g.outboxColor);
            col("hintcolor",   g.hintColor);
            f << "  \"inboxvis\": "    << (g.inboxVisible ? 1 : 0) << ",\n";
            f << "  \"fontface\": \""  << g.fontFace << "\",\n";
            f << "  \"fontsize\": "    << g.fontSize << ",\n";
            f << "  \"fontweight\": "  << g.fontWeight << ",\n";
            f << "  \"textalign\": "   << g.textAlign << ",\n";
            f << "  \"lineheight\": "  << g.lineHeight << ",\n";
            f << "  \"width\": "       << g.width << ",\n";
            f << "  \"height\": "      << g.height << ",\n";
            f << "  \"posx\": "        << g.posx << ",\n";
            f << "  \"posy\": "        << g.posy << ",\n";
            f << "  \"anchor\": "      << g.anchor << ",\n";
            f << "  \"padding\": "     << g.padding << ",\n";
            f << "  \"opacity\": "     << g.opacity << ",\n";
            f << "  \"textalpha\": "   << g.textAlpha << ",\n";
            f << "  \"blur\": "        << (g.blur ? 1 : 0) << ",\n";
            f << "  \"outboxwidth\": " << g.outboxWidth << ",\n";
            f << "  \"corners\": "     << g.corners << ",\n";
            f << "  \"cursorstyle\": " << g.cursorStyle << ",\n";
            f << "  \"cursorblink\": " << (g.cursorBlink ? 1 : 0) << ",\n";
            f << "  \"historysize\": " << g.historySize << ",\n";
            f << "  \"autocomplete\": "<< (g.autoComplete ? 1 : 0) << ",\n";
            f << "  \"secureinput\": " << (g.secureInput ? 1 : 0) << ",\n";
            f << "  \"togglekey\": \"" << g.toggleKey << "\",\n";
            f << "  \"fadetime\": "    << g.fadeTime << ",\n";
            f << "  \"alwaysontop\": " << (g.alwaysOnTop ? 1 : 0) << ",\n";
            f << "  \"loglines\": "    << g.logLines << ",\n";
            f << "  \"scrollspeed\": " << g.scrollSpeed << ",\n";
            f << "  \"filter\": \""    << g.filter << "\",\n";
            f << "  \"timestamp\": "   << (g.timestamp ? 1 : 0) << ",\n";
            f << "  \"echocmds\": "    << (g.echoCmds ? 1 : 0) << ",\n";
            f << "  \"logfile\": "     << (g.logFile ? 1 : 0) << ",\n";
            f << "  \"notify\": "      << (g.notify ? 1 : 0) << ",\n";
            f << "  \"fade\": "        << (g.fade ? 1 : 0) << "\n";
            f << "}\n";
        }

        bool read_ints(const std::string& s, const std::string& key, int* out, int n)
        {
            const size_t k = s.find("\"" + key + "\"");
            if (k == std::string::npos) return false;
            size_t p = k + key.size() + 2;
            int got = 0;
            while (got < n && p < s.size())
            {
                while (p < s.size() && !std::isdigit(static_cast<unsigned char>(s[p])) && s[p] != '-') ++p;
                if (p >= s.size()) break;
                int sign = 1;
                if (s[p] == '-') { sign = -1; ++p; }
                bool any = false; int val = 0;
                while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p]))) { val = val * 10 + (s[p] - '0'); ++p; any = true; }
                if (!any) break;
                out[got++] = val * sign;
            }
            return got == n;
        }

        bool read_float(const std::string& s, const std::string& key, float& out)
        {
            const size_t k = s.find("\"" + key + "\"");
            if (k == std::string::npos) return false;
            const size_t colon = s.find(':', k);
            if (colon == std::string::npos) return false;
            const char* start = s.c_str() + colon + 1;
            char* end = nullptr;
            const float v = std::strtof(start, &end);
            if (end == start) return false;
            out = v;
            return true;
        }

        bool read_int1(const std::string& s, const std::string& key, int& out)
        {
            int tmp;
            if (!read_ints(s, key, &tmp, 1)) return false;
            out = tmp;
            return true;
        }

        bool read_bool(const std::string& s, const std::string& key, bool& out)
        {
            int tmp;
            if (!read_ints(s, key, &tmp, 1)) return false;
            out = (tmp != 0);
            return true;
        }

        bool read_str(const std::string& s, const std::string& key, std::string& out)
        {
            const size_t k = s.find("\"" + key + "\"");
            if (k == std::string::npos) return false;
            const size_t colon = s.find(':', k);
            if (colon == std::string::npos) return false;
            const size_t q1 = s.find('"', colon);
            if (q1 == std::string::npos) return false;
            const size_t q2 = s.find('"', q1 + 1);
            if (q2 == std::string::npos) return false;
            out = s.substr(q1 + 1, q2 - q1 - 1);
            return true;
        }

        void read_col(const std::string& s, const std::string& key, ImVec4& c)
        {
            int v[3];
            if (read_ints(s, key, v, 3))
                c = ImVec4(clamp255(v[0]) / 255.0f, clamp255(v[1]) / 255.0f, clamp255(v[2]) / 255.0f, c.w);
        }

        // Map a key name / char to a virtual-key code + the char it types.
        // Returns false if unrecognized. `ch` is 0 for non-typing keys.
        bool parse_togglekey(std::string s, int& vk, char& ch)
        {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            ch = 0;

            if (s == "`" || s == "~" || s == "GRAVE" || s == "TILDE") { vk = VK_OEM_3; ch = '`'; return true; }

            if (s.size() == 1)
            {
                const char c = s[0];
                if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) { vk = static_cast<int>(c); ch = c; return true; }
                switch (c)
                {
                case '-': vk = VK_OEM_MINUS;  ch = '-'; return true;
                case '=': vk = VK_OEM_PLUS;   ch = '='; return true;
                case '[': vk = VK_OEM_4;      ch = '['; return true;
                case ']': vk = VK_OEM_6;      ch = ']'; return true;
                case ';': vk = VK_OEM_1;      ch = ';'; return true;
                case '\'':vk = VK_OEM_7;      ch = '\'';return true;
                case ',': vk = VK_OEM_COMMA;  ch = ','; return true;
                case '.': vk = VK_OEM_PERIOD; ch = '.'; return true;
                case '/': vk = VK_OEM_2;      ch = '/'; return true;
                case '\\':vk = VK_OEM_5;      ch = '\\';return true;
                default: break;
                }
            }

            if (s.size() >= 2 && s[0] == 'F')
            {
                const int n = std::atoi(s.c_str() + 1);
                if (n >= 1 && n <= 12) { vk = VK_F1 + (n - 1); ch = 0; return true; }
            }

            static const struct { const char* n; int vk; } named[] = {
                {"INSERT", VK_INSERT}, {"INS", VK_INSERT}, {"HOME", VK_HOME},
                {"END", VK_END}, {"PGUP", VK_PRIOR}, {"PAGEUP", VK_PRIOR},
                {"PGDN", VK_NEXT}, {"PAGEDOWN", VK_NEXT}, {"DELETE", VK_DELETE},
                {"DEL", VK_DELETE}, {"PAUSE", VK_PAUSE}, {"TAB", VK_TAB},
                {"CAPS", VK_CAPITAL}, {"CAPSLOCK", VK_CAPITAL}, {"BACKSPACE", VK_BACK},
                {"SCROLL", VK_SCROLL}, {"SCROLLLOCK", VK_SCROLL},
            };
            for (const auto& e : named)
                if (s == e.n) { vk = e.vk; ch = 0; return true; }

            return false;
        }

        void apply_togglekey()
        {
            int vk; char ch;
            if (parse_togglekey(g.toggleKey, vk, ch)) { g_toggleVk = vk; g_toggleChar = ch; }
        }

        std::string win_fonts_dir()
        {
            char buf[MAX_PATH]{};
            GetWindowsDirectoryA(buf, MAX_PATH);
            return std::string(buf) + "\\Fonts\\";
        }

        bool file_exists(const std::string& p)
        {
            return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES;
        }

        // Resolve a font family name + weight to a .ttf in the Windows fonts
        // folder. Returns "" for the default/empty face (use the built-in font).
        std::string resolve_font(const std::string& face, int weight)
        {
            std::string f = to_lower(face);
            f.erase(std::remove(f.begin(), f.end(), ' '), f.end());
            if (f.empty() || f == "default")
                return "";

            const bool bold = weight >= 600;
            const std::string dir = win_fonts_dir();

            struct Map { const char* key; const char* reg; const char* bold; };
            static const Map m[] = {
                {"arial", "arial.ttf", "arialbd.ttf"},
                {"segoeui", "segoeui.ttf", "segoeuib.ttf"},
                {"segoe", "segoeui.ttf", "segoeuib.ttf"},
                {"consolas", "consola.ttf", "consolab.ttf"},
                {"tahoma", "tahoma.ttf", "tahomabd.ttf"},
                {"verdana", "verdana.ttf", "verdanab.ttf"},
                {"timesnewroman", "times.ttf", "timesbd.ttf"},
                {"times", "times.ttf", "timesbd.ttf"},
                {"couriernew", "cour.ttf", "courbd.ttf"},
                {"courier", "cour.ttf", "courbd.ttf"},
                {"calibri", "calibri.ttf", "calibrib.ttf"},
                {"georgia", "georgia.ttf", "georgiab.ttf"},
                {"trebuchetms", "trebuc.ttf", "trebucbd.ttf"},
                {"comicsansms", "comic.ttf", "comicbd.ttf"},
                {"impact", "impact.ttf", "impact.ttf"},
            };
            for (const auto& e : m)
            {
                if (f == e.key)
                {
                    std::string cand = dir + (bold ? e.bold : e.reg);
                    if (file_exists(cand)) return cand;
                    cand = dir + e.reg;
                    if (file_exists(cand)) return cand;
                }
            }
            std::string cand = dir + f + (bold ? "bd.ttf" : ".ttf");
            if (file_exists(cand)) return cand;
            cand = dir + f + ".ttf";
            if (file_exists(cand)) return cand;
            return "";
        }

        void load_config()
        {
            std::ifstream f(config_path());
            if (!f) return; // no file yet -> keep defaults, create nothing
            const std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

            read_str(s, "name", g.name);
            read_col(s, "titlecolor",  g.titleColor);
            read_col(s, "textcolor",   g.textColor);
            read_col(s, "inboxcolor",  g.inboxColor);
            read_col(s, "outboxcolor", g.outboxColor);
            read_col(s, "hintcolor",   g.hintColor);
            read_bool(s, "inboxvis", g.inboxVisible);
            read_str(s, "fontface", g.fontFace);
            read_float(s, "fontsize", g.fontSize);
            read_int1(s, "fontweight", g.fontWeight);
            read_int1(s, "textalign", g.textAlign);
            read_float(s, "lineheight", g.lineHeight);
            read_float(s, "width", g.width);
            read_float(s, "height", g.height);
            read_float(s, "posx", g.posx);
            read_float(s, "posy", g.posy);
            read_int1(s, "anchor", g.anchor);
            read_float(s, "padding", g.padding);
            read_int1(s, "opacity", g.opacity);
            read_int1(s, "textalpha", g.textAlpha);
            read_bool(s, "blur", g.blur);
            read_float(s, "outboxwidth", g.outboxWidth);
            read_int1(s, "corners", g.corners);
            read_int1(s, "cursorstyle", g.cursorStyle);
            read_bool(s, "cursorblink", g.cursorBlink);
            read_int1(s, "historysize", g.historySize);
            read_bool(s, "autocomplete", g.autoComplete);
            read_bool(s, "secureinput", g.secureInput);
            read_str(s, "togglekey", g.toggleKey);
            read_float(s, "fadetime", g.fadeTime);
            read_bool(s, "alwaysontop", g.alwaysOnTop);
            read_int1(s, "loglines", g.logLines);
            read_float(s, "scrollspeed", g.scrollSpeed);
            read_str(s, "filter", g.filter);
            read_bool(s, "timestamp", g.timestamp);
            read_bool(s, "echocmds", g.echoCmds);
            read_bool(s, "logfile", g.logFile);
            read_bool(s, "notify", g.notify);
            read_bool(s, "fade", g.fade);

            apply_togglekey();
            g_fontDirty = true; // apply any saved font settings on the next frame
        }

        // ---- command argument parsing --------------------------------------
        bool parse_rgb(const std::string& a, ImVec4& col)
        {
            int r, gg, b;
            if (std::sscanf(a.c_str(), "%d %d %d", &r, &gg, &b) != 3) return false;
            col.x = clamp255(r) / 255.0f;
            col.y = clamp255(gg) / 255.0f;
            col.z = clamp255(b) / 255.0f;
            return true;
        }
        bool parse_int(const std::string& a, int& out)   { return std::sscanf(a.c_str(), "%d", &out) == 1; }
        bool parse_float(const std::string& a, float& out){ return std::sscanf(a.c_str(), "%f", &out) == 1; }
        bool parse_bool(const std::string& a, bool& out)
        {
            int v; if (!parse_int(a, v)) return false; out = (v != 0); return true;
        }
        std::string unquote(std::string s)
        {
            while (!s.empty() && s.front() == ' ') s.erase(s.begin());
            while (!s.empty() && s.back() == ' ') s.pop_back();
            if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
            return s;
        }

        // Returns true if `name` was a recognized con_* command.
        bool run_setting(const std::string& name, const std::string& args)
        {
            auto ok  = [&](const char* fmt) { Printf("^2%s^7 -> saved", name.c_str()); (void)fmt; };
            auto bad = [&](const char* usage) { Printf("^1usage: %s", usage); };

            if (name == "con_name")         { if (args.empty()) { bad("con_name <text>"); return true; } g.name = unquote(args); save_config(); ok(""); return true; }
            if (name == "con_titlecolor")   { ImVec4 c = g.titleColor;  if (!parse_rgb(args, c)) { bad("con_titlecolor R G B (0-255)"); return true; } g.titleColor = c;  save_config(); ok(""); return true; }
            if (name == "con_textcolor")    { ImVec4 c = g.textColor;   if (!parse_rgb(args, c)) { bad("con_textcolor R G B (0-255)"); return true; } g.textColor = c;    save_config(); ok(""); return true; }
            if (name == "con_inboxcolor")   { ImVec4 c = g.inboxColor;  if (!parse_rgb(args, c)) { bad("con_inboxcolor R G B (0-255)"); return true; } g.inboxColor = c;  save_config(); ok(""); return true; }
            if (name == "con_outboxcolor")  { ImVec4 c = g.outboxColor; if (!parse_rgb(args, c)) { bad("con_outboxcolor R G B (0-255)"); return true; } g.outboxColor = c; save_config(); ok(""); return true; }
            if (name == "con_hintcolor")    { ImVec4 c = g.hintColor;   if (!parse_rgb(args, c)) { bad("con_hintcolor R G B (0-255)"); return true; } g.hintColor = c;   save_config(); ok(""); return true; }
            if (name == "con_inboxvis")     { if (!parse_bool(args, g.inboxVisible)) { bad("con_inboxvis 0|1"); return true; } save_config(); ok(""); return true; }

            if (name == "con_fontface")     { g.fontFace = unquote(args); g_fontDirty = true; save_config(); ok(""); return true; }
            if (name == "con_fontsize")     { if (!parse_float(args, g.fontSize)) { bad("con_fontsize X"); return true; } g.fontSize = clampf(g.fontSize, 6.0f, 72.0f); g_fontDirty = true; save_config(); ok(""); return true; }
            if (name == "con_fontweight")   { if (!parse_int(args, g.fontWeight)) { bad("con_fontweight 100-900"); return true; } g.fontWeight = g.fontWeight < 100 ? 100 : (g.fontWeight > 900 ? 900 : g.fontWeight); g_fontDirty = true; save_config(); ok(""); return true; }
            if (name == "con_textalign")    { if (!parse_int(args, g.textAlign)) { bad("con_textalign 0|1|2"); return true; } g.textAlign = g.textAlign < 0 ? 0 : (g.textAlign > 2 ? 2 : g.textAlign); save_config(); ok(""); return true; }
            if (name == "con_lineheight")   { if (!parse_float(args, g.lineHeight)) { bad("con_lineheight X"); return true; } g.lineHeight = clampf(g.lineHeight, 0.5f, 4.0f); save_config(); ok(""); return true; }

            if (name == "con_width")        { if (!parse_float(args, g.width)) { bad("con_width X"); return true; } save_config(); ok(""); return true; }
            if (name == "con_height")       { if (!parse_float(args, g.height)) { bad("con_height X"); return true; } save_config(); ok(""); return true; }
            if (name == "con_posx")         { if (!parse_float(args, g.posx)) { bad("con_posx X"); return true; } save_config(); ok(""); return true; }
            if (name == "con_posy")         { if (!parse_float(args, g.posy)) { bad("con_posy X"); return true; } save_config(); ok(""); return true; }
            if (name == "con_anchor")       { if (!parse_int(args, g.anchor)) { bad("con_anchor 0-8"); return true; } g.anchor = g.anchor < 0 ? 0 : (g.anchor > 8 ? 8 : g.anchor); save_config(); ok(""); return true; }
            if (name == "con_padding")      { if (!parse_float(args, g.padding)) { bad("con_padding X"); return true; } save_config(); ok(""); return true; }

            if (name == "con_opacity")      { if (!parse_int(args, g.opacity)) { bad("con_opacity 0-100"); return true; } g.opacity = g.opacity < 0 ? 0 : (g.opacity > 100 ? 100 : g.opacity); save_config(); ok(""); return true; }
            if (name == "con_textalpha")    { if (!parse_int(args, g.textAlpha)) { bad("con_textalpha 0-100"); return true; } g.textAlpha = g.textAlpha < 0 ? 0 : (g.textAlpha > 100 ? 100 : g.textAlpha); save_config(); ok(""); return true; }
            if (name == "con_blur")         { if (!parse_bool(args, g.blur)) { bad("con_blur 0|1"); return true; } save_config(); ok(""); return true; }
            if (name == "con_outboxwidth")  { if (!parse_float(args, g.outboxWidth)) { bad("con_outboxwidth X"); return true; } g.outboxWidth = clampf(g.outboxWidth, 0.0f, 20.0f); save_config(); ok(""); return true; }
            if (name == "con_corners")      { if (!parse_int(args, g.corners)) { bad("con_corners 0|1|2"); return true; } g.corners = g.corners < 0 ? 0 : (g.corners > 2 ? 2 : g.corners); save_config(); ok(""); return true; }

            if (name == "con_cursorstyle")  { if (!parse_int(args, g.cursorStyle)) { bad("con_cursorstyle 0|1|2"); return true; } save_config(); ok(""); return true; }
            if (name == "con_cursorblink")  { if (!parse_bool(args, g.cursorBlink)) { bad("con_cursorblink 0|1"); return true; } save_config(); ok(""); return true; }
            if (name == "con_historysize")  { if (!parse_int(args, g.historySize)) { bad("con_historysize X"); return true; } g.historySize = g.historySize < 1 ? 1 : g.historySize; save_config(); ok(""); return true; }
            if (name == "con_autocomplete") { if (!parse_bool(args, g.autoComplete)) { bad("con_autocomplete 0|1"); return true; } save_config(); ok(""); return true; }
            if (name == "con_secureinput")  { if (!parse_bool(args, g.secureInput)) { bad("con_secureinput 0|1"); return true; } save_config(); ok(""); return true; }

            if (name == "con_togglekey")
            {
                const std::string key = unquote(args);
                int vk; char ch;
                if (key.empty() || !parse_togglekey(key, vk, ch))
                {
                    bad("con_togglekey <`|A-Z|0-9|F1-F12|Insert|Home|End|PgUp|PgDn|Del|Pause|Tab>");
                    return true;
                }
                g.toggleKey = key;
                g_toggleVk = vk;
                g_toggleChar = ch;
                save_config();
                Printf("^2con_togglekey = %s^7 -> saved (press it to open/close the console)", key.c_str());
                return true;
            }
            if (name == "con_fadetime")     { if (!parse_float(args, g.fadeTime)) { bad("con_fadetime X"); return true; } save_config(); ok(""); return true; }
            if (name == "con_alwaysontop")  { if (!parse_bool(args, g.alwaysOnTop)) { bad("con_alwaysontop 0|1"); return true; } save_config(); ok(""); return true; }
            if (name == "con_loglines")     { if (!parse_int(args, g.logLines)) { bad("con_loglines X"); return true; } g.logLines = g.logLines < 16 ? 16 : g.logLines; save_config(); ok(""); return true; }
            if (name == "con_scrollspeed")  { if (!parse_float(args, g.scrollSpeed)) { bad("con_scrollspeed X"); return true; } save_config(); ok(""); return true; }

            if (name == "con_filter")       { g.filter = unquote(args); save_config(); ok(""); return true; }
            if (name == "con_timestamp")    { if (!parse_bool(args, g.timestamp)) { bad("con_timestamp 0|1"); return true; } save_config(); ok(""); return true; }
            if (name == "con_echocmds")     { if (!parse_bool(args, g.echoCmds)) { bad("con_echocmds 0|1"); return true; } save_config(); ok(""); return true; }
            if (name == "con_logfile")      { if (!parse_bool(args, g.logFile)) { bad("con_logfile 0|1"); return true; } save_config(); ok(""); return true; }
            if (name == "con_notify")       { if (!parse_bool(args, g.notify)) { bad("con_notify 0|1"); return true; } save_config(); ok(""); return true; }
            if (name == "con_fade")         { if (!parse_bool(args, g.fade)) { bad("con_fade 0|1"); return true; } save_config(); ok(""); return true; }

            return false;
        }

        // Pull every registered dvar from the live engine table. Re-pulls
        // whenever the count changes, so dvars registered later get added too.
        void try_load_dvars()
        {
            if (!Dvar::TableReady())
                return;
            const int count = Dvar::Count();
            if (count <= 0 || count == g_lastDvarCount)
                return;
            g_lastDvarCount = count;
            for (int i = 0; i < count; ++i)
                if (const char* name = Dvar::NameAt(i))
                    if (g_dvarSet.insert(name).second)
                        g_names.push_back(name);
        }

        void execute(const std::string& cmd)
        {
            if (g.echoCmds)
                Printf("]%s", cmd.c_str());

            const auto sp = cmd.find(' ');
            const std::string name = sp == std::string::npos ? cmd : cmd.substr(0, sp);
            std::string args = sp == std::string::npos ? "" : cmd.substr(sp + 1);
            while (!args.empty() && args.front() == ' ') args.erase(args.begin());

            if (name.empty())           return;
            if (name == "help")       { print_help(); return; }
            if (name == "diag")       { print_diagnostics(); return; }
            if (name == "clear" || name == "con_clear") { clear_output(); return; }

            if (name == "openmenu")
            {
                if (args.empty())
                {
                    Printf("^1usage: openmenu <menuname>   ^7e.g. openmenu menu_pause");
                    return;
                }
                if (!Dvar::Verified())
                {
                    Printf("^1engine not ready (run ^5diag^7).");
                    return;
                }
                if (Uidump::OpenMenu(args.c_str()))
                {
                    Printf("^2opening menu: ^7%s", args.c_str());
                }
                else
                {
                    // No LUI_OpenMenu address for this title (e.g. S1 SP) -
                    // fall back to the engine's lui_openmenu command.
                    Dvar::Execute(("lui_openmenu " + args).c_str());
                    Printf("^2opening menu: ^7%s ^8(via lui_openmenu)", args.c_str());
                }
                return;
            }

            if (name == "watermark")
            {
                if (args == "on")       { g_watermark = true;  save_player(); Printf("^2watermark on");  return; }
                else if (args == "off") { g_watermark = false; save_player(); Printf("^2watermark off"); return; }
                Printf("^1usage: watermark on|off");
                return;
            }

            if (name == "quit")
            {
                ExitProcess(0); // hard exit - reliable regardless of engine state
                return;
            }

            if (name == "dumpui")
            {
                if (!Dvar::Verified()) { Printf("^1engine not ready (run ^5diag^7)."); return; }
                const std::wstring dir = base_dir() + L"\\assets\\ui_dump";
                const int n = Uidump::Dump(dir.c_str());
                if (n < 0)
                    Printf("^1dumpui:^7 address unresolved");
                else
                    Printf("^2dumpui:^7 dumped %d Lua asset(s) to ^8the mod's assets/ui_dump", n);
                return;
            }

            if (name == "dumpweapon")
            {
                if (!Dvar::Verified()) { Printf("^1engine not ready (run ^5diag^7)."); return; }
                const std::wstring file = base_dir() + L"\\assets\\weapons.txt";
                const int n = Uidump::DumpWeapons(file.c_str());
                if (n < 0)
                    Printf("^1dumpweapon:^7 address unresolved");
                else
                    Printf("^2dumpweapon:^7 wrote %d weapon name(s) to ^8the mod's assets/weapons.txt", n);
                return;
            }

            if (name == "dumpmenu")
            {
                if (!Dvar::Verified()) { Printf("^1engine not ready (run ^5diag^7)."); return; }
                const std::wstring file = base_dir() + L"\\assets\\menunames.txt";
                const int n = Uidump::DumpMenus(file.c_str());
                if (n < 0)
                    Printf("^1dumpmenu:^7 address unresolved");
                else
                {
                    Printf("^2dumpmenu:^7 wrote %d menu name(s) to ^8the mod's assets/menunames.txt", n);
                    if (n == 0)
                        Printf("^8(0 = this build uses LUI menus; look inside the ^5dumpui^8 output for LUI names)");
                }
                return;
            }

            if (name == "give")
            {
                if (args.empty()) { Printf("^1usage: give <weapon>   ^7(run ^5dumpweapon^7 for names)"); return; }
                if (!Dvar::Verified()) { Printf("^1engine not ready (run ^5diag^7)."); return; }
                Dvar::Set("sv_cheats", "1"); g_forceCheats = true;
                Dvar::Execute(("give " + args).c_str());
                Printf("^2give:^7 %s", args.c_str());
                return;
            }

            if (name == "demigod")
            {
                if (!Dvar::Verified()) { Printf("^1engine not ready (run ^5diag^7)."); return; }
                Dvar::Set("sv_cheats", "1"); g_forceCheats = true;
                Dvar::Execute("demigod");
                Printf("^2demigod toggled ^8(sv_cheats enabled)");
                return;
            }

            if (name == "infammo")
            {
                if (!Dvar::Verified()) { Printf("^1engine not ready (run ^5diag^7)."); return; }
                static bool on = false;
                on = !on;
                Dvar::Set("sv_cheats", "1"); g_forceCheats = true;
                Dvar::Set("player_sustainAmmo", on ? "1" : "0");
                Printf("^2infinite ammo %s", on ? "on" : "off");
                return;
            }

            if (name == "fov")
            {
                float v;
                if (!parse_float(args, v)) { Printf("^1usage: fov <scale>   ^7e.g. fov 1.35"); return; }
                g_fov = v < 0.1f ? 0.1f : (v > 3.0f ? 3.0f : v);
                g_fovPending = true;
                save_player();
                if (Dvar::Verified())
                {
                    char b[32]; std::snprintf(b, sizeof(b), "cg_fovScale %.3f", g_fov);
                    Dvar::Execute(b);
                }
                Printf("^2fov scale = %.3f ^8(saved; reapplied after missions)", g_fov);
                return;
            }

            if (name == "fps")
            {
                if (args == "off")      { g_pfps = false; save_player(); Printf("^2fps counter off"); return; }
                else if (args == "on")  { g_pfps = true;  save_player(); Printf("^2fps counter on");  return; }
                g_pfps = !g_pfps; save_player();
                Printf("^2fps counter %s", g_pfps ? "on" : "off");
                return;
            }

            if (name == "fpslabels")
            {
                if (args == "off")      { g_fpsLabels = false; save_player(); Printf("^2fps labels off"); return; }
                else if (args == "on")  { g_fpsLabels = true;  save_player(); Printf("^2fps labels on");  return; }
                g_fpsLabels = !g_fpsLabels; save_player();
                Printf("^2fps labels %s", g_fpsLabels ? "on" : "off");
                return;
            }

            if (name == "disconnect")
            {
                if (!Dvar::Verified()) { Printf("^1engine not ready (run ^5diag^7)."); return; }
                Dvar::Execute("disconnect");
                Dvar::Execute("lui_openmenu menu_main"); // main menu (edit if your build names it differently)
                Printf("^2returning to main menu...");
                return;
            }

            if (run_setting(name, args))
                return;

            // Setting the player name: persist to settings.ini, then apply.
            if (name == "name" && !args.empty())
            {
                g_playerName = unquote(args);
                save_player();
                if (Dvar::Verified())
                    Dvar::Execute(("name \"" + g_playerName + "\"").c_str());
                Printf("^2player name set: ^7%s ^8(saved to settings.ini)", g_playerName.c_str());
                return;
            }

            // A bare dvar name with no value -> print its current value here.
            if (args.empty() && Dvar::FindVar(name.c_str()))
            {
                if (const char* value = Dvar::ValueToString(name.c_str()))
                    Printf("%s is \"^5%s^7\"", name.c_str(), value);
                else
                    Printf("%s is \"^5%.3f^7\"", name.c_str(), Dvar::GetFloat(name.c_str()));
                return;
            }

            // Setting a dvar WITH a value: go through the engine's INTERNAL
            // source so cheat- and write-protected dvars (sv_cheats, timescale,
            // player_sustainAmmo, cg_drawFPS, ...) actually set - the command
            // buffer would silently block them.
            if (!args.empty() && Dvar::FindVar(name.c_str()))
            {
                Dvar::Set(name.c_str(), args.c_str());
                if (name == "sv_cheats")
                    g_forceCheats = (args == "1" || args == "true");
                if (const char* value = Dvar::ValueToString(name.c_str()))
                    Printf("%s = \"^5%s^7\"", name.c_str(), value);
                else
                    Printf("^2%s^7 set to ^5%s", name.c_str(), args.c_str());
                return;
            }

            // Otherwise run the whole line through the engine's command buffer,
            // exactly like the native console (handles commands AND dvar sets).
            if (Dvar::Verified())
                Dvar::Execute(cmd.c_str());
            else
                Printf("^1engine not ready: ^7dvar/command addresses don't match this game build (run ^5diag^7).");
        }

        int input_callback(ImGuiInputTextCallbackData* data)
        {
            switch (data->EventFlag)
            {
            case ImGuiInputTextFlags_CallbackCharFilter:
            {
                const unsigned int ch = data->EventChar;
                // Swallow the grave/tilde pair when that's the toggle key...
                if (g_toggleVk == VK_OEM_3 && (ch == '`' || ch == '~'))
                    return 1;
                // ...and otherwise swallow whatever char the toggle key types.
                if (g_toggleChar && std::toupper(static_cast<int>(ch)) == std::toupper(static_cast<unsigned char>(g_toggleChar)))
                    return 1;
                return 0;
            }

            case ImGuiInputTextFlags_CallbackHistory:
            {
                const int prev = g_historyIndex;
                if (data->EventKey == ImGuiKey_UpArrow)
                {
                    if (++g_historyIndex >= static_cast<int>(g_history.size()))
                        g_historyIndex = static_cast<int>(g_history.size()) - 1;
                }
                else if (data->EventKey == ImGuiKey_DownArrow)
                {
                    if (--g_historyIndex < -1)
                        g_historyIndex = -1;
                }
                if (prev != g_historyIndex)
                {
                    const char* str = (g_historyIndex == -1) ? "" : g_history[g_historyIndex].c_str();
                    data->DeleteChars(0, data->BufTextLen);
                    data->InsertChars(0, str);
                }
                return 0;
            }

            case ImGuiInputTextFlags_CallbackCompletion:
                if (g.autoComplete && !g_matches.empty())
                {
                    data->DeleteChars(0, data->BufTextLen);
                    data->InsertChars(0, g_matches[0].c_str());
                    data->InsertChars(data->BufTextLen, " ");
                }
                return 0;

            case ImGuiInputTextFlags_CallbackEdit:
                find_matches(stripped(data->Buf));
                return 0;
            }
            return 0;
        }

        void draw_hints()
        {
            if (!g.autoComplete || g_matches.empty())
                return;

            const float lineH = ImGui::GetTextLineHeightWithSpacing();
            // Shift+toggle (output view) expands the list to ALL matches, scrollable.
            const bool showAll = g_outputVisible;

            if (!showAll && g_matches.size() > 24)
            {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, g.hintColor);
                ImGui::PushStyleColor(ImGuiCol_Border, g.outboxColor);
                ImGui::BeginChild("##con_hints", ImVec2(0.0f, lineH + 8.0f), true, ImGuiWindowFlags_NoScrollbar);
                ImGui::TextColored(col_dvarMatch, "%zu matches - press Shift+toggle to list them all", g_matches.size());
                ImGui::EndChild();
                ImGui::PopStyleColor(2);
                return;
            }

            const float maxH = ImGui::GetIO().DisplaySize.y * 0.40f;
            float boxH = g_matches.size() * lineH + 8.0f;
            if (showAll && boxH > maxH) boxH = maxH;
            const float valueOffset = ImGui::GetContentRegionAvail().x * 0.40f;

            ImGui::PushStyleColor(ImGuiCol_ChildBg, g.hintColor);
            ImGui::PushStyleColor(ImGuiCol_Border, g.outboxColor);
            ImGui::BeginChild("##con_hints", ImVec2(0.0f, boxH), true,
                              showAll ? 0 : ImGuiWindowFlags_NoScrollbar);
            for (const auto& match : g_matches)
            {
                if (const char* desc = cmd_desc(match))
                {
                    ImGui::TextColored(col_cmdMatch, "%s", match.c_str());
                    ImGui::SameLine(valueOffset);
                    ImGui::TextColored(col_desc, "%s", desc);
                    continue;
                }
                // Anything that isn't one of our console commands is a dvar, so
                // keep the original (non-blue) dvar color even if it isn't
                // currently resolvable in the engine table.
                dvar_t* dvar = Dvar::FindVar(match.c_str());
                ImGui::TextColored(col_dvarMatch, "%s", match.c_str());
                if (dvar)
                {
                    ImGui::SameLine(valueOffset);
                    if (const char* value = Dvar::ValueToString(match.c_str()))
                        ImGui::TextColored(col_dvarValue, "%s", value);
                    else
                        ImGui::TextColored(col_dvarValue, "%.3f", Dvar::GetFloat(match.c_str()));
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
        }

        void draw_output()
        {
            ImGui::Separator();

            ImGui::PushStyleColor(ImGuiCol_ChildBg,      col_outputWindow);
            ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,   col_outputBar);
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, col_outputSlider);
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, col_outputSlider);
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,  col_outputSlider);

            const ImGuiStyle& style = ImGui::GetStyle();
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, style.ItemSpacing.y * g.lineHeight));

            ImGui::BeginChild("##con_output", ImVec2(0.0f, 0.0f), true);
            {
                const float alphaMul = clampf(g.textAlpha / 100.0f, 0.0f, 1.0f);
                const std::string filt = to_lower(g.filter);

                std::lock_guard<std::mutex> lock(g_outputMutex);
                for (const auto& line : g_output)
                {
                    if (!filt.empty() && to_lower(strip_codes(line)).find(filt) == std::string::npos)
                        continue;

                    if (g.textAlign != 0)
                    {
                        const float tw = ImGui::CalcTextSize(strip_codes(line).c_str()).x;
                        const float avail = ImGui::GetContentRegionAvail().x;
                        float off = (g.textAlign == 1) ? (avail - tw) * 0.5f : (avail - tw);
                        if (off > 0.0f)
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);
                    }
                    render_colored(line, alphaMul);
                }
            }
            if (g.scrollSpeed != 1.0f && ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f)
                ImGui::SetScrollY(ImGui::GetScrollY() - ImGui::GetIO().MouseWheel * (g.scrollSpeed - 1.0f) * ImGui::GetTextLineHeightWithSpacing());

            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
                ImGui::SetScrollHereY(1.0f);

            ImGui::Separator();
            ImGui::TextColored(g.titleColor, "%s %s", g.name.c_str(), kVersion());
            ImGui::EndChild();

            ImGui::PopStyleVar();
            ImGui::PopStyleColor(5);
        }

        // Error toast shown on-screen when the console is closed (con_notify).
        void draw_notify()
        {
            if (g_open || g_notifyMsg.empty()) return;
            if (ImGui::GetTime() > g_notifyUntil) return;

            const ImGuiIO& io = ImGui::GetIO();
            ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 12.0f, 12.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.0f, 0.0f, 0.85f));
            ImGui::Begin("##con_notify", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
            render_colored(g_notifyMsg, 1.0f);
            ImGui::End();
            ImGui::PopStyleColor();
        }
    }

    void Setup()
    {
        // Apply the detected game's default theme/name first; a saved
        // console.json (loaded next) overrides these with the user's choices.
        const GameProfile& gp = Game::Current();
        g.name        = gp.displayName;
        g.titleColor  = ImVec4(gp.titleColor[0],  gp.titleColor[1],  gp.titleColor[2],  gp.titleColor[3]);
        g.inboxColor  = ImVec4(gp.inboxColor[0],  gp.inboxColor[1],  gp.inboxColor[2],  gp.inboxColor[3]);
        g.outboxColor = ImVec4(gp.outboxColor[0], gp.outboxColor[1], gp.outboxColor[2], gp.outboxColor[3]);

        ensure_dir(base_dir()); // create <game>/<baseFolder> on load
        load_config();          // load console.json if present (no folder made)

        for (const auto& c : kCmds)
            g_names.push_back(c.name);
        for (const char* d : dvar_list::kGhostsDvars)
            g_names.push_back(d);

        load_player(); // from assets/player/settings.ini (applied once verified)

        // Don't touch the engine here - the game's dvar system may not exist
        // yet at inject. Enumeration happens lazily in Draw once verified.
        char ready[128];
        std::snprintf(ready, sizeof(ready),
            "^7%s %s ready on ^5%s^7. ^5help^7 lists commands, ^5diag^7 shows engine status.",
            Game::Current().displayName, Game::Current().version, Game::ExeName());
        Print(ready);
    }

    void Toggle()
    {
        g_open = !g_open;
        g_matches.clear();
        g_historyIndex = -1;
        g_input[0] = '\0';
        if (g_open)
            g_focusInput = true;
    }

    void ToggleOutput()
    {
        if (!g_open)
            Toggle();
        g_outputVisible = !g_outputVisible;
    }

    bool IsOpen() { return g_open; }

    int ToggleVk() { return g_toggleVk; }

    void BeginFrame()
    {
        // Apply a saved player name as soon as the engine is verified (even if
        // the console is never opened).
        if (g_namePending && Dvar::Verified())
        {
            Dvar::Execute(("name \"" + g_playerName + "\"").c_str());
            g_namePending = false;
        }

        // Keep the saved FOV applied; reapply periodically so a mission/level
        // transition that resets it gets overridden (effectively "saves" it).
        if (g_fov > 0.0f && Dvar::Verified())
        {
            static unsigned long long lastFov = 0;
            const unsigned long long now = GetTickCount64();
            if (g_fovPending || now - lastFov > 3000)
            {
                char b[32];
                std::snprintf(b, sizeof(b), "cg_fovScale %.3f", g_fov);
                Dvar::Execute(b);
                g_fovPending = false;
                lastFov = now;
            }
        }

        // Keep sv_cheats enabled (some builds reset it), so cheats stay active.
        if (g_forceCheats && Dvar::Verified())
        {
            static unsigned long long lastCheat = 0;
            const unsigned long long now2 = GetTickCount64();
            if (now2 - lastCheat > 1000)
            {
                Dvar::Set("sv_cheats", "1");
                lastCheat = now2;
            }
        }

        if (!g_fontDirty)
            return;
        g_fontDirty = false;

        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->Clear();

        const std::string path = resolve_font(g.fontFace, g.fontWeight);
        const float px = clampf(g.fontSize, 6.0f, 72.0f);

        g_font = nullptr;
        if (!path.empty())
        {
            g_font = io.Fonts->AddFontFromFileTTF(path.c_str(), px);
            if (g_font) g_fontBase = px;
        }
        if (!g_font)
        {
            g_font = io.Fonts->AddFontDefault();
            g_fontBase = 13.0f; // ImGui's built-in font base size
        }
        io.Fonts->Build();
        ImGui_ImplDX11_InvalidateDeviceObjects(); // backend recreates on next NewFrame
    }

    void AddName(const std::string& name)
    {
        if (std::find(g_names.begin(), g_names.end(), name) == g_names.end())
            g_names.push_back(name);
    }

    void Print(const std::string& line)
    {
        std::string entry = line;
        if (g.timestamp)
        {
            std::time_t t = std::time(nullptr);
            std::tm tmv{};
            localtime_s(&tmv, &t);
            char b[16];
            std::snprintf(b, sizeof(b), "[%02d:%02d:%02d] ", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
            entry = std::string(b) + entry;
        }

        if (g.logFile)
        {
            std::ofstream lf(logfile_path(), std::ios::app);
            if (lf) lf << strip_codes(entry) << "\n";
        }

        // Surface error lines (^1...) as an on-screen toast when enabled.
        if (g.notify && entry.size() >= 2 && entry[0] == '^' && entry[1] == '1' && ImGui::GetCurrentContext())
        {
            g_notifyMsg = entry;
            g_notifyUntil = ImGui::GetTime() + 3.0;
        }

        std::lock_guard<std::mutex> lock(g_outputMutex);
        g_output.push_back(entry);
        while (static_cast<int>(g_output.size()) > g.logLines)
            g_output.pop_front();
    }

    void Printf(const char* fmt, ...)
    {
        char buffer[1024] = {};
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buffer, sizeof(buffer), fmt, ap);
        va_end(ap);

        const std::string text(buffer);
        size_t start = 0;
        while (start <= text.size())
        {
            const size_t nl = text.find('\n', start);
            if (nl == std::string::npos)
            {
                if (start < text.size())
                    Print(text.substr(start));
                break;
            }
            Print(text.substr(start, nl - start));
            start = nl + 1;
        }
    }

    void Draw()
    {
        draw_notify(); // toast shows even when the console is closed

        // Watermark (top-left). Hidden while the console is open.
        if (g_watermark && !g_open)
        {
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            const ImU32 col = ImGui::ColorConvertFloat4ToU32(g.titleColor);
            static std::string wm = std::string(Game::Current().displayName) + " V" + Game::Current().version;
            fg->AddText(ImVec2(11.0f, 9.0f), IM_COL32(0, 0, 0, 180), wm.c_str()); // shadow
            fg->AddText(ImVec2(10.0f, 8.0f), col, wm.c_str());
        }

        // FPS counter (top-right), drawn every frame when enabled.
        if (g_pfps)
        {
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            char buf[32];
            if (g_fpsLabels)
                std::snprintf(buf, sizeof(buf), "FPS: %.0f", ImGui::GetIO().Framerate);
            else
                std::snprintf(buf, sizeof(buf), "%.0f", ImGui::GetIO().Framerate);
            const float x = ImGui::GetIO().DisplaySize.x - 96.0f;
            fg->AddText(ImVec2(x + 1.0f, 9.0f), IM_COL32(0, 0, 0, 180), buf);
            fg->AddText(ImVec2(x, 8.0f), IM_COL32(255, 255, 255, 255), buf);
        }

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigInputTextCursorBlink = g.cursorBlink;

        // fade in/out (con_fade / con_fadetime)
        const float target = g_open ? 1.0f : 0.0f;
        if (g.fade && g.fadeTime > 0.0f)
        {
            const float step = io.DeltaTime / g.fadeTime;
            if (g_fadeAlpha < target) g_fadeAlpha = (g_fadeAlpha + step > target) ? target : g_fadeAlpha + step;
            else if (g_fadeAlpha > target) g_fadeAlpha = (g_fadeAlpha - step < target) ? target : g_fadeAlpha - step;
        }
        else
        {
            g_fadeAlpha = target;
        }
        if (!g_open && g_fadeAlpha <= 0.001f)
            return;

        try_load_dvars();

        // approximate blur: dim the screen behind the console
        if (g.blur)
        {
            ImDrawList* dl = ImGui::GetBackgroundDrawList();
            dl->AddRectFilled(ImVec2(0.0f, 0.0f), io.DisplaySize, IM_COL32(0, 0, 0, static_cast<int>(130 * g_fadeAlpha)));
        }

        // --- position from anchor (0-8, 3x3 grid) + posx/posy as margin ---
        const int col = g.anchor % 3;   // 0 L, 1 C, 2 R
        const int row = g.anchor / 3;   // 0 T, 1 M, 2 B
        const float sx = (col == 0) ? g.posx : (col == 1 ? io.DisplaySize.x * 0.5f : io.DisplaySize.x - g.posx);
        const float sy = (row == 0) ? g.posy : (row == 1 ? io.DisplaySize.y * 0.5f : io.DisplaySize.y - g.posy);
        ImGui::SetNextWindowPos(ImVec2(sx, sy), ImGuiCond_Always, ImVec2(col * 0.5f, row * 0.5f));

        const float w = (g.width > 0.0f) ? g.width : (io.DisplaySize.x - g.posx * 2.0f);
        if (g_outputVisible)
        {
            const float h = (g.height > 0.0f) ? g.height : (io.DisplaySize.y - g.posy * 2.0f);
            ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
        }
        else
        {
            ImGui::SetNextWindowSize(ImVec2(w, 0.0f), ImGuiCond_Always);
        }

        ImVec4 bg = g.inboxColor;
        if (!g.inboxVisible)
            bg.w = 0.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(g.padding, g.padding));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, g.corners == 0 ? 0.0f : 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, g.outboxWidth);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, clampf((g.opacity / 100.0f) * g_fadeAlpha, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, bg);
        ImGui::PushStyleColor(ImGuiCol_Border,   g.outboxColor);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoScrollbar;
        if (!g_outputVisible)
            flags |= ImGuiWindowFlags_AlwaysAutoResize;

        ImGui::Begin("##iw6sp_console", nullptr, flags);
        if (g_font) ImGui::PushFont(g_font);
        ImGui::SetWindowFontScale(g.fontSize / g_fontBase);

        ImGui::TextColored(g.titleColor, "%s: %s>", g.name.c_str(), kVersion());
        ImGui::SameLine(0.0f, 6.0f);

        if (g_focusInput)
        {
            ImGui::SetKeyboardFocusHere();
            g_focusInput = false;
        }

        ImGui::PushItemWidth(-1.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGuiInputTextFlags itf =
            ImGuiInputTextFlags_EnterReturnsTrue   |
            ImGuiInputTextFlags_CallbackHistory    |
            ImGuiInputTextFlags_CallbackCompletion |
            ImGuiInputTextFlags_CallbackCharFilter |
            ImGuiInputTextFlags_CallbackEdit;
        if (g.secureInput)
            itf |= ImGuiInputTextFlags_Password;
        const bool entered = ImGui::InputText("##iw6sp_input", g_input, sizeof(g_input), itf, input_callback);
        ImGui::PopStyleColor();
        ImGui::PopItemWidth();

        if (entered)
        {
            const std::string line = stripped(g_input);
            if (!line.empty())
            {
                if (g_history.empty() || g_history.front() != g_input)
                    g_history.push_front(g_input);
                while (static_cast<int>(g_history.size()) > g.historySize)
                    g_history.pop_back();
                g_historyIndex = -1;
                execute(line);
            }
            g_input[0] = '\0';
            g_matches.clear();
            g_focusInput = true;
        }

        draw_hints();

        if (g_outputVisible)
            draw_output();

        if (g_font) ImGui::PopFont();
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);
    }
}
