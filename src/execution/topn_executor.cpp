//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// topn_executor.cpp
//
// Identification: src/execution/topn_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include "execution/executors/topn_executor.h"
#include <algorithm>
#include <queue>
#include "execution/execution_common.h"

namespace bustub {

TopNExecutor::TopNExecutor(ExecutorContext *exec_ctx, const TopNPlanNode *plan,
                           std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)), output_index_(0) {}

void TopNExecutor::Init() {
  child_executor_->Init();
  top_entries_.clear();
  output_index_ = 0;

  auto n = plan_->GetN();
  auto &order_bys = plan_->GetOrderBy();
  auto &schema = child_executor_->GetOutputSchema();

  // Create comparator for the heap
  TupleComparator tuple_cmp(order_bys);

  // Use tuple_cmp directly - it naturally creates the right heap type
  // For DESC: tuple_cmp(a,b) = true when a > b → MIN heap
  // For ASC: tuple_cmp(a,b) = true when a < b → MAX heap
  auto heap_cmp = [&tuple_cmp](const SortEntry &a, const SortEntry &b) {
    return tuple_cmp(a, b);  // ✓ Don't reverse!
  };

  std::priority_queue<SortEntry, std::vector<SortEntry>, decltype(heap_cmp)> heap(heap_cmp);

  // Process all tuples from child
  std::vector<Tuple> tuple_batch;
  std::vector<RID> rid_batch;
  while (child_executor_->Next(&tuple_batch, &rid_batch, BUSTUB_BATCH_SIZE)) {
    for (const auto &tuple : tuple_batch) {
      auto sort_key = GenerateSortKey(tuple, order_bys, schema);
      SortEntry entry(sort_key, tuple);

      if (heap.size() < n) {
        // Haven't reached N elements yet, just add
        heap.push(entry);
      } else {
        // Heap is full, check if new element should replace top
        // heap.top() is the "worst" of our current top N
        // Replace it if the new entry is better
        if (tuple_cmp(entry, heap.top())) {
          heap.pop();
          heap.push(entry);
        }
      }
    }
  }

  // Extract all elements from heap
  top_entries_.reserve(heap.size());
  while (!heap.empty()) {
    top_entries_.push_back(heap.top());
    heap.pop();
  }

  // Sort the top N elements in correct order
  std::sort(top_entries_.begin(), top_entries_.end(), tuple_cmp);
}

/**
 * Yield the next tuple batch from the TopN.
 * @param[out] tuple_batch The next tuple batch produced by the TopN
 * @param[out] rid_batch The next tuple RID batch produced by the TopN
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 */
auto TopNExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch, size_t batch_size)
    -> bool {
  tuple_batch->clear();
  rid_batch->clear();

  // Return batches from the pre-computed top_entries_
  while (output_index_ < top_entries_.size() && tuple_batch->size() < batch_size) {
    tuple_batch->push_back(top_entries_[output_index_].second);
    rid_batch->emplace_back();  // Empty RID
    output_index_++;
  }

  return !tuple_batch->empty();
}

auto TopNExecutor::GetNumInHeap() -> size_t { return top_entries_.size(); }

}  // namespace bustub
