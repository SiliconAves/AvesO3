#pragma once
#include <cstdint>

enum class BookStatus : uint8_t {
  START = 0,
  READING = 1,
  FINISHED = 2,
  WAITING_FOR_CHAPTER = 3,
  NEW_CHAPTER_AVAILABLE = 4
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
    default:
      return "Unknown";
  }
}

