//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_loop_join_executor.cpp
//
// Identification: src/execution/nested_loop_join_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include "execution/executors/nested_loop_join_executor.h"
#include "binder/table_ref/bound_join_ref.h"
#include "common/exception.h"
#include "common/macros.h"
#include "type/value_factory.h"

namespace bustub {

/**
 * Construct a new NestedLoopJoinExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The nested loop join plan to be executed
 * @param left_executor The child executor that produces tuple for the left side of join
 * @param right_executor The child executor that produces tuple for the right side of join
 */
NestedLoopJoinExecutor::NestedLoopJoinExecutor(ExecutorContext *exec_ctx, const NestedLoopJoinPlanNode *plan,
                                               std::unique_ptr<AbstractExecutor> &&left_executor,
                                               std::unique_ptr<AbstractExecutor> &&right_executor)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      left_executor_(std::move(left_executor)),
      right_executor_(std::move(right_executor)) {
  if (plan->GetJoinType() != JoinType::LEFT && plan->GetJoinType() != JoinType::INNER) {
    // Note for Spring 2025: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

/** Initialize the join */
void NestedLoopJoinExecutor::Init() {
  // Initialize both child executors
  left_executor_->Init();
  right_executor_->Init();

  // Reset state
  has_left_tuple_ = false;
  left_matched_ = false;
}

/**
 * Yield the next tuple from the join.
 * @param[out] tuple The next tuple produced by the join
 * @param[out] rid The next tuple RID produced, not used by nested loop join.
 * @return `true` if a tuple was produced, `false` if there are no more tuples.
 */
auto NestedLoopJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  while (true) {
    // If we don't have a left tuple, get one
    if (!has_left_tuple_) {
      if (!left_executor_->Next(&left_tuple_, &left_rid_)) {
        return false;  // No more left tuples
      }
      has_left_tuple_ = true;
      left_matched_ = false;

      // Reinitialize right executor for the new left tuple
      right_executor_->Init();
    }

    // Try to get a right tuple
    Tuple right_tuple;
    RID right_rid;

    if (right_executor_->Next(&right_tuple, &right_rid)) {
      // We have both left and right tuples, check the join predicate

      // Construct a combined tuple for predicate evaluation
      // The predicate expects: left columns followed by right columns
      std::vector<Value> values;

      // Add all left tuple values
      for (uint32_t i = 0; i < left_executor_->GetOutputSchema().GetColumnCount(); i++) {
        values.push_back(left_tuple_.GetValue(&left_executor_->GetOutputSchema(), i));
      }

      // Add all right tuple values
      for (uint32_t i = 0; i < right_executor_->GetOutputSchema().GetColumnCount(); i++) {
        values.push_back(right_tuple.GetValue(&right_executor_->GetOutputSchema(), i));
      }

      // Create a combined tuple for predicate evaluation
      auto left_schema = left_executor_->GetOutputSchema();
      auto right_schema = right_executor_->GetOutputSchema();

      // Evaluate the predicate
      auto predicate_result = plan_->Predicate()->EvaluateJoin(&left_tuple_, left_schema, &right_tuple, right_schema);

      if (!predicate_result.IsNull() && predicate_result.GetAs<bool>()) {
        // Predicate evaluates to true, emit the joined tuple
        *tuple = Tuple(values, &plan_->OutputSchema());
        *rid = left_rid_;  // Use left RID
        left_matched_ = true;
        return true;
      }

      // Predicate was false, continue to next right tuple
      continue;
    }

    // No more right tuples for current left tuple
    // For LEFT JOIN, if no match was found, emit left tuple with NULLs for right
    if (plan_->GetJoinType() == JoinType::LEFT && !left_matched_) {
      std::vector<Value> values;

      // Add all left tuple values
      for (uint32_t i = 0; i < left_executor_->GetOutputSchema().GetColumnCount(); i++) {
        values.push_back(left_tuple_.GetValue(&left_executor_->GetOutputSchema(), i));
      }

      // Add NULL values for all right tuple columns
      for (uint32_t i = 0; i < right_executor_->GetOutputSchema().GetColumnCount(); i++) {
        values.push_back(ValueFactory::GetNullValueByType(right_executor_->GetOutputSchema().GetColumn(i).GetType()));
      }

      *tuple = Tuple(values, &plan_->OutputSchema());
      *rid = left_rid_;

      // Move to next left tuple
      has_left_tuple_ = false;
      return true;
    }

    // Move to next left tuple
    has_left_tuple_ = false;
  }
}

}  // namespace bustub
