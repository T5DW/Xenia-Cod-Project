// camotool: find a Black Ops II July 20 prototype install, export its camo
// textures, and swap them for your own images.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "fastfile.h"
#include "images.h"
#include "ipak.h"

namespace fs = std::filesystem;

namespace {

const char* kFastFile = "common_mp.ff";
const char* kBackupSuffix = ".original";

fs::path exe_dir() {
#ifdef _WIN32
  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (n && n < MAX_PATH) return fs::path(buf).parent_path();
#else
  std::error_code ec;
  fs::path p = fs::read_symlink("/proc/self/exe", ec);
  if (!ec) return p.parent_path();
#endif
  return fs::current_path();
}

fs::path config_path() { return exe_dir() / "camotool.cfg"; }

bool is_game_dir(const fs::path& p) {
  std::error_code ec;
  return fs::is_regular_file(p / kFastFile, ec);
}

std::string trim_path(std::string s) {
  while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ')) s.pop_back();
  size_t start = s.find_first_not_of(' ');
  s = start == std::string::npos ? "" : s.substr(start);
  if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') && s.back() == s.front())
    s = s.substr(1, s.size() - 2);
  return s;
}

std::string ask(const std::string& prompt) {
  std::cout << prompt << std::flush;
  std::string line;
  if (!std::getline(std::cin, line)) std::exit(0);
  return trim_path(line);
}

void search(const fs::path& root, int max_depth, std::set<fs::path>& found) {
  static const std::set<std::string> kSkip = {"windows", "$recycle.bin", "system volume information",
                                              "programdata", "appdata", "node_modules", ".git",
                                              "program files", "program files (x86)", "proc", "sys"};
  std::error_code ec;
  if (!fs::is_directory(root, ec)) return;
  if (is_game_dir(root)) found.insert(fs::weakly_canonical(root, ec));
  fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
  for (; !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    if (!it->is_directory(e2) || it->is_symlink(e2)) continue;
    std::string lower = it->path().filename().string();
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (kSkip.count(lower) || it.depth() >= max_depth) {
      it.disable_recursion_pending();
      if (kSkip.count(lower)) continue;
    }
    if (is_game_dir(it->path())) found.insert(fs::weakly_canonical(it->path(), e2));
  }
}

std::vector<fs::path> find_game_dirs() {
  std::set<fs::path> found;
  std::error_code ec;
  for (fs::path p : {exe_dir(), fs::current_path(ec)}) {
    for (int up = 0; up < 3 && !p.empty(); up++, p = p.parent_path()) {
      if (is_game_dir(p)) found.insert(fs::weakly_canonical(p, ec));
    }
  }
  search(exe_dir(), 3, found);
  search(fs::current_path(ec), 3, found);
  const char* home = std::getenv("USERPROFILE");
  if (!home) home = std::getenv("HOME");
  if (home) {
    for (const char* sub : {"Downloads", "Desktop", "Documents"}) search(fs::path(home) / sub, 5, found);
  }
#ifdef _WIN32
  for (char drive = 'C'; drive <= 'Z'; drive++) {
    std::string root = std::string(1, drive) + ":\\";
    UINT type = GetDriveTypeA(root.c_str());
    if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE) search(root, 3, found);
  }
#endif
  return {found.begin(), found.end()};
}

fs::path pick_game_dir(bool force_search) {
  if (!force_search) {
    std::ifstream cfg(config_path());
    std::string saved;
    if (cfg && std::getline(cfg, saved) && is_game_dir(trim_path(saved))) return trim_path(saved);
  }
  std::cout << "Looking for your game folder (the one with " << kFastFile << ")...\n";
  std::vector<fs::path> dirs = find_game_dirs();
  fs::path chosen;
  if (dirs.size() == 1) {
    chosen = dirs[0];
    std::cout << "Found it: " << chosen.string() << "\n";
  } else {
    if (dirs.empty()) {
      std::cout << "Couldn't find it automatically.\n";
    } else {
      std::cout << "Found more than one:\n";
      for (size_t i = 0; i < dirs.size(); i++) std::cout << "  " << i + 1 << ") " << dirs[i].string() << "\n";
    }
    while (true) {
      std::string in = ask(dirs.empty() ? "Paste or drag in the game folder: "
                                        : "Pick a number, or paste a folder path: ");
      char* endp = nullptr;
      long n = std::strtol(in.c_str(), &endp, 10);
      if (!in.empty() && *endp == 0 && n >= 1 && size_t(n) <= dirs.size()) {
        chosen = dirs[size_t(n) - 1];
        break;
      }
      if (is_game_dir(in)) {
        chosen = in;
        break;
      }
      std::cout << "That folder has no " << kFastFile << ".\n";
    }
  }
  std::ofstream(config_path()) << chosen.string() << "\n";
  return chosen;
}

