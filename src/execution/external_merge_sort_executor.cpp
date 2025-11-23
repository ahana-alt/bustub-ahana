//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// external_merge_sort_executor.cpp
//
// Identification: src/execution/external_merge_sort_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/external_merge_sort_executor.h"
#include <algorithm>
#include <queue>
#include <vector>
#include "common/macros.h"
#include "execution/plans/sort_plan.h"
#include "storage/page/page_guard.h"

namespace bustub {

template <size_t K>
ExternalMergeSortExecutor<K>::ExternalMergeSortExecutor(ExecutorContext *exec_ctx, const SortPlanNode *plan,
                                                        std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), cmp_(plan->GetOrderBy()), child_executor_(std::move(child_executor)) {}

template <size_t K>
void ExternalMergeSortExecutor<K>::Init() {
  child_executor_->Init();
  is_initialized_ = false;
  final_run_.reset();
  output_iterator_.reset();
  std::vector<MergeSortRun> runs;
  std::vector<SortEntry> current_page_tuples;
  auto &schema = child_executor_->GetOutputSchema();

  // Estimate tuples per page conservatively
  uint32_t tuple_size_est = 0;
  for (uint32_t i = 0; i < schema.GetColumnCount(); i++) {
    tuple_size_est += 50;
  }
  size_t max_tuples_per_page = (BUSTUB_PAGE_SIZE - 100) / (tuple_size_est + 10);
  if (max_tuples_per_page < 1) {
    max_tuples_per_page = 1;
  }

  // Create larger runs to reduce merge passes
  const size_t min_tuples_per_run = max_tuples_per_page * 15;

  // Read all tuples from child in batches
  std::vector<Tuple> child_batch;
  std::vector<RID> child_rid_batch;
  while (child_executor_->Next(&child_batch, &child_rid_batch, BUSTUB_BATCH_SIZE)) {
    for (const auto &tuple : child_batch) {
      auto sort_key = GenerateSortKey(tuple, plan_->GetOrderBy(), schema);
      current_page_tuples.emplace_back(sort_key, tuple);
      if (current_page_tuples.size() >= min_tuples_per_run) {
        runs.push_back(CreateSortedRun(current_page_tuples));
        current_page_tuples.clear();
      }
    }
  }

  if (!current_page_tuples.empty()) {
    runs.push_back(CreateSortedRun(current_page_tuples));
  }

  while (runs.size() > 1) {
    runs = MergePass(runs);
  }

  if (!runs.empty()) {
    final_run_ = std::make_unique<MergeSortRun>(std::move(runs[0]));
    output_iterator_ = final_run_->Begin();
  }

  is_initialized_ = true;
}

template <size_t K>
auto ExternalMergeSortExecutor<K>::Next(std::vector<Tuple> *tuple_batch, std::vector<RID> *rid_batch, size_t batch_size)
    -> bool {
  tuple_batch->clear();
  rid_batch->clear();

  if (!is_initialized_ || !final_run_ || !output_iterator_.has_value()) {
    return false;
  }

  auto end_iter = final_run_->End();

  while (tuple_batch->size() < batch_size && *output_iterator_ != end_iter) {
    tuple_batch->push_back(**output_iterator_);
    rid_batch->push_back(RID());
    ++(*output_iterator_);
  }

  return !tuple_batch->empty();
}

template <size_t K>
auto ExternalMergeSortExecutor<K>::CreateSortedRun(std::vector<SortEntry> &entries) -> MergeSortRun {
  std::sort(entries.begin(), entries.end(), cmp_);
  std::vector<page_id_t> pages;
  auto *bpm = exec_ctx_->GetBufferPoolManager();
  page_id_t page_id = bpm->NewPage();
  auto page_guard = bpm->WritePage(page_id);
  auto *page = page_guard.AsMut<IntermediateResultPage>();
  page->Init();
  pages.push_back(page_id);
  for (const auto &entry : entries) {
    if (!page->Insert(entry.second)) {
      page_guard.Drop();
      page_id = bpm->NewPage();
      page_guard = bpm->WritePage(page_id);
      page = page_guard.AsMut<IntermediateResultPage>();
      page->Init();
      pages.push_back(page_id);
      if (!page->Insert(entry.second)) {
        throw std::runtime_error("Tuple too large");
      }
    }
  }
  page_guard.Drop();
  return {pages, bpm};
}

template <size_t K>
auto ExternalMergeSortExecutor<K>::MergePass(std::vector<MergeSortRun> &runs) -> std::vector<MergeSortRun> {
  std::vector<MergeSortRun> merged_runs;
  auto *bpm = exec_ctx_->GetBufferPoolManager();

  for (size_t i = 0; i < runs.size(); i += K) {
    std::vector<MergeSortRun> runs_to_merge;
    for (size_t j = 0; j < K && i + j < runs.size(); j++) {
      runs_to_merge.push_back(std::move(runs[i + j]));
    }

    if (runs_to_merge.size() > 1) {
      merged_runs.push_back(MergeRuns(runs_to_merge));
      for (auto &run : runs_to_merge) {
        for (auto page_id : run.GetPages()) {
          bpm->DeletePage(page_id);
        }
      }
    } else {
      merged_runs.push_back(std::move(runs_to_merge[0]));
    }
  }

  return merged_runs;
}

template <size_t K>
auto ExternalMergeSortExecutor<K>::MergeRuns(std::vector<MergeSortRun> &runs_to_merge) -> MergeSortRun {
  auto *bpm = exec_ctx_->GetBufferPoolManager();
  auto &schema = plan_->OutputSchema();
  struct QueueItem {
    SortEntry entry_;
    size_t run_idx_;
  };
  auto item_comparator = [this](const QueueItem &a, const QueueItem &b) { return !cmp_(a.entry_, b.entry_); };
  std::priority_queue<QueueItem, std::vector<QueueItem>, decltype(item_comparator)> pq(item_comparator);
  std::vector<MergeSortRun::Iterator> iterators;
  std::vector<MergeSortRun::Iterator> end_iterators;
  for (size_t i = 0; i < runs_to_merge.size(); i++) {
    auto begin_iter = runs_to_merge[i].Begin();
    auto end_iter = runs_to_merge[i].End();
    if (begin_iter != end_iter) {
      Tuple tuple = *begin_iter;
      SortKey sort_key = GenerateSortKey(tuple, plan_->GetOrderBy(), schema);
      pq.push({SortEntry(sort_key, tuple), i});
      ++begin_iter;
    }
    iterators.push_back(std::move(begin_iter));
    end_iterators.push_back(std::move(end_iter));
  }
  std::vector<page_id_t> merged_pages;
  page_id_t page_id = bpm->NewPage();
  auto page_guard = bpm->WritePage(page_id);
  auto *page = page_guard.AsMut<IntermediateResultPage>();
  page->Init();
  merged_pages.push_back(page_id);
  while (!pq.empty()) {
    auto item = pq.top();
    pq.pop();
    if (!page->Insert(item.entry_.second)) {
      page_guard.Drop();
      page_id = bpm->NewPage();
      page_guard = bpm->WritePage(page_id);
      page = page_guard.AsMut<IntermediateResultPage>();
      page->Init();
      merged_pages.push_back(page_id);
      if (!page->Insert(item.entry_.second)) {
        throw std::runtime_error("Tuple too large");
      }
    }
    size_t run_idx = item.run_idx_;
    if (iterators[run_idx] != end_iterators[run_idx]) {
      Tuple next_tuple = *iterators[run_idx];
      SortKey next_sort_key = GenerateSortKey(next_tuple, plan_->GetOrderBy(), schema);
      pq.push({SortEntry(next_sort_key, next_tuple), run_idx});
      ++iterators[run_idx];
    }
  }
  page_guard.Drop();
  return {merged_pages, bpm};
}

template class ExternalMergeSortExecutor<2>;
}  // namespace bustub
