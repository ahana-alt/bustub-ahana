//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// update_executor.cpp
//
// Identification: src/execution/update_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include "execution/executors/update_executor.h"
#include <memory>
#include "common/macros.h"

namespace bustub {

/**
 * Construct a new UpdateExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The update plan to be executed
 * @param child_executor The child executor that feeds the update
 */
UpdateExecutor::UpdateExecutor(ExecutorContext *exec_ctx, const UpdatePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)), table_info_(nullptr) {}

/** Initialize the update */
void UpdateExecutor::Init() {
  // Get table metadata from catalog
  auto catalog = exec_ctx_->GetCatalog();
  table_info_ = catalog->GetTable(plan_->GetTableOid());

  // Initialize child executor
  child_executor_->Init();

  // Reset returned flag
  returned_ = false;
}

/**
 * Yield the number of rows updated in the table.
 * @param[out] tuple_batch The tuple batch with one integer indicating the number of rows updated in the table
 * @param[out] rid_batch The next tuple RID batch produced by the update (ignore, not used)
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 *
 * NOTE: UpdateExecutor::Next() does not use the `rid_batch` out-parameter.
 * NOTE: UpdateExecutor::Next() returns true with the number of updated rows produced only once.
 */
auto UpdateExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                          size_t batch_size) -> bool {
  // Return false if we've already returned the result
  if (returned_) {
    return false;
  }

  tuple_batch->clear();
  rid_batch->clear();

  // Get catalog to access indexes
  auto catalog = exec_ctx_->GetCatalog();

  // Get all indexes for this table
  auto indexes = catalog->GetTableIndexes(table_info_->name_);

  // IMPORTANT: Collect all tuples first to avoid deadlock
  // We must finish iterating before we start modifying table/indexes
  std::vector<std::pair<Tuple, RID>> tuples_to_update;

  std::vector<Tuple> child_batch;
  std::vector<RID> child_rid_batch;
  while (child_executor_->Next(&child_batch, &child_rid_batch, BUSTUB_BATCH_SIZE)) {
    for (size_t i = 0; i < child_batch.size(); i++) {
      tuples_to_update.emplace_back(child_batch[i], child_rid_batch[i]);
    }
  }

  // Counter for updated rows
  int update_count = 0;

  // Now process all updates
  for (const auto &[old_tuple, old_rid] : tuples_to_update) {
    // Step 1: Evaluate target expressions to create the new tuple
    std::vector<Value> new_values;
    new_values.reserve(plan_->target_expressions_.size());
    for (const auto &expr : plan_->target_expressions_) {
      new_values.push_back(expr->Evaluate(&old_tuple, child_executor_->GetOutputSchema()));
    }

    // Create the new tuple with updated values
    Tuple new_tuple(new_values, &table_info_->schema_);

    // Step 2: Mark the old tuple as deleted
    TupleMeta delete_meta;
    delete_meta.is_deleted_ = true;
    table_info_->table_->UpdateTupleMeta(delete_meta, old_rid);

    // Step 3: Insert the new tuple
    TupleMeta insert_meta;
    insert_meta.is_deleted_ = false;
    auto new_rid = table_info_->table_->InsertTuple(insert_meta, new_tuple);

    // Step 4: Update indexes if insertion was successful
    if (new_rid.has_value()) {
      for (const auto &index_info : indexes) {
        // Delete the old index entry
        auto old_key =
            old_tuple.KeyFromTuple(table_info_->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs());
        index_info->index_->DeleteEntry(old_key, old_rid, exec_ctx_->GetTransaction());

        // Insert the new index entry
        auto new_key =
            new_tuple.KeyFromTuple(table_info_->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs());
        index_info->index_->InsertEntry(new_key, new_rid.value(), exec_ctx_->GetTransaction());
      }
      update_count++;
    }
  }

  // Create the output tuple with the count
  std::vector<Value> values;
  values.emplace_back(TypeId::INTEGER, update_count);
  tuple_batch->emplace_back(values, &GetOutputSchema());
  rid_batch->emplace_back();  // Empty RID (not used)

  // Mark that we've returned the result
  returned_ = true;
  return true;
}

}  // namespace bustub
