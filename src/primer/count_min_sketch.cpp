//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// count_min_sketch.cpp
//
// Identification: src/primer/count_min_sketch.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "primer/count_min_sketch.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace bustub {

/**
 * Constructor for the count-min sketch.
 *
 * @param width The width of the sketch matrix.
 * @param depth The depth of the sketch matrix.
 * @throws std::invalid_argument if width or depth are zero.
 */
template <typename KeyType>
CountMinSketch<KeyType>::CountMinSketch(uint32_t width, uint32_t depth) : width_(width), depth_(depth) {
  if (width == 0 || depth == 0) {
    throw std::invalid_argument("Width and depth must be non-zero.");
  }
  // Initialize the table with depth rows and width columns, all set to 0.
  table_.resize(depth_, std::vector<uint32_t>(width_, 0));

  row_latches_.reserve(depth_);
  for (uint32_t i = 0; i < depth_; ++i) {
    row_latches_.push_back(std::make_unique<std::mutex>());
  }

  /** @fall2025 PLEASE DO NOT MODIFY THE FOLLOWING */
  // Initialize seeded hash functions
  hash_functions_.reserve(depth_);
  for (size_t i = 0; i < depth_; i++) {
    hash_functions_.push_back(this->HashFunction(i));
  }
}

template <typename KeyType>
CountMinSketch<KeyType>::CountMinSketch(CountMinSketch &&other) noexcept
    : width_(other.width_),
      depth_(other.depth_),
      table_(std::move(other.table_)),
      row_latches_(std::move(other.row_latches_)) {
  // Regenerate hash functions so they are bound to THIS object's `width_`
  hash_functions_.reserve(depth_);
  for (size_t i = 0; i < depth_; i++) {
    hash_functions_.push_back(this->HashFunction(i));
  }

  // Leave the moved-from object in a valid, empty state
  other.width_ = 0;
  other.depth_ = 0;
  other.hash_functions_.clear();
  // other.table_ and other.row_latches_ are already empty due to std::move
}

template <typename KeyType>
auto CountMinSketch<KeyType>::operator=(CountMinSketch &&other) noexcept -> CountMinSketch & {
  if (this != &other) {
    std::scoped_lock lock(latch_, other.latch_);

    // Move resources
    width_ = other.width_;
    depth_ = other.depth_;
    table_ = std::move(other.table_);
    row_latches_ = std::move(other.row_latches_);

    // Regenerate hash functions for THIS object
    hash_functions_.clear();
    hash_functions_.reserve(depth_);
    for (size_t i = 0; i < depth_; i++) {
      hash_functions_.push_back(this->HashFunction(i));
    }

    // Leave the moved-from object in a valid, empty state
    other.width_ = 0;
    other.depth_ = 0;
    other.hash_functions_.clear();
  }
  return *this;
}

template <typename KeyType>
void CountMinSketch<KeyType>::Insert(const KeyType &item) {
  // Lock is now INSIDE the loop
  for (uint32_t i = 0; i < depth_; ++i) {
    uint32_t index = hash_functions_[i](item);
    // Lock only the mutex for the current row 'i'
    std::scoped_lock<std::mutex> lock(*row_latches_[i]);
    table_[i][index]++;
  }
}

template <typename KeyType>
void CountMinSketch<KeyType>::Merge(const CountMinSketch<KeyType> &other) {
  if (width_ != other.width_ || depth_ != other.depth_) {
    throw std::invalid_argument("Incompatible CountMinSketch dimensions for merge.");
  }
  /** @TODO(student) Implement this function! */
  // Lock both mutexes to prevent deadlock and ensure thread safety.
  std::scoped_lock lock(latch_, other.latch_);
  for (uint32_t i = 0; i < depth_; ++i) {
    for (uint32_t j = 0; j < width_; ++j) {
      table_[i][j] += other.table_[i][j];
    }
  }
}

template <typename KeyType>
auto CountMinSketch<KeyType>::Count(const KeyType &item) const -> uint32_t {
  uint32_t min_count = std::numeric_limits<uint32_t>::max();
  for (uint32_t i = 0; i < depth_; ++i) {
    uint32_t index = hash_functions_[i](item) % width_;
    min_count = std::min(min_count, table_[i][index]);
  }
  return min_count;
}

template <typename KeyType>
void CountMinSketch<KeyType>::Clear() {
  std::scoped_lock<std::mutex> lock(latch_);
  for (auto &row : table_) {
    std::fill(row.begin(), row.end(), 0);
  }
}

template <typename KeyType>
auto CountMinSketch<KeyType>::TopK(uint16_t k, const std::vector<KeyType> &candidates)
    -> std::vector<std::pair<KeyType, uint32_t>> {
  std::vector<std::pair<KeyType, uint32_t>> estimated_counts;
  estimated_counts.reserve(candidates.size());

  for (const auto &candidate : candidates) {
    estimated_counts.emplace_back(candidate, Count(candidate));
  }

  // Define a comparator for sorting pairs by count in descending order.
  auto comparator = [](const auto &a, const auto &b) { return a.second > b.second; };

  // Partially sort to find the top k elements efficiently.
  uint16_t effective_k = std::min(k, static_cast<uint16_t>(estimated_counts.size()));
  std::partial_sort(estimated_counts.begin(), estimated_counts.begin() + effective_k, estimated_counts.end(),
                    comparator);

  // Resize the vector to contain only the top k elements.
  estimated_counts.resize(effective_k);

  return estimated_counts;
}

// Explicit instantiations for all types used in tests
template class CountMinSketch<std::string>;
template class CountMinSketch<int64_t>;  // For int64_t tests
template class CountMinSketch<int>;      // This covers both int and int32_t
}  // namespace bustub
