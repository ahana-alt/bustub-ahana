//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// aggregation_executor.cpp
//
// Identification: src/execution/aggregation_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include <memory>

#include "common/macros.h"
#include "execution/executors/aggregation_executor.h"

namespace bustub {

/**
 * Construct a new AggregationExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The insert plan to be executed
 * @param child_executor The child executor from which inserted tuples are pulled (may be `nullptr`)
 */
AggregationExecutor::AggregationExecutor(ExecutorContext *exec_ctx, const AggregationPlanNode *plan,
                                         std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      child_executor_(std::move(child_executor)),
      aht_(plan->GetAggregates(), plan->GetAggregateTypes()),
      aht_iterator_(aht_.Begin()) {}

/** Initialize the aggregation */
void AggregationExecutor::Init() {
  // Initialize child executor
  child_executor_->Init();

  // Clear hash table for re-initialization
  aht_.Clear();
  empty_result_returned_ = false;

  // Build phase: consume all tuples from child and populate hash table
  Tuple child_tuple;
  RID child_rid;
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    auto agg_key = MakeAggregateKey(&child_tuple);
    auto agg_val = MakeAggregateValue(&child_tuple);
    aht_.InsertCombine(agg_key, agg_val);
  }

  // Reset iterator to beginning
  aht_iterator_ = aht_.Begin();
}

/**
 * Yield the next tuple from the insert.
 * @param[out] tuple The next tuple produced by the aggregation
 * @param[out] rid The next tuple RID produced by the aggregation
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 */
auto AggregationExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  // Iterate through hash table results
  if (aht_iterator_ != aht_.End()) {
    // Construct output tuple: group-by keys followed by aggregate values
    std::vector<Value> values;
    for (const auto &group_by_val : aht_iterator_.Key().group_bys_) {
      values.push_back(group_by_val);
    }
    for (const auto &agg_val : aht_iterator_.Val().aggregates_) {
      values.push_back(agg_val);
    }

    *tuple = Tuple(values, &GetOutputSchema());
    ++aht_iterator_;
    return true;
  }

  // Special case: no GROUP BY and empty input
  // Return one tuple with initial aggregate values (COUNT(*) = 0, others = NULL)
  // Only do this if the hash table is actually empty (Begin == End)
  if (plan_->GetGroupBys().empty() && aht_.Begin() == aht_.End() && !empty_result_returned_) {
    auto initial_val = aht_.GenerateInitialAggregateValue();
    std::vector<Value> values;
    values.reserve(aht_iterator_.Val().aggregates_.size());
    for (const auto &agg_val : initial_val.aggregates_) {
      values.push_back(agg_val);
    }
    *tuple = Tuple(values, &GetOutputSchema());
    empty_result_returned_ = true;
    return true;
  }

  return false;
}

/** Do not use or remove this function; otherwise, you will get zero points. */
auto AggregationExecutor::GetChildExecutor() const -> const AbstractExecutor * { return child_executor_.get(); }

}  // namespace bustub
