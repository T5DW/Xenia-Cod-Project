#include "server.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "../third_party/json.hpp"
#include "status_page.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace bo1 {

// ---------------------------------------------------------------------------------------- helpers

static std::string upper(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
	return s;
}

static std::string lower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

static double now() {
	using namespace std::chrono;
	return duration_cast<duration<double>>(system_clock::now().time_since_epoch()).count();
}

static std::vector<std::string> split(const std::string& s, char sep) {
	std::vector<std::string> out;
	std::string cur;
	for (char c : s) {
		if (c == sep) { out.push_back(cur); cur.clear(); }
		else cur += c;
	}
	out.push_back(cur);
	return out;
}

static bool read_file(const fs::path& p, std::string& out) {
	std::ifstream f(p, std::ios::binary);
	if (!f) return false;
	std::ostringstream ss;
	ss << f.rdbuf();
	out = ss.str();
	return true;
}

static bool write_file_atomic(const fs::path& p, const std::string& data) {
	std::error_code ec;
	fs::create_directories(p.parent_path(), ec);
	fs::path tmp = p;
	tmp += ".tmp";
	{
		std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
		if (!f) return false;
		f.write(data.data(), static_cast<std::streamsize>(data.size()));
		if (!f) return false;
	}
	fs::rename(tmp, p, ec);
	if (ec) {
		fs::remove(p, ec);
		fs::rename(tmp, p, ec);
	}
	return !ec;
}

static std::string safe_name(std::string name) {
	std::replace(name.begin(), name.end(), '\\', '/');
	name = name.substr(name.find_last_of('/') + 1);
	for (char& c : name)
		if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-')) c = '_';
	while (!name.empty() && name.front() == '.') name.erase(0, 1);
	while (!name.empty() && name.back() == '.') name.pop_back();
	return name.empty() ? "unnamed" : name;
}

static std::string hex16(uint64_t v) {
	char buf[17];
	std::snprintf(buf, sizeof(buf), "%016llX", static_cast<unsigned long long>(v));
	return buf;
}

static std::string timestamp() {
	std::time_t t = std::time(nullptr);
	std::tm tm{};
#ifdef _WIN32
	localtime_s(&tm, &t);
#else
	localtime_r(&t, &tm);
#endif
	char buf[16];
	std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
	return buf;
}

// --------------------------------------------------------------------------- DemonWare typed data

constexpr uint32_t BD_NO_ERROR = 0;
constexpr uint32_t BD_NO_FILE = 1000;

class Writer {
public:
	Writer& raw(const std::string& d) { b += d; return *this; }
	Writer& u8(uint8_t v) { b += char(3); put(&v, 1); return *this; }
	Writer& u32(uint32_t v) { b += char(8); put(&v, 4); return *this; }
	Writer& u64(uint64_t v) { b += char(10); put(&v, 8); return *this; }
	Writer& blob(const std::string& d) {
		b += char(19);
		u32(static_cast<uint32_t>(d.size()));
		b += d;
		return *this;
	}
	const std::string& str() const { return b; }
private:
	void put(const void* p, size_t n) {
		// little-endian on the wire
		const auto* c = static_cast<const unsigned char*>(p);
		uint64_t v = 0;
		std::memcpy(&v, c, n);
		for (size_t i = 0; i < n; ++i) b += static_cast<char>((v >> (8 * i)) & 0xFF);
	}
	std::string b;
};

struct Value {
	int type = 0;  // 1 bool, 3 u8, 6 u16, 7 i32, 8 u32, 9 i64, 10 u64, 13 float, 16 string, 19 blob, 100+ array
	uint64_t num = 0;
	std::string data;
};

static uint64_t le(const std::string& d, size_t pos, size_t n) {
	uint64_t v = 0;
	for (size_t i = 0; i < n; ++i) v |= static_cast<uint64_t>(static_cast<unsigned char>(d[pos + i])) << (8 * i);
	return v;
}

static std::vector<Value> read_values(const std::string& d, size_t pos) {
	static const std::map<int, size_t> fixed{{1, 1}, {2, 1}, {3, 1}, {4, 2}, {5, 2}, {6, 2}, {7, 4}, {8, 4},
		{9, 8}, {10, 8}, {11, 4}, {12, 4}, {13, 4}, {14, 8}, {15, 4}};
	std::vector<Value> out;
	while (pos < d.size()) {
		int t = static_cast<unsigned char>(d[pos++]);
		Value v;
		v.type = t;
		if (auto it = fixed.find(t); it != fixed.end()) {
			if (pos + it->second > d.size()) break;
			v.num = le(d, pos, it->second);
			pos += it->second;
		}
		else if (t == 16 || t == 17) {
			size_t e = d.find('\0', pos);
			if (e == std::string::npos) break;
			v.type = 16;
			v.data = d.substr(pos, e - pos);
			pos = e + 1;
		}
		else if (t == 19) {
			if (pos + 5 > d.size() || d[pos] != 8) break;
			size_t n = static_cast<size_t>(le(d, pos + 1, 4));
			pos += 5;
			if (pos + n > d.size()) break;
			v.data = d.substr(pos, n);
			pos += n;
		}
		else if (t > 100) {
			if (pos + 9 > d.size() || d[pos] != 8) break;
			size_t size = static_cast<size_t>(le(d, pos + 1, 4));
			v.num = le(d, pos + 5, 4);
			pos += 9;
			if (pos + size > d.size()) break;
			v.data = d.substr(pos, size);
			pos += size;
		}
		else break;
		out.push_back(v);
	}
	return out;
}

static const Value* first(const std::vector<Value>& vals, int type) {
	for (const auto& v : vals)
		if (v.type == type) return &v;
	return nullptr;
}

static std::string show(const std::vector<Value>& vals) {
	std::string out;
	for (const auto& v : vals) {
		if (!out.empty()) out += ", ";
		if (v.type == 16) out += "'" + v.data + "'";
		else if (v.type == 19) out += "blob[" + std::to_string(v.data.size()) + "]";
		else if (v.type > 100) out += "array[" + std::to_string(v.num) + "]";
		else if (v.type == 9 || v.type == 10) out += "0x" + hex16(v.num);
		else out += std::to_string(v.num);
	}
	return out;
}

static const std::map<int, const char*> kServices{{4, "bdStats"}, {6, "bdMessaging"}, {8, "bdProfiles"},
	{10, "bdStorage"}, {12, "bdTitleUtilities"}, {23, "bdCounter"}, {27, "bdDML"}, {28, "bdGroup"},
	{38, "bdAntiCheat"}, {50, "bdContentStreaming"}, {52, "bdTags"}, {55, "bdVoteRank"}, {58, "bdPooledStorage"}};
static const std::map<std::pair<int, int>, const char*> kTasks{{{4, 1}, "writeStats"},
	{{4, 3}, "readStatsByEntityID"}, {{4, 4}, "readStatsByRank"}, {{4, 5}, "readStatsByPivot"},
	{{6, 5}, "getNotifications"}, {{8, 1}, "getPublicInfos"}, {{8, 3}, "setPublicInfo"}, {{10, 1}, "uploadFile"},
	{{10, 3}, "getFile"}, {{10, 7}, "getPublisherFile"}, {{12, 3}, "recordEvent"}, {{12, 6}, "getServerTime"},
	{{23, 1}, "incrementCounters"}, {{23, 2}, "getCounterTotals"}, {{27, 1}, "recordIP"}, {{28, 1}, "setGroups"},
	{{28, 4}, "getGroupCounts"}, {{38, 1}, "answerChallenges"}};

