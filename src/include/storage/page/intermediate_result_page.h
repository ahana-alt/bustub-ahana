//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// intermediate_result_page.h
//
// Identification: src/include/storage/page/intermediate_result_page.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#pragma once
#include <cstddef>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>
#include "common/config.h"
#include "storage/table/tuple.h"
namespace bustub {
/**
 * Page to hold the intermediate data for external merge sort and hash join.
 * Supports variable-length tuples.
 */
class IntermediateResultPage {
 public:
  void Init() {
    SetTupleCount(0);
    SetFreeSpaceOffset(BUSTUB_PAGE_SIZE);
  }

  auto Insert(const Tuple &tuple) -> bool {
    auto tuple_size = tuple.GetLength();
    // Need to store: size header (4 bytes) + tuple data + offset entry
    auto total_tuple_size = sizeof(uint32_t) + tuple_size;
    auto offset_size = sizeof(uint32_t);
    auto required_space = total_tuple_size + offset_size;

    if (GetFreeSpaceRemaining() < required_space) {
      return false;
    }

    // Calculate new offset (store from the end backwards)
    auto new_tuple_offset = GetFreeSpaceOffset() - total_tuple_size;

    // Store tuple size first
    *reinterpret_cast<uint32_t *>(GetData() + new_tuple_offset) = tuple_size;

    // Then store tuple data
    memcpy(GetData() + new_tuple_offset + sizeof(uint32_t), tuple.GetData(), tuple_size);

    auto tuple_count = GetTupleCount();
    SetOffset(tuple_count, new_tuple_offset);
    SetFreeSpaceOffset(new_tuple_offset);
    SetTupleCount(tuple_count + 1);

    return true;
  }

  auto GetTuple(uint32_t index) const -> Tuple {
    BUSTUB_ASSERT(index < GetTupleCount(), "Tuple index out of bounds");

    auto offset = GetOffset(index);
    Tuple tuple;
    // DeserializeFrom expects size header followed by data
    tuple.DeserializeFrom(GetData() + offset);

    return tuple;
  }

  auto GetTupleCount() const -> uint32_t { return *reinterpret_cast<const uint32_t *>(data_); }

  auto GetFreeSpaceRemaining() const -> uint32_t {
    auto header_size = sizeof(uint32_t) * 2;  // tuple_count + free_space_offset
    auto offset_array_size = GetTupleCount() * sizeof(uint32_t);
    auto used_space_start = header_size + offset_array_size;
    auto used_space_end = GetFreeSpaceOffset();

    if (used_space_end <= used_space_start) {
      return 0;
    }

    return used_space_end - used_space_start;
  }

  auto GetData() -> char * { return data_; }
  auto GetData() const -> const char * { return data_; }

 private:
  void SetTupleCount(uint32_t count) { *reinterpret_cast<uint32_t *>(data_) = count; }

  auto GetFreeSpaceOffset() const -> uint32_t { return *reinterpret_cast<const uint32_t *>(data_ + sizeof(uint32_t)); }

  void SetFreeSpaceOffset(uint32_t offset) { *reinterpret_cast<uint32_t *>(data_ + sizeof(uint32_t)) = offset; }

  auto GetOffset(uint32_t index) const -> uint32_t {
    auto offset_array_start = sizeof(uint32_t) * 2;
    return *reinterpret_cast<const uint32_t *>(data_ + offset_array_start + index * sizeof(uint32_t));
  }

  void SetOffset(uint32_t index, uint32_t offset) {
    auto offset_array_start = sizeof(uint32_t) * 2;
    *reinterpret_cast<uint32_t *>(data_ + offset_array_start + index * sizeof(uint32_t)) = offset;
  }

  char data_[BUSTUB_PAGE_SIZE]{};
};
}  // namespace bustub
