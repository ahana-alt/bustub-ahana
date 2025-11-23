#include "execution/executors/hash_join_executor.h"
#include "common/macros.h"
#include "type/value_factory.h"

namespace bustub {

HashJoinExecutor::HashJoinExecutor(ExecutorContext *exec_ctx, const HashJoinPlanNode *plan,
                                   std::unique_ptr<AbstractExecutor> &&left_child,
                                   std::unique_ptr<AbstractExecutor> &&right_child)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      left_child_(std::move(left_child)),
      right_child_(std::move(right_child)) {
  if (plan->GetJoinType() != JoinType::LEFT && plan->GetJoinType() != JoinType::INNER) {
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

void HashJoinExecutor::Init() {
  left_child_->Init();
  right_child_->Init();

  ht_.Clear();
  current_matches_.clear();
  match_index_ = 0;
  right_batch_.clear();
  right_batch_index_ = 0;
  emitting_unmatched_ = false;
  unmatched_index_ = 0;

  BuildHashTable();
}

void HashJoinExecutor::BuildHashTable() {
  std::vector<Tuple> left_batch;
  std::vector<RID> left_rid_batch;
  bool is_left_join = (plan_->GetJoinType() == JoinType::LEFT);

  while (left_child_->Next(&left_batch, &left_rid_batch, BUSTUB_BATCH_SIZE)) {
    for (const auto &left_tuple : left_batch) {
      auto left_key = plan_->LeftJoinKeyExpressions();
      std::vector<Value> key_values;
      key_values.reserve(left_key.size());

      for (const auto &expr : left_key) {
        key_values.push_back(expr->Evaluate(&left_tuple, left_child_->GetOutputSchema()));
      }

      auto hash_key = HashJoinKey(key_values);
      ht_.Insert(hash_key, left_tuple, is_left_join);
    }
  }
}

auto HashJoinExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                            size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();

  while (tuple_batch->size() < batch_size) {
    if (match_index_ < current_matches_.size()) {
      tuple_batch->push_back(current_matches_[match_index_++]);
      rid_batch->emplace_back();
      continue;
    }

    current_matches_.clear();
    match_index_ = 0;

    if (right_batch_index_ >= right_batch_.size()) {
      right_batch_.clear();
      right_batch_index_ = 0;

      std::vector<RID> right_rid_batch;
      if (!right_child_->Next(&right_batch_, &right_rid_batch, BUSTUB_BATCH_SIZE)) {
        break;
      }
    }

    if (right_batch_index_ < right_batch_.size()) {
      const auto &right_tuple = right_batch_[right_batch_index_++];

      auto right_key = plan_->RightJoinKeyExpressions();
      std::vector<Value> key_values;
      key_values.reserve(right_key.size());

      for (const auto &expr : right_key) {
        key_values.push_back(expr->Evaluate(&right_tuple, right_child_->GetOutputSchema()));
      }

      auto hash_key = HashJoinKey(key_values);
      auto *entry = ht_.Get(hash_key);

      if (entry != nullptr) {
        auto left_key_exprs = plan_->LeftJoinKeyExpressions();
        auto right_key_exprs = plan_->RightJoinKeyExpressions();

        for (size_t i = 0; i < entry->tuples_.size(); i++) {
          const auto &left_tuple = entry->tuples_[i];

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
            if (plan_->GetJoinType() == JoinType::LEFT) {
              entry->matched_[i] = true;
            }
            current_matches_.push_back(CombineTuples(left_tuple, &right_tuple));
          }
        }
      }

      while (match_index_ < current_matches_.size() && tuple_batch->size() < batch_size) {
        tuple_batch->push_back(current_matches_[match_index_++]);
        rid_batch->emplace_back();
      }
    }
  }

  if (!tuple_batch->empty()) {
    return true;
  }

  if (plan_->GetJoinType() == JoinType::LEFT) {
    if (!emitting_unmatched_) {
      emitting_unmatched_ = true;
      unmatched_iter_ = ht_.Begin();
      unmatched_index_ = 0;
    }

    while (unmatched_iter_ != ht_.End() && tuple_batch->size() < batch_size) {
      auto &entry = unmatched_iter_->second;

      while (unmatched_index_ < entry.tuples_.size() && tuple_batch->size() < batch_size) {
        if (!entry.matched_[unmatched_index_]) {
          tuple_batch->push_back(CombineTuples(entry.tuples_[unmatched_index_], nullptr));
          rid_batch->emplace_back();
        }
        unmatched_index_++;
      }

      if (unmatched_index_ >= entry.tuples_.size()) {
        ++unmatched_iter_;
        unmatched_index_ = 0;
      }
    }
  }

  return !tuple_batch->empty();
}

}  // namespace bustub
