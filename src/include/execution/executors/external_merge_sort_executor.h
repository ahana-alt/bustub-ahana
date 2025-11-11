//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// external_merge_sort_executor.h
//
// Identification: src/include/execution/executors/external_merge_sort_executor.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>
#include "common/config.h"
#include "common/macros.h"
#include "execution/execution_common.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/sort_plan.h"
#include "storage/page/intermediate_result_page.h"
#include "storage/page/page_guard.h"
#include "storage/table/tuple.h"

namespace bustub {

class MergeSortRun {
 public:
  MergeSortRun() = default;
  MergeSortRun(std::vector<page_id_t> pages, BufferPoolManager *bpm) : pages_(std::move(pages)), bpm_(bpm) {}

  auto GetPageCount() -> size_t { return pages_.size(); }

  auto GetPages() const -> const std::vector<page_id_t> & { return pages_; }

  class Iterator {
    friend class MergeSortRun;

   public:
    Iterator() = default;

    auto operator++() -> Iterator & {
      tuple_idx_++;

      if (current_page_guard_.has_value()) {
        const auto *page = current_page_guard_->As<IntermediateResultPage>();
        if (tuple_idx_ >= page->GetTupleCount()) {
          page_idx_++;
          tuple_idx_ = 0;
          current_page_guard_.reset();

          if (page_idx_ < run_->pages_.size()) {
            current_page_guard_ = run_->bpm_->ReadPage(run_->pages_[page_idx_]);
          }
        }
      }

      return *this;
    }

    auto operator*() const -> Tuple {
      if (!current_page_guard_.has_value()) {
        throw std::runtime_error("Dereferencing invalid iterator");
      }
      const auto *page = current_page_guard_->As<IntermediateResultPage>();
      return page->GetTuple(tuple_idx_);
    }

    auto operator==(const Iterator &other) const -> bool {
      return run_ == other.run_ && page_idx_ == other.page_idx_ && tuple_idx_ == other.tuple_idx_;
    }

    auto operator!=(const Iterator &other) const -> bool { return !(*this == other); }

   private:
    explicit Iterator(const MergeSortRun *run, size_t page_idx = 0, size_t tuple_idx = 0)
        : run_(run), page_idx_(page_idx), tuple_idx_(tuple_idx) {
      if (run_ != nullptr && page_idx_ < run_->pages_.size()) {
        current_page_guard_ = run_->bpm_->ReadPage(run_->pages_[page_idx_]);
      }
    }

    const MergeSortRun *run_{nullptr};
    size_t page_idx_{0};
    size_t tuple_idx_{0};
    std::optional<ReadPageGuard> current_page_guard_;
  };

  auto Begin() -> Iterator { return Iterator(this, 0, 0); }

  auto End() -> Iterator { return Iterator(this, pages_.size(), 0); }

 private:
  std::vector<page_id_t> pages_;
  [[maybe_unused]] BufferPoolManager *bpm_;
};

template <size_t K>
class ExternalMergeSortExecutor : public AbstractExecutor {
 public:
  ExternalMergeSortExecutor(ExecutorContext *exec_ctx, const SortPlanNode *plan,
                            std::unique_ptr<AbstractExecutor> &&child_executor);

  void Init() override;

  auto Next(Tuple *tuple, RID *rid) -> bool override;

  auto GetOutputSchema() const -> const Schema & override { return plan_->OutputSchema(); }

 private:
  auto CreateSortedRun(std::vector<SortEntry> &entries) -> MergeSortRun;
  auto MergePass(std::vector<MergeSortRun> &runs) -> std::vector<MergeSortRun>;
  auto MergeRuns(std::vector<MergeSortRun> &runs_to_merge) -> MergeSortRun;

  const SortPlanNode *plan_;
  TupleComparator cmp_;
  std::unique_ptr<AbstractExecutor> child_executor_;

  std::unique_ptr<MergeSortRun> final_run_;
  std::optional<MergeSortRun::Iterator> output_iterator_;
  bool is_initialized_{false};
};

}  // namespace bustub