// ---------------------------------------------------------------------------------- online WAD

static uint32_t adler32(const std::string& d) {
	uint32_t a = 1, b = 0;
	for (unsigned char c : d) {
		a = (a + c) % 65521;
		b = (b + a) % 65521;
	}
	return (b << 16) | a;
}

// A zlib stream made of stored (uncompressed) deflate blocks: valid for any inflater.
static std::string zlib_stored(const std::string& d) {
	std::string out("\x78\x01", 2);
	size_t pos = 0;
	do {
		size_t n = std::min<size_t>(d.size() - pos, 0xFFFF);
		bool last = pos + n == d.size();
		out += static_cast<char>(last ? 1 : 0);
		out += static_cast<char>(n & 0xFF);
		out += static_cast<char>(n >> 8);
		out += static_cast<char>(~n & 0xFF);
		out += static_cast<char>((~n >> 8) & 0xFF);
		out += d.substr(pos, n);
		pos += n;
	} while (pos < d.size());
	uint32_t a = adler32(d);
	for (int i = 3; i >= 0; --i) out += static_cast<char>((a >> (8 * i)) & 0xFF);
	return out;
}

static void be32(std::string& s, size_t pos, uint32_t v) {
	for (int i = 0; i < 4; ++i) s[pos + i] = static_cast<char>((v >> (24 - 8 * i)) & 0xFF);
}

std::string build_wad(const std::vector<std::pair<std::string, std::string>>& entries) {
	// header: magic, timestamp, count, required ffotd version (0: no ffotd). Entries 0x2c bytes each.
	std::string out(0x10 + 0x2C * entries.size(), '\0');
	be32(out, 0, 0x543377AB);
	be32(out, 4, static_cast<uint32_t>(std::time(nullptr)));
	be32(out, 8, static_cast<uint32_t>(entries.size()));
	be32(out, 12, 0);
	for (size_t i = 0; i < entries.size(); ++i) {
		std::string z = zlib_stored(entries[i].second);
		size_t e = 0x10 + 0x2C * i;
		std::string name = entries[i].first.substr(0, 0x1F);
		std::memcpy(&out[e], name.data(), name.size());
		be32(out, e + 0x20, static_cast<uint32_t>(z.size()));
		be32(out, e + 0x24, static_cast<uint32_t>(entries[i].second.size()));
		be32(out, e + 0x28, static_cast<uint32_t>(out.size()));
		out += z;
	}
	return out;
}

static const char* kMaps[] = {"mp_array", "mp_cracked", "mp_crisis", "mp_firingrange", "mp_duga", "mp_hanoi",
	"mp_cairo", "mp_havoc", "mp_cosmodrome", "mp_nuked", "mp_radiation", "mp_mountain", "mp_villa",
	"mp_russianbase"};

std::string build_playlists(const Config& cfg, const std::string& override_text) {
	// The "settings" block sets dvars as soon as the game reads the playlists. bot_fill is read by
	// the patched _bot.gsc (common_mp.ff): ranked matches get bots up to that many players.
	std::string s = "version 1\n\nsettings\n";
	s += "setdvar bot_fill " + std::to_string(cfg.bots ? std::clamp(cfg.bot_fill, 2, 18) : 0) + "\n";
	s += "setdvar bot_difficulty " + cfg.bot_difficulty + "\n";
	if (cfg.start_alone) s += "setdvar party_minplayers 1\n";
	s += "\n";
	if (!override_text.empty()) {
		// a custom playlist file: keep everything except its own "version" line
		std::istringstream in(override_text);
		std::string line;
		while (std::getline(in, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.rfind("version", 0) == 0) continue;
			s += line + "\n";
		}
		return s;
	}
	struct Gt { const char* id; const char* name; bool team; };
	const Gt gts[] = {{"tdm", "Team Deathmatch", true}, {"dm", "Free-for-All", false},
		{"dom", "Domination", true}, {"sd", "Search and Destroy", true}, {"koth", "Headquarters", true}};
	for (const auto& g : gts) {
		s += std::string("gametype ") + g.id + "\nname english \"" + g.name + "\"\nscript " + g.id + "\n";
		if (g.team) s += "teambased\n";
		s += "\n";
	}
	s += "category core\nname english \"Core\"\ndescription english \"Standard playlists.\"\n\n";
	struct Pl { const char* gt; const char* name; const char* desc; bool team; bool nuke_only; };
	const Pl pls[] = {
		{"tdm", "Team Deathmatch", "Two teams compete to reach the score limit first.", true, false},
		{"dm", "Free-for-All", "Every player for themselves.", false, false},
		{"dom", "Domination", "Capture and hold the flags.", true, false},
		{"sd", "Search and Destroy", "Plant or defuse the bomb. No respawns.", true, false},
		{"koth", "Headquarters", "Capture and hold the headquarters.", true, false},
		{"tdm", "Nuketown 24/7", "Team Deathmatch on Nuketown, all day, every day.", true, true},
	};
	int n = 1;
	for (const auto& p : pls) {
		s += "playlist " + std::to_string(n++) + "\nname english \"" + p.name + "\"\ndescription english \"" + p.desc +
			"\"\nranked\nmaxparty " + (p.team ? "6" : "1") + "\nminparty 1\ncategories core\nrule party_maxplayers " +
			(p.team ? "12" : "18") + "\n";
		for (const char* m : kMaps) {
			if (p.nuke_only && std::strcmp(m, "mp_nuked") != 0) continue;
			s += std::string(m) + "," + p.gt + ",1\n";
		}
		s += "\n";
	}
	return s;
}

// ------------------------------------------------------------------------------------ state

struct Session {
	std::string id;
	int flags = 0;
	std::string host_address, mac_address, xuid, title, media_id, version;
	int port = 0;
	int public_slots = 0, private_slots = 0;
	std::map<std::string, bool> players;  // xuid -> private slot
	std::vector<std::string> properties, context;
	double created = 0;
};

struct Player {
	std::string xuid, gamertag, machine_id, host_address, mac_address, presence;
	std::map<std::string, std::vector<std::string>> settings;  // title -> base64 settings
	double seen = 0;
};

struct DwClient {
	std::string peer;
	uint64_t owner = 0;
	double since = 0;
	sock_t sock = BAD_SOCK;
};

struct Peer {
	std::string host;
	int port = 36000;
	bool reachable = false;
	bool discovered = false;  // found on the LAN, not typed in
	std::string label() const { return host + ":" + std::to_string(port); }
};

static const std::string kDefaultGamerpic =
	"QGQADwAAAAAEAAAAAAAAAAAAADIwA5AAAEYARgBGAEUAMAA3AEQAMQAwADAAMAAyADAAMAAwADkAMAAwADAAMQAwADAAMAA5AAA=";
