// Black Ops 1 (Xbox 360) online server for the Xenia Canary netplay build:
//   * the Xenia web API ("Xbox Live": sign-in, players, sessions / matchmaking, title servers)
//   * the DemonWare lobby BO1 connects to ("COD7 - DW ATVI LSP", TCP 1020)
//   * links to other BO1 Live servers (peers), so matchmaking also finds their games
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "net.hpp"

namespace bo1 {

constexpr const char* kVersion = "2.0";
constexpr const char* kTitleId = "41560855";
constexpr const char* kLspName = "COD7 - DW ATVI LSP";
constexpr int kDiscoveryPort = 36001;

struct Config {
	std::string ip;                // address other players reach this PC on
	int http_port = 36000;
	int dw_port = 1020;
	std::string data_dir = "data";
	std::string wad_dir = "wad";   // optional overrides: playlists_mp.info, contracts.info, motd-mp-english.txt
	// bots and lobby
	bool bots = true;
	int bot_fill = 12;             // bots fill matches up to this many players
	std::string bot_difficulty = "normal";
	bool start_alone = true;       // matches start without waiting for more humans
	std::string motd = "Welcome to BO1 Live! Bots fill every match.";
	// peers
	std::vector<std::string> peers;  // "host" or "host:port"
	bool lan_discovery = true;
};

struct PlayerInfo {
	std::string gamertag;
	int minutes = 0;
};

struct Status {
	bool running = false;
	int online = 0;
	std::vector<PlayerInfo> players;
	int matches_local = 0;
	int matches_peers = 0;
	int registered = 0;
	std::vector<std::pair<std::string, bool>> peers;  // address, reachable
	long uptime = 0;
};

class Server {
public:
	using LogFn = std::function<void(const std::string&)>;

	explicit Server(LogFn log);
	~Server();

	bool start(const Config& cfg, std::string& error);
	void stop();
	bool running() const { return m_running; }
	Status status();
	// Rebuilds the online WAD (playlists, bot settings, MOTD) from the current config.
	void update_config(const Config& cfg);

	void log(const std::string& line);

	// --- used by the connection handlers
	struct Impl;
private:
	std::unique_ptr<Impl> m;
	LogFn m_log;
	std::atomic_bool m_running{false};
};

// Online WAD (online_mp_<lang>.wad): playlists, contracts, MOTD. Exposed for tests.
std::string build_playlists(const Config& cfg, const std::string& override_text);
std::string build_wad(const std::vector<std::pair<std::string, std::string>>& entries);

}  // namespace bo1
