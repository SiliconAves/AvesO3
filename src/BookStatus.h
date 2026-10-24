#pragma once
#include <cstdint>

enum class BookStatus : uint8_t {
  START = 0,
  READING = 1,
  FINISHED = 2,
  WAITING_FOR_CHAPTER = 3,
  NEW_CHAPTER_AVAILABLE = 4,
  MARKED_FOR_LATER = 5
};

inline const char* getStatusLabel(BookStatus status) {
  switch (status) {
    case BookStatus::START:
      return "Unread";
    case BookStatus::READING:
      return "Reading";
    case BookStatus::FINISHED:
      return "Finished";
    case BookStatus::WAITING_FOR_CHAPTER:
      return "Waiting for Chapter";
    case BookStatus::NEW_CHAPTER_AVAILABLE:
      return "New Chapter Available";
    case BookStatus::MARKED_FOR_LATER:
      return "Marked for Later";
    default:
      return "Unknown";
  }
}

// Statuses the user can pick manually in the status popup.
// MARKED_FOR_LATER is intentionally excluded: it is managed only by the
// Mark for Later toggle. Popup index == enum value for these five.
inline constexpr BookStatus kSelectableStatuses[] = {BookStatus::START, BookStatus::READING, BookStatus::FINISHED,
                                                     BookStatus::WAITING_FOR_CHAPTER,
                                                     BookStatus::NEW_CHAPTER_AVAILABLE};
inline constexpr int kSelectableStatusCount = sizeof(kSelectableStatuses) / sizeof(kSelectableStatuses[0]);

// Index of `status` in kSelectableStatuses, or 0 if it isn't selectable.
inline int selectableStatusIndex(BookStatus status) {
  for (int i = 0; i < kSelectableStatusCount; i++) {
    if (kSelectableStatuses[i] == status) return i;
  }
  return 0;
}