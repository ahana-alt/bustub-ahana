//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_leaf_page.cpp
//
// Identification: src/storage/page/b_plus_tree_leaf_page.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <sstream>

#include "common/exception.h"
#include "common/rid.h"
#include "storage/page/b_plus_tree_leaf_page.h"

namespace bustub {

/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/

/**
 * @brief Init method after creating a new leaf page
 *
 * After creating a new leaf page from buffer pool, must call initialize method to set default values,
 * including set page type, set current size to zero, set page id/parent id, set
 * next page id and set max size.
 *
 * @param max_size Max size of the leaf node
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::Init(int max_size) {
  SetPageType(IndexPageType::LEAF_PAGE);
  SetSize(0);
  SetMaxSize(max_size);
  // SetParentPageId(INVALID_PAGE_ID); //where is this function
  next_page_id_ = INVALID_PAGE_ID;
  num_tombstones_ = 0;
}

/**
 * @brief Helper function for fetching tombstones of a page.
 * @return The last `NumTombs` keys with pending deletes in this page in order of recency (oldest at front).
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetTombstones() const -> std::vector<KeyType> {
  std::vector<KeyType> result;
  result.reserve(num_tombstones_);

  for (size_t i = 0; i < num_tombstones_; i++) {
    result.push_back(key_array_[tombstones_[i]]);
  }

  return result;
}

/**
 * Helper methods to set/get next page id
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetNextPageId() const -> page_id_t { return next_page_id_; }

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetNextPageId(page_id_t next_page_id) { next_page_id_ = next_page_id; }

/*
 * Helper method to find and return the key associated with input "index" (a.k.a
 * array offset)
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::KeyAt(int index) const -> KeyType { return key_array_[index]; }

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::ValueAt(int index) const -> ValueType { return rid_array_[index]; }

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetKeyAt(int index, const KeyType &key) { key_array_[index] = key; }

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetValueAt(int index, const ValueType &value) { rid_array_[index] = value; }

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::HasDuplicates(const KeyType &key, const KeyComparator &comparator) const -> bool {
  for (int i = 0; i < GetSize(); i++) {
    if (comparator(KeyAt(i), key) == 0) {
      return true;
    }
  }
  return false;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::InsertAt(int index, const KeyType &key, const ValueType &value) {
  // Shift everything from index to the right
  for (int i = GetSize(); i > index; i--) {
    key_array_[i] = key_array_[i - 1];
    rid_array_[i] = rid_array_[i - 1];
  }

  // Insert at index
  key_array_[index] = key;
  rid_array_[index] = value;

  // Increase size
  ChangeSizeBy(1);
}

// implemented for delete case
FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::FindKey(const KeyType &key, const KeyComparator &comparator) const -> int {
  int index = -1;
  for (int i = 0; i < GetSize(); i++) {
    if (comparator(KeyAt(i), key) == 0) {
      index = i;
      return i;
    }
  }
  return index;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::RemoveAt(int index) {
  // Shift everything from index+1 to the left
  for (int i = index; i < GetSize() - 1; i++) {
    key_array_[i] = key_array_[i + 1];
    rid_array_[i] = rid_array_[i + 1];
  }

  // Decrease size
  ChangeSizeBy(-1);
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::KeyIndex(const KeyType &key, const KeyComparator &comparator) const -> int {
  // Binary search to find insertion point
  int left = 0;
  int right = GetSize();

  while (left < right) {
    int mid = left + (right - left) / 2;
    if (comparator(key_array_[mid], key) < 0) {
      left = mid + 1;
    } else {
      right = mid;
    }
  }

  return left;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::InsertAndSplit(const KeyType &key, const ValueType &value,
                                                BPlusTreeLeafPage *recipient, KeyType *middle_key,
                                                const KeyComparator &comparator) {
  // Collect all entries including new one
  int total_size = GetSize() + 1;
  std::vector<std::pair<KeyType, ValueType>> all_entries;
  all_entries.reserve(total_size);

  bool inserted = false;
  for (int i = 0; i < GetSize(); i++) {
    if (!inserted && comparator(key, KeyAt(i)) < 0) {
      all_entries.emplace_back(key, value);
      inserted = true;
    }
    all_entries.emplace_back(KeyAt(i), ValueAt(i));
  }
  if (!inserted) {
    all_entries.emplace_back(key, value);
  }

  // Split point
  int split_index = total_size / 2;

  // First half stays in this page
  SetSize(0);
  for (int i = 0; i < split_index; i++) {
    SetKeyAt(i, all_entries[i].first);
    SetValueAt(i, all_entries[i].second);
  }
  SetSize(split_index);

  // Second half goes to recipient
  for (int i = split_index; i < total_size; i++) {
    recipient->SetKeyAt(i - split_index, all_entries[i].first);
    recipient->SetValueAt(i - split_index, all_entries[i].second);
  }
  recipient->SetSize(total_size - split_index);

  // Redistribute tombstones
  std::vector<size_t> old_tombs;
  std::vector<size_t> new_tombs;
  for (int i = 0; i < GetTombstoneCount(); i++) {
    size_t tomb_idx = GetTombstoneAt(i);
    if (tomb_idx < static_cast<size_t>(split_index)) {
      old_tombs.push_back(tomb_idx);
    } else {
      new_tombs.push_back(tomb_idx - split_index);
    }
  }
  SetTombstones(old_tombs);
  recipient->SetTombstones(new_tombs);

  // Update linked list
  recipient->SetNextPageId(GetNextPageId());
  SetNextPageId(INVALID_PAGE_ID);  // Will be set by caller

  // Return middle key (first key of recipient)
  *middle_key = recipient->KeyAt(0);
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetTombstoneCount() const -> int { return num_tombstones_; }

// tombstone helper fucntions
FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetTombstones(const std::vector<size_t> &tombs) {
  num_tombstones_ = std::min(tombs.size(), static_cast<size_t>(LEAF_PAGE_TOMB_CNT));
  for (size_t i = 0; i < num_tombstones_; i++) {
    tombstones_[i] = tombs[i];
  }
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetTombstoneAt(size_t index) const -> size_t { return tombstones_[index]; }

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::HasTombstones() const -> bool { return num_tombstones_ > 0; }

// add to .h file
FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::AlreadyMarked(const KeyType &key, const KeyComparator &comparator_) const -> bool {
  for (int i = 0; i < GetTombstoneCount(); i++) {
    size_t tomb_idx = GetTombstoneAt(i);
    if (comparator_(KeyAt(tomb_idx), key) == 0) {
      // Already marked for deletion
      return true;
    }
  }
  return false;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::ProcessAllTombstones() {
  while (num_tombstones_ > 0) {
    ProcessTombstone();
  }
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::ProcessTombstone() {
  if (num_tombstones_ == 0) {
    return;
  }
  // Get the index of the entry to delete (oldest tombstone)
  size_t delete_index = tombstones_[0];

  // Actually delete the entry by shifting everything left
  for (int i = static_cast<int>(delete_index); i < GetSize() - 1; i++) {
    key_array_[i] = key_array_[i + 1];
    rid_array_[i] = rid_array_[i + 1];
  }
  SetSize(GetSize() - 1);

  // Update all remaining tombstones
  // Any tombstone pointing to an index > delete_index needs to be decremented
  for (size_t i = 1; i < num_tombstones_; i++) {
    if (tombstones_[i] > delete_index) {
      tombstones_[i]--;
    }
  }

  // Remove the processed tombstone from buffer (shift tombstones left)
  for (size_t i = 0; i < num_tombstones_ - 1; i++) {
    tombstones_[i] = tombstones_[i + 1];
  }
  num_tombstones_--;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::AddTombstone(int index) {
  BUSTUB_ASSERT(num_tombstones_ < LEAF_PAGE_TOMB_CNT, "Tombstone buffer full");
  tombstones_[num_tombstones_] = index;
  num_tombstones_++;
  std::cout << "[LEAF PAGE] added tombstone " << num_tombstones_;
}

template class BPlusTreeLeafPage<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>, 3>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>, 2>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>, 1>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>, -1>;

template class BPlusTreeLeafPage<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTreeLeafPage<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTreeLeafPage<GenericKey<64>, RID, GenericComparator<64>>;
}  // namespace bustub
