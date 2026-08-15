#pragma once
#include <string>
#include <vector>

struct Ao3WipEntry {
    std::string path;
    std::string title;
    std::string author;

    bool operator==(const Ao3WipEntry& other) const {
        return path == other.path;
    }
};

class Ao3WipsStore {
    static Ao3WipsStore instance;
    std::vector<Ao3WipEntry> entries;

    void sortAlphabetically();

public:
    ~Ao3WipsStore() = default;

    static Ao3WipsStore& getInstance() { return instance; }

    void addBook(const std::string& path, const std::string& title, const std::string& author);
    bool removeBook(const std::string& path);
    bool pruneMissing();

    const std::vector<Ao3WipEntry>& getEntries() const { return entries; }
    size_t size() const { return entries.size(); }

    bool saveToFile() const;
    bool loadFromFile();
};

// Convenience accessor macro matching NewChapters
#define AO3_WIPS_STORE Ao3WipsStore::getInstance()