constexpr uint32_t kGamerpicId = 0x4064000F;

static std::string b64decode(const std::string& in) {
	static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	int val = 0, bits = -8;
	for (unsigned char c : in) {
		size_t p = chars.find(static_cast<char>(c));
		if (p == std::string::npos) break;
		val = (val << 6) + static_cast<int>(p);
		bits += 6;
		if (bits >= 0) {
			out += static_cast<char>((val >> bits) & 0xFF);
			bits -= 8;
		}
	}
	return out;
}

static uint32_t setting_id(const std::string& b64) {
	std::string d = b64decode(b64.substr(0, 8));
	if (d.size() < 4) return 0;
	return (uint32_t(uint8_t(d[0])) << 24) | (uint32_t(uint8_t(d[1])) << 16) | (uint32_t(uint8_t(d[2])) << 8) |
		uint8_t(d[3]);
}

struct Server::Impl {
	Server* owner = nullptr;
	Config cfg;
	std::mutex lock;  // players, sessions, qos, peers, cfg
	std::map<std::string, Player> players;
	std::map<std::pair<std::string, std::string>, Session> sessions;  // (title, sid)
	std::map<std::pair<std::string, std::string>, std::string> qos;
	std::map<std::string, std::string> remote_sessions;  // sid -> peer label it came from
	std::vector<Peer> peers;

	std::mutex wad_lock;
	std::string wad;

	std::mutex dw_lock;
	std::set<DwClient*> dw_clients;
	uint64_t next_conn = 0;

	sock_t http_sock = BAD_SOCK, dw_sock = BAD_SOCK, udp_sock = BAD_SOCK;
	std::vector<std::thread> threads;
	std::atomic_bool stopping{false};
	double started = 0;
	std::mutex log_lock;

	fs::path data, users, publisher;

	void log(const std::string& line) { owner->log(line); }

	// ---------------------------------------------------------------- storage
	fs::path user_path(uint64_t xuid, const std::string& name) { return users / hex16(xuid) / safe_name(name); }

	bool read_user(uint64_t xuid, const std::string& name, std::string& out) {
		return read_file(user_path(xuid, name), out);
	}

	bool read_publisher(const std::string& name, std::string& out) {
		std::string n = lower(safe_name(name));
		// the online WAD is built from the settings so the bot options apply; a file in
		// data/publisher/ with the same name wins
		if (read_file(publisher / safe_name(name), out) && !out.empty()) return true;
		if (n.rfind("online_mp_", 0) == 0 && n.size() > 4 && n.substr(n.size() - 4) == ".wad") {
			std::lock_guard<std::mutex> g(wad_lock);
			out = wad;
			return !out.empty();
		}
		return false;  // ffotd_mp.ff and everything else: "no file", which is what the game needs
	}

	void rebuild_wad() {
		Config c;
		{
			std::lock_guard<std::mutex> g(lock);
			c = cfg;
		}
		fs::path wd = c.wad_dir;
		std::string playlists, contracts, motd;
		read_file(wd / "playlists_mp.info", playlists);
		read_file(wd / "contracts.info", contracts);
		if (!read_file(wd / "motd-mp-english.txt", motd)) motd = c.motd;
		std::string w = build_wad({{"contracts.info", contracts}, {"playlists_mp.info", build_playlists(c, playlists)},
			{"motd-mp-english.txt", motd}});
		if (w.size() > 0x10000) log("warning: the online WAD is " + std::to_string(w.size()) + " bytes; the game only takes 65536");
		std::lock_guard<std::mutex> g(wad_lock);
		wad = w;
	}

	// ---------------------------------------------------------------- DemonWare lobby
	void dw_accept_loop() {
		while (!stopping) {
			sockaddr_in a{};
			socklen_t len = sizeof(a);
			sock_t c = accept(dw_sock, reinterpret_cast<sockaddr*>(&a), &len);
			if (c == BAD_SOCK) {
				if (stopping) break;
				continue;
			}
			std::thread(&Impl::dw_client, this, c, net::ip_to_string(a) + ":" + std::to_string(ntohs(a.sin_port))).detach();
		}
	}

	void dw_send(DwClient& cl, std::mutex& send_lock, uint8_t type, const std::string& body) {
		std::string payload = std::string(1, static_cast<char>(type)) + body;
		uint32_t n = static_cast<uint32_t>(payload.size() + 1);
		std::string frame;
		for (int i = 0; i < 4; ++i) frame += static_cast<char>((n >> (8 * i)) & 0xFF);
		frame += '\0';
		frame += payload;
		std::lock_guard<std::mutex> g(send_lock);
		net::send_all(cl.sock, frame.data(), frame.size());
	}

	void dw_client(sock_t s, std::string peer) {
		int one = 1;
		setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
		DwClient cl;
		cl.peer = peer;
		cl.since = now();
		cl.sock = s;
		{
			std::lock_guard<std::mutex> g(dw_lock);
			dw_clients.insert(&cl);
		}
		log("[dw] " + peer + " connected");
		std::mutex send_lock;
		std::atomic_bool alive{true};
		std::thread keepalive([&] {
			int ticks = 0;
			while (alive && !stopping) {
				std::this_thread::sleep_for(std::chrono::milliseconds(250));
				if (++ticks % 80 == 0) {  // every 20 s
					std::lock_guard<std::mutex> g(send_lock);
					if (!net::send_all(s, "\0\0\0\0", 4)) alive = false;
				}
			}
		});
		std::string buf;
		bool got_header = false;
		uint64_t txid = 0;
		char chunk[65536];
		while (alive && !stopping) {
			int n = recv(s, chunk, sizeof(chunk), 0);
			if (n <= 0) break;
			buf.append(chunk, static_cast<size_t>(n));
			while (buf.size() >= 4) {
				uint32_t size = static_cast<uint32_t>(le(buf, 0, 4));
				if (!got_header) {
					got_header = true;
					if (size == 0xC8) {
						if (buf.size() < 8) { got_header = false; break; }
						buf.erase(0, 8);
						log("[dw] " + peer + ": client header");
					}
					continue;
				}
				if (size == 0) { buf.erase(0, 4); continue; }
				if (size > 0x200000) { alive = false; break; }
				if (buf.size() < 4 + size) break;
				std::string msg = buf.substr(4, size);
				buf.erase(0, 4 + size);
				if (msg[0] != 0) {
					log("[dw] " + peer + ": encrypted message ignored");
					continue;
				}
				handle_dw_message(cl, send_lock, msg.substr(1), txid);
			}
		}
		alive = false;
		keepalive.join();
		{
			std::lock_guard<std::mutex> g(dw_lock);
			dw_clients.erase(&cl);
		}
		net::close_socket(s);
		log("[dw] " + peer + " disconnected");
	}

