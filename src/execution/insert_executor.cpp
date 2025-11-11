//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// insert_executor.cpp
//
// Identification: src/execution/insert_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>
#include "common/macros.h"

#include "execution/executors/insert_executor.h"

namespace bustub {

/**
 * Construct a new InsertExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The insert plan to be executed
 * @param child_executor The child executor from which inserted tuples are pulled
 */
InsertExecutor::InsertExecutor(ExecutorContext *exec_ctx, const InsertPlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)), table_info_(nullptr) {}

/** Initialize the insert */
void InsertExecutor::Init() {
  // Get the table metadata from the catalog
  auto catalog = exec_ctx_->GetCatalog();
  table_info_ = catalog->GetTable(plan_->GetTableOid());

  // Initialize the child executor
  child_executor_->Init();

  // Reset the returned flag
  returned_ = false;
}

/**
 * Yield the number of rows inserted into the table.
 * @param[out] tuple The integer tuple indicating the number of rows inserted into the table
 * @param[out] rid The next tuple RID produced by the insert (ignore, not used)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 *
 * NOTE: InsertExecutor::Next() does not use the `rid` out-parameter.
 * NOTE: InsertExecutor::Next() returns true with number of inserted rows produced only once.
 */
auto InsertExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool {
  // Return false if we've already returned the result
  if (returned_) {
    return false;
  }

  // Get catalog to access indexes
  auto catalog = exec_ctx_->GetCatalog();

  // Get all indexes for this table
  auto indexes = catalog->GetTableIndexes(table_info_->name_);

  // Collect all tuples first
  std::vector<Tuple> tuples_to_insert;

  Tuple child_tuple;
  RID child_rid;

  while (child_executor_->Next(&child_tuple, &child_rid)) {
    tuples_to_insert.push_back(child_tuple);
  }

  // Counter for inserted rows
  int insert_count = 0;

  // Now process all insertions
  for (const auto &tuple_to_insert : tuples_to_insert) {
    // Create tuple metadata (not deleted)
    TupleMeta tuple_meta;
    tuple_meta.is_deleted_ = false;

    // Insert the tuple into the table
    auto inserted_rid = table_info_->table_->InsertTuple(tuple_meta, tuple_to_insert);

    // Check if insertion was successful
    if (inserted_rid.has_value()) {
      // Update all indexes for this table
      for (const auto &index_info : indexes) {
        // Extract the key from the inserted tuple
        auto key_tuple = tuple_to_insert.KeyFromTuple(table_info_->schema_, index_info->key_schema_,
                                                      index_info->index_->GetKeyAttrs());

        // Insert the entry into the index
        index_info->index_->InsertEntry(key_tuple, inserted_rid.value(), exec_ctx_->GetTransaction());
      }

      insert_count++;
    }
  }

  // Create the output tuple with the count
  std::vector<Value> values;
  values.emplace_back(TypeId::INTEGER, insert_count);
  *tuple = Tuple(values, &GetOutputSchema());

  // Mark that we've returned the result
  returned_ = true;

  return true;
}

}  // namespace bustub
