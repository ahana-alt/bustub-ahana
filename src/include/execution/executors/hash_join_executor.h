//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// hash_join_executor.h
//
// Identification: src/include/execution/executors/hash_join_executor.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/util/hash_util.h"
#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/hash_join_plan.h"
#include "storage/page/intermediate_result_page.h"
#include "storage/table/tuple.h"
#include "type/value_factory.h"

namespace bustub {

/**
 * A simple hash table for hash join that stores multiple tuples per hash key.
 */
class SimpleJoinHashTable {
 public:
  /**
   * Insert a tuple into the hash table.
   * @param hash_key The hash key
   * @param tuple The tuple to insert
   */
  void Insert(size_t hash_key, const Tuple &tuple) { ht_[hash_key].push_back(tuple); }

  /**
   * Get all tuples matching the hash key.
   * @param hash_key The hash key to look up
   * @return Pointer to vector of matching tuples, or nullptr if not found
   */
  auto Get(size_t hash_key) -> std::vector<Tuple> * {
    auto it = ht_.find(hash_key);
    if (it == ht_.end()) {
      return nullptr;
    }
    return &(it->second);
  }

  /**
   * Check if the hash table is empty.
   */
  auto IsEmpty() const -> bool { return ht_.empty(); }

  /**
   * Get the number of distinct keys in the hash table.
   */
  auto Size() const -> size_t { return ht_.size(); }

  /**
   * Clear the hash table.
   */
  void Clear() { ht_.clear(); }

  /**
   * Get an iterator to the beginning of the hash table.
   */
  auto Begin() -> std::unordered_map<size_t, std::vector<Tuple>>::iterator { return ht_.begin(); }

  /**
   * Get an iterator to the end of the hash table.
   */
  auto End() -> std::unordered_map<size_t, std::vector<Tuple>>::iterator { return ht_.end(); }

 private:
  /** The hash table mapping hash keys to lists of tuples */
  std::unordered_map<size_t, std::vector<Tuple>> ht_;
};

/**
 * HashJoinExecutor executes a hash join on two tables.
 */
class HashJoinExecutor : public AbstractExecutor {
 public:
  /**
   * Construct a new HashJoinExecutor instance.
   * @param exec_ctx The executor context
   * @param plan The HashJoin plan to be executed
   * @param left_child The child executor that produces tuples for the left side of join
   * @param right_child The child executor that produces tuples for the right side of join
   */
  HashJoinExecutor(ExecutorContext *exec_ctx, const HashJoinPlanNode *plan,
                   std::unique_ptr<AbstractExecutor> &&left_child, std::unique_ptr<AbstractExecutor> &&right_child);

  /** Initialize the join */
  void Init() override;

  /**
   * Yield the next tuple from the hash join.
   * @param[out] tuple The next tuple produced by the hash join
   * @param[out] rid The next tuple RID produced by the hash join
   * @return `true` if a tuple was produced, `false` if there are no more tuples
   */
  auto Next(Tuple *tuple, RID *rid) -> bool override;

  /** @return The output schema for the join */
  auto GetOutputSchema() const -> const Schema & override { return plan_->OutputSchema(); }

 private:
  /**
   * Hash a join key into a size_t value.
   * @param key The join key (a tuple with multiple attributes)
   * @return The hash value
   */
  auto HashJoinKey(const std::vector<Value> &key) -> size_t {
    size_t hash = 0;
    for (const auto &val : key) {
      if (!val.IsNull()) {
        hash = HashUtil::CombineHashes(hash, HashUtil::HashValue(&val));
      }
    }
    return hash;
  }

  /**
   * Combine two tuples into one output tuple.
   * @param left_tuple The left tuple
   * @param right_tuple The right tuple (can be nullptr for LEFT JOIN)
   * @return The combined output tuple
   */
  auto CombineTuples(const Tuple &left_tuple, const Tuple *right_tuple) -> Tuple {
    std::vector<Value> values;
    values.reserve(GetOutputSchema().GetColumnCount());

    // Add all values from left tuple
    for (uint32_t i = 0; i < left_child_->GetOutputSchema().GetColumnCount(); i++) {
      values.push_back(left_tuple.GetValue(&left_child_->GetOutputSchema(), i));
    }

    // Add all values from right tuple (or nulls if right is nullptr for LEFT JOIN)
    for (uint32_t i = 0; i < right_child_->GetOutputSchema().GetColumnCount(); i++) {
      if (right_tuple != nullptr) {
        values.push_back(right_tuple->GetValue(&right_child_->GetOutputSchema(), i));
      } else {
        // For LEFT JOIN when no match found
        values.push_back(ValueFactory::GetNullValueByType(right_child_->GetOutputSchema().GetColumn(i).GetType()));
      }
    }

    return {values, &GetOutputSchema()};
  }

  /**
   * Build the hash table from the left child.
   */
  void BuildHashTable();

  /** The HashJoin plan node to be executed */
  const HashJoinPlanNode *plan_;

  /** The left child executor */
  std::unique_ptr<AbstractExecutor> left_child_;

  /** The right child executor */
  std::unique_ptr<AbstractExecutor> right_child_;

  /** The hash table for the join */
  SimpleJoinHashTable ht_;

  /** Buffer for output tuples from current right tuple */
  std::vector<Tuple> current_matches_;

  /** Index into current_matches_ */
  size_t match_index_{0};

  /** Current right tuple being processed */
  Tuple current_right_tuple_{};

  /** Flag indicating if we have a valid right tuple */
  bool has_right_tuple_{false};

  /** For LEFT JOIN: track which left tuples have been matched */
  std::unordered_map<size_t, std::vector<bool>> matched_left_tuples_;

  /** For LEFT JOIN: iterator for unmatched left tuples */
  std::unordered_map<size_t, std::vector<Tuple>>::iterator unmatched_iter_;

  /** For LEFT JOIN: index within current hash bucket */
  size_t unmatched_index_{0};

  /** Flag to indicate if we're emitting unmatched left tuples */
  bool emitting_unmatched_{false};
};

}  // namespace bustub
