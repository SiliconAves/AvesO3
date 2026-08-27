#pragma once
#include <string>
#include <vector>

struct Ao3MarkedForLaterEntry {
  std::string path;
  std::string title;
  std::string author;

  bool operator==(const Ao3MarkedForLaterEntry& other) const {
    return path == other.path;
  }
};

// ---------------------------------------------------------------------------
//  Ao3MarkedForLaterStore — singleton store for "Marked for Later" (Dashboard Tab 0)
//
//  - JSON file at /.crosspoint/marked_for_later.json
//  - Max 10 entries; insertion order is FIFO — oldest entry is at index 0,
//    newest entry is appended at the back.
//  - If the book is already in the list, addBook() is a no-op (no move-to-top).
//  - If the list is full (10 entries), addBook() returns false and the caller
//    is responsible for showing the cap message ("Marked for Later list full (10/10)").
//  - Entries are pruned when their backing file is missing (call pruneMissing()
//    on activity onEnter).
//  - Queue position (1–N) is always derived from index at render time; it is
//    never stored explicitly.
// ---------------------------------------------------------------------------

class Ao3MarkedForLaterStore {
  static Ao3MarkedForLaterStore instance;

  std::vector<Ao3MarkedForLaterEntry> entries;

  static constexpr int MAX_ENTRIES = 10;

 public:
  ~Ao3MarkedForLaterStore() = default;

  static Ao3MarkedForLaterStore& getInstance() { return instance; }

  // Append to the back of the list (FIFO order).
  // Returns false if the list is full or path is already present (no-op in
  // both cases); returns true on successful insertion.
  // Persists on success.
  bool addBook(const std::string& path,
               const std::string& title,
               const std::string& author);
  void clearEntries() { entries.clear(); entries.shrink_to_fit(); }

  // Returns true if path is already in the list.
  bool contains(const std::string& path) const;
  // Updates metadata if it is currently empty. Returns true if a change was made.
  // Does NOT persist automatically; caller must call saveToFile().
  bool updateEntryMetadata(const std::string& path, const std::string& title, const std::string& author);

  // Returns whether the list has reached capacity.
  bool isFull() const { return static_cast<int>(entries.size()) >= MAX_ENTRIES; }

  // Remove entry whose path matches.  Returns true if an entry was removed.
  // Persists on success (best-effort).
  bool removeByPath(const std::string& path);

  // Remove entries whose backing file is no longer on the SD card.
  // Returns true if any entry was removed.  Does not persist — caller decides.
  bool pruneMissing();

  const std::vector<Ao3MarkedForLaterEntry>& getEntries() const { return entries; }

  int getCount() const { return static_cast<int>(entries.size()); }

  // Returns 1-based queue position for path, or -1 if not found.
  int getQueuePosition(const std::string& path) const;

  bool saveToFile() const;
  bool loadFromFile();

 private:
  static bool isMissing(const Ao3MarkedForLaterEntry& e);
};

// Convenience accessor macro.
#define MARKED_FOR_LATER_STORE Ao3MarkedForLaterStore::getInstance()
