//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_index_join_executor.cpp
//
// Identification: src/execution/nested_index_join_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/nested_index_join_executor.h"
#include "common/macros.h"
#include "type/value_factory.h"

namespace bustub {

/**
 * Creates a new nested index join executor.
 * @param exec_ctx the context that the nested index join should be performed in
 * @param plan the nested index join plan to be executed
 * @param child_executor the outer table
 */
NestedIndexJoinExecutor::NestedIndexJoinExecutor(ExecutorContext *exec_ctx, const NestedIndexJoinPlanNode *plan,
                                                 std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {
  if (plan->GetJoinType() != JoinType::LEFT && plan->GetJoinType() != JoinType::INNER) {
    // Note for Spring 2025: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

void NestedIndexJoinExecutor::Init() {
  // Initialize the child executor (outer table)
  child_executor_->Init();

  // Reset state
  has_outer_tuple_ = false;
  inner_rids_.clear();
  inner_rid_idx_ = 0;
}

auto NestedIndexJoinExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                                   size_t batch_size) -> bool {
  auto catalog = exec_ctx_->GetCatalog();
  auto index_info = catalog->GetIndex(plan_->GetIndexOid());
  auto inner_table_info = catalog->GetTable(plan_->GetInnerTableOid());

  tuple_batch->clear();
  rid_batch->clear();

  while (tuple_batch->size() < batch_size) {
    // If we've exhausted inner tuples for current outer tuple, get next outer tuple
    if (!has_outer_tuple_ || inner_rid_idx_ >= inner_rids_.size()) {
      // Try to get next outer tuple
      std::vector<Tuple> temp_batch;
      std::vector<RID> temp_rid_batch;
      if (!child_executor_->Next(&temp_batch, &temp_rid_batch, 1) || temp_batch.empty()) {
        // No more outer tuples
        return !tuple_batch->empty();
      }

      outer_tuple_ = temp_batch[0];
      outer_rid_ = temp_rid_batch[0];
      has_outer_tuple_ = true;

      // Construct the probe key from the outer tuple using key_predicate
      auto key_value = plan_->KeyPredicate()->Evaluate(&outer_tuple_, child_executor_->GetOutputSchema());

      // Create the probe tuple for the index
      std::vector<Value> probe_key_values{key_value};
      auto probe_key = Tuple(probe_key_values, &index_info->key_schema_);

      // Look up matching RIDs in the index
      inner_rids_.clear();
      index_info->index_->ScanKey(probe_key, &inner_rids_, exec_ctx_->GetTransaction());
      inner_rid_idx_ = 0;

      // Handle LEFT JOIN case: if no matching inner tuples found
      if (plan_->GetJoinType() == JoinType::LEFT && inner_rids_.empty()) {
        // Emit outer tuple with NULL values for inner columns
        std::vector<Value> values;

        // Add all outer tuple values
        for (uint32_t i = 0; i < child_executor_->GetOutputSchema().GetColumnCount(); i++) {
          values.push_back(outer_tuple_.GetValue(&child_executor_->GetOutputSchema(), i));
        }

        // Add NULL values for all inner tuple columns
        for (uint32_t i = 0; i < inner_table_info->schema_.GetColumnCount(); i++) {
          values.push_back(ValueFactory::GetNullValueByType(inner_table_info->schema_.GetColumn(i).GetType()));
        }

        tuple_batch->emplace_back(values, &plan_->OutputSchema());
        rid_batch->push_back(outer_rid_);
        continue;
      }

      // If no inner tuples and INNER JOIN, continue to next outer tuple
      if (inner_rids_.empty()) {
        continue;
      }
    }

    // Get the next inner tuple
    auto inner_rid = inner_rids_[inner_rid_idx_++];
    auto inner_tuple_pair = inner_table_info->table_->GetTuple(inner_rid);

    // Check if tuple was deleted (tombstone)
    if (inner_tuple_pair.first.is_deleted_) {
      continue;  // Skip deleted tuples
    }

    auto inner_tuple = inner_tuple_pair.second;

    // Construct the output tuple: outer columns + inner columns
    std::vector<Value> values;

    // Add all outer tuple values
    for (uint32_t i = 0; i < child_executor_->GetOutputSchema().GetColumnCount(); i++) {
      values.push_back(outer_tuple_.GetValue(&child_executor_->GetOutputSchema(), i));
    }

    // Add all inner tuple values
    for (uint32_t i = 0; i < inner_table_info->schema_.GetColumnCount(); i++) {
      values.push_back(inner_tuple.GetValue(&inner_table_info->schema_, i));
    }

    tuple_batch->emplace_back(values, &plan_->OutputSchema());
    rid_batch->push_back(outer_rid_);
  }

  return !tuple_batch->empty();
}

}  // namespace bustub
