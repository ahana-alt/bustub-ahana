//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// seq_scan_executor.cpp
//
// Identification: src/execution/seq_scan_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/seq_scan_executor.h"
#include "common/macros.h"

namespace bustub {

/**
 * Construct a new SeqScanExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The sequential scan plan to be executed
 */
SeqScanExecutor::SeqScanExecutor(ExecutorContext *exec_ctx, const SeqScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan), table_info_(nullptr) {}

/** Initialize the sequential scan */
void SeqScanExecutor::Init() {
  auto catalog = exec_ctx_->GetCatalog();
  table_info_ = catalog->GetTable(plan_->GetTableOid());

  // Create an iterator at the beginning of the table
  table_iterator_ = std::make_unique<TableIterator>(table_info_->table_->MakeIterator());
}

/**
 * Yield the next tuple batch from the seq scan.
 * @param[out] tuple_batch The next tuple batch produced by the scan
 * @param[out] rid_batch The next tuple RID batch produced by the scan
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 */
auto SeqScanExecutor::Next(std::vector<Tuple> *tuple_batch, std::vector<RID> *rid_batch, size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();

  while (!table_iterator_->IsEnd() && tuple_batch->size() < batch_size) {
    // Get the current tuple and its metadata
    auto [tuple_meta, current_tuple] = table_iterator_->GetTuple();
    auto rid = table_iterator_->GetRID();

    // Move to the next tuple
    ++(*table_iterator_);

    // Skip deleted tuples
    if (tuple_meta.is_deleted_) {
      continue;
    }

    // Check if there's a filter predicate
    auto filter_predicate = plan_->filter_predicate_;
    if (filter_predicate != nullptr) {
      // Evaluate the filter predicate on the current tuple
      auto value = filter_predicate->Evaluate(&current_tuple, plan_->OutputSchema());

      // Skip tuples that don't match the filter
      if (value.IsNull() || !value.GetAs<bool>()) {
        continue;
      }
    }

    // Add tuple to the batch
    tuple_batch->push_back(current_tuple);
    rid_batch->push_back(rid);
  }

  // Return true if we got at least one tuple
  return !tuple_batch->empty();
}

}  // namespace bustub
