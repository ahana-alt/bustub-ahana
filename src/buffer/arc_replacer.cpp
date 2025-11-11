//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// arc_replacer.cpp
//
// Identification: src/buffer/arc_replacer.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/arc_replacer.h"
#include <cmath>
#include <optional>
#include "common/config.h"

namespace bustub {

ArcReplacer::ArcReplacer(size_t num_frames) : replacer_size_(num_frames) {}

auto ArcReplacer::Evict() -> std::optional<frame_id_t> {
  std::lock_guard<std::mutex> guard(latch_);

  auto evict_from = [&](std::list<frame_id_t> &alive_list, std::list<page_id_t> &ghost_list,
                        std::unordered_map<page_id_t, std::list<page_id_t>::iterator> &ghost_iters,
                        ArcStatus ghost_status) -> std::optional<frame_id_t> {
    for (auto rit = alive_list.rbegin(); rit != alive_list.rend(); ++rit) {
      const frame_id_t fid = *rit;

      auto mit = alive_map_.find(fid);
      if (mit == alive_map_.end()) {
        continue;
      }

      std::shared_ptr<FrameStatus> fs = mit->second;
      if (!fs || !fs->evictable_) {
        continue;
      }

      fs->evictable_ = false;
      curr_size_--;

      // Erase using the stored iterator for O(1) removal
      alive_list.erase(std::next(rit).base());

      alive_map_.erase(mit);

      const page_id_t pid = fs->page_id_;
      ghost_list.push_front(pid);
      ghost_iters[pid] = ghost_list.begin();  // Store iterator for O(1) removal
      fs->arc_status_ = ghost_status;
      fs->frame_id_ = -1;
      fs->evictable_ = false;
      fs->list_iter_ = std::list<frame_id_t>::iterator();  // Reset iterator
      ghost_map_[pid] = fs;

      return fid;
    }
    return std::nullopt;
  };

  const bool try_mru_first = (mru_.size() >= mru_target_size_);

  if (try_mru_first) {
    if (auto v = evict_from(mru_, mru_ghost_, mru_ghost_iters_, ArcStatus::MRU_GHOST)) {
      return v;
    }
    if (auto v = evict_from(mfu_, mfu_ghost_, mfu_ghost_iters_, ArcStatus::MFU_GHOST)) {
      return v;
    }
  } else {
    if (auto v = evict_from(mfu_, mfu_ghost_, mfu_ghost_iters_, ArcStatus::MFU_GHOST)) {
      return v;
    }
    if (auto v = evict_from(mru_, mru_ghost_, mru_ghost_iters_, ArcStatus::MRU_GHOST)) {
      return v;
    }
  }

  return std::nullopt;
}

void ArcReplacer::RecordAccess(frame_id_t frame_id, page_id_t page_id, [[maybe_unused]] AccessType access_type) {
  std::lock_guard<std::mutex> guard(latch_);

  auto a_it = alive_map_.find(frame_id);
  auto g_it = ghost_map_.find(page_id);

  const size_t mru_g_sz = mru_ghost_.size();
  const size_t mfu_g_sz = mfu_ghost_.size();
  const size_t mru_sz = mru_.size();
  const size_t mfu_sz = mfu_.size();

  auto delta_from = [](size_t lhs, size_t rhs) -> size_t {
    if (lhs >= rhs) {
      return 1;
    }
    if (lhs > 0) {
      return std::max<size_t>(1, rhs / lhs);
    }
    return 1;
  };

  // Case 1: Page already in MRU or MFU
  if (a_it != alive_map_.end()) {
    auto fs = a_it->second;
    fs->page_id_ = page_id;

    // O(1) removal using stored iterator
    if (fs->arc_status_ == ArcStatus::MRU) {
      mru_.erase(fs->list_iter_);
    } else if (fs->arc_status_ == ArcStatus::MFU) {
      mfu_.erase(fs->list_iter_);
    }

    // Add to front of MFU
    mfu_.push_front(frame_id);
    fs->list_iter_ = mfu_.begin();
    fs->arc_status_ = ArcStatus::MFU;
    return;
  }

  // Case 2/3: Page in ghost lists
  if (g_it != ghost_map_.end()) {
    std::shared_ptr<FrameStatus> fs = g_it->second;

    if (fs->arc_status_ == ArcStatus::MRU_GHOST) {
      const size_t d = delta_from(mru_g_sz, mfu_g_sz);
      mru_target_size_ = std::min(mru_target_size_ + d, replacer_size_);

      // O(1) removal using stored iterator
      auto ghost_it = mru_ghost_iters_.find(page_id);
      if (ghost_it != mru_ghost_iters_.end()) {
        mru_ghost_.erase(ghost_it->second);
        mru_ghost_iters_.erase(ghost_it);
      }
      ghost_map_.erase(g_it);

      fs->arc_status_ = ArcStatus::MFU;
      fs->frame_id_ = frame_id;
      fs->evictable_ = false;

      mfu_.push_front(frame_id);
      fs->list_iter_ = mfu_.begin();
      alive_map_.emplace(frame_id, fs);
      return;
    }

    // MFU_GHOST case
    const size_t d = delta_from(mfu_g_sz, mru_g_sz);
    if (mru_target_size_ > d) {
      mru_target_size_ -= d;
    } else {
      mru_target_size_ = 0;
    }

    // O(1) removal using stored iterator
    auto ghost_it = mfu_ghost_iters_.find(page_id);
    if (ghost_it != mfu_ghost_iters_.end()) {
      mfu_ghost_.erase(ghost_it->second);
      mfu_ghost_iters_.erase(ghost_it);
    }
    ghost_map_.erase(g_it);

    fs->arc_status_ = ArcStatus::MFU;
    fs->frame_id_ = frame_id;
    fs->evictable_ = false;

    mfu_.push_front(frame_id);
    fs->list_iter_ = mfu_.begin();
    alive_map_.emplace(frame_id, fs);
    return;
  }

  // Case 4: Page not in any list
  if (mru_sz + mru_g_sz == replacer_size_) {
    if (!mru_ghost_.empty()) {
      page_id_t tail_pid = mru_ghost_.back();
      mru_ghost_.pop_back();
      mru_ghost_iters_.erase(tail_pid);
      ghost_map_.erase(tail_pid);
    }

    mru_.push_front(frame_id);
    auto fs = std::make_shared<FrameStatus>(page_id, frame_id, /*evictable=*/false, ArcStatus::MRU);
    fs->list_iter_ = mru_.begin();
    alive_map_.emplace(frame_id, std::move(fs));
    return;
  }

  if (mru_sz + mru_g_sz + mfu_sz + mfu_g_sz == 2 * replacer_size_) {
    if (!mfu_ghost_.empty()) {
      page_id_t tail_pid = mfu_ghost_.back();
      mfu_ghost_.pop_back();
      mfu_ghost_iters_.erase(tail_pid);
      ghost_map_.erase(tail_pid);
    }
  }

  mru_.push_front(frame_id);
  auto fs = std::make_shared<FrameStatus>(page_id, frame_id, /*evictable=*/false, ArcStatus::MRU);
  fs->list_iter_ = mru_.begin();
  alive_map_.emplace(frame_id, std::move(fs));
}

void ArcReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::lock_guard<std::mutex> lk(latch_);
  auto it = alive_map_.find(frame_id);
  if (it == alive_map_.end()) {
    return;
  }
  if (it->second->evictable_ == set_evictable) {
    return;
  }
  it->second->evictable_ = set_evictable;
  if (set_evictable) {
    ++curr_size_;
  } else {
    if (curr_size_ > 0) {
      --curr_size_;
    }
  }
}

void ArcReplacer::Remove(frame_id_t frame_id) {
  std::lock_guard<std::mutex> lk(latch_);
  auto it = alive_map_.find(frame_id);
  if (it == alive_map_.end()) {
    return;
  }

  auto fs = it->second;

  // Adjust size if this entry was counted
  if (fs->evictable_ && curr_size_ > 0) {
    --curr_size_;
  }

  // O(1) removal using stored iterator
  if (fs->arc_status_ == ArcStatus::MRU) {
    mru_.erase(fs->list_iter_);
  } else if (fs->arc_status_ == ArcStatus::MFU) {
    mfu_.erase(fs->list_iter_);
  }

  alive_map_.erase(it);
}

auto ArcReplacer::Size() -> size_t {
  std::lock_guard<std::mutex> guard(latch_);
  return curr_size_;
}

}  // namespace bustub
