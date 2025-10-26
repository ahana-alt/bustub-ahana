//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_delete_test.cpp
//
// Identification: test/storage/b_plus_tree_delete_test.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdio>

#include "buffer/buffer_pool_manager.h"
#include "gtest/gtest.h"
#include "storage/b_plus_tree_utils.h"
#include "storage/disk/disk_manager_memory.h"
#include "storage/index/b_plus_tree.h"
#include "test_util.h"  // NOLINT

namespace bustub {

using bustub::DiskManagerUnlimitedMemory;

TEST(BPlusTreeTests, DISABLED_DebugTwoKeyDelete) {
  auto key_schema = ParseCreateStatement("a bigint");
  GenericComparator<8> comparator(key_schema.get());
  auto disk_manager = std::make_unique<DiskManagerUnlimitedMemory>();
  auto *bpm = new BufferPoolManager(50, disk_manager.get());
  
  page_id_t page_id = bpm->NewPage();
  BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("foo_pk", page_id, bpm, comparator, 2, 3);
  
  GenericKey<8> index_key;
  RID rid;
  
  std::cout << "\n=== Insert keys 1 and 2 ===" << std::endl;
  
  // Insert two keys
  for (int i = 1; i <= 2; i++) {
    index_key.SetFromInteger(i);
    rid.Set(0, i);
    std::cout << "Inserting key " << i << std::endl;
    tree.Insert(index_key, rid);
  }
  
  // Verify both are there
  std::cout << "\n=== Verify both keys present ===" << std::endl;
  for (int i = 1; i <= 2; i++) {
    index_key.SetFromInteger(i);
    std::vector<RID> rids;
    bool found = tree.GetValue(index_key, &rids);
    std::cout << "Key " << i << " - Found: " << found << ", size: " << rids.size() << std::endl;
    EXPECT_TRUE(found);
    std::cout<<"till here";
  }
  
  // Remove key 1
  std::cout << "\n=== Remove key 1 ===" << std::endl;
  index_key.SetFromInteger(1);
  tree.Remove(index_key);
  
  // Verify key 1 is gone, key 2 remains
  std::cout << "\n=== Verify after removal ===" << std::endl;
  
  index_key.SetFromInteger(1);
  std::vector<RID> rids;
  bool found = tree.GetValue(index_key, &rids);
  std::cout << "Key 1 - Found: " << found << " (should be false)" << std::endl;
  EXPECT_FALSE(found);
  
  index_key.SetFromInteger(2);
  rids.clear();
  found = tree.GetValue(index_key, &rids);
  std::cout << "Key 2 - Found: " << found << " (should be true), size: " << rids.size() << std::endl;
  EXPECT_TRUE(found);
  EXPECT_EQ(rids.size(), 1);
  
  delete bpm;
}

TEST(BPlusTreeTests, DISABLED_SimpleTombstoneMarkingTest) {
  auto key_schema = ParseCreateStatement("a bigint");
  GenericComparator<8> comparator(key_schema.get());
  auto disk_manager = std::make_unique<DiskManagerUnlimitedMemory>();
  auto *bpm = new BufferPoolManager(50, disk_manager.get());
  page_id_t page_id = bpm->NewPage();
  
  // leaf_max_size=2, internal_max_size=3
  BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 1> tree("foo_pk", page_id, bpm, comparator, 2, 3);
  //                                                   ↑ Tombstone buffer = 1
  
  GenericKey<8> index_key;
  RID rid;
  
  std::cout << "\n=== TEST: Tombstone Marking (no processing) ===" << std::endl;
  std::cout << "Leaf max size: 2, Tombstone buffer: 1" << std::endl;
  
  // Insert keys 1 and 2
  std::cout << "\n=== Insert key 1 ===" << std::endl;
  index_key.SetFromInteger(1);
  rid.Set(0, 1);
  tree.Insert(index_key, rid);
  
  std::cout << "\n=== Insert key 2 ===" << std::endl;
  index_key.SetFromInteger(2);
  rid.Set(0, 2);
  tree.Insert(index_key, rid);
  
  // Verify both keys exist
  std::cout << "\n=== Verify both keys present ===" << std::endl;
  for (int i = 1; i <= 2; i++) {
    index_key.SetFromInteger(i);
    std::vector<RID> rids;
    bool found = tree.GetValue(index_key, &rids);
    std::cout << "Key " << i << " - Found: " << found << ", size: " << rids.size() << std::endl;
    EXPECT_TRUE(found);
    EXPECT_EQ(rids.size(), 1);
  }
  
  // Remove key 1 - should add to tombstone buffer
  std::cout << "\n=== Remove key 1 (should add to tombstone buffer) ===" << std::endl;
  index_key.SetFromInteger(1);
  tree.Remove(index_key);
  
  std::cout << "\n=== After removal - checking state ===" << std::endl;
  
  // Key 1 might still return true in GetValue if tombstone hasn't been processed
  // This depends on your GetValue implementation
  index_key.SetFromInteger(1);
  std::vector<RID> rids;
  bool found = tree.GetValue(index_key, &rids);
  std::cout << "Key 1 - Found: " << found 
            << " (might be true if tombstoned but not yet processed)" << std::endl;
  
  // Key 2 should definitely still be accessible
  index_key.SetFromInteger(2);
  rids.clear();
  found = tree.GetValue(index_key, &rids);
  std::cout << "Key 2 - Found: " << found << " (should be true)" << std::endl;
  EXPECT_TRUE(found);
  EXPECT_EQ(rids.size(), 1);
  
  // Try to remove key 1 again - should detect it's already marked
  std::cout << "\n=== Try to remove key 1 again ===" << std::endl;
  index_key.SetFromInteger(1);
  tree.Remove(index_key);
  std::cout << "Should see message: 'Key already marked'" << std::endl;
  
  delete bpm;
}

TEST(BPlusTreeTests, DISABLED_TombstoneProcessingAndEmptyRootTest) {
  auto key_schema = ParseCreateStatement("a bigint");
  GenericComparator<8> comparator(key_schema.get());
  auto disk_manager = std::make_unique<DiskManagerUnlimitedMemory>();
  auto *bpm = new BufferPoolManager(50, disk_manager.get());
  page_id_t page_id = bpm->NewPage();
  

  BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 1> tree("foo_pk", page_id, bpm, comparator, 3, 4);
  //                                                   ↑ Tombstone buffer = 1 (can only hold 1 tombstone)
  
  GenericKey<8> index_key;
  RID rid;

  std::cout << "=== Step 1: Insert keys 1, 2, 3 ===" << std::endl;
  for (int i = 1; i <= 3; i++) {
    index_key.SetFromInteger(i);
    rid.Set(0, i);
    std::cout << "Inserting key " << i << std::endl;
    tree.Insert(index_key, rid);
  }
  
  // Verify all keys present
  std::cout << "\n=== Verify all 3 keys present ===" << std::endl;
  for (int i = 1; i <= 3; i++) {
    index_key.SetFromInteger(i);
    std::vector<RID> rids;
    bool found = tree.GetValue(index_key, &rids);
    std::cout << "Key " << i << " - Found: " << found << std::endl;
    EXPECT_TRUE(found);
  }
  
  std::cout << "\n=== Step 2: Delete key 1 ===" << std::endl;
  index_key.SetFromInteger(1);
  tree.Remove(index_key);
  
  std::cout << "\n=== Step 3: Delete key 2 ===" << std::endl;
  index_key.SetFromInteger(2);
  tree.Remove(index_key);
  
  // Check state after 2 deletions
  std::cout << "\n=== After deleting keys 1 and 2 ===" << std::endl;
  index_key.SetFromInteger(1);
  std::vector<RID> rids;
  bool found = tree.GetValue(index_key, &rids);
  EXPECT_FALSE(found);
  
  index_key.SetFromInteger(2);
  rids.clear();
  found = tree.GetValue(index_key, &rids);
  EXPECT_TRUE(found);
  
  index_key.SetFromInteger(3);
  rids.clear();
  found = tree.GetValue(index_key, &rids);
  EXPECT_TRUE(found);
  
  std::cout<<"==============removing key 3===================="<< std::endl;
  index_key.SetFromInteger(3);
  tree.Remove(index_key);

  std::cout << "\n=== Step 5: Verify all keys deleted ===" << std::endl;
  for (int i = 1; i <= 3; i++) {
    index_key.SetFromInteger(i);
    std::vector<RID> rids_check;
    bool is_found = tree.GetValue(index_key, &rids_check);
    std::cout << "Key " << i << " - Found: " << is_found << " (should be FALSE)" << std::endl;
    EXPECT_FALSE(is_found);
  }
  
  std::cout << "\n=== Step 6: Check root page ID ===" << std::endl;
  auto root_id = tree.GetRootPageId();
  EXPECT_EQ(root_id, INVALID_PAGE_ID);
  
  std::cout << "\n=== TEST COMPLETE ===" << std::endl;
  
  delete bpm;
}

TEST(BPlusTreeTests, DISABLED_RedistributeTest) {
  auto key_schema = ParseCreateStatement("a bigint");
  GenericComparator<8> comparator(key_schema.get());
  auto disk_manager = std::make_unique<DiskManagerUnlimitedMemory>();
  auto *bpm = new BufferPoolManager(50, disk_manager.get());
  page_id_t page_id = bpm->NewPage();
  
  // Leaf max size = 3, Internal max size = 2, No tombstones
  BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 0> tree("foo_pk", page_id, bpm, comparator, 3, 2);
  GenericKey<8> index_key;
  RID rid;
  
  std::cout << "=== Step 1: Insert keys 1, 2, 3 ===" << std::endl;
  for (int i = 1; i <= 3; i++) {
    index_key.SetFromInteger(i);
    rid.Set(0, i);
    std::cout << "Inserting key " << i << std::endl;
    tree.Insert(index_key, rid);
  }
  
  std::cout << "\n=== Step 2: Insert keys 4, 5 ===" << std::endl;
  for (int i = 4; i <= 5; i++) {
    index_key.SetFromInteger(i);
    rid.Set(0, i);
    std::cout << "Inserting key " << i << std::endl;
    tree.Insert(index_key, rid);
  }
  
  std::cout << "\n=== Step 3: Delete key 5 ===" << std::endl;
  index_key.SetFromInteger(5);
  tree.Remove(index_key);
  
  // Store the key value BEFORE dropping the guard
  std::string separator_key_str;
  {
    auto root_id = tree.GetRootPageId();
    auto root_guard = bpm->WritePage(root_id);
    auto *root_page = root_guard.As<BPlusTreeInternalPage<GenericKey<8>, page_id_t, GenericComparator<8>>>();
    EXPECT_FALSE(root_page->IsLeafPage());
    
    // Compare the separator key
    GenericKey<8> expected_key;
    expected_key.SetFromInteger(3);
    EXPECT_EQ(comparator(root_page->KeyAt(1), expected_key), 0);
    
    // Save key for printing later
    separator_key_str = root_page->KeyAt(1).ToString();
  } // root_guard drops here - lock released!
  
  // NOW it's safe to print
  std::cout << "=== Tree Structure ===" << std::endl;
  std::cout << tree.DrawBPlusTree() << std::endl;
  std::cout << "Root separator key: " << separator_key_str << " (expected: 3)" << std::endl;
  std::cout << "\n=== TEST COMPLETE ===" << std::endl;
  
  delete bpm;
}

TEST(BPlusTreeTests, DISABLED_RedistributeTest_left) {
  auto key_schema = ParseCreateStatement("a bigint");
  GenericComparator<8> comparator(key_schema.get());
  auto disk_manager = std::make_unique<DiskManagerUnlimitedMemory>();
  auto *bpm = new BufferPoolManager(50, disk_manager.get());
  page_id_t page_id = bpm->NewPage();
  
  // Leaf max size = 3, Internal max size = 2, No tombstones
  BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 0> tree("foo_pk", page_id, bpm, comparator, 3, 4);
  GenericKey<8> index_key;
  RID rid;
  
  std::cout << "=== Step 1: Insert keys 1, 2, 3 ===" << std::endl;
  for (int i = 1; i <= 3; i++) {
    index_key.SetFromInteger(i);
    rid.Set(0, i);
    std::cout << "Inserting key " << i << std::endl;
    tree.Insert(index_key, rid);
  }
  
  std::cout << "\n=== Step 2: Insert keys 4, 5, 6 ===" << std::endl;
  for (int i = 4; i <= 6; i++) {
    index_key.SetFromInteger(i);
    rid.Set(0, i);
    std::cout << "Inserting key " << i << std::endl;
    tree.Insert(index_key, rid);
  }

  std::cout << "=== Tree Structure after inserting everything===" << std::endl;
  std::cout << tree.DrawBPlusTree() << std::endl;
  
  std::cout << "\n=== Step 3: Delete key 5 ===" << std::endl;
  index_key.SetFromInteger(5);
  tree.Remove(index_key);

  std::cout << "=== Tree Structure after deleting 5===" << std::endl;
  std::cout << tree.DrawBPlusTree() << std::endl;

  std::cout << "\n=== Step 3: Delete key 6 ===" << std::endl;
  index_key.SetFromInteger(6);
  tree.Remove(index_key);
  
  // Store the key value BEFORE dropping the guard
  std::string separator_key_str;
  {
    auto root_id = tree.GetRootPageId();
    auto root_guard = bpm->WritePage(root_id);
    auto *root_page = root_guard.As<BPlusTreeInternalPage<GenericKey<8>, page_id_t, GenericComparator<8>>>();
    EXPECT_FALSE(root_page->IsLeafPage());
    
    // Compare the separator key
    GenericKey<8> expected_key;
    expected_key.SetFromInteger(3);
    EXPECT_EQ(comparator(root_page->KeyAt(1), expected_key), 0);
    
    // Save key for printing later
    separator_key_str = root_page->KeyAt(1).ToString();
  } // root_guard drops here - lock released!
  
  // NOW it's safe to print
  std::cout << "=== Tree Structure ===" << std::endl;
  std::cout << tree.DrawBPlusTree() << std::endl;
  std::cout << "Root separator key: " << separator_key_str << " (expected: 3)" << std::endl;
  std::cout << "\n=== TEST COMPLETE ===" << std::endl;
  
  delete bpm;
}

TEST(BPlusTreeTests, DISABLED_promoteToRoot) {
  auto key_schema = ParseCreateStatement("a bigint");
  GenericComparator<8> comparator(key_schema.get());
  auto disk_manager = std::make_unique<DiskManagerUnlimitedMemory>();
  auto *bpm = new BufferPoolManager(50, disk_manager.get());
  page_id_t page_id = bpm->NewPage();
  
  // Leaf max size = 3, Internal max size = 2, No tombstones
  BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 0> tree("foo_pk", page_id, bpm, comparator, 2, 3);
  GenericKey<8> index_key;
  RID rid;
  
  std::cout << "=== Step 1: Insert keys 1, 2, 3, 4===" << std::endl;
  for (int i = 1; i <= 2; i++) {
    index_key.SetFromInteger(i);
    rid.Set(0, i);
    std::cout << "Inserting key " << i << std::endl;
    tree.Insert(index_key, rid);
  }

  std::cout << "=== Tree Structure after inserting everything===" << std::endl;
  std::cout << tree.DrawBPlusTree() << std::endl;

  std::cout << "\n=== Step 2: Insert keys 3===" << std::endl;

  index_key.SetFromInteger(3);
  rid.Set(0, 3);
  std::cout << "Inserting key " << 3 << std::endl;
  tree.Insert(index_key, rid);

  std::cout << "=== Tree Structure after inserting everything===" << std::endl;
  std::cout << tree.DrawBPlusTree() << std::endl;

  index_key.SetFromInteger(4);
  rid.Set(0, 4);
  std::cout << "Inserting key " << 4 << std::endl;
  tree.Insert(index_key, rid);

  std::cout << "=== Tree Structure after inserting everything===" << std::endl;
  std::cout << tree.DrawBPlusTree() << std::endl;

  std::cout << "\n=== Step 3: Delete key 3 ===" << std::endl;
  index_key.SetFromInteger(3);
  tree.Remove(index_key);

  std::cout << "=== Tree Structure after inserting everything===" << std::endl;
  std::cout << tree.DrawBPlusTree() << std::endl;

  std::cout << "\n=== Step 3: Delete key 2 ===" << std::endl;
  index_key.SetFromInteger(2);
  tree.Remove(index_key);

  std::cout << "=== Tree Structure after inserting everything===" << std::endl;
  std::cout << tree.DrawBPlusTree() << std::endl;
  
  std::cout << "\n=== TEST COMPLETE ===" << std::endl;
  
  delete bpm;
}

TEST(BPlusTreeTests, DeleteTestNoIterator) {
  // create KeyComparator and index schema
  auto key_schema = ParseCreateStatement("a bigint");
  GenericComparator<8> comparator(key_schema.get());

  auto disk_manager = std::make_unique<DiskManagerUnlimitedMemory>();
  auto *bpm = new BufferPoolManager(50, disk_manager.get());
  // allocate header_page
  page_id_t page_id = bpm->NewPage();
  // create b+ tree
  BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("foo_pk", page_id, bpm, comparator, 2, 3);
  GenericKey<8> index_key;
  RID rid;

  // std::vector<int64_t> keys = {1, 2, 3, 4, 5};
  std::vector<int64_t> keys = {1, 2, 3, 4, 5};
  for (auto key : keys) {
    int64_t value = key & 0xFFFFFFFF;
    rid.Set(static_cast<int32_t>(key >> 32), value);
    index_key.SetFromInteger(key);
    tree.Insert(index_key, rid);
  }

  std::vector<RID> rids;
  for (auto key : keys) {
    rids.clear();
    index_key.SetFromInteger(key);
    tree.GetValue(index_key, &rids);
    EXPECT_EQ(rids.size(), 1);

    int64_t value = key & 0xFFFFFFFF;
    EXPECT_EQ(rids[0].GetSlotNum(), value);
  }

  std::cout << "=== Tree Structure after inserting everything===" << std::endl;
  std::cout << tree.DrawBPlusTree() << std::endl;

  // // std::vector<int64_t> remove_keys = {1, 5, 3, 4};
  // // std::vector<int64_t> remove_keys = {1, 5};
  // // for (auto key : remove_keys) {
  // //   std::cout<<"[TEST] starting removal"<<key<<std::endl;
  // //   index_key.SetFromInteger(key);
  // //   tree.Remove(index_key);
  // // }

  std::cout<<"[TEST] starting removal 1"<<std::endl;
  index_key.SetFromInteger(1);
  tree.Remove(index_key);

  std::cout << "=== Tree Structure after inserting everything===" << std::endl;
  std::cout << tree.DrawBPlusTree() << std::endl;

  // std::cout<<"[TEST] starting removal 5"<<std::endl;
  // index_key.SetFromInteger(5);
  // tree.Remove(index_key);

  // std::cout << "=== Tree Structure after inserting everything===" << std::endl;
  // std::cout << tree.DrawBPlusTree() << std::endl;

  // std::cout<<"[TEST] starting removal 3"<<std::endl;
  // index_key.SetFromInteger(3);
  // tree.Remove(index_key);

  // std::cout << "=== Tree Structure after inserting everything===" << std::endl;
  // std::cout << tree.DrawBPlusTree() << std::endl;

  // std::cout<<"[TEST] starting removal 4"<<std::endl;
  // index_key.SetFromInteger(4);
  // tree.Remove(index_key);

  // std::cout << "=== Tree Structure after inserting everything===" << std::endl;
  // std::cout << tree.DrawBPlusTree() << std::endl;

  // int64_t size = 0;
  // bool is_present;

  // for (auto key : keys) {
  //   rids.clear();
  //   index_key.SetFromInteger(key);
  //   is_present = tree.GetValue(index_key, &rids);

  //   if (!is_present) {
  //     EXPECT_NE(std::find(remove_keys.begin(), remove_keys.end(), key), remove_keys.end());
  //   } else {
  //     EXPECT_EQ(rids.size(), 1);
  //     EXPECT_EQ(rids[0].GetPageId(), 0);
  //     EXPECT_EQ(rids[0].GetSlotNum(), key);
  //     ++size;
  //   }
  // }
  // EXPECT_EQ(size, 1);

  // // Remove the remaining key
  // index_key.SetFromInteger(2);
  // tree.Remove(index_key);
  // auto root_page_id = tree.GetRootPageId();
  // ASSERT_EQ(root_page_id, INVALID_PAGE_ID);

  delete bpm;
}

TEST(BPlusTreeTests, DISABLED_OptimisticDeleteTest) {
  auto key_schema = ParseCreateStatement("a bigint");
  GenericComparator<8> comparator(key_schema.get());

  auto disk_manager = std::make_unique<DiskManagerUnlimitedMemory>();
  auto *bpm = new BufferPoolManager(50, disk_manager.get());
  // allocate header_page
  page_id_t page_id = bpm->NewPage();
  // create b+ tree
  BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("foo_pk", page_id, bpm, comparator, 4, 3);
  GenericKey<8> index_key;
  RID rid;

  size_t num_keys = 25;
  for (size_t i = 0; i < num_keys; i++) {
    int64_t value = i & 0xFFFFFFFF;
    rid.Set(static_cast<int32_t>(i >> 32), value);
    index_key.SetFromInteger(i);
    tree.Insert(index_key, rid);
  }

  size_t to_delete = num_keys + 1;
  auto leaf = IndexLeaves<GenericKey<8>, RID, GenericComparator<8>>(tree.GetRootPageId(), bpm);
  while (leaf.Valid()) {
    if ((*leaf)->GetSize() > (*leaf)->GetMinSize()) {
      to_delete = (*leaf)->KeyAt(0).GetAsInteger();
    }
    ++leaf;
  }

  auto base_reads = tree.bpm_->GetReads();
  auto base_writes = tree.bpm_->GetWrites();

  index_key.SetFromInteger(to_delete);
  tree.Remove(index_key);

  auto new_reads = tree.bpm_->GetReads();
  auto new_writes = tree.bpm_->GetWrites();

  EXPECT_GT(new_reads - base_reads, 0);
  EXPECT_EQ(new_writes - base_writes, 1);

  delete bpm;
}

TEST(BPlusTreeTests, DISABLED_SequentialEdgeMixTest) {  // NOLINT
  // create KeyComparator and index schema
  auto key_schema = ParseCreateStatement("a bigint");
  GenericComparator<8> comparator(key_schema.get());

  auto disk_manager = std::make_unique<DiskManagerUnlimitedMemory>();
  auto *bpm = new BufferPoolManager(50, disk_manager.get());

  for (int leaf_max_size = 2; leaf_max_size <= 5; leaf_max_size++) {
    // create and fetch header_page
    page_id_t page_id = bpm->NewPage();

    // create b+ tree
    BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 2> tree("foo_pk", page_id, bpm, comparator, leaf_max_size, 3);
    GenericKey<8> index_key;
    RID rid;

    std::vector<int64_t> keys = {1, 5, 15, 20, 25, 2, -1, -2, 6, 14, 4};
    std::vector<int64_t> inserted = {};
    std::vector<int64_t> deleted = {};
    for (auto key : keys) {
      int64_t value = key & 0xFFFFFFFF;
      rid.Set(static_cast<int32_t>(key >> 32), value);
      index_key.SetFromInteger(key);
      tree.Insert(index_key, rid);
      inserted.push_back(key);
      auto res = TreeValuesMatch<GenericKey<8>, RID, GenericComparator<8>, 2>(tree, inserted, deleted);
      ASSERT_TRUE(res);
    }

    index_key.SetFromInteger(1);
    tree.Remove(index_key);
    deleted.push_back(1);
    inserted.erase(std::find(inserted.begin(), inserted.end(), 1));
    auto res = TreeValuesMatch<GenericKey<8>, RID, GenericComparator<8>, 2>(tree, inserted, deleted);
    ASSERT_TRUE(res);

    index_key.SetFromInteger(3);
    rid.Set(3, 3);
    tree.Insert(index_key, rid);
    inserted.push_back(3);
    res = TreeValuesMatch<GenericKey<8>, RID, GenericComparator<8>, 2>(tree, inserted, deleted);
    ASSERT_TRUE(res);

    keys = {4, 14, 6, 2, 15, -2, -1, 3, 5, 25, 20};
    for (auto key : keys) {
      index_key.SetFromInteger(key);
      tree.Remove(index_key);
      deleted.push_back(key);
      inserted.erase(std::find(inserted.begin(), inserted.end(), key));
      res = TreeValuesMatch<GenericKey<8>, RID, GenericComparator<8>, 2>(tree, inserted, deleted);
      ASSERT_TRUE(res);
    }
  }

  delete bpm;
}
}  // namespace bustub
