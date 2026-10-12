#pragma once
#include <string>
#include <utility>
#include <vector>

class Ao3FandomCoverStore {
  std::vector<std::pair<std::string, std::string>> entries;  // fandom -> bmp filename
  bool loaded = false;
  void load();
  bool save() const;
  static std::string key(const char* f) { return std::string(f).substr(0, 31); }

 public:
  static Ao3FandomCoverStore& getInstance() { static Ao3FandomCoverStore s; return s; }
  const std::string* find(const char* fandom);                 // lazy-loads on first call
  void assign(const char* fandom, const std::string& file);    // empty file = unassign; saves
};