struct Session {
  fs::path game;
  FastFile ff;
  std::vector<CamoImage> camos;

  bool load() {
    std::string err;
    std::cout << "Unpacking " << kFastFile << "...\n";
    if (!ff.load(game / kFastFile, err)) {
      std::cout << "Error: " << err << "\n";
      return false;
    }
    camos = find_camo_images(ff.zone());
    std::cout << "Found " << camos.size() << " camo textures.\n";
    return true;
  }
};

const char* status(const CamoImage& c) {
  if (c.editable()) return "can replace";
  if (!c.embedded) return "streamed from .ipak (not supported yet)";
  return "unsupported format";
}

void list_camos(const Session& s) {
  std::printf("\n  #  %-34s %-28s %-9s %s\n", "camo", "texture", "size", "status");
  for (size_t i = 0; i < s.camos.size(); i++) {
    const CamoImage& c = s.camos[i];
    char size[32];
    std::snprintf(size, sizeof(size), "%ux%u", c.width, c.height);
    std::printf("%3zu  %-34s %-28s %-9s %s\n", i + 1, c.label.c_str(), c.name.c_str(), size, status(c));
  }
  std::printf("\n");
}

void export_camos(const Session& s, bool all_mips) {
  fs::path out = exe_dir() / "camos_export";
  std::error_code ec;
  fs::create_directories(out, ec);
  int n = 0;
  for (const CamoImage& c : s.camos) {
    for (uint32_t level = 0; level < (all_mips ? c.levels : 1u); level++) {
      Rgba img;
      if (!decode_level(s.ff.zone(), c, level, img)) break;
      std::string file = c.name + (level ? "_mip" + std::to_string(level) : "") + ".png";
      if (stbi_write_png((out / file).string().c_str(), int(img.width), int(img.height), 4,
                         img.pixels.data(), int(img.width * 4)))
        n++;
    }
  }
  std::cout << "Exported " << n << " images to " << out.string() << "\n";
  std::cout << "Streamed camos (Nevada, Sahara, ...) aren't included; their pixels are in the .ipak.\n";
}

bool load_picture(const fs::path& path, Rgba& out) {
  int w, h, comp;
  std::ifstream f(path, std::ios::binary);
  std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (bytes.empty()) return false;
  unsigned char* px = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &comp, 4);
  if (!px) return false;
  out.width = uint32_t(w);
  out.height = uint32_t(h);
  out.pixels.assign(px, px + size_t(w) * h * 4);
  stbi_image_free(px);
  return true;
}

// Also recolours the matching far-away tint, e.g. solid_camo_desert_nevada for camo_desert_nevada.
const CamoImage* tint_for(const Session& s, const CamoImage& c) {
  if (c.name.rfind("camo_", 0) != 0) return nullptr;
  std::string want = "solid_" + c.name;
  for (const CamoImage& t : s.camos)
    if (t.name == want && t.editable()) return &t;
  return nullptr;
}

bool replace_camo(Session& s, const CamoImage& camo, const fs::path& picture) {
  Rgba src;
  if (!load_picture(picture, src)) {
    std::cout << "Couldn't read " << picture.string() << " (use PNG, JPG, BMP or TGA).\n";
    return false;
  }
  std::vector<uint8_t> zone = s.ff.zone();
  std::string err;
  if (!encode_image(zone, camo, src, err)) {
    std::cout << "Error: " << err << "\n";
    return false;
  }
  if (const CamoImage* tint = tint_for(s, camo)) encode_image(zone, *tint, src, err);

  fs::path ff_path = s.game / kFastFile;
  fs::path backup = ff_path;
  backup += kBackupSuffix;
  std::error_code ec;
  if (!fs::exists(backup, ec)) {
    fs::copy_file(ff_path, backup, ec);
    if (ec) {
      std::cout << "Couldn't back up " << kFastFile << ": " << ec.message() << "\n";
      return false;
    }
    std::cout << "Backed up the original to " << backup.filename().string() << "\n";
  }
  if (!s.ff.save(ff_path, zone, err)) {
    std::cout << "Error: " << err << "\n";
    return false;
  }
  // Read it back to make sure the game will see what we wrote.
  FastFile check;
  if (!check.load(ff_path, err) || check.zone() != zone) {
    std::cout << "The written file didn't read back correctly, restoring the original.\n";
    fs::copy_file(backup, ff_path, fs::copy_options::overwrite_existing, ec);
    return false;
  }
  std::cout << "Done. " << camo.label << " (" << camo.name << ") now uses " << picture.filename().string()
            << ". Restart the game in Xenia to see it.\n";
  return s.load();
}

