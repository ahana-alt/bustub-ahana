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
                             std::unique_lock<std::mutex> bpm_lock, std::shared_ptr<DiskScheduler> disk_scheduler,
                             bool increment_pin)
    : page_id_(page_id),
      frame_(std::move(frame)),
      replacer_(std::move(replacer)),
      bpm_latch_(std::move(bpm_latch)),
      disk_scheduler_(std::move(disk_scheduler)),
      is_valid_(true) {
  if (increment_pin) {
    frame_->pin_count_.fetch_add(1, std::memory_order_relaxed);
  }
  bpm_lock.unlock();
  rlatch_ = std::shared_lock<std::shared_mutex>(frame_->rwlatch_);
}

ReadPageGuard::ReadPageGuard(ReadPageGuard &&that) noexcept
    : page_id_(that.page_id_),
      frame_(std::move(that.frame_)),
      replacer_(std::move(that.replacer_)),
      bpm_latch_(std::move(that.bpm_latch_)),
      disk_scheduler_(std::move(that.disk_scheduler_)),
      rlatch_(std::move(that.rlatch_)),
      is_valid_(that.is_valid_) {
  that.is_valid_ = false;
}

auto ReadPageGuard::operator=(ReadPageGuard &&that) noexcept -> ReadPageGuard & {
  if (this != &that) {
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
    return;
  }

  is_valid_ = false;

  if (rlatch_.owns_lock()) {
    rlatch_.unlock();
  }

  std::scoped_lock lk(*bpm_latch_);
  size_t prev = frame_->pin_count_.fetch_sub(1, std::memory_order_relaxed);

  if (prev == 1) {
    replacer_->SetEvictable(frame_->frame_id_, true);
  }
}

ReadPageGuard::~ReadPageGuard() { Drop(); }

/**********************************************************************************************************************/

WritePageGuard::WritePageGuard(page_id_t page_id, std::shared_ptr<FrameHeader> frame,
                               std::shared_ptr<ArcReplacer> replacer, std::shared_ptr<std::mutex> bpm_latch,
                               std::unique_lock<std::mutex> bpm_lock, std::shared_ptr<DiskScheduler> disk_scheduler,
                               bool increment_pin)
    : page_id_(page_id),
      frame_(std::move(frame)),
      replacer_(std::move(replacer)),
      bpm_latch_(std::move(bpm_latch)),
      disk_scheduler_(std::move(disk_scheduler)),
      is_valid_(true) {
  if (increment_pin) {
    frame_->pin_count_.fetch_add(1);
  }
  bpm_lock.unlock();
  wlatch_ = std::unique_lock<std::shared_mutex>(frame_->rwlatch_);
}

WritePageGuard::WritePageGuard(WritePageGuard &&that) noexcept
    : page_id_(that.page_id_),
      frame_(std::move(that.frame_)),
      replacer_(std::move(that.replacer_)),
      bpm_latch_(std::move(that.bpm_latch_)),
      disk_scheduler_(std::move(that.disk_scheduler_)),
      wlatch_(std::move(that.wlatch_)),
      is_valid_(that.is_valid_) {
  that.is_valid_ = false;
}

auto WritePageGuard::operator=(WritePageGuard &&that) noexcept -> WritePageGuard & {
  if (this != &that) {
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
    return;
  }

  is_valid_ = false;

  if (wlatch_.owns_lock()) {
    wlatch_.unlock();
  }

  std::scoped_lock lk(*bpm_latch_);
  const size_t prev = frame_->pin_count_.fetch_sub(1, std::memory_order_relaxed);

  if (prev == 1) {
    replacer_->SetEvictable(frame_->frame_id_, true);
  }
}

WritePageGuard::~WritePageGuard() { Drop(); }

}  // namespace bustub
