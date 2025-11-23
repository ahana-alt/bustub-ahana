//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree.cpp
//
// Identification: src/storage/index/b_plus_tree.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/index/b_plus_tree.h"
#include "buffer/traced_buffer_pool_manager.h"
#include "storage/index/b_plus_tree_debug.h"

namespace bustub {

FULL_INDEX_TEMPLATE_ARGUMENTS
BPLUSTREE_TYPE::BPlusTree(std::string name, page_id_t header_page_id, BufferPoolManager *buffer_pool_manager,
                          const KeyComparator &comparator, int leaf_max_size, int internal_max_size)
    : bpm_(std::make_shared<TracedBufferPoolManager>(buffer_pool_manager)),
      index_name_(std::move(name)),
      comparator_(std::move(comparator)),
      leaf_max_size_(leaf_max_size),
      internal_max_size_(internal_max_size),
      header_page_id_(header_page_id) {
  WritePageGuard guard = bpm_->WritePage(header_page_id_);
  auto root_page = guard.AsMut<BPlusTreeHeaderPage>();
  root_page->root_page_id_ = INVALID_PAGE_ID;
}

/**
 * @brief Helper function to decide whether current b+tree is empty
 * @return Returns true if this B+ tree has no keys and values.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::IsEmpty() const -> bool {
  ReadPageGuard guard = bpm_->ReadPage(header_page_id_);  // header page id from when the B+tree was created
  auto header = guard.As<BPlusTreeHeaderPage>();          // tells whether to give a const pointer or just a pointer
  return header->root_page_id_ == INVALID_PAGE_ID;
}

/*****************************************************************************
 * SEARCH
 *****************************************************************************/
/**
 * @brief Return the only value that associated with input key
 *
 * This method is used for point query
 *
 * @param key input key
 * @param[out] result vector that stores the only value that associated with input key, if the value exists
 * @return : true means key exists
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetValue(const KeyType &key, std::vector<ValueType> *result) -> bool {
  // Declaration of context instance. Using the Context is not necessary but advised.
  Context ctx;

  // Get header page from the buffer pool or the disk
  ReadPageGuard header_guard = bpm_->ReadPage(header_page_id_);
  // Pointer to header page
  auto header = header_guard.As<BPlusTreeHeaderPage>();
  page_id_t current_page_id = header->root_page_id_;  // Integer assignment
  header_guard.Drop();

  // std::cout << "GETVALUE: Searching for key=" << key.ToString() << " root=" << current_page_id << std::endl;

  // Check if tree is empty
  if (current_page_id == INVALID_PAGE_ID) {
    // std::cout << "GETVALUE: Tree is empty" << std::endl;
    return false;
  }

  ReadPageGuard current_guard = bpm_->ReadPage(current_page_id);

  while (!current_guard.As<BPlusTreePage>()->IsLeafPage()) {
    auto internal = current_guard.As<InternalPage>();

    // std::cout << "GETVALUE: At internal page=" << current_guard.GetPageId() << " size=" << internal->GetSize()
    //           << std::endl;

    page_id_t child_page = internal->ValueAt(0);

    for (int i = 1; i < internal->GetSize(); i++) {
      if (comparator_(key, internal->KeyAt(i)) < 0) {
        break;
      }
      child_page = internal->ValueAt(i);
    }

    // std::cout << "GETVALUE: Going to child page=" << child_page << std::endl;

    // Taking guard on each page and assigning it as B+ tree page, looping through
    // if it is an internal page
    ReadPageGuard child_guard = bpm_->ReadPage(child_page);
    current_guard.Drop();
    current_guard = std::move(child_guard);
    // Will exit if child page is a leaf
  }

  // Binary search on child_page
  auto leaf = current_guard.As<LeafPage>();
  // for (int i = 0; i < leaf->GetSize(); i++) {
  //   if (leaf->IsTombstone(i)) {
  //     std::cout << " (TOMBSTONE)";
  //   }
  //   std::cout << std::endl;
  // }
  int index = leaf->KeyIndex(key, comparator_);

  if (index < leaf->GetSize() && comparator_(leaf->KeyAt(index), key) == 0) {
    if (leaf->IsTombstone(index)) {
      // std::cout << "GETVALUE: Key found at index=" << index << " but it's TOMBSTONED" << std::endl;
      return false;  // Tombstoned = not found
    }
    result->push_back(leaf->ValueAt(index));
    return true;
  }
  return false;
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
/**
 * @brief Insert constant key & value pair into b+ tree
 *
 * if current tree is empty, start new tree, update root page id and insert
 * entry; otherwise, insert into leaf page.
 *
 * @param key the key to insert
 * @param value the value associated with key
 * @return: since we only support unique key, if user try to insert duplicate
 * keys return false; otherwise, return true.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value) -> bool {
  // std::cout << "\n=== INSERTING KEY=" << key.ToString() << " ===" << std::endl;
  Context ctx;

  // Use READ lock for header in optimistic path
  ReadPageGuard header_guard = bpm_->ReadPage(header_page_id_);
  auto header = header_guard.As<BPlusTreeHeaderPage>();
  page_id_t root_page_id = header->root_page_id_;

  // Empty tree - need WRITE lock on header
  if (root_page_id == INVALID_PAGE_ID) {
    header_guard.Drop();  // Drop read lock

    // Now get write lock
    ctx.header_page_ = bpm_->WritePage(header_page_id_);
    auto header_write = ctx.header_page_->AsMut<BPlusTreeHeaderPage>();

    if (header_write->root_page_id_ != INVALID_PAGE_ID) {
      // Tree is no longer empty, drop header and restart with pessimistic
      ctx.header_page_.reset();
      return InsertPessimistic(key, value);
    }

    page_id_t new_page_id = bpm_->NewPage();
    WritePageGuard new_guard = bpm_->WritePage(new_page_id);
    auto new_leaf = new_guard.AsMut<LeafPage>();
    new_leaf->Init(leaf_max_size_);
    new_leaf->InsertAt(0, key, value);
    header_write->root_page_id_ = new_page_id;
    return true;
  }

  // Release header read lock - don't need it anymore for optimistic
  header_guard.Drop();

  // OPTIMISTIC: Traverse with READ locks
  ReadPageGuard current_guard = bpm_->ReadPage(root_page_id);

  while (!current_guard.As<BPlusTreePage>()->IsLeafPage()) {
    auto internal = current_guard.As<InternalPage>();
    page_id_t child_page_id = internal->ValueAt(0);

    for (int i = 1; i < internal->GetSize(); i++) {
      if (comparator_(key, internal->KeyAt(i)) < 0) {
        break;
      }
      child_page_id = internal->ValueAt(i);
    }

    ReadPageGuard child_guard = bpm_->ReadPage(child_page_id);
    current_guard.Drop();
    current_guard = std::move(child_guard);
  }

  // At leaf - check with READ lock
  auto leaf_read = current_guard.As<LeafPage>();
  page_id_t leaf_page_id = current_guard.GetPageId();

  int existing_index = leaf_read->FindKey(key, comparator_);
  if (existing_index != -1 && NumTombs > 0 && leaf_read->IsTombstone(existing_index)) {
    // Key is tombstoned - need WRITE lock to modify
    // std::cout << "OPTIMISTIC: Key is tombstoned, clearing tombstone and updating value" << std::endl;
    current_guard.Drop();  // Drop READ lock

    // Get WRITE lock
    WritePageGuard leaf_write = bpm_->WritePage(leaf_page_id);
    auto leaf = leaf_write.AsMut<LeafPage>();

    // Recheck that it's still tombstoned (page could have changed)
    existing_index = leaf->FindKey(key, comparator_);
    if (existing_index != -1 && leaf->IsTombstone(existing_index)) {
      leaf->ClearTombstoneForKey(key, comparator_);
      leaf->SetValueAt(existing_index, value);
      return true;
    }

    // Tombstone was removed by someone else - key now exists as real duplicate
    return false;
  }

  // Check if safe
  bool needs_tombstone_processing = (leaf_read->GetSize() >= leaf_read->GetMaxSize() && leaf_read->HasTombstones());
  bool is_safe = (leaf_read->GetSize() < leaf_read->GetMaxSize()) || needs_tombstone_processing;

  // OPTIMISTIC PATH
  if (is_safe) {
    // Log before dropping
    // std::cout << "OPTIMISTIC: leaf_page_id=" << leaf_page_id << " size=" << leaf_read->GetSize()
    //           << " max=" << leaf_read->GetMaxSize() << std::endl;

    current_guard.Drop();
    WritePageGuard leaf_write = bpm_->WritePage(leaf_page_id);
    auto leaf = leaf_write.AsMut<LeafPage>();

    // Log after reacquiring
    // std::cout << "OPTIMISTIC REACQUIRED: leaf_page_id=" << leaf_page_id << " size=" << leaf->GetSize()
    //           << " max=" << leaf->GetMaxSize() << std::endl;

    // Recheck duplicate
    if (leaf->HasDuplicates(key, comparator_)) {
      // std::cout << "OPTIMISTIC: Found duplicate for key" << std::endl;
      return false;
    }

    // Process tombstones if needed
    if (leaf->GetSize() >= leaf->GetMaxSize() && leaf->HasTombstones()) {
      // std::cout << "OPTIMISTIC: Processing tombstones" << std::endl;
      leaf->ProcessAllTombstones();
    }

    // Try insert
    if (leaf->GetSize() < leaf->GetMaxSize()) {
      int index = leaf->KeyIndex(key, comparator_);
      // std::cout << "OPTIMISTIC INSERT: key=" << key.ToString() << " index=" << index << " size=" << leaf->GetSize()
      //           << std::endl;
      leaf->InsertAt(index, key, value);
      return true;
    }

    // std::cout << "OPTIMISTIC FAILED: leaf still full, going pessimistic" << std::endl;
    leaf_write.Drop();
  } else {
    current_guard.Drop();
  }

  // PESSIMISTIC
  return InsertPessimistic(key, value);
}

// if we have processed tombstones and decided that even after that leaf is not getting reduced
// just get make this function to have a write_set_
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::InsertPessimistic(const KeyType &key, const ValueType &value) -> bool {
  // std::cout << "PESSIMISTIC INSERT key=" << key.ToString() << std::endl;
  Context ctx;

  ctx.header_page_ = bpm_->WritePage(header_page_id_);
  auto header = ctx.header_page_->AsMut<BPlusTreeHeaderPage>();
  ctx.root_page_id_ = header->root_page_id_;

  WritePageGuard current_guard = bpm_->WritePage(ctx.root_page_id_);

  while (!current_guard.As<BPlusTreePage>()->IsLeafPage()) {
    auto internal = current_guard.As<InternalPage>();

    page_id_t child_page_id = internal->ValueAt(0);
    for (int i = 1; i < internal->GetSize(); i++) {
      if (comparator_(key, internal->KeyAt(i)) < 0) {
        break;
      }
      child_page_id = internal->ValueAt(i);
    }

    // WritePageGuard child_guard = bpm_->WritePage(child_page_id);
    // bool is_safe = (internal->GetSize() < internal->GetMaxSize());
    // if (is_safe) {
    //   // ctx.write_set_.clear();
    //   // ctx.header_page_.reset();
    // } else{
    //   ctx.write_set_.push_back(std::move(current_guard));
    // }
    ctx.write_set_.push_back(std::move(current_guard));

    current_guard = bpm_->WritePage(child_page_id);
  }

  // Get leaf
  auto leaf = current_guard.AsMut<LeafPage>();

  // MUST recheck duplicate! Tree might have changed!
  if (leaf->HasDuplicates(key, comparator_)) {
    return false;
  }

  // Process tombstones if needed
  if (leaf->GetSize() >= leaf->GetMaxSize() && leaf->HasTombstones()) {
    leaf->ProcessAllTombstones();
  }

  // Try direct insert after tombstone processing
  if (leaf->GetSize() < leaf->GetMaxSize()) {
    int index = leaf->KeyIndex(key, comparator_);
    leaf->InsertAt(index, key, value);
    // ctx.write_set_.clear();
    // ctx.header_page_.reset();
    return true;  // Tombstone processing made room!
  }

  // Still full - must split
  return SplitLeaf(leaf, std::move(current_guard), key, value, ctx);
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::SplitLeaf(LeafPage *old_leaf, WritePageGuard old_leaf_guard, const KeyType &key,
                               const ValueType &value, Context &ctx) -> bool {
  page_id_t old_leaf_id = old_leaf_guard.GetPageId();
  // std::cout << "SPLITLEAF: old_leaf_id=" << old_leaf_id << " old_size=" << old_leaf->GetSize()
  //           << " inserting key=" << key.ToString() << std::endl;

  // Create new leaf
  page_id_t new_leaf_id = bpm_->NewPage();
  WritePageGuard new_leaf_guard = bpm_->WritePage(new_leaf_id);
  auto new_leaf = new_leaf_guard.AsMut<LeafPage>();
  new_leaf->Init(leaf_max_size_);

  // Insert and split - all logic in leaf page
  KeyType push_up_key;
  old_leaf->InsertAndSplit(key, value, new_leaf, &push_up_key, comparator_);

  // std::cout << "AFTER SPLIT: old_size=" << old_leaf->GetSize() << " new_size=" << new_leaf->GetSize()
  //           << " push_up_key=" << push_up_key.ToString() << std::endl;

  // Fix linked list (set old leaf's next pointer)
  old_leaf->SetNextPageId(new_leaf_id);

  // Drop guards
  old_leaf_guard.Drop();
  new_leaf_guard.Drop();

  // Insert into parent
  return InsertIntoParent(old_leaf_id, push_up_key, new_leaf_id, ctx);
}

/**
 * Insert split key into parent (or create new root)
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::InsertIntoParent(page_id_t left_page_id, const KeyType &key, page_id_t right_page_id, Context &ctx)
    -> bool {
  // Check if left_page was root (write_set_ is empty)
  if (ctx.write_set_.empty()) {
    // Create new root
    BUSTUB_ASSERT(ctx.header_page_.has_value(), "Header page should be locked");

    page_id_t new_root_id = bpm_->NewPage();
    WritePageGuard new_root_guard = bpm_->WritePage(new_root_id);
    auto new_root = new_root_guard.AsMut<InternalPage>();

    new_root->Init(internal_max_size_);
    new_root->SetSize(2);
    new_root->SetValueAt(0, left_page_id);
    new_root->SetKeyAt(1, key);
    new_root->SetValueAt(1, right_page_id);

    // std::cout << "NEW ROOT created page=" << new_root_id << std::endl;
    // for (int i = 0; i < new_root->GetSize(); i++) {
    //   if (i == 0) {
    //     std::cout << "  Child[0]=" << new_root->ValueAt(0) << std::endl;
    //   } else {
    //     std::cout << "  Key[" << i << "]=" << new_root->KeyAt(i).ToString() << " Child[" << i
    //               << "]=" << new_root->ValueAt(i) << std::endl;
    //   }
    // }

    // Update header
    auto header = ctx.header_page_->AsMut<BPlusTreeHeaderPage>();
    header->root_page_id_ = new_root_id;

    ctx.header_page_.reset();
    return true;
  }

  auto parent_guard = std::move(ctx.write_set_.back());
  ctx.write_set_.pop_back();

  auto parent = parent_guard.AsMut<InternalPage>();
  page_id_t parent_id = parent_guard.GetPageId();  // You may need to add this

  // Check if parent has space
  if (parent->GetSize() < parent->GetMaxSize()) {
    // Find position of left_page_id
    int pos = 0;
    for (int i = 0; i < parent->GetSize(); i++) {
      if (parent->ValueAt(i) == left_page_id) {
        pos = i;
        break;
      }
    }

    parent->InsertAt(pos + 1, key, right_page_id);
    // std::cout << "INSERTED INTO PARENT page=" << parent_id << std::endl;
    // for (int i = 0; i < parent->GetSize(); i++) {
    //   if (i == 0) {
    //     std::cout << "  Child[0]=" << parent->ValueAt(0) << std::endl;
    //   } else {
    //     std::cout << "  Key[" << i << "]=" << parent->KeyAt(i).ToString() << " Child[" << i
    //               << "]=" << parent->ValueAt(i) << std::endl;
    //   }
    // }
    ctx.header_page_.reset();
    ctx.write_set_.clear();
    return true;
  }

  // Parent is full - split parent
  return SplitInternal(parent, std::move(parent_guard), parent_id, left_page_id, key, right_page_id, ctx);
}

/**
 * Split internal node
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::SplitInternal(InternalPage *old_internal, WritePageGuard old_guard, page_id_t old_internal_id,
                                   page_id_t left_child_id, const KeyType &key, page_id_t right_child_id, Context &ctx)
    -> bool {
  // Create new internal node
  page_id_t new_internal_id = bpm_->NewPage();
  WritePageGuard new_guard = bpm_->WritePage(new_internal_id);
  auto new_internal = new_guard.AsMut<InternalPage>();
  new_internal->Init(internal_max_size_);

  // Insert and split - all logic in internal page
  KeyType middle_key;
  old_internal->InsertAndSplit(left_child_id, key, right_child_id, new_internal, &middle_key, comparator_);

  // Drop guards
  old_guard.Drop();
  new_guard.Drop();

  // Recursively insert into parent
  return InsertIntoParent(old_internal_id, middle_key, new_internal_id, ctx);
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
/**
 * @brief Delete key & value pair associated with input key
 * If current tree is empty, return immediately.
 * If not, User needs to first find the right leaf page as deletion target, then
 * delete entry from leaf page. Remember to deal with redistribute or merge if
 * necessary.
 *
 * @param key input key
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Remove(const KeyType &key) {
  // std::cout << "[Remove] Starting optimistic removal" << std::endl;

  // Try optimistic approach first
  if (TryRemoveOptimistic(key)) {
    // std::cout << "[Remove] Optimistic removal succeeded" << std::endl;
    return;
  }

  // Optimistic failed - fall back to pessimistic
  // std::cout << "[Remove] Falling back to pessimistic removal" << std::endl;
  RemovePessimistic(key);
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::TryRemoveOptimistic(const KeyType &key) -> bool {
  // std::cout << "[RemoveOpt] Starting optimistic attempt" << std::endl;

  auto header_guard = bpm_->ReadPage(header_page_id_);
  auto *header = header_guard.As<BPlusTreeHeaderPage>();
  page_id_t root_page_id = header->root_page_id_;
  header_guard.Drop();

  if (root_page_id == INVALID_PAGE_ID) {
    return true;  // Empty tree - nothing to remove
  }

  // Navigate to leaf with READ guards (optimistic!)
  ReadPageGuard current_guard = bpm_->ReadPage(root_page_id);
  while (!current_guard.As<BPlusTreePage>()->IsLeafPage()) {
    auto *internal = current_guard.As<InternalPage>();
    page_id_t child_page_id = internal->ValueAt(0);
    for (int i = 1; i < internal->GetSize(); i++) {
      if (comparator_(key, internal->KeyAt(i)) < 0) {
        break;
      }
      child_page_id = internal->ValueAt(i);
    }
    current_guard = bpm_->ReadPage(child_page_id);
  }

  // std::cout << "[RemoveOpt] Reached leaf page " << current_guard.GetPageId() << std::endl;

  // Save page ID before dropping read guard
  page_id_t leaf_page_id = current_guard.GetPageId();
  current_guard.Drop();  // Drop read guard BEFORE acquiring write guard

  // Now get write guard
  auto leaf_guard = bpm_->WritePage(leaf_page_id);
  auto *leaf = leaf_guard.template AsMut<LeafPage>();

  // Find key
  int key_index = leaf->FindKey(key, comparator_);
  // std::cout << "[RemoveOpt] FindKey(" << key << ") returned index=" << key_index << std::endl;

  if (key_index == -1) {
    // std::cout << "[RemoveOpt] Key not found" << std::endl;
    return true;  // Key doesn't exist - success
  }

  // Check if already tombstoned
  if (NumTombs > 0 && leaf->AlreadyMarked(key, comparator_)) {
    return true;
  }

  // std::cout << "[RemoveOpt] Found key at index=" << key_index << std::endl;

  int current_size = leaf->GetSize();
  int current_tombs = leaf->GetTombstoneCount();
  int predicted_effective_size;

  if constexpr (NumTombs == 0) {
    // Will physically remove 1 entry
    predicted_effective_size = current_size - 1;
  } else {
    predicted_effective_size = current_size - current_tombs - 1;
  }

  bool is_root = (leaf_page_id == root_page_id);
  // std::cout << "[RemoveOpt] predicted_effective_size=" << predicted_effective_size << ", is_root=" << is_root
  //           << ", min_size=" << leaf->GetMinSize() << std::endl;

  // CRITICAL: Check if root will become empty AFTER removal
  if (is_root && predicted_effective_size == 0) {
    // std::cout << "[RemoveOpt] Root will become empty, handling deletion" << std::endl;

    // Perform the removal
    if constexpr (NumTombs == 0) {
      leaf->RemoveAt(key_index);
    } else {
      if (leaf->GetTombstoneCount() >= NumTombs) {
        leaf->ProcessTombstone();
        key_index = leaf->FindKey(key, comparator_);
        if (key_index == -1) {
          return true;
        }
      }
      leaf->AddTombstone(key_index);
    }

    // Drop leaf guard and update header
    leaf_guard.Drop();
    auto header_guard_write = bpm_->WritePage(header_page_id_);
    auto *header_write = header_guard_write.template AsMut<BPlusTreeHeaderPage>();
    header_write->root_page_id_ = INVALID_PAGE_ID;
    bpm_->DeletePage(leaf_page_id);
    // std::cout << "[RemoveOpt] Root deleted, tree is now empty" << std::endl;
    return true;
  }

  // Check if would cause underflow (non-root only)
  if (!is_root && predicted_effective_size < leaf->GetMinSize()) {
    // std::cout << "[RemoveOpt] Would cause underflow, must use pessimistic" << std::endl;
    return false;  // Don't modify - fall back to pessimistic
  }

  // Safe to remove
  if constexpr (NumTombs == 0) {
    leaf->RemoveAt(key_index);
  } else {
    if (leaf->GetTombstoneCount() >= NumTombs) {
      // std::cout << "[RemoveOpt] Processing tombstone" << std::endl;
      leaf->ProcessTombstone();
      key_index = leaf->FindKey(key, comparator_);
      if (key_index == -1) {
        return true;  // Key was processed away
      }
    }
    leaf->AddTombstone(key_index);
  }

  // std::cout << "[RemoveOpt] After removal - size=" << leaf->GetSize() << ", tombstones=" << leaf->GetTombstoneCount()
  //           << std::endl;
  // std::cout << "[RemoveOpt] Success - no underflow!" << std::endl;
  return true;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::RemovePessimistic(const KeyType &key) {
  // std::cout << "[Remove] Starting removal, NumTombs=" << NumTombs << std::endl;
  Context ctx;
  ctx.header_page_ = bpm_->WritePage(header_page_id_);
  auto *header = ctx.header_page_->template AsMut<BPlusTreeHeaderPage>();
  ctx.root_page_id_ = header->root_page_id_;

  if (header->root_page_id_ == INVALID_PAGE_ID) {
    ctx.header_page_ = std::nullopt;
    return;
  }

  // Navigate to leaf
  page_id_t root_id = header->root_page_id_;
  WritePageGuard leaf_guard = bpm_->WritePage(root_id);
  // ctx.write_set_.push_back(std::move(ctx.header_page_.value()));

  while (!leaf_guard.As<BPlusTreePage>()->IsLeafPage()) {
    auto internal = leaf_guard.As<InternalPage>();

    // std::cout << "[Remove-Nav] At internal page " << leaf_guard.GetPageId() << " size=" << internal->GetSize()
    //           << std::endl;
    // std::cout << "[Remove-Nav] Looking for key=" << key << std::endl;

    page_id_t child_page_id = internal->ValueAt(0);
    // std::cout << "[Remove-Nav] Starting with ValueAt(0)=" << child_page_id << std::endl;

    for (int i = 1; i < internal->GetSize(); i++) {
      // std::cout << "[Remove-Nav] i=" << i << " KeyAt(" << i << ")=" << internal->KeyAt(i) << std::endl;
      // std::cout << "[Remove-Nav] Comparing: key(" << key << ") <= KeyAt(" << i << ")->(" << internal->KeyAt(i) << ")"
      //           << std::endl;

      if (comparator_(key, internal->KeyAt(i)) < 0) {
        // std::cout << "[Remove-Nav] TRUE - breaking, staying with child_page_id=" << child_page_id << std::endl;
        break;
      }

      child_page_id = internal->ValueAt(i);
      // std::cout << "[Remove-Nav] FALSE - moving to ValueAt(" << i << ")=" << child_page_id << std::endl;
    }

    // std::cout << "[Remove-Nav] Final child_page_id=" << child_page_id << std::endl;

    ctx.write_set_.push_back(std::move(leaf_guard));
    leaf_guard = bpm_->WritePage(child_page_id);
  }

  // std::cout << "[Remove-Nav] Reached leaf page " << leaf_guard.GetPageId() << std::endl;

  auto *leaf = leaf_guard.template AsMut<LeafPage>();

  // Find key
  int key_index = leaf->FindKey(key, comparator_);
  // std::cout << "[Remove] FindKey(" << key << ") returned index=" << key_index << std::endl;
  // std::cout << "[Remove] FindKey returned key_index=" << key_index << std::endl;
  // std::cout << "[Remove] Leaf page_id=" << leaf_guard.GetPageId() << ", size=" << leaf->GetSize() << std::endl;

  // std::cout << "[Remove] Leaf keys: ";
  // for (int i = 0; i < leaf->GetSize(); i++) {
  //   std::cout << leaf->KeyAt(i) << " ";
  // }
  // std::cout << std::endl;
  if (key_index == -1) {
    // std::cout << "[Remove] Key not found in leaf!" << std::endl;
    ctx.header_page_ = std::nullopt;
    return;
  }
  // Check if already tombstoned (only matters if tombstones exist)
  if (NumTombs > 0 && leaf->AlreadyMarked(key, comparator_)) {
    ctx.header_page_ = std::nullopt;
    return;
  }

  // std::cout << "[Remove] Found key at index=" << key_index << ", max_tombs=" << NumTombs << std::endl;

  // THIS IS THE CRITICAL PART
  if constexpr (NumTombs == 0) {
    // No tombstone buffer - immediately physically delete
    // std::cout << "[Remove] No tombstone buffer, physically removing entry" << std::endl;
    leaf->RemoveAt(key_index);
  } else {
    // Process oldest tombstone if buffer is full
    if (leaf->GetTombstoneCount() >= NumTombs) {
      // std::cout << "[Remove] Tombstone buffer full, processing oldest" << std::endl;
      leaf->ProcessTombstone();

      key_index = leaf->FindKey(key, comparator_);
      if (key_index == -1) {
        // std::cout << "[Remove] ERROR: Key disappeared after processing tombstone!" << std::endl;
        return;
      }
      // std::cout << "[Remove] After processing, key now at index=" << key_index << std::endl;
    }
    // Tombstone buffer available - mark for deletion
    // std::cout << "[Remove] Adding tombstone for index=" << key_index << std::endl;
    leaf->AddTombstone(key_index);
  }

  // std::cout << "[Remove] After removal - size=" << leaf->GetSize() << ", tombstones=" << leaf->GetTombstoneCount()
  //           << std::endl;

  // Calculate effective size
  // int effective_size = leaf->GetSize() - leaf->GetTombstoneCount();
  bool is_root = (leaf_guard.GetPageId() == ctx.root_page_id_);

  // std::cout << "[Remove] effective_size=" << effective_size << ", is_root=" << is_root << std::endl;

  // Check if root is now effectively empty
  if (is_root && leaf->GetSize() == leaf->GetTombstoneCount()) {
    // std::cout << "[Remove] Root is empty, setting to INVALID_PAGE_ID" << std::endl;

    // Get header from write_set (it was moved there earlier)
    auto *header = ctx.header_page_->template AsMut<BPlusTreeHeaderPage>();
    header->root_page_id_ = INVALID_PAGE_ID;

    bpm_->DeletePage(leaf_guard.GetPageId());
    return;
  }

  // if (effective_size == 0 && leaf->GetSize() > 0) {
  //   // Entire leaf is tombstones - process them all!
  //   std::cout << "[Remove] Entire leaf is tombstones, processing all" << std::endl;
  //   leaf->ProcessAllTombstones();
  // }

  // // Handle underflow - use effective size
  if (!is_root && leaf->GetSize() < leaf->GetMinSize()) {
    // std::cout << "[Remove] Underflow detected, coalescing/redistributing" << std::endl;
    CoalesceOrRedistribute(leaf_guard, ctx);
  }

  ctx.header_page_ = std::nullopt;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::CoalesceOrRedistribute(WritePageGuard &node_guard, Context &ctx) {
  auto *node = node_guard.template AsMut<LeafPage>();
  // std::cout << "[CoalesceOrRedistribute] node_page=" << node_guard.GetPageId() << " size=" << node->GetSize()
  //           << " tombstones=" << node->GetTombstoneCount() << std::endl;

  // Get parent from write set
  if (ctx.write_set_.empty()) {
    // std::cout << "[CoalesceOrRedistribute] No parent (root node), returning" << std::endl;
    return;  // Root node, nothing to do
  }

  auto parent_guard = std::move(ctx.write_set_.back());
  ctx.write_set_.pop_back();
  auto *parent = parent_guard.template AsMut<InternalPage>();
  // std::cout << "[CoalesceOrRedistribute] parent_page=" << parent_guard.GetPageId()
  //           << " parent_size=" << parent->GetSize() << std::endl;

  // Find node's index in parent
  int node_index = -1;
  for (int i = 0; i < parent->GetSize(); i++) {
    if (parent->ValueAt(i) == node_guard.GetPageId()) {
      node_index = i;
      break;
    }
  }
  BUSTUB_ASSERT(node_index != -1, "Node not found in parent");
  // std::cout << "[CoalesceOrRedistribute] node_index_in_parent=" << node_index << std::endl;

  // Determine sibling (prefer left sibling)
  WritePageGuard sibling_guard;
  int sibling_index;
  bool is_predecessor;

  if (node_index > 0) {
    // Use left sibling
    sibling_index = node_index - 1;
    is_predecessor = true;
    // std::cout << "[CoalesceOrRedistribute] Using LEFT sibling, sibling_index=" << sibling_index << std::endl;
  } else {
    // Use right sibling
    sibling_index = node_index + 1;
    is_predecessor = false;
    // std::cout << "[CoalesceOrRedistribute] Using RIGHT sibling, sibling_index=" << sibling_index << std::endl;
  }

  sibling_guard = bpm_->WritePage(parent->ValueAt(sibling_index));
  auto *sibling = sibling_guard.template AsMut<LeafPage>();
  // std::cout << "[CoalesceOrRedistribute] sibling_page=" << sibling_guard.GetPageId()
  //           << " sibling_size=" << sibling->GetSize() << " sibling_tombstones=" << sibling->GetTombstoneCount()
  //           << std::endl;

  // Decide: redistribute or coalesce?
  // Use ACTUAL sizes (tombstones still occupy physical space)
  int total_size = node->GetSize() + sibling->GetSize();
  int min_size = node->GetMinSize();

  // std::cout << "[CoalesceOrRedistribute] total_size=" << total_size << " min_size=" << min_size
  //           << " threshold(2*min_size)=" << (2 * min_size) << std::endl;

  if (total_size >= 2 * min_size) {
    // Enough entries to keep both nodes alive
    // std::cout << "[CoalesceOrRedistribute] REDISTRIBUTING (enough entries)" << std::endl;
    Redistribute(node_guard, sibling_guard, parent_guard, node_index, sibling_index, is_predecessor);
    ctx.write_set_.push_back(std::move(parent_guard));
  } else {
    // Not enough - must merge
    // std::cout << "[CoalesceOrRedistribute] COALESCING (not enough entries)" << std::endl;
    Coalesce(node_guard, sibling_guard, parent_guard, node_index, sibling_index, is_predecessor, ctx);
  }
  // std::cout << "[CoalesceOrRedistribute] COMPLETE" << std::endl;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Redistribute(WritePageGuard &node_guard, WritePageGuard &sibling_guard,
                                  WritePageGuard &parent_guard, int node_index, int sibling_index,
                                  bool is_predecessor) {
  auto *node = node_guard.template AsMut<LeafPage>();
  auto *sibling = sibling_guard.template AsMut<LeafPage>();
  auto *parent = parent_guard.template AsMut<InternalPage>();

  // std::cout << "[Redistribute] START: node_size=" << node->GetSize() << " sibling_size=" << sibling->GetSize()
  //           << " is_predecessor=" << is_predecessor << std::endl;

  // std::cout << "[Redistribute] BEFORE - Node page " << node_guard.GetPageId() << " keys: ";
  for (int i = 0; i < node->GetSize(); i++) {
    // std::cout << node->KeyAt(i) << " ";
  }
  // std::cout << " | tombstones: ";
  for (int i = 0; i < node->GetTombstoneCount(); i++) {
    // std::cout << "[" << node->GetTombstoneAt(i) << "]=>" << node->KeyAt(node->GetTombstoneAt(i)) << " ";
  }
  // std::cout << std::endl;

  // std::cout << "[Redistribute] BEFORE - Sibling page " << sibling_guard.GetPageId() << " keys: ";
  // for (int i = 0; i < sibling->GetSize(); i++) {
  //   std::cout << sibling->KeyAt(i) << " ";
  // }
  // std::cout << " | tombstones: ";
  // for (int i = 0; i < sibling->GetTombstoneCount(); i++) {
  //   std::cout << "[" << sibling->GetTombstoneAt(i) << "]=>" << sibling->KeyAt(sibling->GetTombstoneAt(i)) << " ";
  // }
  // std::cout << std::endl;

  int total_size = node->GetSize() + sibling->GetSize();
  int target_size = total_size / 2;

  // std::cout << "[Redistribute] target_size=" << target_size << std::endl;

  if (is_predecessor) {
    // ===== Move entries from LEFT sibling to RIGHT node =====
    // std::cout << "[Redistribute] Moving from LEFT sibling to RIGHT node" << std::endl;

    int to_move = sibling->GetSize() - target_size;
    int move_start_idx = sibling->GetSize() - to_move;

    // std::cout << "[Redistribute] to_move=" << to_move << " move_start_idx=" << move_start_idx << std::endl;

    // Step 1: Identify tombstones in sibling that will be transferred
    std::vector<size_t> transferred_tombs;
    for (int i = 0; i < sibling->GetTombstoneCount(); i++) {
      size_t tomb_idx = sibling->GetTombstoneAt(i);
      // std::cout << "[Redistribute] Checking sibling tombstone " << i << " at idx=" << tomb_idx
      //           << " (move_start=" << move_start_idx << ")" << std::endl;
      if (tomb_idx >= static_cast<size_t>(move_start_idx)) {
        size_t new_idx = tomb_idx - move_start_idx;
        transferred_tombs.push_back(new_idx);
        // std::cout << "[Redistribute]   -> Transferring to node at new_idx=" << new_idx << std::endl;
      }
    }
    // std::cout << "[Redistribute] transferred_tombs count=" << transferred_tombs.size() << std::endl;

    // Step 2: Shift existing entries in node to make room
    for (int i = node->GetSize() - 1; i >= 0; i--) {
      node->SetKeyAt(i + to_move, node->KeyAt(i));
      node->SetValueAt(i + to_move, node->ValueAt(i));
    }

    // Step 3: Adjust existing node tombstones (they shifted right)
    std::vector<size_t> node_tombs;
    node_tombs.reserve(node->GetTombstoneCount());
    for (int i = 0; i < node->GetTombstoneCount(); i++) {
      size_t adjusted = node->GetTombstoneAt(i) + to_move;
      node_tombs.push_back(adjusted);
      // std::cout << "[Redistribute] Adjusted node tombstone " << i << " from " << node->GetTombstoneAt(i) << " to "
      //           << adjusted << std::endl;
    }

    // Step 4: Copy entries from sibling to node
    for (int i = 0; i < to_move; i++) {
      int src_idx = move_start_idx + i;
      node->SetKeyAt(i, sibling->KeyAt(src_idx));
      node->SetValueAt(i, sibling->ValueAt(src_idx));
    }

    // Step 5: Update sizes
    node->SetSize(node->GetSize() + to_move);
    sibling->SetSize(sibling->GetSize() - to_move);

    // Step 6: Combine tombstones (DONOR FIRST = higher priority)
    std::vector<size_t> combined_tombs;

    // std::cout << "[Redistribute] Combining tombstones (buffer size=" << LEAF_PAGE_TOMB_CNT << ")" << std::endl;
    // Add donor's transferred tombstones FIRST
    for (size_t tomb : transferred_tombs) {
      if (combined_tombs.size() < static_cast<size_t>(LEAF_PAGE_TOMB_CNT)) {
        combined_tombs.push_back(tomb);
        // std::cout << "[Redistribute]   Added DONOR tombstone at idx=" << tomb << std::endl;
      } else {
        // std::cout << "[Redistribute]   Dropped DONOR tombstone at idx=" << tomb << " (buffer full)" << std::endl;
      }
    }

    // Add recipient's adjusted tombstones SECOND
    for (size_t tomb : node_tombs) {
      if (combined_tombs.size() < static_cast<size_t>(LEAF_PAGE_TOMB_CNT)) {
        combined_tombs.push_back(tomb);
        std::cout << "[Redistribute]   Added RECIPIENT tombstone at idx=" << tomb << std::endl;
      } else {
        std::cout << "[Redistribute]   Dropped RECIPIENT tombstone at idx=" << tomb << " (buffer full)" << std::endl;
      }
    }

    node->SetTombstones(combined_tombs);
    // std::cout << "[Redistribute] Node now has " << node->GetTombstoneCount() << " tombstones" << std::endl;

    // Step 7: Update sibling's remaining tombstones
    std::vector<size_t> remaining_tombs;
    for (int i = 0; i < sibling->GetTombstoneCount(); i++) {
      size_t tomb_idx = sibling->GetTombstoneAt(i);
      if (tomb_idx < static_cast<size_t>(move_start_idx)) {
        remaining_tombs.push_back(tomb_idx);
        std::cout << "[Redistribute] Keeping sibling tombstone at idx=" << tomb_idx << std::endl;
      } else {
        std::cout << "[Redistribute] Discarding sibling tombstone at idx=" << tomb_idx << " (transferred)" << std::endl;
      }
    }

    sibling->SetTombstones(remaining_tombs);
    // std::cout << "[Redistribute] Sibling now has " << sibling->GetTombstoneCount() << " tombstones" << std::endl;

    // Step 8: Update parent separator key
    parent->SetKeyAt(node_index, node->KeyAt(0));
    // std::cout << "[Redistribute] Updated parent separator key at index " << node_index << std::endl;

  } else {
    // ===== Move entries from RIGHT sibling to LEFT node =====
    // std::cout << "[Redistribute] Moving from RIGHT sibling to LEFT node" << std::endl;

    int to_move = sibling->GetSize() - target_size;
    int old_node_size = node->GetSize();

    // std::cout << "[Redistribute] to_move=" << to_move << " old_node_size=" << old_node_size << std::endl;

    // Step 1: Identify tombstones in sibling that will be transferred
    std::vector<size_t> transferred_tombs;
    for (int i = 0; i < sibling->GetTombstoneCount(); i++) {
      size_t tomb_idx = sibling->GetTombstoneAt(i);
      // std::cout << "[Redistribute] Checking sibling tombstone " << i << " at idx=" << tomb_idx
      //           << " (to_move=" << to_move << ")" << std::endl;
      if (tomb_idx < static_cast<size_t>(to_move)) {
        size_t new_idx = old_node_size + tomb_idx;
        transferred_tombs.push_back(new_idx);
        // std::cout << "[Redistribute]   -> Transferring to node at new_idx=" << new_idx << std::endl;
      }
    }
    // std::cout << "[Redistribute] transferred_tombs count=" << transferred_tombs.size() << std::endl;

    // Step 2: Copy entries from sibling to end of node
    // std::cout << "[Redistribute] Copying " << to_move << " entries from sibling to node" << std::endl;
    for (int i = 0; i < to_move; i++) {
      // std::cout << "[Redistribute]   Copying sibling[" << i << "]=" << sibling->KeyAt(i) << " to node["
      //           << (old_node_size + i) << "]" << std::endl;
      node->SetKeyAt(old_node_size + i, sibling->KeyAt(i));
      node->SetValueAt(old_node_size + i, sibling->ValueAt(i));
    }
    node->SetSize(node->GetSize() + to_move);

    // Step 3: Shift remaining entries in sibling left
    // std::cout << "[Redistribute] Shifting sibling entries left by " << to_move << std::endl;
    for (int i = 0; i < sibling->GetSize() - to_move; i++) {
      // std::cout << "[Redistribute]   Moving sibling[" << (i + to_move) << "]=" << sibling->KeyAt(i + to_move)
      //           << " to sibling[" << i << "]" << std::endl;
      sibling->SetKeyAt(i, sibling->KeyAt(i + to_move));
      sibling->SetValueAt(i, sibling->ValueAt(i + to_move));
    }
    sibling->SetSize(sibling->GetSize() - to_move);

    // Step 4: Combine tombstones (DONOR FIRST = higher priority)
    std::vector<size_t> combined_tombs;

    // std::cout << "[Redistribute] Combining tombstones (buffer size=" << LEAF_PAGE_TOMB_CNT << ")" << std::endl;
    // Add donor's transferred tombstones FIRST
    for (size_t tomb : transferred_tombs) {
      if (combined_tombs.size() < static_cast<size_t>(LEAF_PAGE_TOMB_CNT)) {
        combined_tombs.push_back(tomb);
        std::cout << "[Redistribute]   Added DONOR tombstone at idx=" << tomb << std::endl;
      } else {
        std::cout << "[Redistribute]   Dropped DONOR tombstone at idx=" << tomb << " (buffer full)" << std::endl;
      }
    }

    // Add recipient's existing tombstones SECOND
    for (int i = 0; i < node->GetTombstoneCount(); i++) {
      if (combined_tombs.size() < static_cast<size_t>(LEAF_PAGE_TOMB_CNT)) {
        size_t tomb = node->GetTombstoneAt(i);
        combined_tombs.push_back(tomb);
        std::cout << "[Redistribute]   Added RECIPIENT tombstone at idx=" << tomb << std::endl;
      } else {
        std::cout << "[Redistribute]   Dropped RECIPIENT tombstone at idx=" << node->GetTombstoneAt(i)
                  << " (buffer full)" << std::endl;
      }
    }

    node->SetTombstones(combined_tombs);
    // std::cout << "[Redistribute] Node now has " << node->GetTombstoneCount() << " tombstones" << std::endl;

    // Step 5: Update sibling's remaining tombstones (adjust indices)
    std::vector<size_t> remaining_tombs;
    // std::cout << "[Redistribute] Updating sibling's " << sibling->GetTombstoneCount() << " tombstones" << std::endl;
    for (int i = 0; i < sibling->GetTombstoneCount(); i++) {
      size_t tomb_idx = sibling->GetTombstoneAt(i);
      // std::cout << "[Redistribute]   Checking tombstone at idx=" << tomb_idx << std::endl;
      if (tomb_idx >= static_cast<size_t>(to_move)) {
        size_t adjusted = tomb_idx - to_move;
        remaining_tombs.push_back(adjusted);
        std::cout << "[Redistribute]   Keeping at adjusted idx=" << adjusted << std::endl;
      } else {
        std::cout << "[Redistribute]   Discarding (transferred to node)" << std::endl;
      }
    }
    sibling->SetTombstones(remaining_tombs);
    // std::cout << "[Redistribute] Sibling now has " << sibling->GetTombstoneCount() << " tombstones" << std::endl;

    // Step 6: Update parent separator key
    parent->SetKeyAt(sibling_index, sibling->KeyAt(0));
    // std::cout << "[Redistribute] Updated parent separator key at index " << sibling_index << std::endl;
  }

  // std::cout << "[Redistribute] AFTER - Node keys: ";
  // for (int i = 0; i < node->GetSize(); i++) {
  //   std::cout << node->KeyAt(i) << " ";
  // }
  // std::cout << " | tombstones: ";
  // for (int i = 0; i < node->GetTombstoneCount(); i++) {
  //   std::cout << "[" << node->GetTombstoneAt(i) << "]=>" << node->KeyAt(node->GetTombstoneAt(i)) << " ";
  // }
  // std::cout << std::endl;

  // std::cout << "[Redistribute] AFTER - Sibling keys: ";
  // for (int i = 0; i < sibling->GetSize(); i++) {
  //   std::cout << sibling->KeyAt(i) << " ";
  // }
  // std::cout << " | tombstones: ";
  // for (int i = 0; i < sibling->GetTombstoneCount(); i++) {
  //   std::cout << "[" << sibling->GetTombstoneAt(i) << "]=>" << sibling->KeyAt(sibling->GetTombstoneAt(i)) << " ";
  // }
  // std::cout << std::endl;

  // std::cout << "[Redistribute] COMPLETE: node_size=" << node->GetSize() << " sibling_size=" << sibling->GetSize()
  //           << std::endl;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Coalesce(WritePageGuard &node_guard, WritePageGuard &sibling_guard, WritePageGuard &parent_guard,
                              int node_index, int sibling_index, bool is_predecessor, Context &ctx) {
  auto *node = node_guard.template AsMut<LeafPage>();
  auto *sibling = sibling_guard.template AsMut<LeafPage>();
  auto *parent = parent_guard.template AsMut<InternalPage>();

  // std::cout << "[Coalesce] START: node_page=" << node_guard.GetPageId() << " node_size=" << node->GetSize()
  //           << " sibling_page=" << sibling_guard.GetPageId() << " sibling_size=" << sibling->GetSize()
  //           << " is_predecessor=" << is_predecessor << std::endl;

  // if (node->HasTombstones()) {
  //   std::cout << "[Coalesce] Processing node tombstones first" << std::endl;
  //   node->ProcessAllTombstones();
  // }
  // if (sibling->HasTombstones()) {
  //   std::cout << "[Coalesce] Processing sibling tombstones first" << std::endl;
  //   sibling->ProcessAllTombstones();
  // }

  // Determine recipient (left) and donor (right)
  LeafPage *recipient;
  LeafPage *donor;
  page_id_t page_to_delete;
  int parent_key_to_remove;

  if (is_predecessor) {
    recipient = sibling;
    donor = node;
    page_to_delete = node_guard.GetPageId();
    parent_key_to_remove = node_index;
    // std::cout << "[Coalesce] Merging node INTO sibling (sibling=left, node=right)" << std::endl;
    // std::cout << "[Coalesce] page_to_delete=" << page_to_delete
    //           << " parent_key_to_remove_index=" << parent_key_to_remove << std::endl;
  } else {
    recipient = node;
    donor = sibling;
    page_to_delete = sibling_guard.GetPageId();
    parent_key_to_remove = sibling_index;
    // std::cout << "[Coalesce] Merging sibling INTO node (node=left, sibling=right)" << std::endl;
    // std::cout << "[Coalesce] page_to_delete=" << page_to_delete
    //           << " parent_key_to_remove_index=" << parent_key_to_remove << std::endl;
  }

  int recipient_old_size = recipient->GetSize();
  // std::cout << "[Coalesce] recipient_old_size=" << recipient_old_size << " donor_size=" << donor->GetSize()
  //           << std::endl;

  // Copy all entries from donor to recipient
  for (int i = 0; i < donor->GetSize(); i++) {
    recipient->SetKeyAt(recipient_old_size + i, donor->KeyAt(i));
    recipient->SetValueAt(recipient_old_size + i, donor->ValueAt(i));
  }
  recipient->SetSize(recipient_old_size + donor->GetSize());

  // std::cout << "[Coalesce] After copy, recipient_size=" << recipient->GetSize() << std::endl;

  // Combine tombstones - prioritize recipient's existing tombstones
  std::vector<size_t> combined_tombs;
  // combined_tombs.reserve(node->GetTombstoneCount());
  // for (int i = 0; i < recipient->GetTombstoneCount(); i++) {
  //   combined_tombs.push_back(recipient->GetTombstoneAt(i));
  // }

  for (int i = 0; i < donor->GetTombstoneCount(); i++) {
    if (combined_tombs.size() < static_cast<size_t>(LEAF_PAGE_TOMB_CNT)) {
      size_t donor_tomb_idx = donor->GetTombstoneAt(i);
      size_t adjusted_idx = recipient_old_size + donor_tomb_idx;
      combined_tombs.push_back(adjusted_idx);
      // std::cout << "[Coalesce]   Added DONOR tombstone at adjusted idx=" << adjusted_idx << std::endl;
    } else {
      std::cout << "[Coalesce]   Dropped DONOR tombstone (buffer full)" << std::endl;
    }
  }

  // for (int i = 0; i < donor->GetTombstoneCount(); i++) {
  //   if (combined_tombs.size() < static_cast<size_t>(LEAF_PAGE_TOMB_CNT)) {
  //     size_t donor_tomb_idx = donor->GetTombstoneAt(i);
  //     combined_tombs.push_back(recipient_old_size + donor_tomb_idx);
  //   }
  // }
  for (int i = 0; i < recipient->GetTombstoneCount(); i++) {
    if (combined_tombs.size() < static_cast<size_t>(LEAF_PAGE_TOMB_CNT)) {
      size_t recip_tomb_idx = recipient->GetTombstoneAt(i);
      combined_tombs.push_back(recip_tomb_idx);
      std::cout << "[Coalesce]   Added RECIPIENT tombstone at idx=" << recip_tomb_idx << std::endl;
    } else {
      std::cout << "[Coalesce]   Dropped RECIPIENT tombstone (buffer full)" << std::endl;
    }
  }

  recipient->SetTombstones(combined_tombs);

  // std::cout << "[Coalesce] Combined tombstones count=" << combined_tombs.size() << std::endl;

  // Update linked list
  recipient->SetNextPageId(donor->GetNextPageId());

  // Remove entry from parent
  // std::cout << "[Coalesce] Removing entry from parent at index " << parent_key_to_remove << std::endl;
  parent->RemoveAt(parent_key_to_remove);
  // std::cout << "[Coalesce] Parent size after removal=" << parent->GetSize() << std::endl;

  // Delete the merged page
  // std::cout << "[Coalesce] Deleting page " << page_to_delete << std::endl;
  bpm_->DeletePage(page_to_delete);

  // std::cout << "=== Tree Structure after coalesce===" << std::endl;
  // std::cout << DrawBPlusTree() << std::endl;

  // CRITICAL: Check if parent is now empty or needs rebalancing
  bool parent_is_root = ctx.IsRootPage(parent_guard.GetPageId());
  // std::cout << "[Coalesce] parent_is_root=" << parent_is_root << " parent_size=" << parent->GetSize() << std::endl;

  if (parent_is_root && parent->GetSize() == 1) {
    // Parent was root and now has only one child
    // std::cout << "[Coalesce] Parent is root with only 1 child, promoting child to root" << std::endl;
    auto *header = ctx.header_page_->template AsMut<BPlusTreeHeaderPage>();
    page_id_t new_root = parent->ValueAt(0);
    // std::cout << "[Coalesce] New root page_id=" << new_root << std::endl;
    header->root_page_id_ = new_root;
    page_id_t old_root = parent_guard.GetPageId();
    // std::cout << "[Coalesce] Deleting old root page " << old_root << std::endl;
    bpm_->DeletePage(old_root);
    // Don't put parent_guard back - it's been deleted
  } else if (parent->GetSize() == 1) {
    // Size 1 is NEVER valid for non-root internal nodes
    // Recursively handle this parent
    CoalesceOrRedistributeInternal(parent_guard, ctx);
  } else if (!parent_is_root && parent->GetSize() < parent->GetMinSize()) {
    // Parent underflow - recursively handle
    // std::cout << "[Coalesce] Parent underflow detected, recursively handling" << std::endl;
    CoalesceOrRedistributeInternal(parent_guard, ctx);
  } else {
    // Parent is fine - put it back in write_set
    // std::cout << "[Coalesce] Parent is fine, putting back in write_set" << std::endl;
    ctx.write_set_.push_back(std::move(parent_guard));
  }
  // std::cout << "[Coalesce] === Parent structure after coalesce ===" << std::endl;
  // for (int i = 0; i < parent->GetSize(); i++) {
  //   // std::cout << "[Coalesce]   Parent[" << i << "] = page_id " << parent->ValueAt(i);
  //   if (i > 0) {
  //     std::cout << ", key=" << parent->KeyAt(i).ToString();
  //   }
  //   std::cout << std::endl;
  // }
  // std::cout << "[Coalesce] COMPLETE" << std::endl;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::CoalesceOrRedistributeInternal(WritePageGuard &node_guard, Context &ctx) {
  // std::cout << "[CoalesceOrRedistributeInternal] node_page=" << node_guard.GetPageId() << std::endl;
  auto *node = node_guard.template AsMut<InternalPage>();
  // std::cout << "[CoalesceOrRedistributeInternal] node_size=" << node->GetSize() << std::endl;

  // Get parent from write set
  if (ctx.write_set_.empty()) {
    // std::cout << "[CoalesceOrRedistributeInternal] No parent (root node), returning" << std::endl;
    return;  // Root node
  }

  auto parent_guard = std::move(ctx.write_set_.back());
  ctx.write_set_.pop_back();
  auto *parent = parent_guard.template AsMut<InternalPage>();
  // std::cout << "[CoalesceOrRedistributeInternal] parent_page=" << parent_guard.GetPageId()
  //           << " parent_size=" << parent->GetSize() << std::endl;

  // Find node's index in parent
  int node_index = -1;
  for (int i = 0; i < parent->GetSize(); i++) {
    if (parent->ValueAt(i) == node_guard.GetPageId()) {
      node_index = i;
      break;
    }
  }
  BUSTUB_ASSERT(node_index != -1, "Node not found in parent");
  // std::cout << "[CoalesceOrRedistributeInternal] node_index_in_parent=" << node_index << std::endl;

  // Get sibling
  WritePageGuard sibling_guard;
  int sibling_index;
  bool is_predecessor;

  if (node_index > 0) {
    sibling_index = node_index - 1;
    is_predecessor = true;
    // std::cout << "[CoalesceOrRedistributeInternal] Using LEFT sibling, sibling_index=" << sibling_index << std::endl;
  } else {
    sibling_index = node_index + 1;
    is_predecessor = false;
    // std::cout << "[CoalesceOrRedistributeInternal] Using RIGHT sibling, sibling_index=" << sibling_index <<
    // std::endl;
  }

  sibling_guard = bpm_->WritePage(parent->ValueAt(sibling_index));
  auto *sibling = sibling_guard.template AsMut<InternalPage>();
  // std::cout << "[CoalesceOrRedistributeInternal] sibling_page=" << sibling_guard.GetPageId()
  //           << " sibling_size=" << sibling->GetSize() << std::endl;

  // Decide: redistribute or coalesce
  int total_size = node->GetSize() + sibling->GetSize();
  int min_size = node->GetMinSize();

  // std::cout << "[CoalesceOrRedistributeInternal] total_size=" << total_size << " min_size=" << min_size
  //           << " threshold(2*min_size)=" << (2 * min_size) << std::endl;

  if (total_size >= 2 * min_size) {
    // Redistribute (implementation similar to leaf case)
    // std::cout << "[CoalesceOrRedistributeInternal] REDISTRIBUTING" << std::endl;
    RedistributeInternal(node_guard, sibling_guard, parent_guard, node_index, sibling_index, is_predecessor);

    if (sibling->GetSize() <= sibling->GetMinSize()) {
      // std::cout << "[CoalesceOrRedistributeInternal] Sibling underfull after redistribution, COALESCING now"
      //           << std::endl;
      // Merge node and sibling together
      if (is_predecessor) {
        // sibling is left, node is right - merge node into sibling
        // NOLINTNEXTLINE
        CoalesceInternal(node_guard, sibling_guard, parent_guard, node_index, sibling_index, is_predecessor, ctx);
      } else {
        // sibling is right, node is left - merge sibling into node
        // NOLINTNEXTLINE
        CoalesceInternal(sibling_guard, node_guard, parent_guard, sibling_index, node_index, !is_predecessor, ctx);
      }
      return;  // CoalesceInternal handles parent
    }
    ctx.write_set_.push_back(std::move(parent_guard));
  } else {
    // Coalesce (implementation similar to leaf case)
    // std::cout << "[CoalesceOrRedistributeInternal] COALESCING" << std::endl;
    CoalesceInternal(node_guard, sibling_guard, parent_guard, node_index, sibling_index, is_predecessor, ctx);
  }
  // std::cout << "[CoalesceOrRedistributeInternal] COMPLETE" << std::endl;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::RedistributeInternal(WritePageGuard &node_guard, WritePageGuard &sibling_guard,
                                          WritePageGuard &parent_guard, int node_index, int sibling_index,
                                          bool is_predecessor) {
  auto *node = node_guard.AsMut<InternalPage>();
  auto *sibling = sibling_guard.AsMut<InternalPage>();
  auto *parent = parent_guard.AsMut<InternalPage>();

  // std::cout << "[RedistributeInternal] START: node_size=" << node->GetSize() << " sibling_size=" <<
  // sibling->GetSize()
  //           << " is_predecessor=" << is_predecessor << std::endl;

  if (is_predecessor) {
    // ===== Move entries from LEFT sibling to RIGHT node =====
    // Need to pull down parent key and push up new separator
    // std::cout << "[RedistributeInternal] Moving from LEFT sibling to RIGHT node" << std::endl;

    // Step 1: Get the separator key from parent
    KeyType separator_key = parent->KeyAt(node_index);

    // Step 2: Get the last child pointer from sibling
    page_id_t moved_child = sibling->ValueAt(sibling->GetSize() - 1);
    // std::cout << "[RedistributeInternal] moved_child=" << moved_child << std::endl;

    // Step 3: Shift entries in node to make room
    // for (int i = node->GetSize() - 1; i >= 0; i--) {
    //   node->SetKeyAt(i + 1, node->KeyAt(i));
    //   node->SetValueAt(i + 1, node->ValueAt(i));
    // }
    // Step 3: Shift VALUES right
    for (int i = node->GetSize() - 1; i >= 0; i--) {
      node->SetValueAt(i + 1, node->ValueAt(i));
    }
    // Shift KEYS right (keys start at index 1, not 0!)
    for (int i = node->GetSize() - 1; i >= 1; i--) {
      node->SetKeyAt(i + 1, node->KeyAt(i));
    }
    // Step 4: Insert separator key and moved child at beginning of node
    // The first value slot already has the leftmost child, so we set the key
    node->SetValueAt(0, moved_child);
    node->SetKeyAt(0, separator_key);
    node->SetSize(node->GetSize() + 1);

    // Step 5: Remove last entry from sibling
    sibling->SetSize(sibling->GetSize() - 1);

    // Step 6: Push up new separator key to parent
    // The new separator is the last key in sibling (before removal)
    KeyType new_separator = sibling->KeyAt(sibling->GetSize());
    parent->SetKeyAt(node_index, new_separator);
    // std::cout << "[RedistributeInternal] Updated parent separator at index " << node_index << std::endl;

  } else {
    // ===== Move entries from RIGHT sibling to LEFT node =====
    // Need to pull down parent key and push up new separator
    // std::cout << "[RedistributeInternal] Moving from RIGHT sibling to LEFT node" << std::endl;

    // Step 1: Get the separator key from parent
    KeyType separator_key = parent->KeyAt(sibling_index);

    // Step 2: Get the first child pointer from sibling
    page_id_t moved_child = sibling->ValueAt(0);
    // std::cout << "[RedistributeInternal] moved_child=" << moved_child << std::endl;

    // Step 3: Append separator key and moved child to end of node
    node->SetKeyAt(node->GetSize(), separator_key);
    node->SetValueAt(node->GetSize(), moved_child);
    node->SetSize(node->GetSize() + 1);

    // Step 4: Push up new separator key to parent
    // The new separator is the first key in sibling (before removal)
    // std::cout << "[RedistributeInternal] sibling size before shift: " << sibling->GetSize() << std::endl;
    // std::cout << "[RedistributeInternal] sibling->KeyAt(1) = " << sibling->KeyAt(1) << std::endl;
    KeyType new_separator = sibling->KeyAt(1);
    parent->SetKeyAt(sibling_index, new_separator);
    // std::cout << "[RedistributeInternal] new_separator=" << new_separator << std::endl;
    // std::cout << "[RedistributeInternal] Updated parent separator at index " << sibling_index << std::endl;

    // Step 5: Shift entries in sibling left (remove first entry)
    for (int i = 0; i < sibling->GetSize() - 1; i++) {
      sibling->SetValueAt(i, sibling->ValueAt(i + 1));
    }
    // Shift keys (keys start at index 1)
    for (int i = 1; i < sibling->GetSize() - 1; i++) {
      sibling->SetKeyAt(i, sibling->KeyAt(i + 1));
    }
    sibling->SetSize(sibling->GetSize() - 1);
  }

  // std::cout << "[RedistributeInternal] COMPLETE: node_size=" << node->GetSize()
  //           << " sibling_size=" << sibling->GetSize() << std::endl;
}

// ==================== COALESCE INTERNAL ====================
FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::CoalesceInternal(WritePageGuard &node_guard, WritePageGuard &sibling_guard,
                                      WritePageGuard &parent_guard, int node_index, int sibling_index,
                                      bool is_predecessor, Context &ctx) {
  auto *node = node_guard.AsMut<InternalPage>();
  auto *sibling = sibling_guard.AsMut<InternalPage>();
  auto *parent = parent_guard.AsMut<InternalPage>();

  // std::cout << "[CoalesceInternal] START: node_page=" << node_guard.GetPageId() << " node_size=" << node->GetSize()
  //           << " sibling_page=" << sibling_guard.GetPageId() << " sibling_size=" << sibling->GetSize()
  //           << " is_predecessor=" << is_predecessor << std::endl;

  // Determine recipient (left) and donor (right)
  InternalPage *recipient;
  InternalPage *donor;
  page_id_t page_to_delete;
  int parent_key_to_remove;
  KeyType separator_key;

  if (is_predecessor) {
    // Merge node into sibling: sibling (left) ← node (right)
    recipient = sibling;
    donor = node;
    page_to_delete = node_guard.GetPageId();
    parent_key_to_remove = node_index;
    separator_key = parent->KeyAt(node_index);
    // std::cout << "[CoalesceInternal] Merging node INTO sibling (sibling=left, node=right)" << std::endl;
    // std::cout << "[CoalesceInternal] page_to_delete=" << page_to_delete
    //           << " parent_key_to_remove_index=" << parent_key_to_remove << std::endl;
  } else {
    // Merge sibling into node: node (left) ← sibling (right)
    recipient = node;
    donor = sibling;
    page_to_delete = sibling_guard.GetPageId();
    parent_key_to_remove = sibling_index;
    separator_key = parent->KeyAt(sibling_index);
    // std::cout << "[CoalesceInternal] Merging sibling INTO node (node=left, sibling=right)" << std::endl;
    // std::cout << "[CoalesceInternal] page_to_delete=" << page_to_delete
    //           << " parent_key_to_remove_index=" << parent_key_to_remove << std::endl;
  }

  int recipient_old_size = recipient->GetSize();
  // std::cout << "[CoalesceInternal] recipient_old_size=" << recipient_old_size << " donor_size=" << donor->GetSize()
  //           << std::endl;

  // Step 1: Append the separator key from parent
  recipient->SetKeyAt(recipient_old_size, separator_key);
  // recipient->SetSize(recipient_old_size + 1);
  // std::cout << "[CoalesceInternal] Appended separator key, recipient_size=" << recipient->GetSize() << std::endl;

  // Step 2: Copy all entries from donor to recipient
  for (int i = 0; i < donor->GetSize(); i++) {
    recipient->SetValueAt(recipient_old_size + i, donor->ValueAt(i));
  }
  // std::cout << "[CoalesceInternal] Copied " << donor->GetSize() << " values from donor" << std::endl;

  for (int i = 1; i < donor->GetSize(); i++) {
    recipient->SetKeyAt(recipient_old_size + i, donor->KeyAt(i));
  }
  // std::cout << "[CoalesceInternal] Copied " << (donor->GetSize() - 1) << " keys from donor" << std::endl;

  // recipient->SetSize(recipient_old_size + 1 + donor->GetSize());
  recipient->SetSize(recipient_old_size + donor->GetSize());
  // std::cout << "[CoalesceInternal] After copy, recipient_size=" << recipient->GetSize() << std::endl;

  // Step 3: Remove entry from parent
  // std::cout << "[CoalesceInternal] Removing entry from parent at index " << parent_key_to_remove << std::endl;
  parent->RemoveAt(parent_key_to_remove);
  // std::cout << "[CoalesceInternal] Parent size after removal=" << parent->GetSize() << std::endl;

  // Step 4: Delete the merged page
  // std::cout << "[CoalesceInternal] Deleting page " << page_to_delete << std::endl;
  bpm_->DeletePage(page_to_delete);

  if (is_predecessor) {
    // std::cout << "[CoalesceInternal] Dropping node_guard (page " << node_guard.GetPageId() << ")" << std::endl;
    node_guard.Drop();
  } else {
    // std::cout << "[CoalesceInternal] Dropping sibling_guard (page " << sibling_guard.GetPageId() << ")" << std::endl;
    sibling_guard.Drop();
  }

  // Step 5: Handle parent underflow
  bool parent_is_root = ctx.IsRootPage(parent_guard.GetPageId());
  // std::cout << "[CoalesceInternal] parent_is_root=" << parent_is_root << " parent_size=" << parent->GetSize()
  //           << std::endl;

  if (parent_is_root && parent->GetSize() == 1) {
    // Parent was root and now has only one child - make child new root
    // std::cout << "[CoalesceInternal] Parent is root with only 1 child, promoting child to root" << std::endl;
    auto *header = ctx.header_page_->AsMut<BPlusTreeHeaderPage>();
    page_id_t new_root = parent->ValueAt(0);
    // std::cout << "[CoalesceInternal] New root page_id=" << new_root << std::endl;
    header->root_page_id_ = new_root;
    ctx.root_page_id_ = new_root;
    page_id_t old_root = parent_guard.GetPageId();
    parent_guard.Drop();
    // std::cout << "[CoalesceInternal] Deleting old root page " << old_root << std::endl;
    bpm_->DeletePage(old_root);

  } else if (!parent_is_root && parent->GetSize() < parent->GetMinSize()) {
    // Parent underflow - recursively handle
    // std::cout << "[CoalesceInternal] Parent underflow detected, recursively handling" << std::endl;
    CoalesceOrRedistributeInternal(parent_guard, ctx);
  } else {
    // Parent is fine - put it back in write_set
    // std::cout << "[CoalesceInternal] Parent is fine, putting back in write_set" << std::endl;
    ctx.write_set_.push_back(std::move(parent_guard));
  }
  // std::cout << "[CoalesceInternal] COMPLETE" << std::endl;
}

/*****************************************************************************
 * INDEX ITERATOR
 *****************************************************************************/
/**
 * @brief Input parameter is void, find the leftmost leaf page first, then construct
 * index iterator
 *
 * You may want to implement this while implementing Task #3.
 *
 * @return : index iterator
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin() -> INDEXITERATOR_TYPE {
  // std::cout << "[B+Tree::Begin] START" << std::endl;

  auto header_guard = bpm_->ReadPage(header_page_id_);
  auto *header = header_guard.As<BPlusTreeHeaderPage>();

  // std::cout << "[B+Tree::Begin] root_page_id=" << header->root_page_id_ << std::endl;

  if (header->root_page_id_ == INVALID_PAGE_ID) {
    // std::cout << "[B+Tree::Begin] Root is invalid, returning End()" << std::endl;
    return End();
  }

  ReadPageGuard current = bpm_->ReadPage(header->root_page_id_);

  // std::cout << "[B+Tree::Begin] Starting from root page_id=" << current.GetPageId() << std::endl;

  // Navigate to leftmost leaf
  while (!current.As<BPlusTreePage>()->IsLeafPage()) {
    auto *internal = current.As<InternalPage>();
    page_id_t child_page_id = internal->ValueAt(0);
    // std::cout << "[B+Tree::Begin] Going to child page_id=" << child_page_id << std::endl;
    current = bpm_->ReadPage(child_page_id);
  }

  page_id_t leaf_page_id = current.GetPageId();
  // std::cout << "[B+Tree::Begin] Found leftmost leaf page_id=" << leaf_page_id << std::endl;

  return INDEXITERATOR_TYPE(bpm_.get(), leaf_page_id, 0);
}

/**
 * @brief Input parameter is low key, find the leaf page that contains the input key
 * first, then construct index iterator
 * @return : index iterator
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> INDEXITERATOR_TYPE {
  auto header_guard = bpm_->ReadPage(header_page_id_);
  auto *header = header_guard.As<BPlusTreeHeaderPage>();

  if (header->root_page_id_ == INVALID_PAGE_ID) {
    return End();
  }

  ReadPageGuard current = bpm_->ReadPage(header->root_page_id_);

  // Navigate to leaf containing the key
  while (!current.As<BPlusTreePage>()->IsLeafPage()) {
    auto *internal = current.As<InternalPage>();
    page_id_t child_page_id = internal->ValueAt(0);

    // Find the right child pointer
    for (int i = 1; i < internal->GetSize(); i++) {
      if (comparator_(key, internal->KeyAt(i)) < 0) {
        break;
      }
      child_page_id = internal->ValueAt(i);
    }

    current = bpm_->ReadPage(child_page_id);
  }

  // Find position in leaf where key would be (first key >= search key)
  auto *leaf = current.As<LeafPage>();
  int index = leaf->KeyIndex(key, comparator_);

  page_id_t leaf_page_id = current.GetPageId();

  return INDEXITERATOR_TYPE(bpm_.get(), leaf_page_id, index);
}

/**
 * @brief Input parameter is void, construct an index iterator representing the end
 * of the key/value pair in the leaf node
 * @return : index iterator
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::End() -> INDEXITERATOR_TYPE { return INDEXITERATOR_TYPE(); }

/**
 * @return Page id of the root of this tree
 *
 * You may want to implement this while implementing Task #3.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetRootPageId() -> page_id_t {
  Context ctx;
  ctx.header_page_ = bpm_->WritePage(header_page_id_);
  auto header = ctx.header_page_->AsMut<BPlusTreeHeaderPage>();
  return header->root_page_id_;
}

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 3>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 2>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 1>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, -1>;

template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