	void handle_dw_message(DwClient& cl, std::mutex& send_lock, const std::string& payload, uint64_t& txid) {
		if (payload.empty()) return;
		int svc = static_cast<unsigned char>(payload[0]);
		if (svc == 7 && (payload.size() < 3 || payload[1] != 3)) {
			// the lobby "hello" with the title's auth info: answer with a connection id
			uint64_t id;
			{
				std::lock_guard<std::mutex> g(dw_lock);
				if (!next_conn) next_conn = static_cast<uint64_t>(std::time(nullptr)) << 16;
				id = ++next_conn;
			}
			log("[dw] " + cl.peer + ": lobby hello");
			dw_send(cl, send_lock, 4, Writer().u64(id).str());
			return;
		}
		auto vals = read_values(payload, 1);
		if (vals.empty() || vals[0].type != 3) {
			log("[dw] " + cl.peer + ": unreadable task for service " + std::to_string(svc));
			return;
		}
		int task = static_cast<int>(vals[0].num);
		std::vector<Value> params(vals.begin() + 1, vals.end());
		std::string name = std::string(kServices.count(svc) ? kServices.at(svc) : ("service" + std::to_string(svc)).c_str());
		auto tn = kTasks.find({svc, task});
		name += "." + (tn != kTasks.end() ? std::string(tn->second) : "task" + std::to_string(task));

		uint32_t error = BD_NO_ERROR;
		std::vector<std::string> results;
		if (svc == 12 && task == 6) {  // getServerTime
			results.push_back(Writer().u32(static_cast<uint32_t>(std::time(nullptr))).str());
		}
		else if (svc == 10 && task == 3) {  // getFile(name, owner)
			const Value* fname = first(params, 16);
			const Value* fowner = first(params, 10);
			uint64_t o = fowner && fowner->num ? fowner->num : cl.owner;
			if (o) cl.owner = o;
			std::string data;
			if (fname && read_user(o, fname->data, data)) results.push_back(Writer().blob(data).str());
			else error = BD_NO_FILE;
		}
		else if (svc == 10 && task == 7) {  // getPublisherFile(name)
			const Value* fname = first(params, 16);
			std::string data;
			if (fname && read_publisher(fname->data, data)) results.push_back(Writer().blob(data).str());
			else error = BD_NO_FILE;
		}
		else if (svc == 10 && task == 1) {  // uploadFile(name, private, data, [owner])
			const Value* fname = first(params, 16);
			const Value* fdata = first(params, 19);
			const Value* fowner = first(params, 10);
			uint64_t o = fowner && fowner->num ? fowner->num : cl.owner;
			if (fname && fdata) {
				write_file_atomic(user_path(o, fname->data), fdata->data);
				log("[dw]   saved " + fname->data + " for " + hex16(o) + " (" + std::to_string(fdata->data.size()) + " bytes)");
			}
		}
		log("[dw] " + cl.peer + ": " + name + "(" + show(params) + ") -> " +
			(error ? "error " + std::to_string(error) : std::to_string(results.size()) + " result(s)"));
		Writer w;
		w.u64(++txid).u32(error).u8(static_cast<uint8_t>(task));
		if (!error) {
			w.u32(static_cast<uint32_t>(results.size()));
			if (!results.empty()) {
				w.u32(static_cast<uint32_t>(results.size()));
				for (const auto& r : results) w.raw(r);
			}
		}
		dw_send(cl, send_lock, 1, w.str());
	}

	// ---------------------------------------------------------------- web API
	void http_accept_loop() {
		while (!stopping) {
			sock_t c = accept(http_sock, nullptr, nullptr);
			if (c == BAD_SOCK) {
				if (stopping) break;
				continue;
			}
			std::thread([this, c] {
				net::HttpRequest req;
				if (net::read_request(c, req)) {
					net::HttpResponse res;
					try {
						route(req, res);
					}
					catch (const std::exception& e) {
						log(std::string("[api] ") + req.method + " " + req.path + " failed: " + e.what());
						res.status = 500;
						res.body = R"({"message":"server error"})";
					}
					net::write_response(c, res, req.method == "HEAD");
				}
				net::close_socket(c);
			}).detach();
		}
	}

	static void reply(net::HttpResponse& res, int status, const json& body = nullptr) {
		res.status = status;
		res.body = body.is_null() ? "" : body.dump();
	}

	static json body_json(const net::HttpRequest& req) {
		if (req.body.empty()) return json::object();
		json j = json::parse(req.body, nullptr, false);
		return j.is_discarded() ? json::object() : j;
	}

	std::string public_ip(const net::HttpRequest& req) {
		if (net::is_loopback(req.client_ip)) {
			std::lock_guard<std::mutex> g(lock);
			return cfg.ip;
		}
		return req.client_ip;
	}

	std::string lsp_ip(const net::HttpRequest& req) {
		if (net::is_loopback(req.client_ip)) return "127.0.0.1";
		// the address the game used to reach this API also reaches the DemonWare port
		auto it = req.headers.find("host");
		if (it != req.headers.end()) {
			std::string host = it->second.substr(0, it->second.rfind(':'));
			if (!host.empty() && !net::is_loopback(host)) {
				addrinfo hints{}, *res = nullptr;
				hints.ai_family = AF_INET;
				if (getaddrinfo(host.c_str(), nullptr, &hints, &res) == 0 && res) {
					std::string ip = net::ip_to_string(*reinterpret_cast<sockaddr_in*>(res->ai_addr));
					freeaddrinfo(res);
					return ip;
				}
			}
		}
		std::lock_guard<std::mutex> g(lock);
		return cfg.ip;
	}

	static json session_view(const Session& s, bool details = false) {
		int filled_pub = 0, filled_priv = 0;
		for (const auto& [x, priv] : s.players) (priv ? filled_priv : filled_pub)++;
		json v = {{"id", s.id}, {"flags", s.flags}, {"hostAddress", s.host_address}, {"macAddress", s.mac_address},
			{"port", s.port}, {"publicSlotsCount", s.public_slots}, {"privateSlotsCount", s.private_slots},
			{"openPublicSlotsCount", std::max(0, s.public_slots - filled_pub)},
			{"openPrivateSlotsCount", std::max(0, s.private_slots - filled_priv)},
			{"filledPublicSlotsCount", filled_pub}, {"filledPrivateSlotsCount", filled_priv}};
		if (details) {
			json players = json::array();
			for (const auto& [x, priv] : s.players) players.push_back({{"xuid", x}});
			v["players"] = players;
		}
		else {
			v["xuid"] = s.xuid;
			v["title"] = s.title;
			v["mediaId"] = s.media_id;
			v["version"] = s.version;
		}
		return v;
	}

	static std::string jstr(const json& j, const char* key, const std::string& def = "") {
		auto it = j.find(key);
		if (it == j.end() || it->is_null()) return def;
		if (it->is_string()) return it->get<std::string>();
		return it->dump();
	}

	static int jint(const json& j, const char* key, int def = 0) {
		auto it = j.find(key);
		if (it == j.end()) return def;
		if (it->is_number()) return it->get<int>();
		if (it->is_string()) return std::atoi(it->get<std::string>().c_str());
		return def;
	}

