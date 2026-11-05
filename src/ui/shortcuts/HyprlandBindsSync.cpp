// src/ui/shortcuts/HyprlandBindsSync.cpp

#include "HyprlandBindsSync.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace saf {

namespace fs = std::filesystem;

namespace {

constexpr const char *CONF_BEGIN =
    "# >>> simpleautoframer: managed block — do not edit by hand <<<";
constexpr const char *CONF_END = "# <<< simpleautoframer: end managed block >>>";
constexpr const char *LUA_BEGIN =
    "-- >>> simpleautoframer: managed block — do not edit by hand <<<";
constexpr const char *LUA_END =
    "-- <<< simpleautoframer: end managed block >>>";
constexpr const char *LUA_BINDS_FILE = "simpleautoframer.lua";

fs::path hyprConfigDir() {
  if (const char *xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
    return fs::path(xdg) / "hypr";
  if (const char *home = std::getenv("HOME"); home && *home)
    return fs::path(home) / ".config" / "hypr";
  return {};
}

/// Translate "Ctrl+Alt+1" -> (mods={"CTRL","ALT"}, key="1").
std::pair<std::vector<std::string>, std::string>
splitAccel(const std::string &accel) {
  std::vector<std::string> mods;
  std::string key, cur;
  auto flush = [&]() {
    if (cur.empty())
      return;
    std::string up = cur;
    std::transform(up.begin(), up.end(), up.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    if (up == "CTRL" || up == "CONTROL" || up == "ALT" || up == "SHIFT" ||
        up == "SUPER" || up == "META" || up == "WIN") {
      mods.push_back((up == "WIN" || up == "META") ? "SUPER" : up);
    } else {
      key = cur;
    }
    cur.clear();
  };
  for (char c : accel) {
    if (c == '+')
      flush();
    else
      cur.push_back(c);
  }
  flush();
  return {mods, key};
}

std::string join(const std::vector<std::string> &v, const std::string &sep) {
  std::string out;
  for (const auto &s : v) {
    if (!out.empty())
      out += sep;
    out += s;
  }
  return out;
}

// Lua string literal (ids/keys are sanitized already, but be safe).
std::string luaQuote(const std::string &s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\')
      out += '\\';
    out += c;
  }
  return out + "\"";
}

std::optional<std::string> readFile(const fs::path &p) {
  std::ifstream in(p, std::ios::binary);
  if (!in)
    return std::nullopt;
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

/// Atomic write that follows symlinks, so a dotfile-managed config stays a
/// symlink and the real file gets updated.
bool writeFileAtomic(const fs::path &p, const std::string &content) {
  std::error_code ec;
  fs::path target = fs::exists(p, ec) ? fs::canonical(p, ec) : p;
  if (ec)
    target = p;
  fs::path tmp = target;
  tmp += ".saf-tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      std::cerr << "[hypr-sync] Cannot create " << tmp << std::endl;
      return false;
    }
    out << content;
    out.flush();
    if (!out) {
      std::cerr << "[hypr-sync] Write to " << tmp << " failed." << std::endl;
      fs::remove(tmp, ec);
      return false;
    }
  }
  fs::rename(tmp, target, ec);
  if (ec) {
    std::cerr << "[hypr-sync] rename " << tmp << " -> " << target
              << " failed: " << ec.message() << std::endl;
    fs::remove(tmp, ec);
    return false;
  }
  return true;
}

/// Replace the block between `begin`/`end` markers in `original` with
/// `block` (which includes the markers). Empty `block` removes it; a
/// missing block is appended.
std::string replaceBlock(const std::string &original, const char *begin,
                         const char *end, const std::string &block) {
  auto b = original.find(begin);
  if (b == std::string::npos) {
    if (block.empty())
      return original;
    std::string out = original;
    if (!out.empty() && out.back() != '\n')
      out += '\n';
    if (!out.empty())
      out += '\n';
    return out + block;
  }
  auto e = original.find(end, b);
  auto lineEnd =
      (e == std::string::npos) ? original.size() : original.find('\n', e);
  if (lineEnd == std::string::npos)
    lineEnd = original.size();
  else
    lineEnd += 1;
  return original.substr(0, b) + block + original.substr(lineEnd);
}

/// Update `file`'s managed block; returns true if the file changed.
bool updateFileBlock(const fs::path &file, const char *begin, const char *end,
                     const std::string &block, bool &failed) {
  auto content = readFile(file);
  if (!content) {
    std::cerr << "[hypr-sync] Cannot read " << file << std::endl;
    failed = true;
    return false;
  }
  const std::string updated = replaceBlock(*content, begin, end, block);
  if (updated == *content)
    return false;
  if (!writeFileAtomic(file, updated)) {
    failed = true;
    return false;
  }
  std::cerr << "[hypr-sync] Updated " << file.string() << std::endl;
  return true;
}

bool runHyprctlReload() {
  FILE *p = popen("hyprctl reload 2>&1", "r");
  if (!p)
    return false;
  char buf[256];
  while (std::fgets(buf, sizeof(buf), p)) {
    std::string s(buf);
    if (!s.empty() && s.back() == '\n')
      s.pop_back();
    std::cerr << "[hypr-sync] hyprctl: " << s << std::endl;
  }
  return pclose(p) == 0;
}

std::string buildConfBlock(const std::vector<ShortcutDescriptor> &sc) {
  std::ostringstream os;
  os << CONF_BEGIN << '\n'
     << "# Generated by SimpleAutoFramer (disable via its menu: \"Sync "
        "shortcuts to Hyprland config\").\n";
  for (const auto &d : sc) {
    auto [mods, key] = splitAccel(d.defaultAccel);
    if (!key.empty())
      os << "bind = " << join(mods, " ") << ", " << key << ", global, :"
         << d.id << '\n';
  }
  os << CONF_END << '\n';
  return os.str();
}

std::string buildLuaBindsFile(const std::vector<ShortcutDescriptor> &sc) {
  std::ostringstream os;
  os << "-- Generated by SimpleAutoFramer — overwritten on every change.\n"
     << "-- Disable via its menu: \"Sync shortcuts to Hyprland config\".\n";
  for (const auto &d : sc) {
    auto [mods, key] = splitAccel(d.defaultAccel);
    if (key.empty())
      continue;
    mods.push_back(key);
    os << "hl.bind(" << luaQuote(join(mods, " + ")) << ", hl.dsp.global("
       << luaQuote(":" + d.id) << "))\n";
  }
  return os.str();
}

std::string buildLuaIncludeBlock(const fs::path &bindsFile) {
  std::ostringstream os;
  os << LUA_BEGIN << '\n'
     << "pcall(dofile, " << luaQuote(bindsFile.string()) << ")\n"
     << LUA_END << '\n';
  return os.str();
}

} // namespace