bool restore(Session& s) {
  fs::path ff_path = s.game / kFastFile, backup = ff_path;
  backup += kBackupSuffix;
  std::error_code ec;
  if (!fs::exists(backup, ec)) {
    std::cout << "No backup found, the file hasn't been changed by camotool.\n";
    return false;
  }
  fs::copy_file(backup, ff_path, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    std::cout << "Restore failed: " << ec.message() << "\n";
    return false;
  }
  std::cout << "Restored the original " << kFastFile << ".\n";
  return s.load();
}

void ipak_report(const Session& s) {
  fs::path ipak = s.game / "common_mp.ipak";
  std::error_code ec;
  if (!fs::exists(ipak, ec)) {
    std::cout << "No common_mp.ipak in " << s.game.string() << "\n";
    return;
  }
  fs::path out = exe_dir() / "camotool_ipak_report.txt";
  std::ofstream(out) << inspect_ipak(ipak, s.camos);
  std::cout << "Wrote " << out.string() << ". Send that file over so ipak camo support can be added.\n";
}

const CamoImage* find_camo(const Session& s, const std::string& key) {
  char* endp = nullptr;
  long n = std::strtol(key.c_str(), &endp, 10);
  if (!key.empty() && *endp == 0 && n >= 1 && size_t(n) <= s.camos.size()) return &s.camos[size_t(n) - 1];
  for (const CamoImage& c : s.camos) {
    std::string label = c.label;
    std::transform(label.begin(), label.end(), label.begin(), ::tolower);
    std::string k = key;
    std::transform(k.begin(), k.end(), k.begin(), ::tolower);
    if (c.name == key || label == k) return &c;
  }
  return nullptr;
}

int usage() {
  std::cout << "usage: camotool [--game <folder>] [list | export [--mips] | replace <camo> <image> |\n"
               "                 restore | ipak]\n"
               "Run with no command for the menu.\n";
  return 1;
}

int interactive(Session& s) {
  while (true) {
    std::cout << "\nGame folder: " << s.game.string() << "\n"
              << "  1) List camos\n"
              << "  2) Export camos to PNG\n"
              << "  3) Replace a camo with your own image\n"
              << "  4) Restore the original " << kFastFile << "\n"
              << "  5) Make an .ipak report (for streamed camos)\n"
              << "  6) Pick a different game folder\n"
              << "  0) Quit\n";
    std::string choice = ask("> ");
    if (choice == "0" || choice == "q") return 0;
    if (choice == "1") list_camos(s);
    else if (choice == "2") export_camos(s, false);
    else if (choice == "3") {
      list_camos(s);
      const CamoImage* c = find_camo(s, ask("Camo number: "));
      if (!c) { std::cout << "No such camo.\n"; continue; }
      if (!c->editable()) { std::cout << c->name << ": " << status(*c) << "\n"; continue; }
      replace_camo(s, *c, ask("Drag your image here (PNG/JPG) and press Enter: "));
    } else if (choice == "4") restore(s);
    else if (choice == "5") ipak_report(s);
    else if (choice == "6") {
      s.game = pick_game_dir(true);
      s.load();
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  Session s;
  if (args.size() >= 2 && args[0] == "--game") {
    s.game = args[1];
    args.erase(args.begin(), args.begin() + 2);
    if (!is_game_dir(s.game)) {
      std::cout << s.game.string() << " has no " << kFastFile << "\n";
      return 1;
    }
  } else {
    s.game = pick_game_dir(false);
  }
  if (!s.load()) {
    if (args.empty()) ask("Press Enter to exit.");
    return 1;
  }
  if (args.empty()) return interactive(s);
  const std::string& cmd = args[0];
  if (cmd == "list") list_camos(s);
  else if (cmd == "export") export_camos(s, args.size() > 1 && args[1] == "--mips");
  else if (cmd == "replace" && args.size() == 3) {
    const CamoImage* c = find_camo(s, args[1]);
    if (!c) { std::cout << "No camo called " << args[1] << "\n"; return 1; }
    return replace_camo(s, *c, args[2]) ? 0 : 1;
  } else if (cmd == "restore") return restore(s) ? 0 : 1;
  else if (cmd == "ipak") ipak_report(s);
  else return usage();
  return 0;
}
