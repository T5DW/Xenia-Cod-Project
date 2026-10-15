// BO1 Live: Black Ops 1 (Xbox 360) "Xbox Live" + DemonWare server for Xenia, with a Windows GUI.
//   bo1live.exe            the window
//   bo1live.exe --nogui    console only (also the only mode on Linux), see --help
#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "server.hpp"

namespace fs = std::filesystem;

// ------------------------------------------------------------------------------- settings file

static fs::path exe_dir() {
#ifdef _WIN32
	wchar_t buf[MAX_PATH];
	GetModuleFileNameW(nullptr, buf, MAX_PATH);
	return fs::path(buf).parent_path();
#else
	std::error_code ec;
	fs::path p = fs::read_symlink("/proc/self/exe", ec);
	return ec ? fs::current_path() : p.parent_path();
#endif
}

static fs::path ini_path() { return exe_dir() / "bo1live.ini"; }

static void load_config(bo1::Config& c) {
	c.data_dir = (exe_dir() / "data").string();
	c.wad_dir = (exe_dir() / "wad").string();
	std::ifstream f(ini_path());
	std::string line;
	while (std::getline(f, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		std::string k = line.substr(0, eq), v = line.substr(eq + 1);
		if (k == "ip") c.ip = v;
		else if (k == "http_port") c.http_port = std::atoi(v.c_str());
		else if (k == "dw_port") c.dw_port = std::atoi(v.c_str());
		else if (k == "bots") c.bots = v == "1";
		else if (k == "bot_fill") c.bot_fill = std::atoi(v.c_str());
		else if (k == "bot_difficulty") c.bot_difficulty = v;
		else if (k == "start_alone") c.start_alone = v == "1";
		else if (k == "lan_discovery") c.lan_discovery = v == "1";
		else if (k == "motd") c.motd = v;
		else if (k == "peer" && !v.empty()) c.peers.push_back(v);
	}
	if (c.http_port <= 0 || c.http_port > 65535) c.http_port = 36000;
	if (c.dw_port <= 0 || c.dw_port > 65535) c.dw_port = 1020;
	if (c.bot_fill < 2 || c.bot_fill > 18) c.bot_fill = 12;
	if (c.bot_difficulty != "easy" && c.bot_difficulty != "normal" && c.bot_difficulty != "hard" && c.bot_difficulty != "fu")
		c.bot_difficulty = "normal";
}

static void save_config(const bo1::Config& c) {
	std::ofstream f(ini_path(), std::ios::trunc);
	f << "ip=" << c.ip << "\nhttp_port=" << c.http_port << "\ndw_port=" << c.dw_port << "\nbots=" << (c.bots ? 1 : 0)
	  << "\nbot_fill=" << c.bot_fill << "\nbot_difficulty=" << c.bot_difficulty << "\nstart_alone=" << (c.start_alone ? 1 : 0)
	  << "\nlan_discovery=" << (c.lan_discovery ? 1 : 0) << "\nmotd=" << c.motd << "\n";
	for (const auto& p : c.peers) f << "peer=" << p << "\n";
}

// ----------------------------------------------------------------------------------- console

static int run_console(bo1::Config cfg) {
	bo1::Server server([](const std::string& line) {
		static std::mutex m;
		std::lock_guard<std::mutex> g(m);
		std::cout << line << std::endl;
	});
	std::string err;
	if (!server.start(cfg, err)) {
		std::cerr << "error: " << err << std::endl;
		return 1;
	}
	std::cout << "Press Ctrl+C to stop." << std::endl;
	for (;;) std::this_thread::sleep_for(std::chrono::hours(1));
}

static const char* kHelp =
	"BO1 Live server\n"
	"  --nogui               run in the console\n"
	"  --ip ADDRESS          the address other players reach this PC on (default: LAN IP)\n"
	"  --http-port N         Xenia web API port (default 36000)\n"
	"  --dw-port N           DemonWare port (default 1020)\n"
	"  --data DIR            stats, publisher files and the log\n"
	"  --peer HOST[:PORT]    link another BO1 Live server (repeatable)\n"
	"  --bots N              bots fill matches up to N players (0 = off)\n"
	"  --difficulty NAME     easy, normal, hard or fu\n"
	"  --wait-for-players    don't start matches with just you\n"
	"  --no-lan              don't look for servers on the LAN\n";

static bool parse_args(int argc, char** argv, bo1::Config& c, bool& gui) {
	for (int i = 1; i < argc; ++i) {
		std::string a = argv[i];
		auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
		if (a == "--nogui") gui = false;
		else if (a == "--ip") c.ip = next();
		else if (a == "--http-port") c.http_port = std::atoi(next().c_str());
		else if (a == "--dw-port") c.dw_port = std::atoi(next().c_str());
		else if (a == "--data") c.data_dir = next();
		else if (a == "--wad") c.wad_dir = next();
		else if (a == "--peer") c.peers.push_back(next());
		else if (a == "--bots") {
			int n = std::atoi(next().c_str());
			c.bots = n >= 2;
			if (c.bots) c.bot_fill = n;
		}
		else if (a == "--difficulty") c.bot_difficulty = next();
		else if (a == "--wait-for-players") c.start_alone = false;
		else if (a == "--no-lan") c.lan_discovery = false;
		else if (a == "--help" || a == "-h" || a == "/?") {
			std::cout << kHelp;
			return false;
		}
	}
	return true;
}

#ifndef _WIN32

int main(int argc, char** argv) {
	bo1::Config cfg;
	load_config(cfg);
	bool gui = false;
	if (!parse_args(argc, argv, cfg, gui)) return 0;
	return run_console(cfg);
}

#else

// --------------------------------------------------------------------------------------- GUI

#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>

enum : int {
	ID_IP = 100, ID_HTTP, ID_DW, ID_START, ID_API, ID_COPY, ID_OPEN, ID_BOTS, ID_FILL, ID_FILL_SPIN, ID_DIFF,
	ID_ALONE, ID_PEERS, ID_PEER_EDIT, ID_PEER_ADD, ID_PEER_DEL, ID_LAN, ID_ONLINE, ID_PLAYERS, ID_LOG, ID_STATE,
	ID_FOLDER,
};
constexpr UINT WM_APP_LOG = WM_APP + 1;
constexpr UINT_PTR TIMER_STATUS = 1;

struct App {
	HWND wnd{};
	HFONT font{}, bold{}, big{};
	bo1::Config cfg;
	std::unique_ptr<bo1::Server> server;
	std::map<int, HWND> ctl;
	HBRUSH bg{};
	bool ready = false;  // false while the controls are created and filled
} g;

static std::wstring W(const std::string& s) {
	if (s.empty()) return L"";
	int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
	std::wstring w(static_cast<size_t>(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
	return w;
}

static std::string U(const std::wstring& w) {
	if (w.empty()) return "";
	int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
	std::string s(static_cast<size_t>(n), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
	return s;
}

static std::string text_of(int id) {
	HWND h = g.ctl[id];
	int n = GetWindowTextLengthW(h);
	std::wstring w(static_cast<size_t>(n) + 1, L'\0');
	GetWindowTextW(h, w.data(), n + 1);
	w.resize(static_cast<size_t>(n));
	return U(w);
}

static void set_text(int id, const std::string& s) { SetWindowTextW(g.ctl[id], W(s).c_str()); }
static bool checked(int id) { return Button_GetCheck(g.ctl[id]) == BST_CHECKED; }

static HWND add(int id, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h,
		DWORD ex = 0, HFONT font = nullptr) {
	HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, g.wnd,
		reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
	SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : g.font), TRUE);
	if (id) g.ctl[id] = c;
	return c;
}

static void label(const wchar_t* text, int x, int y, int w, int h = 18, HFONT font = nullptr) {
	add(0, L"STATIC", text, 0, x, y, w, h, 0, font);
}

static void append_log(const std::string& line) {
	HWND h = g.ctl[ID_LOG];
	int len = GetWindowTextLengthW(h);
	if (len > 200000) {  // keep the box small
		SendMessageW(h, EM_SETSEL, 0, 100000);
		SendMessageW(h, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
		len = GetWindowTextLengthW(h);
	}
	SendMessageW(h, EM_SETSEL, static_cast<WPARAM>(len), static_cast<LPARAM>(len));
	std::wstring w = W(line) + L"\r\n";
	SendMessageW(h, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(w.c_str()));
}

static std::string api_address() {
	std::string ip = text_of(ID_IP);
	return "http://" + (ip.empty() ? std::string("127.0.0.1") : ip) + ":" + text_of(ID_HTTP) + "/";
}

static void read_form() {
	g.cfg.ip = text_of(ID_IP);
	g.cfg.http_port = std::atoi(text_of(ID_HTTP).c_str());
	g.cfg.dw_port = std::atoi(text_of(ID_DW).c_str());
	g.cfg.bots = checked(ID_BOTS);
	g.cfg.bot_fill = std::clamp(std::atoi(text_of(ID_FILL).c_str()), 2, 18);
	int d = ComboBox_GetCurSel(g.ctl[ID_DIFF]);
	const char* diffs[] = {"easy", "normal", "hard", "fu"};
	g.cfg.bot_difficulty = diffs[d >= 0 && d < 4 ? d : 1];
	g.cfg.start_alone = checked(ID_ALONE);
	g.cfg.lan_discovery = checked(ID_LAN);
}

static void refresh_peer_list(const std::vector<std::pair<std::string, bool>>* live = nullptr) {
	HWND lb = g.ctl[ID_PEERS];
	int sel = ListBox_GetCurSel(lb);
	ListBox_ResetContent(lb);
	std::vector<std::string> shown;
	auto norm = [](std::string s) {
		if (s.rfind("http://", 0) == 0) s = s.substr(7);
		while (!s.empty() && s.back() == '/') s.pop_back();
		if (s.find(':') == std::string::npos) s += ":36000";
		return s;
	};
	for (const auto& p : g.cfg.peers) {
		std::string label = norm(p), state = g.server && g.server->running() ? "  (checking...)" : "";
		if (live)
			for (auto& [addr, ok] : *live)
				if (addr == label) state = ok ? "  (online)" : "  (offline)";
		shown.push_back(label);
		ListBox_AddString(lb, W(label + state).c_str());
	}
	if (live)
		for (auto& [addr, ok] : *live)
			if (std::find(shown.begin(), shown.end(), addr) == shown.end())
				ListBox_AddString(lb, W(addr + (ok ? "  (LAN, online)" : "  (LAN, offline)")).c_str());
	if (sel >= 0) ListBox_SetCurSel(lb, sel);
}

static void update_buttons() {
	bool on = g.server && g.server->running();
	set_text(ID_START, on ? "Stop server" : "Start server");
	EnableWindow(g.ctl[ID_IP], !on);
	EnableWindow(g.ctl[ID_HTTP], !on);
	EnableWindow(g.ctl[ID_DW], !on);
	set_text(ID_STATE, on ? "Running" : "Stopped");
	set_text(ID_API, api_address());
	InvalidateRect(g.ctl[ID_STATE], nullptr, TRUE);
}

static void settings_changed() {
	if (!g.ready) return;
	read_form();
	save_config(g.cfg);
	if (g.server && g.server->running()) g.server->update_config(g.cfg);
	EnableWindow(g.ctl[ID_FILL], g.cfg.bots);
	EnableWindow(g.ctl[ID_FILL_SPIN], g.cfg.bots);
	EnableWindow(g.ctl[ID_DIFF], g.cfg.bots);
}

static void toggle_server() {
	if (g.server && g.server->running()) {
		g.server->stop();
		update_buttons();
		return;
	}
	read_form();
	if (g.cfg.http_port <= 0 || g.cfg.http_port > 65535 || g.cfg.dw_port <= 0 || g.cfg.dw_port > 65535) {
		MessageBoxW(g.wnd, L"Enter a web port and a DemonWare port between 1 and 65535 (normally 36000 and 1020).",
			L"BO1 Live", MB_OK | MB_ICONWARNING);
		return;
	}
	save_config(g.cfg);
	std::string err;
	if (!g.server->start(g.cfg, err)) {
		append_log("error: " + err);
		MessageBoxW(g.wnd, W(err).c_str(), L"BO1 Live", MB_OK | MB_ICONERROR);
	}
	update_buttons();
	refresh_peer_list();
}

static void on_timer() {
	if (!g.server || !g.server->running()) {
		set_text(ID_ONLINE, "Online: 0");
		return;
	}
	bo1::Status st = g.server->status();
	set_text(ID_ONLINE, "Online: " + std::to_string(st.online) + "    Matches here: " +
		std::to_string(st.matches_local) + "    Saved players: " + std::to_string(st.registered));
	HWND lb = g.ctl[ID_PLAYERS];
	ListBox_ResetContent(lb);
	for (auto& p : st.players)
		ListBox_AddString(lb, W(p.gamertag + "   (" + (p.minutes < 1 ? std::string("just now") : std::to_string(p.minutes) + " min") + ")").c_str());
	if (st.players.empty()) ListBox_AddString(lb, L"Nobody is online yet.");
	refresh_peer_list(&st.peers);
}

static void create_controls() {
	const int M = 14;
	int y = 12;
	label(L"BO1 Live", M, y, 300, 30, g.big);
	add(ID_STATE, L"STATIC", L"Stopped", SS_RIGHT, 560, y + 8, 200, 20, 0, g.bold);
	y += 40;

	// --- server
	add(0, L"BUTTON", L"Server", BS_GROUPBOX, M, y, 360, 196);
	label(L"Your IP (others connect to this)", M + 12, y + 24, 200);
	add(ID_IP, WC_COMBOBOXW, L"", CBS_DROPDOWN | WS_VSCROLL | WS_TABSTOP, M + 12, y + 44, 200, 200);
	for (auto& ip : net::local_ips()) ComboBox_AddString(g.ctl[ID_IP], W(ip).c_str());
	label(L"Web port", M + 222, y + 24, 60);
	add(ID_HTTP, L"EDIT", L"", ES_NUMBER | WS_TABSTOP, M + 222, y + 44, 56, 24, WS_EX_CLIENTEDGE);
	label(L"DW port", M + 288, y + 24, 60);
	add(ID_DW, L"EDIT", L"", ES_NUMBER | WS_TABSTOP, M + 288, y + 44, 56, 24, WS_EX_CLIENTEDGE);
	add(ID_START, L"BUTTON", L"Start server", BS_DEFPUSHBUTTON | WS_TABSTOP, M + 12, y + 80, 160, 32, 0, g.bold);
	add(ID_OPEN, L"BUTTON", L"Status page", WS_TABSTOP, M + 182, y + 80, 80, 32);
	add(ID_FOLDER, L"BUTTON", L"Data folder", WS_TABSTOP, M + 268, y + 80, 80, 32);
	label(L"Put this in Xenia's config as api_address:", M + 12, y + 124, 330);
	add(ID_API, L"EDIT", L"", ES_READONLY | ES_AUTOHSCROLL, M + 12, y + 144, 260, 24, WS_EX_CLIENTEDGE);
	add(ID_COPY, L"BUTTON", L"Copy", WS_TABSTOP, M + 278, y + 143, 70, 26);

	// --- bots
	int bx = M + 372;
	add(0, L"BUTTON", L"Bots in Xbox Live matches", BS_GROUPBOX, bx, y, 388, 196);
	add(ID_BOTS, L"BUTTON", L"Fill matchmaking games with bots", BS_AUTOCHECKBOX | WS_TABSTOP, bx + 12, y + 24, 360, 22);
	label(L"Players per match (bots + people)", bx + 12, y + 56, 220);
	add(ID_FILL, L"EDIT", L"12", ES_NUMBER | WS_TABSTOP, bx + 240, y + 53, 50, 24, WS_EX_CLIENTEDGE);
	HWND spin = CreateWindowExW(0, UPDOWN_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT |
		UDS_ARROWKEYS, 0, 0, 0, 0, g.wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_FILL_SPIN)), GetModuleHandleW(nullptr), nullptr);
	g.ctl[ID_FILL_SPIN] = spin;
	SendMessageW(spin, UDM_SETBUDDY, reinterpret_cast<WPARAM>(g.ctl[ID_FILL]), 0);
	SendMessageW(spin, UDM_SETRANGE32, 2, 18);
	label(L"Difficulty", bx + 12, y + 90, 100);
	add(ID_DIFF, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP, bx + 240, y + 86, 130, 200);
	for (const wchar_t* d : {L"Recruit (easy)", L"Regular (normal)", L"Hardened (hard)", L"Veteran (fu)"})
		ComboBox_AddString(g.ctl[ID_DIFF], d);
	add(ID_ALONE, L"BUTTON", L"Start matches without waiting for more people", BS_AUTOCHECKBOX | WS_TABSTOP, bx + 12, y + 124, 370, 22);
	label(L"Bots join when the match starts and take the empty spots.\nChanges apply the next time the game signs in.",
		bx + 12, y + 152, 370, 36);
	y += 208;

	// --- peers
	add(0, L"BUTTON", L"Other servers (their matches show up in your matchmaking)", BS_GROUPBOX, M, y, 360, 220);
	add(ID_PEERS, L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT, M + 12, y + 24, 336, 104, WS_EX_CLIENTEDGE);
	add(ID_PEER_EDIT, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, M + 12, y + 136, 200, 24, WS_EX_CLIENTEDGE);
	SendMessageW(g.ctl[ID_PEER_EDIT], EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"friend's IP, e.g. 203.0.113.7"));
	add(ID_PEER_ADD, L"BUTTON", L"Add", WS_TABSTOP, M + 218, y + 135, 60, 26);
	add(ID_PEER_DEL, L"BUTTON", L"Remove", WS_TABSTOP, M + 284, y + 135, 64, 26);
	add(ID_LAN, L"BUTTON", L"Find servers on my network automatically", BS_AUTOCHECKBOX | WS_TABSTOP, M + 12, y + 168, 330, 22);
	label(L"Add a friend's IP to play in their matches.", M + 12, y + 194, 330);

	// --- online
	add(0, L"BUTTON", L"Online now", BS_GROUPBOX, bx, y, 388, 220);
	add(ID_ONLINE, L"STATIC", L"Online: 0", 0, bx + 12, y + 24, 368, 20, 0, g.bold);
	add(ID_PLAYERS, L"LISTBOX", L"", WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOSEL, bx + 12, y + 48, 364, 160, WS_EX_CLIENTEDGE);
	y += 232;

	add(ID_LOG, L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, M, y, 746, 170, WS_EX_CLIENTEDGE);
}

