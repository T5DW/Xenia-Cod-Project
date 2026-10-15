BO1 Live 2.0 - Black Ops 1 Xbox Live server for Xenia (C++)



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

Bots do not auto fill when match starts

Source: source\ (C++17, Win32 GUI, nlohmann/json). Build with source\build_mingw.sh
(MinGW-w64) or CMake / Visual Studio 2022 (CMakeLists.txt).
