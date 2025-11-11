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
  Tuple tuple;
  RID rid;
  while (child_executor_->Next(&tuple, &rid)) {
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

  // Extract all elements from heap
  top_entries_.reserve(heap.size());
  while (!heap.empty()) {
    top_entries_.push_back(heap.top());
    heap.pop();
  }

  // Sort the top N elements in correct order
  std::sort(top_entries_.begin(), top_entries_.end(), tuple_cmp);
}

auto TopNExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (output_index_ >= top_entries_.size()) {
    return false;
  }

  *tuple = top_entries_[output_index_].second;
  *rid = RID();
  output_index_++;

  return true;
}

auto TopNExecutor::GetNumInHeap() -> size_t { return top_entries_.size(); }

}  // namespace bustub
