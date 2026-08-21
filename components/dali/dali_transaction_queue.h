#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace dali_transaction {

/// Small, allocation-free-after-warmup transaction queue for the component's
/// main-loop scheduler. Payloads are opaque, stable pointers owned by the bus
/// component. Re-enqueueing a payload coalesces it instead of allowing stale
/// writes (for example, intermediate brightness slider positions) to pile up.
enum class Priority : uint8_t {
  INTERACTIVE = 0,
  BACKGROUND = 1,
};

class Queue {
 public:
  void enqueue(void *payload, Priority priority) {
    for (auto &entry : entries_) {
      if (entry.payload != payload)
        continue;
      // Keep the earliest position, but never lower an existing priority.
      if (static_cast<uint8_t>(priority) < static_cast<uint8_t>(entry.priority))
        entry.priority = priority;
      return;
    }
    entries_.push_back({payload, priority});
  }

  bool take_next(void *&payload) {
    if (entries_.empty())
      return false;

    size_t selected = 0;
    for (size_t i = 1; i < entries_.size(); i++) {
      if (static_cast<uint8_t>(entries_[i].priority) < static_cast<uint8_t>(entries_[selected].priority))
        selected = i;
    }
    payload = entries_[selected].payload;
    entries_.erase(entries_.begin() + selected);
    return true;
  }

  bool empty() const { return entries_.empty(); }
  size_t size() const { return entries_.size(); }

 private:
  struct Entry {
    void *payload;
    Priority priority;
  };
  std::vector<Entry> entries_;
};

}  // namespace dali_transaction
