BO1 Live 2.0 - Black Ops 1 Xbox Live server for Xenia (C++)
=============================================================

bo1live.exe is the DemonWare + Xbox Live server as one Windows program with a window.
No Python, nothing to install. It does everything the Python server did, plus:

  * Bots in Xbox Live matchmaking. With the common_mp.ff in game-files\, ranked
    matchmaking games fill up with bots when the match starts.
  * Shared matchmaking. Your server runs on your IP, and you can link other people's
    BO1 Live servers: their matches show up in your matchmaking and you can join them.
    Servers on the same network find each other automatically.


1. Game files
  Copy game-files\common_mp.ff and game-files\ui_mp.ff into the game's folder (same place
  as before), with the patched default_mp.xex. They contain everything from the earlier
  packages (bot matches menu, stats saving) plus the Xbox Live bots.

2. Xenia
  Use the Xenia Canary NETPLAY build. In xenia-canary.config.toml under [Live]:
      api_address = "http://127.0.0.1:36000/"
      network_mode = 2
      upnp = true
  The window shows the api_address with a Copy button. Use a profile with Xbox Live
  enabled.

3. Run bo1live.exe
  Press "Start server". Allow it through the Windows firewall when asked. Then start
  Black Ops, go to Multiplayer, Xbox LIVE, and pick a playlist.


Bots
  "Fill matchmaking games with bots" and "Players per match" decide how many bots join:
  with 12, a match with you alone gets 11 bots, a match with 3 people gets 9. Team modes
  split them across both teams. Difficulty is Recruit to Veteran.
  "Start matches without waiting for more people" lets the lobby start with just you.
  Bots join when the match starts; the pre-game lobby only shows people.
  The settings reach the game through the online playlists, so a change applies the next
  time the game signs in to Xbox Live. Without the server's setting, the game still fills
  ranked matches up to 12 players.

Playing with other people
  Everyone runs their own bo1live.exe and points Xenia at it (127.0.0.1).
  * Same network: leave "Find servers on my network automatically" on. Done.
  * Over the internet: type your friend's public IP under "Other servers" and press Add,
    and they add yours. Each of you needs TCP 36000 open/forwarded to your PC, and the
    game itself needs Xenia's UPnP (upnp = true) or port forwarding.
    Set "Your IP" to your public IP so others get the right address for your matches.
  Matchmaking then searches your server and every linked server. When you join a match
  from another server, your server passes the join on to it.
  A friend can also skip running their own server and set api_address to yours.

Status page
  "Status page" opens http://127.0.0.1:36000/ with players online, matches and linked
  servers (refreshes every 5 seconds). Others can open http://<your IP>:36000/.

Files next to bo1live.exe
  bo1live.ini          your settings
  data\users\<xuid>\   saved stats and classes per gamer
  data\server.log      every request the game made (send this if something gets stuck)
  data\publisher\      optional: a file here is sent to the game instead of the built-in
                       one with the same name
  wad\                 optional: playlists_mp.info / contracts.info / motd-mp-english.txt
                       to replace the built-in playlists (bot settings are still added)

Console mode
  bo1live.exe --nogui [--ip X] [--http-port 36000] [--dw-port 1020] [--peer HOST[:PORT]]
              [--bots N] [--difficulty easy|normal|hard|fu] [--wait-for-players] [--no-lan]
  The source also builds on Linux (console only) for a VPS: see source\build_mingw.sh.

Not tested in the real game yet
  * The Xbox Live bots (patched _bot.gsc) and "start without waiting" (party_minplayers 1).
  * Joining a match hosted through another server. The linking itself was tested with two
    servers and a fake game client; the game-to-game connection is Xenia's own netplay.

Source: source\ (C++17, Win32 GUI, nlohmann/json). Build with source\build_mingw.sh
(MinGW-w64) or CMake / Visual Studio 2022 (CMakeLists.txt).