static void load_form() {
	std::string ip = g.cfg.ip.empty() ? net::lan_ip() : g.cfg.ip;
	set_text(ID_IP, ip);
	set_text(ID_HTTP, std::to_string(g.cfg.http_port));
	set_text(ID_DW, std::to_string(g.cfg.dw_port));
	Button_SetCheck(g.ctl[ID_BOTS], g.cfg.bots ? BST_CHECKED : BST_UNCHECKED);
	SendMessageW(g.ctl[ID_FILL_SPIN], UDM_SETPOS32, 0, std::clamp(g.cfg.bot_fill, 2, 18));
	int d = g.cfg.bot_difficulty == "easy" ? 0 : g.cfg.bot_difficulty == "hard" ? 2 : g.cfg.bot_difficulty == "fu" ? 3 : 1;
	ComboBox_SetCurSel(g.ctl[ID_DIFF], d);
	Button_SetCheck(g.ctl[ID_ALONE], g.cfg.start_alone ? BST_CHECKED : BST_UNCHECKED);
	Button_SetCheck(g.ctl[ID_LAN], g.cfg.lan_discovery ? BST_CHECKED : BST_UNCHECKED);
	refresh_peer_list();
	update_buttons();
	EnableWindow(g.ctl[ID_FILL], g.cfg.bots);
	EnableWindow(g.ctl[ID_FILL_SPIN], g.cfg.bots);
	EnableWindow(g.ctl[ID_DIFF], g.cfg.bots);
	g.ready = true;
}

