#pragma once
#include <stdint.h>
#include <vector>
#include <utility>

/**
 * @brief Lightweight in-memory lookup table for tag merge groups.
 * Maps sub-tag hashes to their master-tag hash for both fandoms and relationships.
 * Loaded on library open, freed on exit. Two sorted vectors of pairs — negligible RAM.
 */
class Ao3TagMergeStore {
public:
    static constexpr const char* kPath = "/.crosspoint/ao3_tag_merges.json";

    static void load();
    static void unload();

    // If hash is a known sub-tag, returns the master's hash. Otherwise returns hash unchanged.
    static uint32_t resolveFandom(uint32_t hash);
    static uint32_t resolveRelationship(uint32_t hash);

    // Returns true if this hash belongs to a sub-tag (must be suppressed in pickers).
    static bool isSubFandom(uint32_t hash);
    static bool isSubRelationship(uint32_t hash);

private:
    static std::vector<std::pair<uint32_t, uint32_t>> fandomMap_;   // sub_hash -> master_hash, sorted
    static std::vector<std::pair<uint32_t, uint32_t>> relMap_;
};