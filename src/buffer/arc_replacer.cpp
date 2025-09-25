// :bustub-keep-private:
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

/**
 *
 * TODO(P1): Add implementation
 *
 * @brief a new ArcReplacer, with lists initialized to be empty and target size to 0
 * @param num_frames the maximum number of frames the ArcReplacer will be required to cache
 */
ArcReplacer::ArcReplacer(size_t num_frames) : replacer_size_(num_frames) {}

/**
 * TODO(P1): Add implementation
 *
 * @brief Performs the Replace operation as described by the writeup
 * that evicts from either mfu_ or mru_ into its corresponding ghost list
 * according to balancing policy.
 *
 * If you wish to refer to the original ARC paper, please note that there are
 * two changes in our implementation:
 * 1. When the size of mru_ equals the target size, we don't check
 * the last access as the paper did when deciding which list to evict from.
 * This is fine since the original decision is stated to be arbitrary.
 * 2. Entries that are not evictable are skipped. If all entries from the desired side
 * (mru_ / mfu_) are pinned, we instead try victimize the other side (mfu_ / mru_),
 * and move it to its corresponding ghost list (mfu_ghost_ / mru_ghost_).
 *
 * @return frame id of the evicted frame, or std::nullopt if cannot evict
 */
auto ArcReplacer::Evict() -> std::optional<frame_id_t> {
  std::lock_guard<std::mutex> guard(latch_);
  int flag = 0;
  if (mru_.size() >= mru_target_size_) {
    // primary target mru
    // print mru list
    // std::cout << "mru ->";
    // for (auto it = mru_.begin(); it != mru_.end(); ++it) {
    //   std::cout << *it << " ";
    // }
    // std::cout << std::endl;
    flag = 1;
    for (auto it = mru_.rbegin(); it != mru_.rend(); ++it) {
      auto frame_status = alive_map_[*it];
      if (frame_status->evictable_) {
        // evict this one
        frame_id_t victim_id = *it;
        mru_.remove(victim_id);
        alive_map_.erase(victim_id);
        curr_size_--;
        // add to ghost
        mru_ghost_.push_front(frame_status->page_id_);
        frame_status->arc_status_ = ArcStatus::MRU_GHOST;
        frame_status->frame_id_ = -1;      // ghost has no frame id
        frame_status->evictable_ = false;  // ghost is not evictable
        ghost_map_[frame_status->page_id_] = frame_status;
        std::cout << "victim id cond1: " << victim_id << std::endl;
        return victim_id;
      }
    }
  }
  // target mfu
  for (auto it = mfu_.rbegin(); it != mfu_.rend(); ++it) {
    // std::cout << "mfu ->";
    // for (auto it = mfu_.begin(); it != mfu_.end(); ++it) {
    //   std::cout << *it << " ";
    // }
    auto frame_status = alive_map_[*it];
    if (frame_status->evictable_) {
      // evict this one
      frame_id_t victim_id = *it;
      std::cout << *it << std::endl;
      mfu_.remove(victim_id);
      alive_map_.erase(victim_id);
      curr_size_--;
      // add to ghost
      mfu_ghost_.push_front(frame_status->page_id_);
      frame_status->arc_status_ = ArcStatus::MFU_GHOST;
      frame_status->frame_id_ = -1;      // ghost has no frame id
      frame_status->evictable_ = false;  // ghost is not evictable
      ghost_map_[frame_status->page_id_] = frame_status;
      std::cout << "victim id cond2: " << victim_id << std::endl;
      return victim_id;
    }
  }
  if (flag != 1) {
    // check mru again
    for (auto it = mru_.rbegin(); it != mru_.rend(); ++it) {
      auto frame_status = alive_map_[*it];
      if (frame_status->evictable_) {
        // evict this one
        frame_id_t victim_id = *it;
        mru_.remove(victim_id);
        alive_map_.erase(victim_id);
        curr_size_--;
        // add to ghost
        mru_ghost_.push_front(frame_status->page_id_);
        frame_status->arc_status_ = ArcStatus::MRU_GHOST;
        frame_status->frame_id_ = -1;      // ghost has no frame id
        frame_status->evictable_ = false;  // ghost is not evictable
        ghost_map_[frame_status->page_id_] = frame_status;
        std::cout << "victim id cond3: " << victim_id << std::endl;
        return victim_id;
      }
    }
  }
  return std::nullopt;
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Record access to a frame, adjusting ARC bookkeeping accordingly
 * by bring the accessed page to the front of mfu_ if it exists in any of the lists
 * or the front of mru_ if it does not.
 *
 * Performs the operations EXCEPT REPLACE described in original paper, which is
 * handled by `Evict()`.
 *
 * Consider the following four cases, handle accordingly:
 * 1. Access hits mru_ or mfu_
 * 2/3. Access hits mru_ghost_ / mfu_ghost_
 * 4. Access misses all the lists
 *
 * This routine performs all changes to the four lists as preperation
 * for `Evict()` to simply find and evict a victim into ghost lists.
 *
 * Note that frame_id is used as identifier for alive pages and
 * page_id is used as identifier for the ghost pages, since page_id is
 * the unique identifier to the page after it's dead.
 * Using page_id for alive pages should be the same since it's one to one mapping,
 * but using frame_id is slightly more intuitive.
 *
 * @param frame_id id of frame that received a new access.
 * @param page_id id of page that is mapped to the frame.
 * @param access_type type of access that was received. This parameter is only needed for
 * leaderboard tests.
 */
void ArcReplacer::RecordAccess(frame_id_t frame_id, page_id_t page_id, [[maybe_unused]] AccessType access_type) {
  std::cout << frame_id << " " << page_id << " mru target size: " << mru_target_size_ << std::endl;

  std::lock_guard<std::mutex> guard(latch_);

  auto it = alive_map_.find(frame_id);
  auto ghost_it = ghost_map_.find(page_id);
  auto mru_ghost_size = mru_ghost_.size();
  auto mfu_ghost_size = mfu_ghost_.size();
  auto mru_size = mru_.size();
  auto mfu_size = mfu_.size();
  // print all these sizes
  std::cout << "mru size: " << mru_size << " mfu size: " << mfu_size << " mru ghost size: " << mru_ghost_size
            << " mfu ghost size: " << mfu_ghost_size << std::endl;
  if (it != alive_map_.end()) {
    // hit
    std::cout << "hit alive: " << frame_id << " " << page_id << std::endl;
    if (it->second->arc_status_ == ArcStatus::MRU) {
      // move to mfu_
      mru_.remove(frame_id);
      mfu_.push_front(frame_id);
      it->second->arc_status_ = ArcStatus::MFU;
    } else {
      // already in mfu_, move to front
      mfu_.remove(frame_id);
      mfu_.push_front(frame_id);
    }
  } else if (ghost_it != ghost_map_.end()) {
    // hit ghost
    // check which ghost list
    std::cout << "hit ghost: " << frame_id << " " << page_id << std::endl;
    if (ghost_it->second->arc_status_ == ArcStatus::MRU_GHOST) {
      if (mru_ghost_size >= mfu_ghost_size) {
        mru_target_size_ = std::min(mru_target_size_ + 1, replacer_size_);
      } else {
        // Use proper floating point division, then convert to integer
        size_t delta =
            std::max(1UL, static_cast<size_t>(std::ceil(static_cast<double>(mfu_ghost_size) / mru_ghost_size)));
        mru_target_size_ = std::min(mru_target_size_ + delta, replacer_size_);
      }

      // move to mfu_
      ghost_it->second->arc_status_ = ArcStatus::MFU;
      ghost_it->second->frame_id_ = frame_id;
      ghost_it->second->evictable_ = false;  // when brought back, it's not evictable
      alive_map_[frame_id] = ghost_it->second;
      ghost_map_.erase(ghost_it);
      mru_ghost_.remove(page_id);
      mfu_.push_front(frame_id);
    } else {
      if (mfu_ghost_size >= mru_ghost_size) {
        mru_target_size_ = (mru_target_size_ > 0) ? mru_target_size_ - 1 : 0;
      } else {
        size_t delta =
            std::max(1UL, static_cast<size_t>(std::ceil(static_cast<double>(mru_ghost_size) / mfu_ghost_size)));
        mru_target_size_ = (mru_target_size_ > delta) ? mru_target_size_ - delta : 0;
      }
      // move to mfu_
      ghost_it->second->arc_status_ = ArcStatus::MFU;
      ghost_it->second->frame_id_ = frame_id;
      ghost_it->second->evictable_ = false;  // when brought back, it's evictable
      alive_map_[frame_id] = ghost_it->second;
      ghost_map_.erase(ghost_it);
      mfu_ghost_.remove(page_id);
      mfu_.push_front(frame_id);
    }
  } else {
    // miss
    std::cout << "miss: " << frame_id << " " << page_id << std::endl;
    if (mru_size + mru_ghost_size == replacer_size_) {
      // kill last element in mru ghost
      page_id_t victim_id = mru_ghost_.back();
      mru_ghost_.pop_back();
      ghost_map_.erase(victim_id);

      // add in front of mru
      mru_.push_front(frame_id);
      auto new_frame = std::make_shared<FrameStatus>(page_id, frame_id, false, ArcStatus::MRU);
      alive_map_[frame_id] = new_frame;
    } else {
      if (mru_ghost_size + mfu_ghost_size + mru_size + mfu_size == 2 * replacer_size_) {
        // kill last element in mfu ghost
        page_id_t victim_id = mfu_ghost_.back();
        mfu_ghost_.pop_back();
        ghost_map_.erase(victim_id);

        // add in front of mru
        mru_.push_front(frame_id);
        auto new_frame = std::make_shared<FrameStatus>(page_id, frame_id, false, ArcStatus::MRU);
        alive_map_[frame_id] = new_frame;
      } else {
        // add in front of mru
        mru_.push_front(frame_id);
        auto new_frame = std::make_shared<FrameStatus>(page_id, frame_id, false, ArcStatus::MRU);
        alive_map_[frame_id] = new_frame;
      }
    }
  }
  //   std::cout << "mru ->";
  //   for (auto it = mru_.begin(); it != mru_.end(); ++it) {
  //     std::cout << *it << " ";
  //   }
  //   std::cout << std::endl;
  //   std::cout << "mfu ->";
  //   for (auto it = mfu_.begin(); it != mfu_.end(); ++it) {
  //     std::cout << *it << " ";
  //   }
  //   std::cout << std::endl;
  //   std::cout << "mru ghost->";
  //   for (auto it = mru_ghost_.begin(); it != mru_ghost_.end(); ++it) {
  //     std::cout << *it << " ";
  //   }
  //   std::cout << std::endl;
  //   std::cout << "mfu ghost->";
  //   for (auto it = mfu_ghost_.begin(); it != mfu_ghost_.end(); ++it) {
  //     std::cout << *it << " ";
  //   }
  //   std::cout << std::endl;
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Toggle whether a frame is evictable or non-evictable. This function also
 * controls replacer's size. Note that size is equal to number of evictable entries.
 *
 * If a frame was previously evictable and is to be set to non-evictable, then size should
 * decrement. If a frame was previously non-evictable and is to be set to evictable,
 * then size should increment.
 *
 * If frame id is invalid, throw an exception or abort the process.
 *
 * For other scenarios, this function should terminate without modifying anything.
 *
 * @param frame_id id of frame whose 'evictable' status will be modified
 * @param set_evictable whether the given frame is evictable or not
 */
void ArcReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::lock_guard<std::mutex> guard(latch_);

  auto it = alive_map_.find(frame_id);
  if (it == alive_map_.end()) {
    throw std::runtime_error("Frame not found");
  }
  if (set_evictable && !it->second->evictable_) {
    it->second->evictable_ = true;
    curr_size_++;
  } else if (!set_evictable && it->second->evictable_) {
    it->second->evictable_ = false;
    curr_size_--;
  }
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Remove an evictable frame from replacer.
 * This function should also decrement replacer's size if removal is successful.
 *
 * Note that this is different from evicting a frame, which always remove the frame
 * decided by the ARC algorithm.
 *
 * If Remove is called on a non-evictable frame, throw an exception or abort the
 * process.
 *
 * If specified frame is not found, directly return from this function.
 *
 * @param frame_id id of frame to be removed
 */
void ArcReplacer::Remove(frame_id_t frame_id) {
  std::lock_guard<std::mutex> guard(latch_);

  auto it = alive_map_.find(frame_id);
  if (it == alive_map_.end()) {
    return;
  }
  if (!it->second->evictable_) {
    throw std::runtime_error("Frame not evictable");
  }
  // remove from list
  if (it->second->arc_status_ == ArcStatus::MRU) {
    mru_.remove(frame_id);
  } else {
    mfu_.remove(frame_id);
  }
  alive_map_.erase(it);
  curr_size_--;
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Return replacer's size, which tracks the number of evictable frames.
 *
 * @return size_t
 */
auto ArcReplacer::Size() -> size_t {
  std::lock_guard<std::mutex> guard(latch_);
  return curr_size_;
}

}  // namespace bustub