	void route(const net::HttpRequest& req, net::HttpResponse& res) {
		std::string path = req.path;
		while (!path.empty() && path.front() == '/') path.erase(0, 1);
		while (!path.empty() && path.back() == '/') path.pop_back();
		std::vector<std::string> p = path.empty() ? std::vector<std::string>{} : split(path, '/');
		std::vector<std::string> low;
		for (auto& x : p) low.push_back(lower(x));
		const std::string& verb = req.method;

		if (p.empty() || low == std::vector<std::string>{"index.html"}) {
			res.content_type = "text/html; charset=utf-8";
			res.body = kStatusPage;
			return;
		}
		if (low == std::vector<std::string>{"api", "status"}) return reply(res, 200, status_json());
		if (low[0] == "peer") return peer_api(req, res, p, low);
		if (low == std::vector<std::string>{"whoami"}) return reply(res, 200, {{"address", public_ip(req)}});
		if (low[0] == "deletesessions") {
			std::string ip = public_ip(req);
			std::lock_guard<std::mutex> g(lock);
			for (auto it = sessions.begin(); it != sessions.end();)
				it = it->second.host_address == ip ? sessions.erase(it) : std::next(it);
			return reply(res, 200);
		}
		if (low[0] == "players") return players_api(req, res, std::vector<std::string>(low.begin() + 1, low.end()));
		if (low[0] == "title" && p.size() >= 3) {
			std::string title = upper(p[1]);
			if (low[2] == "servers") {
				json servers = json::array();
				if (title == kTitleId) servers.push_back({{"address", lsp_ip(req)}, {"flags", 0}, {"description", kLspName}});
				log("[api] " + req.client_ip + " asked for title servers of " + title);
				return reply(res, 200, servers);
			}
			if (low[2] == "services") return reply(res, 200, {{"services", json::array()}, {"querysearch", json::array()}});
			if (low[2] == "ports") return reply(res, 200, json::object());
			if (low[2] == "sessions")
				return sessions_api(req, res, title, std::vector<std::string>(p.begin() + 3, p.end()));
		}
		if (low[0] == "leaderboards") return reply(res, 404, {{"message", "no leaderboards"}});
		if (low[0] == "xstorage") return reply(res, 404, {{"message", "not stored"}});
		log("[api] unhandled " + verb + " /" + path);
		reply(res, 404, {{"message", "not found"}});
	}

	void players_api(const net::HttpRequest& req, net::HttpResponse& res, const std::vector<std::string>& p) {
		const std::string& verb = req.method;
		if (verb == "POST" && p.empty()) {
			json r = body_json(req);
			std::string xuid = upper(jstr(r, "xuid"));
			std::string from = public_ip(req);
			std::lock_guard<std::mutex> g(lock);
			Player& pl = players[xuid];
			pl.xuid = xuid;
			std::string tag = jstr(r, "gamertag");
			if (!tag.empty()) pl.gamertag = tag;
			pl.machine_id = jstr(r, "machineId");
			pl.host_address = jstr(r, "hostAddress");
			if (pl.host_address.empty()) pl.host_address = from;
			pl.mac_address = jstr(r, "macAddress");
			pl.seen = now();
			log("[api] player " + pl.gamertag + " (" + xuid + ") signed in from " + req.client_ip);
			return reply(res, 201);
		}
		if (p == std::vector<std::string>{"find"}) {
			json r = body_json(req);
			std::string addr = jstr(r, "hostAddress");
			{
				std::lock_guard<std::mutex> g(lock);
				for (auto& [x, pl] : players)
					if (pl.host_address == addr)
						return reply(res, 201, {{"xuid", pl.xuid}, {"gamertag", pl.gamertag}, {"hostAddress", pl.host_address},
							{"machineId", pl.machine_id}, {"port", 0}, {"macAddress", pl.mac_address}, {"sessionId", "0000000000000000"}});
			}
			// a player of another server (someone who joined through a peer)
			if (!is_peer_request(req)) {
				for (const Peer& peer : reachable_peers()) {
					int st = 0;
					std::string body;
					if (net::http_call(peer.host, peer.port, "POST", "/players/find", req.body, 1500, st, body) && st == 201) {
						res.status = 201;
						res.body = body;
						return;
					}
				}
			}
			return reply(res, 404, {{"message", "Player not found."}});
		}
		if (p == std::vector<std::string>{"deletemyprofiles"}) return reply(res, 200);
		if (p == std::vector<std::string>{"setpresence"}) {
			json r = body_json(req);
			std::lock_guard<std::mutex> g(lock);
			if (r.contains("presence") && r["presence"].is_array())
				for (auto& u : r["presence"]) {
					auto it = players.find(upper(jstr(u, "xuid")));
					if (it != players.end()) {
						it->second.presence = jstr(u, "richPresence");
						it->second.seen = now();
					}
				}
			return reply(res, 201);
		}
		if (p == std::vector<std::string>{"presence"}) {
			json r = body_json(req), out = json::array();
			std::lock_guard<std::mutex> g(lock);
			if (r.contains("xuids") && r["xuids"].is_array())
				for (auto& x : r["xuids"]) {
					auto it = players.find(upper(x.is_string() ? x.get<std::string>() : x.dump()));
					if (it == players.end()) continue;
					out.push_back({{"xuid", it->second.xuid}, {"gamertag", it->second.gamertag}, {"state", 1},
						{"sessionId", "0000000000000000"}, {"titleId", kTitleId}, {"stateChangeTime", 0},
						{"richPresence", it->second.presence}});
				}
			return reply(res, 201, out);
		}
		if (p == std::vector<std::string>{"findusers"}) {
			json r = body_json(req), out = json::array();
			std::lock_guard<std::mutex> g(lock);
			if (r.contains("UsersInfo") && r["UsersInfo"].is_array())
				for (auto& u : r["UsersInfo"]) {
					if (!u.is_array() || u.size() < 2) continue;
					std::string xuid = u[0].is_string() ? u[0].get<std::string>() : u[0].dump();
					std::string tag = u[1].is_string() ? u[1].get<std::string>() : "";
					json info = {{"xuid", xuid}, {"gamertag", tag}};
					for (auto& [x, pl] : players) {
						if (!xuid.empty() && pl.xuid == upper(xuid) && tag.empty()) info["gamertag"] = pl.gamertag;
						if (!tag.empty() && lower(pl.gamertag) == lower(tag) && xuid.empty()) info["xuid"] = pl.xuid;
					}
					out.push_back(info);
				}
			return reply(res, 201, out);
		}
		if (p == std::vector<std::string>{"setsettings"}) {
			json r = body_json(req);
			std::lock_guard<std::mutex> g(lock);
			if (r.contains("settings") && r["settings"].is_array())
				for (auto& entry : r["settings"])
					for (auto& [xuid, titles] : entry.items()) {
						auto it = players.find(upper(xuid));
						if (it == players.end() || !titles.is_array()) continue;
						for (auto& t : titles)
							for (auto& [title, values] : t.items()) {
								auto& cur = it->second.settings[upper(title)];
								for (auto& b : values) {
									if (!b.is_string()) continue;
									std::string s = b.get<std::string>();
									uint32_t id = setting_id(s);
									cur.erase(std::remove_if(cur.begin(), cur.end(), [&](const std::string& c) { return setting_id(c) == id; }), cur.end());
									cur.push_back(s);
								}
							}
					}
			return reply(res, 201);
		}
		if (p == std::vector<std::string>{"getsettings"}) {
			json r = body_json(req), out = json::array();
			std::lock_guard<std::mutex> g(lock);
			if (r.contains("settings") && r["settings"].is_array())
				for (auto& entry : r["settings"])
					for (auto& [xuid, titles] : entry.items()) {
						json tlist = json::array();
						auto it = players.find(upper(xuid));
						if (titles.is_array())
							for (auto& t : titles)
								for (auto& [title, ids] : t.items()) {
									std::map<uint32_t, std::string> have;
									if (it != players.end())
										for (auto& b : it->second.settings[upper(title)]) have[setting_id(b)] = b;
									json vals = json::array();
									if (ids.is_array())
										for (auto& sid : ids) {
											std::string hex = sid.is_string() ? sid.get<std::string>() : sid.dump();
											uint32_t n = static_cast<uint32_t>(std::strtoul(hex.c_str(), nullptr, 16));
											if (have.count(n)) vals.push_back(have[n]);
											else if (n == kGamerpicId) vals.push_back(kDefaultGamerpic);
										}
									tlist.push_back({{title, vals}});
								}
						out.push_back({{xuid, tlist}});
					}
			return reply(res, 201, {{"settings", out}});
		}
		reply(res, 404, {{"message", "not found"}});
	}

