//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// delete_executor.cpp
//
// Identification: src/execution/delete_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>
#include <vector>

#include "common/macros.h"
#include "execution/executors/delete_executor.h"

namespace bustub {

/**
 * Construct a new DeleteExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The delete plan to be executed
 * @param child_executor The child executor that feeds the delete
 */
DeleteExecutor::DeleteExecutor(ExecutorContext *exec_ctx, const DeletePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)), table_info_(nullptr) {}

/** Initialize the delete */
void DeleteExecutor::Init() {
  // Get table metadata from catalog
  auto catalog = exec_ctx_->GetCatalog();
  table_info_ = catalog->GetTable(plan_->GetTableOid());

  // Initialize child executor
  child_executor_->Init();

  // Reset returned flag
  returned_ = false;
}

/**
 * Yield the number of rows deleted from the table.
 * @param[out] tuple_batch The tuple batch with one integer indicating the number of rows deleted from the table
 * @param[out] rid_batch The next tuple RID batch produced by the delete (ignore, not used)
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 *
 * NOTE: DeleteExecutor::Next() does not use the `rid_batch` out-parameter.
 * NOTE: DeleteExecutor::Next() returns true with the number of deleted rows produced only once.
 */
auto DeleteExecutor::Next(std::vector<Tuple> *tuple_batch, std::vector<RID> *rid_batch, size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();

  // Return false if we've already returned the result
  if (returned_) {
    return false;
  }

  // Collect all tuples first to avoid deadlock
  std::vector<std::pair<Tuple, RID>> tuples_to_delete;

  std::vector<Tuple> child_batch;
  std::vector<RID> child_rid_batch;

  while (child_executor_->Next(&child_batch, &child_rid_batch, BUSTUB_BATCH_SIZE)) {
    for (size_t i = 0; i < child_batch.size(); i++) {
      tuples_to_delete.emplace_back(child_batch[i], child_rid_batch[i]);
    }
  }

  // CRITICAL: Destroy the child executor to release all latches
  child_executor_.reset();

  // Get catalog to access indexes
  auto catalog = exec_ctx_->GetCatalog();

  // Get all indexes for this table
  auto indexes = catalog->GetTableIndexes(table_info_->name_);

  // Counter for deleted rows
  int delete_count = 0;

  // Now process all deletions
  for (const auto &[tuple_to_delete, rid_to_delete] : tuples_to_delete) {
    // Mark the tuple as deleted
    TupleMeta delete_meta;
    delete_meta.is_deleted_ = true;
    table_info_->table_->UpdateTupleMeta(delete_meta, rid_to_delete);

    // Update all indexes for this table
    for (const auto &index_info : indexes) {
      // Extract the key from the deleted tuple
      auto key_tuple = tuple_to_delete.KeyFromTuple(table_info_->schema_, index_info->key_schema_,
                                                    index_info->index_->GetKeyAttrs());

      // Delete the entry from the index
      index_info->index_->DeleteEntry(key_tuple, rid_to_delete, exec_ctx_->GetTransaction());
    }

    delete_count++;
  }

  // Create the output tuple with the count
  std::vector<Value> values;
  values.emplace_back(TypeId::INTEGER, delete_count);
  tuple_batch->push_back(Tuple(values, &GetOutputSchema()));

  // Mark that we've returned the result
  returned_ = true;

  return true;
}

}  // namespace bustub
