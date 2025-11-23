#include "execution/executors/aggregation_executor.h"
#include <memory>
#include "common/macros.h"

namespace bustub {

AggregationExecutor::AggregationExecutor(ExecutorContext *exec_ctx, const AggregationPlanNode *plan,
                                         std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      child_executor_(std::move(child_executor)),
      aht_(plan->GetAggregates(), plan->GetAggregateTypes()),
      aht_iterator_(aht_.Begin()) {
  std::cerr << "AggExec Constructor: plan=" << plan_ << " aggs=" << plan_->GetAggregates().size()
            << " types=" << plan_->GetAggregateTypes().size() << std::endl;
}

void AggregationExecutor::Init() {
  child_executor_->Init();
  aht_.Clear();
  has_started_ = false;

  std::vector<Tuple> tuple_batch;
  std::vector<RID> rid_batch;

  while (child_executor_->Next(&tuple_batch, &rid_batch, BUSTUB_BATCH_SIZE)) {
    for (const auto &tuple : tuple_batch) {
      AggregateKey key = MakeAggregateKey(&tuple);
      AggregateValue value = MakeAggregateValue(&tuple);
      aht_.InsertCombine(key, value);
    }
  }

  aht_iterator_ = aht_.Begin();
}

auto AggregationExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                               size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();

  if (!has_started_) {
    has_started_ = true;
    if (aht_.Begin() == aht_.End() && plan_->GetGroupBys().empty()) {
      std::vector<Value> values;
      for (const auto &agg_val : aht_.GenerateInitialAggregateValue().aggregates_) {
        values.push_back(agg_val);
      }
      tuple_batch->emplace_back(Tuple(values, &plan_->OutputSchema()));
      rid_batch->emplace_back(RID());
      return true;
    }
  }

  while (aht_iterator_ != aht_.End() && tuple_batch->size() < batch_size) {
    std::vector<Value> values;
    for (const auto &group_by_val : aht_iterator_.Key().group_bys_) {
      values.push_back(group_by_val);
    }
    for (const auto &agg_val : aht_iterator_.Val().aggregates_) {
      values.push_back(agg_val);
    }
    tuple_batch->emplace_back(Tuple(values, &plan_->OutputSchema()));
    rid_batch->emplace_back(RID());
    ++aht_iterator_;
  }

  return !tuple_batch->empty();
}

auto AggregationExecutor::GetChildExecutor() const -> const AbstractExecutor * { return child_executor_.get(); }

}  // namespace bustub