	static bool is_peer_request(const net::HttpRequest& req) {
		auto it = req.headers.find("user-agent");
		return it != req.headers.end() && it->second.find("BO1Live-peer") != std::string::npos;
	}

	std::vector<Peer> reachable_peers() {
		std::lock_guard<std::mutex> g(lock);
		std::vector<Peer> out;
		for (const auto& p : peers)
			if (p.reachable) out.push_back(p);
		return out;
	}

	std::vector<json> search_local(const std::string& title, const std::string& me, bool matchmaking_only) {
		std::vector<json> out;
		std::lock_guard<std::mutex> g(lock);
		for (auto& [key, s] : sessions) {
			if (key.first != title) continue;
			if (matchmaking_only && (s.xuid == me || !(s.flags & 8))) continue;
			json v = session_view(s);
			if (matchmaking_only && v["openPublicSlotsCount"].get<int>() <= 0) continue;
			out.push_back(v);
		}
		return out;
	}

	void sessions_api(const net::HttpRequest& req, net::HttpResponse& res, const std::string& title,
			const std::vector<std::string>& p) {
		const std::string& verb = req.method;
		if (p.empty()) {
			if (verb != "POST") return reply(res, 404);
			json r = body_json(req);
			int flags = jint(r, "flags");
			bool host = (flags & 1) || flags == 4;
			if (host) {
				Session s;
				s.id = lower(jstr(r, "sessionId"));
				s.flags = flags;
				s.host_address = jstr(r, "hostAddress", public_ip(req));
				s.mac_address = jstr(r, "macAddress");
				s.port = jint(r, "port");
				s.public_slots = jint(r, "publicSlotsCount");
				s.private_slots = jint(r, "privateSlotsCount");
				s.xuid = upper(jstr(r, "xuid"));
				s.title = jstr(r, "title");
				s.media_id = jstr(r, "mediaId");
				s.version = jstr(r, "version");
				s.created = now();
				char fl[16];
				std::snprintf(fl, sizeof(fl), "0x%X", flags);
				log("[api] " + req.client_ip + " hosted session " + s.id + " (flags " + fl + ")");
				std::lock_guard<std::mutex> g(lock);
				sessions[{title, s.id}] = s;
			}
			return reply(res, 201);
		}
		if (lower(p[0]) == "search") {
			json r = verb == "POST" ? body_json(req) : json::object();
			std::string me = upper(jstr(r, "searcher_xuid"));
			int count = jint(r, "resultsCount", 50);
			if (count <= 0) count = 50;
			if (verb != "POST") {
				auto v = search_local(title, "", false);
				return reply(res, 200, json(v));
			}
			auto found = search_local(title, me, true);
			int local = static_cast<int>(found.size());
			// games hosted through other BO1 Live servers
			if (!is_peer_request(req)) {
				for (const Peer& peer : reachable_peers()) {
					int st = 0;
					std::string body;
					json q = {{"searcher_xuid", me}};
					if (!net::http_call(peer.host, peer.port, "POST", "/peer/sessions/" + title, q.dump(), 1500, st, body) || st != 200)
						continue;
					json list = json::parse(body, nullptr, false);
					if (!list.is_array()) continue;
					std::lock_guard<std::mutex> g(lock);
					for (auto& v : list) {
						std::string sid = lower(jstr(v, "id"));
						if (sid.empty() || sessions.count({title, sid})) continue;
						remote_sessions[sid] = peer.label();
						found.push_back(v);
					}
				}
			}
			if (static_cast<int>(found.size()) > count) found.resize(static_cast<size_t>(count));
			log("[api] " + req.client_ip + " searched for matches: " + std::to_string(local) + " here, " +
				std::to_string(found.size() - std::min<size_t>(found.size(), static_cast<size_t>(local))) + " on other servers");
			return reply(res, 201, json(found));
		}
		std::string sid = lower(p[0]);
		std::string sub = p.size() > 1 ? lower(p[1]) : "";
		std::pair<std::string, std::string> key{title, sid};

		// a match hosted through another server: pass the request on to it
		std::string remote;
		{
			std::lock_guard<std::mutex> g(lock);
			if (!sessions.count(key) && !qos.count(key)) {
				auto it = remote_sessions.find(sid);
				if (it != remote_sessions.end()) remote = it->second;
			}
		}
		if (!remote.empty() && !is_peer_request(req)) {
			size_t c = remote.rfind(':');
			int st = 0;
			std::string body;
			std::string target = req.path + (req.query.empty() ? "" : "?" + req.query);
			if (net::http_call(remote.substr(0, c), std::atoi(remote.c_str() + c + 1), verb, target, req.body, 3000, st, body)) {
				res.status = st;
				res.body = body;
				if (sub == "qos") res.content_type = "application/octet-stream";
				return;
			}
			return reply(res, 502, {{"message", "the server hosting this match did not answer"}});
		}

		std::lock_guard<std::mutex> g(lock);
		auto sit = sessions.find(key);
		Session* s = sit == sessions.end() ? nullptr : &sit->second;
		if (sub.empty() && verb == "GET") return s ? reply(res, 200, session_view(*s)) : reply(res, 404, {{"message", "not found"}});
		if (sub.empty() && verb == "DELETE") {
			sessions.erase(key);
			qos.erase(key);
			return reply(res, 200);
		}
		if (sub == "qos") {
			if (verb == "POST") {
				qos[key] = req.body;
				return reply(res, 201);
			}
			auto q = qos.find(key);
			if (q == qos.end()) {
				res.status = 204;
				return;
			}
			res.status = 200;
			res.content_type = "application/octet-stream";
			res.body = q->second;
			return;
		}
		if (sub == "prejoin") return reply(res, 201);
		if (!s) return reply(res, 404, {{"message", "Session " + sid + " was not found."}});
		if (sub == "details") return reply(res, 200, session_view(*s, true));
		if (sub == "arbitration") {
			std::map<std::string, json> machines;
			for (auto& [x, priv] : s->players) {
				auto pit = players.find(x);
				std::string mid = pit != players.end() && !pit->second.machine_id.empty() ? pit->second.machine_id : x;
				if (!machines.count(mid)) machines[mid] = json::array();
				machines[mid].push_back({{"xuid", x}});
			}
			json ms = json::array();
			for (auto& [mid, list] : machines) ms.push_back({{"id", mid}, {"players", list}});
			return reply(res, 200, {{"totalPlayers", s->players.size()}, {"machines", ms}});
		}
		if (sub == "properties" && verb == "GET") return reply(res, 200, {{"properties", s->properties}});
		if (sub == "context" && verb == "GET") return reply(res, 200, {{"context", s->context}});
		json r = verb == "POST" ? body_json(req) : json::object();
		if (sub == "modify") {
			s->flags = jint(r, "flags", s->flags);
			s->public_slots = jint(r, "publicSlotsCount", s->public_slots);
			s->private_slots = jint(r, "privateSlotsCount", s->private_slots);
		}
		else if (sub == "join") {
			json priv = r.contains("privateSlots") && r["privateSlots"].is_array() ? r["privateSlots"] : json::array();
			if (r.contains("xuids") && r["xuids"].is_array())
				for (size_t i = 0; i < r["xuids"].size(); ++i) {
					auto& x = r["xuids"][i];
					s->players[upper(x.is_string() ? x.get<std::string>() : x.dump())] = i < priv.size() && priv[i].is_boolean() && priv[i].get<bool>();
				}
		}
		else if (sub == "leave") {
			if (r.contains("xuids") && r["xuids"].is_array())
				for (auto& x : r["xuids"]) s->players.erase(upper(x.is_string() ? x.get<std::string>() : x.dump()));
		}
		else if (sub == "migrate") {
			s->host_address = jstr(r, "hostAddress", s->host_address);
			s->mac_address = jstr(r, "macAddress", s->mac_address);
			s->port = jint(r, "port", s->port);
			std::string x = jstr(r, "xuid");
			if (!x.empty()) s->xuid = upper(x);
			return reply(res, 201, session_view(*s));
		}
		else if (sub == "properties") {
			std::map<uint32_t, std::string> props;
			for (auto& b : s->properties) props[setting_id(b)] = b;
			if (r.contains("properties") && r["properties"].is_array())
				for (auto& b : r["properties"])
					if (b.is_string()) props[setting_id(b.get<std::string>())] = b.get<std::string>();
			s->properties.clear();
			for (auto& [id, b] : props) s->properties.push_back(b);
		}
		else if (sub == "context") {
			s->context.clear();
			if (r.contains("contexts") && r["contexts"].is_array())
				for (auto& c : r["contexts"]) s->context.push_back(c.is_string() ? c.get<std::string>() : c.dump());
		}
		reply(res, 201);
	}

