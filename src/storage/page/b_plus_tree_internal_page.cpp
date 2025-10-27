//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_internal_page.cpp
//
// Identification: src/storage/page/b_plus_tree_internal_page.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <sstream>

#include "common/exception.h"
#include "storage/page/b_plus_tree_internal_page.h"

namespace bustub {
/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/

/**
 * @brief Init method after creating a new internal page.
 *
 * Writes the necessary header information to a newly created page,
 * including set page type, set current size, set page id, set parent id and set max page size,
 * must be called after the creation of a new page to make a valid BPlusTreeInternalPage.
 *
 * @param max_size Maximal size of the page
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Init(int max_size) {
  SetPageType(IndexPageType::INTERNAL_PAGE);
  SetSize(0);
  SetMaxSize(max_size);
  // SetParentPageId(INVALID_PAGE_ID); //where is this function
}

/**
 * @brief Helper method to get/set the key associated with input "index"(a.k.a
 * array offset).
 *
 * @param index The index of the key to get. Index must be non-zero.
 * @return Key at index
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::KeyAt(int index) const -> KeyType { return key_array_[index]; }

/**
 * @brief Set key at the specified index.
 *
 * @param index The index of the key to set. Index must be non-zero.
 * @param key The new value for key
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetKeyAt(int index, const KeyType &key) { key_array_[index] = key; }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetValueAt(int index, const ValueType &value) { page_id_array_[index] = value; }

// implemented for delete case
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::FindKey(const KeyType &key, const KeyComparator &comparator) const -> int {
  int index = -1;
  for (int i = 0; i < GetSize(); i++) {
    if (comparator(KeyAt(i), key) == 0) {
      index = i;
      return i;
    }
  }
  return index;
}

/**
 * @brief Helper method to get the value associated with input "index"(a.k.a array
 * offset)
 *
 * @param index The index of the value to get.
 * @return Value at index
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueAt(int index) const -> ValueType { return page_id_array_[index]; }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::InsertAt(int index, const KeyType &key, const ValueType &value) {
  // Shift everything from index to the right
  for (int i = GetSize(); i > index; i--) {
    key_array_[i] = key_array_[i - 1];
    page_id_array_[i] = page_id_array_[i - 1];
  }

  // Insert at index
  key_array_[index] = key;
  page_id_array_[index] = value;

  // Increase size
  ChangeSizeBy(1);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::RemoveAt(int index) {
  // Shift everything from index+1 to the left
  for (int i = index; i < GetSize() - 1; i++) {
    key_array_[i] = key_array_[i + 1];
    page_id_array_[i] = page_id_array_[i + 1];
  }

  // Decrease size
  ChangeSizeBy(-1);
}

/**
 * Insert a new child pointer and split the page, moving second half to recipient
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::InsertAndSplit(page_id_t old_child_id, const KeyType &key, page_id_t new_child_id,
                                                    BPlusTreeInternalPage *recipient, KeyType *middle_key,
                                                    const KeyComparator &comparator) {
  // Collect all keys and values
  std::vector<KeyType> all_keys;
  std::vector<page_id_t> all_values;

  bool inserted = false;
  for (int i = 0; i < GetSize(); i++) {
    if (!inserted && ValueAt(i) == old_child_id) {
      // all_values.push_back(old_child_id);
      // all_keys.push_back(key);
      // all_values.push_back(new_child_id);
      // inserted = true;
      if (i > 0) {
        all_keys.push_back(KeyAt(i));
      }
      all_values.push_back(old_child_id);  // Add the value
      all_keys.push_back(key);             // Add new key (goes between old and new values)
      all_values.push_back(new_child_id);  // Add new value
      inserted = true;
    } else {
      // all_values.push_back(ValueAt(i));
      if (i > 0) {
        all_keys.push_back(KeyAt(i));
      }
      all_values.push_back(ValueAt(i));
    }
  }

  int split_index = static_cast<int>(all_values.size()) / 2;

  // First half stays in this page
  SetSize(0);
  for (int i = 0; i < split_index; i++) {
    SetValueAt(i, all_values[i]);
    if (i > 0) {
      SetKeyAt(i, all_keys[i - 1]);
    }
  }
  SetSize(split_index);

  // Middle key to push up
  *middle_key = all_keys[split_index - 1];

  // Second half goes to recipient
  int total_size = static_cast<int>(all_values.size());
  for (int i = split_index; i < total_size; i++) {
    recipient->SetValueAt(i - split_index, all_values[i]);
    if (i > split_index) {
      recipient->SetKeyAt(i - split_index, all_keys[i - 1]);
    }
  }
  recipient->SetSize(total_size - split_index);
}

// valuetype for internalNode should be page id_t
template class BPlusTreeInternalPage<GenericKey<4>, page_id_t, GenericComparator<4>>;
template class BPlusTreeInternalPage<GenericKey<8>, page_id_t, GenericComparator<8>>;
template class BPlusTreeInternalPage<GenericKey<16>, page_id_t, GenericComparator<16>>;
template class BPlusTreeInternalPage<GenericKey<32>, page_id_t, GenericComparator<32>>;
template class BPlusTreeInternalPage<GenericKey<64>, page_id_t, GenericComparator<64>>;
}  // namespace bustub
