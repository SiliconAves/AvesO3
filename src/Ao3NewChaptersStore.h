#pragma once
#include <string>
#include <vector>

struct Ao3NewChaptersEntry {
  std::string path;
  std::string title;
  std::string author;

  bool operator==(const Ao3NewChaptersEntry& other) const {
    return path == other.path;
  }
};

// ---------------------------------------------------------------------------
//  Ao3NewChaptersStore — singleton store for "New Chapters" (Dashboard Tab 1)
//
//  - JSON file at /.crosspoint/new_chapters.json
//  - Max 10 entries; most recently updated book is at index 0.
//  - When a book already in the list gains a new chapter it moves to index 0.
//  - When the list is full (10 entries) a new book is NOT added (plan §3).
//  - Entries are pruned when their backing file is missing (call pruneMissing()
//    on activity onEnter).
// ---------------------------------------------------------------------------

class Ao3NewChaptersStore {
  static Ao3NewChaptersStore instance;

  std::vector<Ao3NewChaptersEntry> entries;

 public:
  ~Ao3NewChaptersStore() = default;

  static Ao3NewChaptersStore& getInstance() { return instance; }

  // Insert or move-to-top.  No-op if list is full and path not already present.
  // Persists on success.
  void addBook(const std::string& path, const std::string& title, const std::string& author);
  void clearEntries() { entries.clear(); entries.shrink_to_fit(); }

  // Remove entry whose path matches.  Returns true if an entry was removed.
  // Persists on success (best-effort).
  bool removeByPath(const std::string& path);

  // Remove entries whose backing file is no longer on the SD card.
  // Returns true if any entry was removed.  Does not persist — caller decides.
  bool pruneMissing();

  const std::vector<Ao3NewChaptersEntry>& getEntries() const { return entries; }

  int getCount() const { return static_cast<int>(entries.size()); }

  bool saveToFile() const;
  bool loadFromFile();

 private:
  static bool isMissing(const Ao3NewChaptersEntry& e);
};

// Convenience accessor macro.
#define NEW_CHAPTERS_STORE Ao3NewChaptersStore::getInstance()