static void copy_to_clipboard(const std::string& s) {
	if (!OpenClipboard(g.wnd)) return;
	EmptyClipboard();
	std::wstring w = W(s);
	HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
	if (mem) {
		std::memcpy(GlobalLock(mem), w.c_str(), (w.size() + 1) * sizeof(wchar_t));
		GlobalUnlock(mem);
		SetClipboardData(CF_UNICODETEXT, mem);
	}
	CloseClipboard();
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
	case WM_CREATE:
		g.wnd = h;
		create_controls();
		load_form();
		SetTimer(h, TIMER_STATUS, 1000, nullptr);
		return 0;
	case WM_APP_LOG: {
		auto* s = reinterpret_cast<std::string*>(lp);
		append_log(*s);
		delete s;
		return 0;
	}
	case WM_TIMER:
		on_timer();
		return 0;
	case WM_CTLCOLORSTATIC: {
		HDC dc = reinterpret_cast<HDC>(wp);
		if (reinterpret_cast<HWND>(lp) == g.ctl[ID_STATE]) {
			bool on = g.server && g.server->running();
			SetTextColor(dc, on ? RGB(30, 140, 60) : RGB(170, 60, 50));
		}
		if (reinterpret_cast<HWND>(lp) == g.ctl[ID_API] || reinterpret_cast<HWND>(lp) == g.ctl[ID_LOG]) break;
		SetBkMode(dc, TRANSPARENT);
		return reinterpret_cast<LRESULT>(g.bg);
	}
	case WM_COMMAND: {
		int id = LOWORD(wp), code = HIWORD(wp);
		switch (id) {
		case ID_START: toggle_server(); break;
		case ID_COPY: copy_to_clipboard(api_address()); append_log("copied " + api_address()); break;
		case ID_OPEN: {
			std::wstring url = L"http://127.0.0.1:" + W(text_of(ID_HTTP)) + L"/";
			ShellExecuteW(h, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			break;
		}
		case ID_FOLDER: {
			std::error_code ec;
			fs::create_directories(g.cfg.data_dir, ec);
			ShellExecuteW(h, L"open", fs::path(g.cfg.data_dir).wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			break;
		}
		case ID_BOTS: case ID_ALONE: case ID_LAN:
			if (code == BN_CLICKED) settings_changed();
			break;
		case ID_DIFF:
			if (code == CBN_SELCHANGE) settings_changed();
			break;
		case ID_FILL:
			if (code == EN_KILLFOCUS) settings_changed();
			break;
		case ID_IP: case ID_HTTP:
			if (code == EN_CHANGE || code == CBN_EDITCHANGE || code == CBN_SELCHANGE) {
				if (code == CBN_SELCHANGE) {
					int i = ComboBox_GetCurSel(g.ctl[ID_IP]);
					wchar_t buf[64] = {};
					ComboBox_GetLBText(g.ctl[ID_IP], i, buf);
					SetWindowTextW(g.ctl[ID_IP], buf);
				}
				set_text(ID_API, api_address());
			}
			break;
		case ID_PEER_ADD: {
			std::string p = text_of(ID_PEER_EDIT);
			p.erase(0, p.find_first_not_of(" \t"));
			p.erase(p.find_last_not_of(" \t") + 1);
			if (!p.empty() && std::find(g.cfg.peers.begin(), g.cfg.peers.end(), p) == g.cfg.peers.end()) {
				g.cfg.peers.push_back(p);
				set_text(ID_PEER_EDIT, "");
				settings_changed();
				refresh_peer_list();
			}
			break;
		}
		case ID_PEER_DEL: {
			int i = ListBox_GetCurSel(g.ctl[ID_PEERS]);
			if (i >= 0 && i < static_cast<int>(g.cfg.peers.size())) {
				g.cfg.peers.erase(g.cfg.peers.begin() + i);
				settings_changed();
				refresh_peer_list();
			}
			break;
		}
		}
		return 0;
	}
	case WM_NOTIFY: {
		auto* n = reinterpret_cast<NMHDR*>(lp);
		if (n->idFrom == ID_FILL_SPIN && n->code == UDN_DELTAPOS) PostMessageW(h, WM_COMMAND, MAKEWPARAM(ID_FILL, EN_KILLFOCUS), 0);
		return 0;
	}
	case WM_GETMINMAXINFO: {
		auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
		mm->ptMinTrackSize = {790, 760};
		return 0;
	}
	case WM_CLOSE:
		if (g.server) g.server->stop();
		DestroyWindow(h);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(h, msg, wp, lp);
}

static int run_gui(HINSTANCE inst, bo1::Config cfg, bool autostart) {
	INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_UPDOWN_CLASS};
	InitCommonControlsEx(&icc);
	g.cfg = cfg;
	NONCLIENTMETRICSW ncm{sizeof(ncm)};
	SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
	g.font = CreateFontIndirectW(&ncm.lfMessageFont);
	LOGFONTW lf = ncm.lfMessageFont;
	lf.lfWeight = FW_BOLD;
	g.bold = CreateFontIndirectW(&lf);
	lf.lfHeight = -22;
	g.big = CreateFontIndirectW(&lf);
	g.bg = GetSysColorBrush(COLOR_BTNFACE);

	g.server = std::make_unique<bo1::Server>([](const std::string& line) {
		if (g.wnd) PostMessageW(g.wnd, WM_APP_LOG, 0, reinterpret_cast<LPARAM>(new std::string(line)));
	});

	WNDCLASSEXW wc{sizeof(wc)};
	wc.lpfnWndProc = wnd_proc;
	wc.hInstance = inst;
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	wc.hbrBackground = g.bg;
	wc.lpszClassName = L"BO1LiveWindow";
	wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
	wc.hIconSm = wc.hIcon;
	RegisterClassExW(&wc);
	RECT r{0, 0, 774, 696};
	AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
	HWND h = CreateWindowExW(0, wc.lpszClassName, L"BO1 Live - Black Ops Xbox Live server for Xenia",
		WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME, CW_USEDEFAULT, CW_USEDEFAULT,
		r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
	ShowWindow(h, SW_SHOWNORMAL);
	UpdateWindow(h);
	append_log("BO1 Live " + std::string(bo1::kVersion) + ". Press Start server, then start Black Ops in Xenia.");
	if (autostart) toggle_server();
	MSG m;
	while (GetMessageW(&m, nullptr, 0, 0)) {
		if (!IsDialogMessageW(h, &m)) {
			TranslateMessage(&m);
			DispatchMessageW(&m);
		}
	}
	return 0;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
	bo1::Config cfg;
	load_config(cfg);
	int argc = 0;
	wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
	std::vector<std::string> args;
	for (int i = 0; i < argc; ++i) args.push_back(U(wargv[i]));
	LocalFree(wargv);
	std::vector<char*> argv;
	for (auto& a : args) argv.push_back(a.data());
	bool gui = true;
	bool autostart = false;
	for (auto& a : args)
		if (a == "--nogui" || a == "--help" || a == "-h" || a == "/?") gui = false;
	if (!gui) {
		if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
		std::freopen("CONOUT$", "w", stdout);
		std::freopen("CONOUT$", "w", stderr);
	}
	if (!parse_args(argc, argv.data(), cfg, gui)) return 0;
	for (auto& a : args)
		if (a == "--start") autostart = true;
	if (!gui) return run_console(cfg);
	return run_gui(inst, cfg, autostart);
}

#endif
