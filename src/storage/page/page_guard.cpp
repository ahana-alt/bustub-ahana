
//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// page_guard.cpp
//
// Identification: src/storage/page/page_guard.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/page/page_guard.h"
#include <future>  //NOLINT
#include <iostream>
#include <memory>
#include <shared_mutex>
#include <utility>
#include "buffer/arc_replacer.h"
#include "common/macros.h"

namespace bustub {

ReadPageGuard::ReadPageGuard(page_id_t page_id, std::shared_ptr<FrameHeader> frame,
                             std::shared_ptr<ArcReplacer> replacer, std::shared_ptr<std::mutex> bpm_latch,
                             std::shared_ptr<DiskScheduler> disk_scheduler)
    : page_id_(page_id),
      frame_(std::move(frame)),
      replacer_(std::move(replacer)),
      bpm_latch_(std::move(bpm_latch)),
      disk_scheduler_(std::move(disk_scheduler)),
      is_valid_(true) {
  rlatch_ = std::shared_lock<std::shared_mutex>(frame_->rwlatch_);
  is_valid_ = true;
  frame_->pin_count_.fetch_add(1, std::memory_order_relaxed);
  std::cout << "[ReadPageGuard::Constructor] page=" << page_id << " frame_id=" << frame_->frame_id_
            << " pin_count=" << frame_->pin_count_.load() << " owns_lock=" << rlatch_.owns_lock() << std::endl;
}

ReadPageGuard::ReadPageGuard(ReadPageGuard &&that) noexcept
    : page_id_(that.page_id_),
      frame_(std::move(that.frame_)),
      replacer_(std::move(that.replacer_)),
      bpm_latch_(std::move(that.bpm_latch_)),
      disk_scheduler_(std::move(that.disk_scheduler_)),
      rlatch_(std::move(that.rlatch_)),
      is_valid_(that.is_valid_) {
  std::cout << "[ReadPageGuard::MoveConstructor] page=" << page_id_ << " is_valid=" << is_valid_
            << " owns_lock=" << rlatch_.owns_lock() << std::endl;
  that.is_valid_ = false;
}

auto ReadPageGuard::operator=(ReadPageGuard &&that) noexcept -> ReadPageGuard & {
  if (this != &that) {
    std::cout << "[ReadPageGuard::MoveAssignment] from_page=" << that.page_id_ << " to_page=" << page_id_ << std::endl;
    Drop();
    page_id_ = that.page_id_;
    frame_ = std::move(that.frame_);
    replacer_ = std::move(that.replacer_);
    bpm_latch_ = std::move(that.bpm_latch_);
    disk_scheduler_ = std::move(that.disk_scheduler_);
    rlatch_ = std::move(that.rlatch_);
    is_valid_ = that.is_valid_;
    that.is_valid_ = false;
  }
  return *this;
}

auto ReadPageGuard::GetPageId() const -> page_id_t {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid read guard");
  return page_id_;
}

auto ReadPageGuard::GetData() const -> const char * {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid read guard");
  return frame_->GetData();
}

auto ReadPageGuard::IsDirty() const -> bool {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid read guard");
  return frame_->is_dirty_;
}

void ReadPageGuard::Flush() {
  std::cout << "[ReadPageGuard::Flush] page=" << page_id_ << std::endl;
  if (!frame_->is_dirty_) {
    return;
  }

  std::promise<bool> promise;
  std::future<bool> future = promise.get_future();
  std::vector<DiskRequest> requests;
  requests.emplace_back(DiskRequest{true, const_cast<char *>(frame_->GetData()), page_id_, std::move(promise)});
  disk_scheduler_->Schedule(requests);
  future.get();
  frame_->is_dirty_ = false;
}

void ReadPageGuard::Drop() {
  if (!is_valid_) {
    std::cout << "[ReadPageGuard::Drop] SKIPPED page=" << page_id_ << " (already invalid)" << std::endl;
    return;
  }

  std::cout << "[ReadPageGuard::Drop] START page=" << page_id_ << " pin_count=" << frame_->pin_count_.load()
            << " owns_lock=" << rlatch_.owns_lock() << std::endl;

  is_valid_ = false;

  // std::cout << "[ReadPageGuard::Drop] Releasing shared lock, page=" << page_id_ << " prev_pin=" << prev
  //           << " new_pin=" << (prev - 1) << std::endl;
  if (rlatch_.owns_lock()) {
    rlatch_.unlock();
  }

  std::cout << "[ReadPageGuard::Drop] ACQUIRING bpm_latch_ page=" << page_id_ << std::endl;
  std::scoped_lock lk(*bpm_latch_);

  size_t prev = frame_->pin_count_.fetch_sub(1, std::memory_order_relaxed);

  if (prev == 1) {
    std::cout << "[ReadPageGuard::Drop] SetEvictable(true) page=" << page_id_ << " frame_id=" << frame_->frame_id_
              << std::endl;
    replacer_->SetEvictable(frame_->frame_id_, true);
    std::cout << "[ReadPageGuard::Drop] RELEASING bpm_latch_ page=" << page_id_ << std::endl;
  }

  std::cout << "[ReadPageGuard::Drop] COMPLETE page=" << page_id_ << std::endl;
}

ReadPageGuard::~ReadPageGuard() {
  std::cout << "[ReadPageGuard::Destructor] page=" << page_id_ << " is_valid=" << is_valid_ << std::endl;
  // frame_->pin_count_.fetch_sub(1, std::memory_order_relaxed);
  Drop();
}

/**********************************************************************************************************************/

WritePageGuard::WritePageGuard(page_id_t page_id, std::shared_ptr<FrameHeader> frame,
                               std::shared_ptr<ArcReplacer> replacer, std::shared_ptr<std::mutex> bpm_latch,
                               std::shared_ptr<DiskScheduler> disk_scheduler)
    : page_id_(page_id),
      frame_(std::move(frame)),
      replacer_(std::move(replacer)),
      bpm_latch_(std::move(bpm_latch)),
      disk_scheduler_(std::move(disk_scheduler)) {
  wlatch_ = std::unique_lock<std::shared_mutex>(frame_->rwlatch_);
  is_valid_ = true;
  frame_->pin_count_.fetch_add(1, std::memory_order_relaxed);
  std::cout << "[WritePageGuard::Constructor] page=" << page_id << " frame_id=" << frame_->frame_id_
            << " pin_count=" << frame_->pin_count_.load() << " owns_lock=" << wlatch_.owns_lock() << std::endl;
}

WritePageGuard::WritePageGuard(WritePageGuard &&that) noexcept
    : page_id_(that.page_id_),
      frame_(std::move(that.frame_)),
      replacer_(std::move(that.replacer_)),
      bpm_latch_(std::move(that.bpm_latch_)),
      disk_scheduler_(std::move(that.disk_scheduler_)),
      wlatch_(std::move(that.wlatch_)),
      is_valid_(that.is_valid_) {
  std::cout << "[WritePageGuard::MoveConstructor] page=" << page_id_ << " is_valid=" << is_valid_
            << " owns_lock=" << wlatch_.owns_lock() << std::endl;
  that.is_valid_ = false;
}

auto WritePageGuard::operator=(WritePageGuard &&that) noexcept -> WritePageGuard & {
  if (this != &that) {
    std::cout << "[WritePageGuard::MoveAssignment] from_page=" << that.page_id_ << " to_page=" << page_id_ << std::endl;
    Drop();
    page_id_ = that.page_id_;
    frame_ = std::move(that.frame_);
    replacer_ = std::move(that.replacer_);
    bpm_latch_ = std::move(that.bpm_latch_);
    disk_scheduler_ = std::move(that.disk_scheduler_);
    wlatch_ = std::move(that.wlatch_);
    is_valid_ = that.is_valid_;
    that.is_valid_ = false;
  }
  return *this;
}

auto WritePageGuard::GetPageId() const -> page_id_t {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
  return page_id_;
}

auto WritePageGuard::GetData() const -> const char * {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
  return frame_->GetData();
}

auto WritePageGuard::GetDataMut() -> char * {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
  frame_->is_dirty_ = true;
  return frame_->GetDataMut();
}

auto WritePageGuard::IsDirty() const -> bool {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
  return frame_->is_dirty_;
}

void WritePageGuard::Flush() {
  std::cout << "[WritePageGuard::Flush] page=" << page_id_ << std::endl;
  if (!frame_->is_dirty_) {
    return;
  }

  std::promise<bool> promise;
  std::future<bool> future = promise.get_future();
  std::vector<DiskRequest> requests;
  requests.emplace_back(DiskRequest{true, frame_->GetDataMut(), page_id_, std::move(promise)});
  disk_scheduler_->Schedule(requests);
  future.get();
  frame_->is_dirty_ = false;
}

void WritePageGuard::Drop() {
  if (!is_valid_) {
    std::cout << "[WritePageGuard::Drop] SKIPPED page=" << page_id_ << " (already invalid)" << std::endl;
    return;
  }

  std::cout << "[WritePageGuard::Drop] START page=" << page_id_ << " pin_count=" << frame_->pin_count_.load()
            << " owns_lock=" << wlatch_.owns_lock() << std::endl;

  is_valid_ = false;

  // std::cout << "[WritePageGuard::Drop] Releasing exclusive lock, page=" << page_id_ << " prev_pin=" << prev
  //           << " new_pin=" << (prev - 1) << std::endl;
  if (wlatch_.owns_lock()) {
    wlatch_.unlock();
  }

  std::cout << "[WritePageGuard::Drop] ACQUIRING bpm_latch_ page=" << page_id_ << std::endl;
  std::scoped_lock lk(*bpm_latch_);

  const size_t prev = frame_->pin_count_.fetch_sub(1, std::memory_order_relaxed);

  if (prev == 1) {
    std::cout << "[WritePageGuard::Drop] SetEvictable(true) page=" << page_id_ << " frame_id=" << frame_->frame_id_
              << std::endl;
    replacer_->SetEvictable(frame_->frame_id_, true);
  }

  std::cout << "[WritePageGuard::Drop] COMPLETE page=" << page_id_ << std::endl;
}

WritePageGuard::~WritePageGuard() {
  std::cout << "[WritePageGuard::Destructor] page=" << page_id_ << " is_valid=" << is_valid_ << std::endl;
  // frame_->pin_count_.fetch_sub(1, std::memory_order_relaxed);
  Drop();
}

}  // namespace bustub