	// /peer/... : what other BO1 Live servers ask this one
	void peer_api(const net::HttpRequest& req, net::HttpResponse& res, const std::vector<std::string>& p,
			const std::vector<std::string>& low) {
		if (low.size() == 2 && low[1] == "hello") {
			std::lock_guard<std::mutex> g(lock);
			return reply(res, 200, {{"server", "bo1live"}, {"version", kVersion}, {"ip", cfg.ip}});
		}
		if (low.size() == 3 && low[1] == "sessions" && req.method == "POST") {
			json r = body_json(req);
			return reply(res, 200, json(search_local(upper(p[2]), upper(jstr(r, "searcher_xuid")), true)));
		}
		reply(res, 404, {{"message", "not found"}});
	}

	// ---------------------------------------------------------------- peers and LAN discovery
	void peer_loop() {
		int tick = 0;
		while (!stopping) {
			if (tick % 20 == 0) {  // every 10 s
				std::vector<Peer> list;
				{
					std::lock_guard<std::mutex> g(lock);
					list = peers;
				}
				for (auto& peer : list) {
					int st = 0;
					std::string body;
					bool ok = net::http_call(peer.host, peer.port, "GET", "/peer/hello", "", 1500, st, body) && st == 200 &&
						body.find("bo1live") != std::string::npos;
					std::lock_guard<std::mutex> g(lock);
					for (auto& q : peers)
						if (q.label() == peer.label()) {
							if (q.reachable != ok) log(std::string("[peers] ") + q.label() + (ok ? " is online" : " is offline"));
							q.reachable = ok;
						}
				}
				if (udp_sock != BAD_SOCK) {
					bool lan;
					int port;
					{
						std::lock_guard<std::mutex> g(lock);
						lan = cfg.lan_discovery;
						port = cfg.http_port;
					}
					if (lan) {
						sockaddr_in a{};
						a.sin_family = AF_INET;
						a.sin_port = htons(kDiscoveryPort);
						a.sin_addr.s_addr = htonl(INADDR_BROADCAST);
						std::string msg = "BO1LIVE " + std::to_string(port);
						sendto(udp_sock, msg.data(), static_cast<int>(msg.size()), 0, reinterpret_cast<sockaddr*>(&a), sizeof(a));
					}
				}
			}
			++tick;
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
		}
	}

	void udp_loop() {
		char buf[256];
		std::vector<std::string> mine = net::local_ips();
		while (!stopping) {
			sockaddr_in from{};
			socklen_t len = sizeof(from);
			int n = recvfrom(udp_sock, buf, sizeof(buf) - 1, 0, reinterpret_cast<sockaddr*>(&from), &len);
			if (n <= 0) {
				if (stopping) break;
				continue;
			}
			buf[n] = 0;
			std::string msg(buf), ip = net::ip_to_string(from);
			if (msg.rfind("BO1LIVE ", 0) != 0) continue;
			int port = std::atoi(msg.c_str() + 8);
			if (port <= 0) continue;
			std::lock_guard<std::mutex> g(lock);
			if (!cfg.lan_discovery) continue;
			bool self = net::is_loopback(ip) || std::find(mine.begin(), mine.end(), ip) != mine.end() || ip == cfg.ip;
			if (self && port == cfg.http_port) continue;
			Peer pe{ip, port, false, true};
			bool known = std::any_of(peers.begin(), peers.end(), [&](const Peer& q) { return q.label() == pe.label(); });
			if (!known) {
				peers.push_back(pe);
				log("[peers] found " + pe.label() + " on the LAN");
			}
		}
	}

	void set_peers_from_config() {
		std::vector<Peer> next;
		for (std::string s : cfg.peers) {
			s.erase(0, s.find_first_not_of(" \t"));
			s.erase(s.find_last_not_of(" \t\r\n") + 1);
			if (s.rfind("http://", 0) == 0) s = s.substr(7);
			while (!s.empty() && s.back() == '/') s.pop_back();
			if (s.empty()) continue;
			Peer pe;
			size_t c = s.rfind(':');
			if (c != std::string::npos) {
				pe.host = s.substr(0, c);
				pe.port = std::atoi(s.c_str() + c + 1);
			}
			else pe.host = s;
			if (pe.port <= 0) pe.port = 36000;
			for (auto& old : peers)
				if (old.label() == pe.label()) pe.reachable = old.reachable;
			next.push_back(pe);
		}
		for (auto& old : peers)  // keep LAN finds
			if (old.discovered && std::none_of(next.begin(), next.end(), [&](const Peer& q) { return q.label() == old.label(); }))
				next.push_back(old);
		peers = next;
	}

