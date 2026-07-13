#pragma once
#include <stdint.h>

enum class SortMode : uint8_t {
    ALPHABETIC  = 0,
    WORD_COUNT  = 1,
    DATE_ADDED  = 2,
    SERIES      = 3,
    AUTHOR      = 4
};

struct SortFilterState {
    char     fandom[32] = {};
    char     relationship[32] = {};
    bool     relationshipNoneOnly = false;
    SortMode sortMode = SortMode::ALPHABETIC;
    bool     ascending = true;
};
