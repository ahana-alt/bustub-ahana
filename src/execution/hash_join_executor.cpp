//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// hash_join_executor.cpp
//
// Identification: src/execution/hash_join_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/hash_join_executor.h"
#include "common/macros.h"
#include "type/value_factory.h"

namespace bustub {

/**
 * Construct a new HashJoinExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The HashJoin join plan to be executed
 * @param left_child The child executor that produces tuples for the left side of join
 * @param right_child The child executor that produces tuples for the right side of join
 */
HashJoinExecutor::HashJoinExecutor(ExecutorContext *exec_ctx, const HashJoinPlanNode *plan,
                                   std::unique_ptr<AbstractExecutor> &&left_child,
                                   std::unique_ptr<AbstractExecutor> &&right_child)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      left_child_(std::move(left_child)),
      right_child_(std::move(right_child)) {
  if (plan->GetJoinType() != JoinType::LEFT && plan->GetJoinType() != JoinType::INNER) {
    // Note for Spring 2025: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

/** Initialize the join */
void HashJoinExecutor::Init() {
  // Initialize child executors
  left_child_->Init();
  right_child_->Init();

  // Clear state
  ht_.Clear();
  current_matches_.clear();
  match_index_ = 0;
  has_right_tuple_ = false;
  matched_left_tuples_.clear();
  emitting_unmatched_ = false;
  unmatched_index_ = 0;

  // Build phase: construct hash table from left child (build side)
  BuildHashTable();
}

/**
 * Build the hash table from the left child.
 */
void HashJoinExecutor::BuildHashTable() {
  Tuple left_tuple{};
  RID left_rid{};

  // Read all tuples from the left child
  while (left_child_->Next(&left_tuple, &left_rid)) {
    // Compute the hash key for this tuple
    auto left_key = plan_->LeftJoinKeyExpressions();
    std::vector<Value> key_values;
    key_values.reserve(left_key.size());

    for (const auto &expr : left_key) {
      key_values.push_back(expr->Evaluate(&left_tuple, left_child_->GetOutputSchema()));
    }

    auto hash_key = HashJoinKey(key_values);

    // Insert into hash table
    ht_.Insert(hash_key, left_tuple);

    // For LEFT JOIN: initialize matched tracking
    if (plan_->GetJoinType() == JoinType::LEFT) {
      if (matched_left_tuples_.find(hash_key) == matched_left_tuples_.end()) {
        matched_left_tuples_[hash_key] = {};
      }
      matched_left_tuples_[hash_key].push_back(false);
    }
  }
}

/**
 * Yield the next tuple from the hash join.
 * @param[out] tuple The next tuple produced by the hash join
 * @param[out] rid The next tuple RID produced by the hash join
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 */
auto HashJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  // Phase 1: Emit matches from the probe phase
  while (true) {
    // If we have matches from the current right tuple, emit them
    if (match_index_ < current_matches_.size()) {
      *tuple = current_matches_[match_index_++];
      return true;
    }

    // Need to get the next right tuple and find its matches
    current_matches_.clear();
    match_index_ = 0;

    Tuple right_tuple{};
    RID right_rid{};

    if (right_child_->Next(&right_tuple, &right_rid)) {
      // Compute the hash key for this right tuple
      auto right_key = plan_->RightJoinKeyExpressions();
      std::vector<Value> key_values;
      key_values.reserve(right_key.size());

      for (const auto &expr : right_key) {
        key_values.push_back(expr->Evaluate(&right_tuple, right_child_->GetOutputSchema()));
      }

      auto hash_key = HashJoinKey(key_values);

      // Probe the hash table
      auto matching_left_tuples = ht_.Get(hash_key);

      if (matching_left_tuples != nullptr) {
        // We have potential matches - verify the join condition
        auto left_key_exprs = plan_->LeftJoinKeyExpressions();
        auto right_key_exprs = plan_->RightJoinKeyExpressions();

        for (size_t i = 0; i < matching_left_tuples->size(); i++) {
          const auto &left_tuple = (*matching_left_tuples)[i];

          // Verify that the join keys actually match (not just hash collision)
          bool keys_match = true;
          for (size_t j = 0; j < left_key_exprs.size(); j++) {
            auto left_val = left_key_exprs[j]->Evaluate(&left_tuple, left_child_->GetOutputSchema());
            auto right_val = right_key_exprs[j]->Evaluate(&right_tuple, right_child_->GetOutputSchema());

            if (left_val.CompareEquals(right_val) != CmpBool::CmpTrue) {
              keys_match = false;
              break;
            }
          }

          if (keys_match) {
            // Mark this left tuple as matched (for LEFT JOIN)
            if (plan_->GetJoinType() == JoinType::LEFT) {
              matched_left_tuples_[hash_key][i] = true;
            }

            // Add the combined tuple to matches
            current_matches_.push_back(CombineTuples(left_tuple, &right_tuple));
          }
        }
      }

      // If we found matches, emit the first one
      if (!current_matches_.empty()) {
        *tuple = current_matches_[match_index_++];
        return true;
      }
      // Otherwise, continue to next right tuple
    } else {
      // No more right tuples - move to phase 2 for LEFT JOIN
      break;
    }
  }

  // Phase 2: For LEFT JOIN, emit unmatched left tuples
  if (plan_->GetJoinType() == JoinType::LEFT) {
    if (!emitting_unmatched_) {
      // Initialize the iterator
      emitting_unmatched_ = true;
      unmatched_iter_ = ht_.Begin();
      unmatched_index_ = 0;
    }

    // Iterate through all left tuples in the hash table
    while (unmatched_iter_ != ht_.End()) {
      auto hash_key = unmatched_iter_->first;
      const auto &left_tuples = unmatched_iter_->second;

      // Check if we have tracking for this hash key
      if (matched_left_tuples_.find(hash_key) != matched_left_tuples_.end()) {
        const auto &matched_flags = matched_left_tuples_[hash_key];

        // Find unmatched left tuples
        while (unmatched_index_ < left_tuples.size()) {
          if (unmatched_index_ < matched_flags.size() && !matched_flags[unmatched_index_]) {
            // This left tuple was not matched
            *tuple = CombineTuples(left_tuples[unmatched_index_], nullptr);
            unmatched_index_++;
            return true;
          }
          unmatched_index_++;
        }
      }

      // Move to next hash bucket
      ++unmatched_iter_;
      unmatched_index_ = 0;
    }
  }

  // No more tuples
  return false;
}

}  // namespace bustub
