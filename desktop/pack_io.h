#pragma once
#include <stdio.h>
#include <string>
#include <vector>
#include "behaviour.h"
#include "engine.h"
#include "pack.h"

// Packs on the desktop and the web page (core/pack.h): their robot is the files in `dir`, the same on both, so a full backup moves between them as it is.
namespace packio {
struct FileRead {
    FILE* f;
    bool operator()(uint32_t off, void* buf, size_t n) const {
      return !fseek(f, (long)off, SEEK_SET) && fread(buf, 1, n, f) == n;
    }
};
inline std::string slurp(const std::string& path, bool& ok) {
  std::string s;
  FILE* f = fopen(path.c_str(), "rb");
  ok = f != nullptr;
  if (!f) return s;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
  fclose(f);
  return s;
}
inline bool spit(const std::string& path, const std::string& bytes) {
  const std::string tmp = path + ".tmp";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (!f) return false;
  const bool ok = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
  if (fclose(f) || !ok) {
    remove(tmp.c_str());
    return false;
  }
  remove(path.c_str());
  return !rename(tmp.c_str(), path.c_str());
}
struct RobotFile {
    const char* kind;
    const char* file;
};
inline const RobotFile ROBOT_FILES[] = {{"cfg", "teevee.cfg"},
                                        {"mind", "mind.bin"},
                                        {"diary", "diary.bin"},
                                        {"looknet", "lookmind.bin"},
                                        {"tastes", "tastes.bin"}};

// Writes the robot to a pack. all: with what it has learned (a backup, not something to share). False if the file cannot be written.
inline bool exportPack(Engine& engine, const std::string& dir, const std::string& path, bool all) {
  engine.save(dir.c_str());
  std::string out = PACK_MAGIC;
  int count = 0;
  const auto add = [&](const char* kind, const char* name, const std::string& bytes) {
    char head[48];
    packHead(head, sizeof head, kind, name, (uint32_t)bytes.size());
    out += head;
    out += bytes;
    out += '\n';
    count++;
  };
  char look[STYLE_TEXT_MAX];
  styleDescribe(engine.style(), look, sizeof look);
  add("look", "current", look);
  bool have;
  const std::string behaviour = slurp(dir + "/behaviour.txt", have);
  if (have) add("behaviour", "main", behaviour);
  if (all)
    for (const RobotFile& r : ROBOT_FILES) {
      const std::string bytes = slurp(dir + "/" + r.file, have);
      if (have) add(r.kind, "main", bytes);
    }
  char end[24];
  snprintf(end, sizeof end, "end %d\n", count);
  out += end;
  return spit(path, out);
}

// Installs a pack. all: also what it has learned, replacing this robot's. A damaged file changes nothing. Returns false and says why in `report`, or says what was done.
inline bool importPack(Engine& engine, const std::string& dir, const std::string& path, bool all,
                       std::string& report) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) {
    report = "cannot open the file";
    return false;
  }
  fseek(f, 0, SEEK_END);
  const uint32_t size = (uint32_t)ftell(f);
  const FileRead read{f};
  if (!packCheck(read, size)) {
    fclose(f);
    report = "not a pack, or a damaged one: nothing was changed";
    return false;
  }
  PackReport rep;
  const auto bytesOf = [&](const PackEntry& e) {
    std::string s(e.size, '\0');
    if (e.size) read(e.offset, &s[0], e.size);
    return s;
  };
  const auto walk = [&](auto&& each) {
    uint32_t at;
    packOpen(read, size, at);
    PackEntry e;
    int seen = 0;
    while (packNext(read, size, at, e, seen) == PACK_ENTRY) each(e);
  };
  bool anyRobot = false;
  walk([&](const PackEntry& e) {
    if (!packIsRobot(e.kind)) return;
    bool mine = false;
    const std::string bytes = bytesOf(e);
    for (const RobotFile& r : ROBOT_FILES)
      if (!strcmp(r.kind, e.kind) && !strcmp(e.name, "main")) {
        mine = all;
        if (mine && !strcmp(r.kind, "mind")) {
          static MindNet check;
          mine = check.load(bytes.data(), bytes.size());
        }
        if (mine && !strcmp(r.kind, "diary")) mine = bytes.size() == sizeof(Diary::Saved);
        if (mine && !strcmp(r.kind, "tastes")) {
          static Tastes check;
          mine = check.load(bytes.data(), bytes.size());
        }
        if (mine) mine = spit(dir + "/" + r.file, bytes);
      }
    if (mine)
      rep.done(e.kind), anyRobot = true;
    else
      rep.skip(e.kind);
  });
  if (anyRobot) engine.load(dir.c_str());
  walk([&](const PackEntry& e) {
    if (packIsRobot(e.kind)) return;
    const std::string bytes = bytesOf(e);
    if (!strcmp(e.kind, "look") && !strcmp(e.name, "current") && bytes.size() < STYLE_TEXT_MAX &&
        engine.styleText(bytes.c_str()))
      rep.done("look");
    else if (!strcmp(e.kind, "behaviour") && !strcmp(e.name, "main")) {
      char err[64];
      if (engine.setBehaviour(bytes.data(), bytes.size(), err, sizeof err) &&
          spit(dir + "/behaviour.txt", bytes))
        rep.done("behaviour");
      else
        rep.skip("behaviour");
    } else if (!strcmp(e.kind, "about")) {
    } else {
      rep.skip(e.kind);
    }
  });
  fclose(f);
  engine.save(dir.c_str());
  char text[200];
  rep.text(text, sizeof text);
  report = text;
  return true;
}
}  // namespace packio
