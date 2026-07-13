#pragma once

#include <stdint.h>
#include <cstring>
#include <algorithm>
#include "Ao3CompactIndexRecord.h"

// FNV-1a 32-bit hash. Returns 0 for null/empty strings (sentinel for "no value").
inline uint32_t fnv1a(const char* str) {
    if (!str || str[0] == '\0') return 0;
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= static_cast<uint8_t>(*str++);
        hash *= 16777619u;
    }
    return hash;
}

/**
 * @brief In-RAM sort/filter key struct — one per live book, loaded sequentially
 *        from ao3_library_index.bin at library startup.
 *
 * 82 raw bytes → compiler pads to 84 bytes.
 * 84 × 400 books = 33.6 KB peak RAM.
 */
struct ViewEntry {
    uint32_t fileOffset;     // byte offset of this record in ao3_library_index.bin
    char     title[32];      // first 31 chars of title, null-terminated (alphabetic sort)
    char     authorKey[16];  // first 15 chars of author lowercased (author sort)
    uint32_t wordCount;      // word count sort
    uint32_t addedSequence;  // date-added sort (monotonic, higher = newer)
    uint32_t seriesHash;     // fnv1a(seriesName), 0 if no series
    uint16_t seriesPart;     // position within series, 0 if not in a series
    uint32_t fandomHash;     // fnv1a(fandom), 0 if empty
    uint32_t rel1Hash;       // fnv1a(relationship1), 0 if empty
    uint32_t rel2Hash;       // fnv1a(relationship2), 0 if empty
    uint32_t cacheHash;      // same as CompactIndexRecord.cacheHash
};

/**
 * @brief Build a ViewEntry from a CompactIndexRecord at a known byte offset.
 */
inline ViewEntry buildViewEntry(const CompactIndexRecord& rec, uint32_t fileOffset) {
    ViewEntry v{};
    v.fileOffset = fileOffset;

    strncpy(v.title,     rec.title,  31); v.title[31]     = '\0';

    // authorKey: first 15 chars, lowercased for case-insensitive sort
    strncpy(v.authorKey, rec.author, 15); v.authorKey[15] = '\0';
    for (char* p = v.authorKey; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') *p += 32;
    }

    v.wordCount     = rec.wordCount;
    v.addedSequence = rec.addedSequence;
    v.seriesHash    = fnv1a(rec.seriesName);
    v.seriesPart    = rec.seriesPart;
    v.fandomHash    = fnv1a(rec.fandom);
    v.rel1Hash      = fnv1a(rec.relationship1);
    v.rel2Hash      = fnv1a(rec.relationship2);
    v.cacheHash     = rec.cacheHash;
    return v;
}
