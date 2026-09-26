#pragma once
#include <algorithm>
#include <limits>
#include <vector>

#include "../language.hpp"

namespace eggc::detail {
// Flat open-addressed keys preserve capacity across candidate searches.
class BindingSet {
 public:
  void reset(std::size_t width) {
    width_ = width;
    count_ = 0;
    keys_.clear();
    std::fill(buckets_.begin(), buckets_.end(), empty);
  }
  bool insert(const std::vector<Id>& key) {
    if (width_ == 0) {
      if (count_) return false;
      ++count_;
      return true;
    }
    if (buckets_.empty() || (count_ + 1) * 2 > buckets_.size()) grow();
    auto slot = hash(key.data()) & (buckets_.size() - 1);
    while (buckets_[slot] != empty) {
      const auto offset = buckets_[slot] * width_;
      if (std::equal(key.begin(), key.end(), keys_.begin() + offset))
        return false;
      slot = (slot + 1) & (buckets_.size() - 1);
    }
    buckets_[slot] = count_++;
    keys_.insert(keys_.end(), key.begin(), key.end());
    return true;
  }

 private:
  static constexpr std::size_t empty = std::numeric_limits<std::size_t>::max();
  std::size_t hash(const Id* key) const {
    std::size_t value = 0;
    for (std::size_t i = 0; i < width_; ++i) hash_combine(value, key[i]);
    return value;
  }
  void grow() {
    buckets_.assign(buckets_.empty() ? 16 : buckets_.size() * 2, empty);
    for (std::size_t i = 0; i < count_; ++i) {
      auto slot = hash(keys_.data() + i * width_) & (buckets_.size() - 1);
      while (buckets_[slot] != empty) slot = (slot + 1) & (buckets_.size() - 1);
      buckets_[slot] = i;
    }
  }
  std::size_t width_ = 0, count_ = 0;
  std::vector<Id> keys_;
  std::vector<std::size_t> buckets_;
};
}  // namespace eggc::detail