	// ---------------------------------------------------------------- status
	json status_json() {
		Status st = owner->status();
		json players_j = json::array();
		for (auto& pl : st.players) players_j.push_back({{"gamertag", pl.gamertag}, {"minutes", pl.minutes}});
		json peers_j = json::array();
		for (auto& [addr, ok] : st.peers) peers_j.push_back({{"address", addr}, {"online", ok}});
		return {{"online", st.online}, {"players", players_j}, {"matches", st.matches_local + st.matches_peers},
			{"registered", st.registered}, {"uptime", st.uptime}, {"version", kVersion}, {"peers", peers_j}};
	}
};

// ------------------------------------------------------------------------------------- Server

Server::Server(LogFn log) : m(std::make_unique<Impl>()), m_log(std::move(log)) {
	m->owner = this;
	net::startup();
}

Server::~Server() { stop(); }

void Server::log(const std::string& line) {
	std::string full = timestamp() + " " + line;
	std::lock_guard<std::mutex> g(m->log_lock);
	if (!m->data.empty()) {
		std::ofstream f(m->data / "server.log", std::ios::app);
		f << full << "\n";
	}
	if (m_log) m_log(full);
}

bool Server::start(const Config& cfg, std::string& error) {
	if (m_running) return true;
	m->stopping = false;
	m->cfg = cfg;
	if (m->cfg.ip.empty()) m->cfg.ip = net::lan_ip();
	m->data = fs::absolute(cfg.data_dir);
	m->users = m->data / "users";
	m->publisher = m->data / "publisher";
	std::error_code ec;
	fs::create_directories(m->users, ec);
	fs::create_directories(m->publisher, ec);
	m->set_peers_from_config();
	m->rebuild_wad();

	m->dw_sock = net::listen_tcp(cfg.dw_port, error);
	if (m->dw_sock == BAD_SOCK) {
		error = "can't open the DemonWare port " + std::to_string(cfg.dw_port) + ": " + error +
			" (is another server already running?)";
		return false;
	}
	m->http_sock = net::listen_tcp(cfg.http_port, error);
	if (m->http_sock == BAD_SOCK) {
		error = "can't open the web API port " + std::to_string(cfg.http_port) + ": " + error +
			" (is another server already running?)";
		net::close_socket(m->dw_sock);
		m->dw_sock = BAD_SOCK;
		return false;
	}
	std::string uerr;
	m->udp_sock = net::bind_udp(kDiscoveryPort, true, uerr);
	if (m->udp_sock == BAD_SOCK) log("[peers] LAN discovery is off: " + uerr);

	m->started = now();
	m_running = true;
	m->threads.emplace_back(&Impl::dw_accept_loop, m.get());
	m->threads.emplace_back(&Impl::http_accept_loop, m.get());
	m->threads.emplace_back(&Impl::peer_loop, m.get());
	if (m->udp_sock != BAD_SOCK) m->threads.emplace_back(&Impl::udp_loop, m.get());

	log(std::string("BO1 Live server ") + kVersion + " started");
	log("Xenia api_address: http://" + m->cfg.ip + ":" + std::to_string(cfg.http_port) + "/  (this PC: http://127.0.0.1:" +
		std::to_string(cfg.http_port) + "/)");
	log("DemonWare lobby on TCP " + std::to_string(cfg.dw_port) + ", status page http://127.0.0.1:" + std::to_string(cfg.http_port) + "/");
	log(cfg.bots ? "Bots fill ranked matches up to " + std::to_string(cfg.bot_fill) + " players (" + cfg.bot_difficulty + ")"
	             : "Bots are off");
	return true;
}

void Server::stop() {
	if (!m_running) return;
	m->stopping = true;
	// unblock accept / recvfrom
#ifdef _WIN32
	shutdown(m->http_sock, SD_BOTH);
	shutdown(m->dw_sock, SD_BOTH);
#else
	shutdown(m->http_sock, SHUT_RDWR);
	shutdown(m->dw_sock, SHUT_RDWR);
	if (m->udp_sock != BAD_SOCK) shutdown(m->udp_sock, SHUT_RDWR);
#endif
	net::close_socket(m->http_sock);
	net::close_socket(m->dw_sock);
	net::close_socket(m->udp_sock);
	m->http_sock = m->dw_sock = m->udp_sock = BAD_SOCK;
	{
		std::lock_guard<std::mutex> g(m->dw_lock);
		for (auto* c : m->dw_clients) {
#ifdef _WIN32
			shutdown(c->sock, SD_BOTH);
#else
			shutdown(c->sock, SHUT_RDWR);
#endif
		}
	}
	for (auto& t : m->threads)
		if (t.joinable()) t.join();
	m->threads.clear();
	m_running = false;
	log("server stopped");
}

void Server::update_config(const Config& cfg) {
	{
		std::lock_guard<std::mutex> g(m->lock);
		std::string ip = m->cfg.ip;
		int http = m->cfg.http_port, dw = m->cfg.dw_port;
		m->cfg = cfg;
		// ports and address only change on restart
		m->cfg.ip = ip;
		m->cfg.http_port = http;
		m->cfg.dw_port = dw;
		m->set_peers_from_config();
	}
	if (m_running) {
		m->rebuild_wad();
		log(cfg.bots ? "settings updated: bots fill matches up to " + std::to_string(cfg.bot_fill) + " players (" + cfg.bot_difficulty + ")"
		             : "settings updated: bots off");
	}
}

Status Server::status() {
	Status st;
	st.running = m_running;
	if (!m_running) return st;
	double t = now();
	std::map<std::string, std::string> tags;
	{
		std::lock_guard<std::mutex> g(m->lock);
		for (auto& [x, pl] : m->players) tags[x] = pl.gamertag;
		for (auto& [key, s] : m->sessions)
			if (key.first == kTitleId) st.matches_local++;
		for (auto& p : m->peers) st.peers.emplace_back(p.label(), p.reachable);
	}
	std::vector<std::pair<double, std::pair<std::string, std::string>>> online;
	{
		std::lock_guard<std::mutex> g(m->dw_lock);
		for (auto* c : m->dw_clients) online.push_back({c->since, {c->owner ? hex16(c->owner) : "", c->peer}});
	}
	std::sort(online.begin(), online.end());
	std::set<std::string> seen;
	for (auto& [since, who] : online) {
		std::string key = who.first.empty() ? who.second : who.first;
		if (!seen.insert(key).second) continue;
		auto it = tags.find(who.first);
		st.players.push_back({it != tags.end() && !it->second.empty() ? it->second : "Player", static_cast<int>((t - since) / 60)});
	}
	st.online = static_cast<int>(st.players.size());
	std::error_code ec;
	for (auto& d : fs::directory_iterator(m->users, ec))
		if (d.is_directory()) st.registered++;
	st.uptime = static_cast<long>(t - m->started);
	return st;
}

}  // namespace bo1
