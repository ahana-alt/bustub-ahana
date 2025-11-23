//===----------------------------------------------------------------------===//
//
// BusTub
//
// index_iterator.cpp
//
// Identification: src/storage/index/index_iterator.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
/**
 * index_iterator.cpp
 */
#include "storage/index/index_iterator.h"
#include <cassert>
#include <iostream>
#include "buffer/buffer_pool_manager.h"

namespace bustub {

/**
 * Default constructor (for end iterator)
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::IndexIterator() : initialized_(true) {
  // std::cout << "[Iterator] Default constructor (end iterator)" << std::endl;
}

/**
 * Lazy initialization - skip tombstones on first access
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
void INDEXITERATOR_TYPE::Initialize() const {
  // std::cout << "[Iterator::Initialize] initialized_=" << initialized_ << " bpm_=" << bpm_ << " page_id_=" << page_id_
  //           << " index_=" << index_ << std::endl;

  if (!initialized_ && bpm_ != nullptr) {
    // std::cout << "[Iterator::Initialize] Calling AdvanceToValidEntry()" << std::endl;
    initialized_ = true;
    const_cast<IndexIterator *>(this)->AdvanceToValidEntry();
    // std::cout << "[Iterator::Initialize] After AdvanceToValidEntry: page_id_=" << page_id_ << " index_=" << index_
    //           << std::endl;
  }
}

/**
 * Helper method to advance to the next valid (non-tombstone) entry
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
void INDEXITERATOR_TYPE::AdvanceToValidEntry() {
  if (bpm_ == nullptr) {
    return;
  }

  // Cast to TracedBufferPoolManager
  auto *traced_bpm = static_cast<TracedBufferPoolManager *>(bpm_);

  while (page_id_ != INVALID_PAGE_ID) {
    auto guard = traced_bpm->ReadPage(page_id_);
    auto *leaf = guard.template As<LeafPage>();

    while (index_ < leaf->GetSize() && leaf->IsTombstone(index_)) {
      index_++;
    }

    if (index_ < leaf->GetSize()) {
      return;
    }

    page_id_t next_page_id = leaf->GetNextPageId();
    page_id_ = next_page_id;
    index_ = 0;
  }
  index_ = 0;
}

/**
 * @return whether this iterator is at the end
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::IsEnd() -> bool {
  // std::cout << "[Iterator::IsEnd] Before Initialize" << std::endl;
  Initialize();
  bool result = page_id_ == INVALID_PAGE_ID;
  // std::cout << "[Iterator::IsEnd] result=" << result << " page_id_=" << page_id_ << std::endl;
  return result;
}

/**
 * @return the key-value pair this iterator is currently pointing at
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator*() -> std::pair<KeyType, ValueType> {
  Initialize();

  // Cast to TracedBufferPoolManager
  auto *traced_bpm = static_cast<TracedBufferPoolManager *>(bpm_);
  auto guard = traced_bpm->ReadPage(page_id_);
  auto *leaf = guard.template As<LeafPage>();

  return std::make_pair(leaf->KeyAt(index_), leaf->ValueAt(index_));
}

/**
 * Move to the next key-value pair
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator++() -> INDEXITERATOR_TYPE & {
  // std::cout << "[Iterator::operator++] Before: page_id_=" << page_id_ << " index_=" << index_ << std::endl;
  Initialize();

  index_++;
  // std::cout << "[Iterator::operator++] After index++: index_=" << index_ << std::endl;
  AdvanceToValidEntry();
  // std::cout << "[Iterator::operator++] After AdvanceToValidEntry: page_id_=" << page_id_ << " index_=" << index_
  //           << std::endl;

  return *this;
}

/**
 * @return whether two iterators are equal
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator==(const INDEXITERATOR_TYPE &other) const -> bool {
  Initialize();  // Initialize this iterator first
  other.Initialize();
  bool result = page_id_ == other.page_id_ && index_ == other.index_;
  // std::cout << "[Iterator::operator==] this: page_id_=" << page_id_ << " index_=" << index_
  //           << " other: page_id_=" << other.page_id_ << " index_=" << other.index_ << " result=" << result <<
  //           std::endl;
  return result;
}

/**
 * @return whether two iterators are not equal
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator!=(const INDEXITERATOR_TYPE &other) const -> bool {
  bool result = !(*this == other);
  // std::cout << "[Iterator::operator!=] result=" << result << std::endl;
  return result;
}

template class IndexIterator<GenericKey<4>, RID, GenericComparator<4>>;
template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>>;
template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>, 3>;
template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>, 2>;
template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>, 1>;
template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>, -1>;
template class IndexIterator<GenericKey<16>, RID, GenericComparator<16>>;
template class IndexIterator<GenericKey<32>, RID, GenericComparator<32>>;
template class IndexIterator<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
