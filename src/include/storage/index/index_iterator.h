//===----------------------------------------------------------------------===//
//
// BusTub
//
// index_iterator.h
//
//===----------------------------------------------------------------------===//
#pragma once
#include <utility>
#include "buffer/buffer_pool_manager.h"  // ADD THIS LINE
#include "buffer/traced_buffer_pool_manager.h"
#include "common/config.h"
#include "storage/page/b_plus_tree_leaf_page.h"
#include "storage/page/page_guard.h"

namespace bustub {

#define INDEXITERATOR_TYPE IndexIterator<KeyType, ValueType, KeyComparator, NumTombs>

#define SHORT_INDEXITERATOR_TYPE IndexIterator<KeyType, ValueType, KeyComparator>

FULL_INDEX_TEMPLATE_ARGUMENTS_DEFN
class IndexIterator {
  using LeafPage = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator, NumTombs>;

 public:
  // Default constructor (for end iterator)
  IndexIterator();

  // Constructor with page_id directly
  template <typename BPMType>
  IndexIterator(BPMType *bpm, page_id_t page_id, int index)
      : bpm_(static_cast<void *>(bpm)), page_id_(page_id), index_(index) {
    // (void)comparator;
    std::cout << "[Iterator Constructor] bpm=" << bpm_ << " page_id=" << page_id_ << " index=" << index_ << std::endl;
  }

  ~IndexIterator() = default;

  auto IsEnd() -> bool;

  auto operator*() -> std::pair<KeyType, ValueType>;

  auto operator++() -> IndexIterator &;

  auto operator==(const IndexIterator &other) const -> bool;

  auto operator!=(const IndexIterator &other) const -> bool;

 private:
  void *bpm_{nullptr};
  page_id_t page_id_{INVALID_PAGE_ID};
  int index_{0};
  mutable bool initialized_{false};

  void AdvanceToValidEntry();
  void Initialize() const;
};

}  // namespace bustub
