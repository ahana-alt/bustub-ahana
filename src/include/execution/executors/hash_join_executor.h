#pragma once

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/util/hash_util.h"
#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/hash_join_plan.h"
#include "storage/table/tuple.h"
#include "type/value_factory.h"

namespace bustub {

struct HashTableEntry {
  std::vector<Tuple> tuples_;
  std::vector<bool> matched_;  // Only used for LEFT JOIN
};

class SimpleJoinHashTable {
 public:
  void Insert(size_t hash_key, const Tuple &tuple, bool track_matched) {
    auto &entry = ht_[hash_key];
    entry.tuples_.push_back(tuple);
    if (track_matched) {
      entry.matched_.push_back(false);
    }
  }

  auto Get(size_t hash_key) -> HashTableEntry * {
    auto it = ht_.find(hash_key);
    if (it == ht_.end()) {
      return nullptr;
    }
    return &(it->second);
  }

  auto IsEmpty() const -> bool { return ht_.empty(); }
  auto Size() const -> size_t { return ht_.size(); }
  void Clear() { ht_.clear(); }

  auto Begin() -> std::unordered_map<size_t, HashTableEntry>::iterator { return ht_.begin(); }
  auto End() -> std::unordered_map<size_t, HashTableEntry>::iterator { return ht_.end(); }

 private:
  std::unordered_map<size_t, HashTableEntry> ht_;
};

class HashJoinExecutor : public AbstractExecutor {
 public:
  HashJoinExecutor(ExecutorContext *exec_ctx, const HashJoinPlanNode *plan,
                   std::unique_ptr<AbstractExecutor> &&left_child, std::unique_ptr<AbstractExecutor> &&right_child);

  void Init() override;

  auto Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch, size_t batch_size)
      -> bool override;

  auto GetOutputSchema() const -> const Schema & override { return plan_->OutputSchema(); }

 private:
  auto HashJoinKey(const std::vector<Value> &key) -> size_t {
    size_t hash = 0;
    for (const auto &val : key) {
      if (!val.IsNull()) {
        hash = HashUtil::CombineHashes(hash, HashUtil::HashValue(&val));
      }
    }
    return hash;
  }

  auto CombineTuples(const Tuple &left_tuple, const Tuple *right_tuple) -> Tuple {
    std::vector<Value> values;
    values.reserve(GetOutputSchema().GetColumnCount());

    for (uint32_t i = 0; i < left_child_->GetOutputSchema().GetColumnCount(); i++) {
      values.push_back(left_tuple.GetValue(&left_child_->GetOutputSchema(), i));
    }

    for (uint32_t i = 0; i < right_child_->GetOutputSchema().GetColumnCount(); i++) {
      if (right_tuple != nullptr) {
        values.push_back(right_tuple->GetValue(&right_child_->GetOutputSchema(), i));
      } else {
        values.push_back(ValueFactory::GetNullValueByType(right_child_->GetOutputSchema().GetColumn(i).GetType()));
      }
    }

    return {values, &GetOutputSchema()};
  }

  void BuildHashTable();

  const HashJoinPlanNode *plan_;
  std::unique_ptr<AbstractExecutor> left_child_;
  std::unique_ptr<AbstractExecutor> right_child_;
  SimpleJoinHashTable ht_;

  std::vector<Tuple> current_matches_;
  size_t match_index_{0};
  std::vector<Tuple> right_batch_;
  size_t right_batch_index_{0};

  std::unordered_map<size_t, HashTableEntry>::iterator unmatched_iter_;
  size_t unmatched_index_{0};
  bool emitting_unmatched_{false};
};

}  // namespace bustub

// connor@dbtlabs.com
