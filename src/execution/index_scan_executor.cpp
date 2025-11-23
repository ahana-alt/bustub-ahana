//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// index_scan_executor.cpp
//
// Identification: src/execution/index_scan_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include "execution/executors/index_scan_executor.h"
#include "common/macros.h"

namespace bustub {

/**
 * Creates a new index scan executor.
 * @param exec_ctx the executor context
 * @param plan the index scan plan to be executed
 */
IndexScanExecutor::IndexScanExecutor(ExecutorContext *exec_ctx, const IndexScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan), table_info_(nullptr), index_info_(nullptr), index_iterator_(nullptr) {}

void IndexScanExecutor::Init() {
  // Get catalog
  auto catalog = exec_ctx_->GetCatalog();

  // Get index information
  index_info_ = catalog->GetIndex(plan_->GetIndexOid());

  // Get table information using the table name from index
  table_info_ = catalog->GetTable(index_info_->table_name_);

  // Cast the index to B+ tree index for two integer columns
  tree_ = dynamic_cast<BPlusTreeIndexForTwoIntegerColumn *>(index_info_->index_.get());

  // Create an iterator starting at the beginning of the index
  // This works for both point lookup and ordered scan
  index_iterator_ = std::make_unique<BPlusTreeIndexIteratorForTwoIntegerColumn>(tree_->GetBeginIterator());
}

auto IndexScanExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                             size_t batch_size) -> bool {
  // Clear output vectors
  tuple_batch->clear();
  rid_batch->clear();

  // Iterate through the index entries until we fill the batch
  while (tuple_batch->size() < batch_size && index_iterator_ != nullptr && !index_iterator_->IsEnd()) {
    // Get the current key-value pair from the index
    auto entry = **index_iterator_;
    auto current_rid = entry.second;

    // Move to the next index entry
    ++(*index_iterator_);

    // Fetch the tuple from the table using the RID
    auto [tuple_meta, table_tuple] = table_info_->table_->GetTuple(current_rid);

    // Skip deleted tuples
    if (tuple_meta.is_deleted_) {
      continue;
    }

    // Evaluate the filter predicate if it exists
    if (plan_->filter_predicate_ != nullptr) {
      auto value = plan_->filter_predicate_->Evaluate(&table_tuple, table_info_->schema_);

      // Skip tuples that don't match the filter
      if (value.IsNull() || !value.GetAs<bool>()) {
        continue;
      }
    }

    // Add the tuple and RID to the batch
    tuple_batch->push_back(table_tuple);
    rid_batch->push_back(current_rid);
  }

  // Return true if we have any tuples in the batch
  return !tuple_batch->empty();
}

}  // namespace bustub