bool isHyprlandSession() {
  if (std::getenv("HYPRLAND_INSTANCE_SIGNATURE"))
    return true;
  const char *desktop = std::getenv("XDG_CURRENT_DESKTOP");
  return desktop && std::strstr(desktop, "Hyprland");
}

namespace {

// Shared implementation: `shortcuts == nullptr` means "remove".
bool applyHyprland(const std::vector<ShortcutDescriptor> *shortcuts) {
  if (!isHyprlandSession())
    return false;

  const fs::path dir = hyprConfigDir();
  const fs::path lua = dir / "hyprland.lua";
  const fs::path conf = dir / "hyprland.conf";
  const fs::path luaBinds = dir / LUA_BINDS_FILE;
  std::error_code ec;

  bool changed = false, failed = false;
  if (fs::exists(lua, ec)) {
    // Hyprland prefers hyprland.lua when both exist.
    if (shortcuts) {
      const std::string binds = buildLuaBindsFile(*shortcuts);
      if (readFile(luaBinds).value_or("") != binds) {
        if (writeFileAtomic(luaBinds, binds))
          changed = true;
        else
          failed = true;
      }
      changed |= updateFileBlock(lua, LUA_BEGIN, LUA_END,
                                 buildLuaIncludeBlock(luaBinds), failed);
    } else {
      changed |= updateFileBlock(lua, LUA_BEGIN, LUA_END, "", failed);
      if (fs::remove(luaBinds, ec))
        changed = true;
    }
  } else if (fs::exists(conf, ec)) {
    changed |= updateFileBlock(conf, CONF_BEGIN, CONF_END,
                               shortcuts ? buildConfBlock(*shortcuts) : "",
                               failed);
  } else {
    std::cerr << "[hypr-sync] No hyprland.lua / hyprland.conf in " << dir
              << " — skipping auto-binds." << std::endl;
    return false;
  }

  if (failed)
    return false;
  if (!changed) {
    std::cerr << "[hypr-sync] Hyprland config already up to date." << std::endl;
    return true;
  }
  if (runHyprctlReload()) {
    std::cerr << "[hypr-sync] hyprctl reload OK." << std::endl;
    return true;
  }
  std::cerr << "[hypr-sync] hyprctl reload failed. Run it manually to apply "
               "the binds."
            << std::endl;
  return false;
}

} // namespace

bool syncHyprlandBinds(const std::vector<ShortcutDescriptor> &shortcuts) {
  return applyHyprland(&shortcuts);
}

bool removeHyprlandBinds() { return applyHyprland(nullptr); }

} // namespace saf
