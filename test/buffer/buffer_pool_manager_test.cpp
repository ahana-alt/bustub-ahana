//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager_test.cpp
//
// Identification: test/buffer/buffer_pool_manager_test.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <cstdio>
#include <filesystem>

#include <random>
#include "buffer/buffer_pool_manager.h"
#include "gtest/gtest.h"
#include "storage/page/page_guard.h"

namespace bustub {

static std::filesystem::path db_fname("test.bustub");

// The number of frames we give to the buffer pool.
const size_t FRAMES = 10;

void CopyString(char *dest, const std::string &src) {
  BUSTUB_ENSURE(src.length() + 1 <= BUSTUB_PAGE_SIZE, "CopyString src too long");
  snprintf(dest, BUSTUB_PAGE_SIZE, "%s", src.c_str());
}

TEST(BufferPoolManagerTest, DISABLED_VeryBasicTest) {
  // A very basic test.

  auto disk_manager = std::make_shared<DiskManager>(db_fname);
  auto bpm = std::make_shared<BufferPoolManager>(FRAMES, disk_manager.get());

  const page_id_t pid = bpm->NewPage();
  const std::string str = "Hello, world!";

  // Check `WritePageGuard` basic functionality.
  {
    auto guard = bpm->WritePage(pid);
    CopyString(guard.GetDataMut(), str);
    EXPECT_STREQ(guard.GetData(), str.c_str());
  }

  // Check `ReadPageGuard` basic functionality.
  {
    const auto guard = bpm->ReadPage(pid);
    EXPECT_STREQ(guard.GetData(), str.c_str());
  }

  // Check `ReadPageGuard` basic functionality (again).
  {
    const auto guard = bpm->ReadPage(pid);
    EXPECT_STREQ(guard.GetData(), str.c_str());
  }

  ASSERT_TRUE(bpm->DeletePage(pid));
}

TEST(BufferPoolManagerTest, DISABLED_PagePinEasyTest) {
  auto disk_manager = std::make_shared<DiskManager>(db_fname);
  auto bpm = std::make_shared<BufferPoolManager>(2, disk_manager.get());

  const page_id_t pageid0 = bpm->NewPage();
  const page_id_t pageid1 = bpm->NewPage();

  const std::string str0 = "page0";
  const std::string str1 = "page1";
  const std::string str0updated = "page0updated";
  const std::string str1updated = "page1updated";

  {
    auto page0_write_opt = bpm->CheckedWritePage(pageid0);
    ASSERT_TRUE(page0_write_opt.has_value());
    auto page0_write = std::move(page0_write_opt.value());  // NOLINT
    CopyString(page0_write.GetDataMut(), str0);

    auto page1_write_opt = bpm->CheckedWritePage(pageid1);
    // std::cout<<"page1_write_opt: "<<page1_write_opt;
    ASSERT_TRUE(page1_write_opt.has_value());
    auto page1_write = std::move(page1_write_opt.value());  // NOLINT
    CopyString(page1_write.GetDataMut(), str1);

    ASSERT_EQ(1, bpm->GetPinCount(pageid0));
    ASSERT_EQ(1, bpm->GetPinCount(pageid1));

    const auto temp_page_id1 = bpm->NewPage();
    const auto temp_page1_opt = bpm->CheckedReadPage(temp_page_id1);
    ASSERT_FALSE(temp_page1_opt.has_value());

    const auto temp_page_id2 = bpm->NewPage();
    const auto temp_page2_opt = bpm->CheckedWritePage(temp_page_id2);
    ASSERT_FALSE(temp_page2_opt.has_value());

    ASSERT_EQ(1, bpm->GetPinCount(pageid0));
    page0_write.Drop();
    ASSERT_EQ(0, bpm->GetPinCount(pageid0));

    ASSERT_EQ(1, bpm->GetPinCount(pageid1));
    page1_write.Drop();
    ASSERT_EQ(0, bpm->GetPinCount(pageid1));
  }

  {
    const auto temp_page_id1 = bpm->NewPage();
    const auto temp_page1_opt = bpm->CheckedReadPage(temp_page_id1);
    ASSERT_TRUE(temp_page1_opt.has_value());

    const auto temp_page_id2 = bpm->NewPage();
    const auto temp_page2_opt = bpm->CheckedWritePage(temp_page_id2);
    ASSERT_TRUE(temp_page2_opt.has_value());
    // std::cout<<"true?\n";

    ASSERT_FALSE(bpm->GetPinCount(pageid0).has_value());
    // std::cout<<"true?\n";
    ASSERT_FALSE(bpm->GetPinCount(pageid1).has_value());
  }

  {
    auto page0_write_opt = bpm->CheckedWritePage(pageid0);
    ASSERT_TRUE(page0_write_opt.has_value());
    auto page0_write = std::move(page0_write_opt.value());  // NOLINT
    EXPECT_STREQ(page0_write.GetData(), str0.c_str());
    CopyString(page0_write.GetDataMut(), str0updated);

    auto page1_write_opt = bpm->CheckedWritePage(pageid1);
    ASSERT_TRUE(page1_write_opt.has_value());
    auto page1_write = std::move(page1_write_opt.value());  // NOLINT
    EXPECT_STREQ(page1_write.GetData(), str1.c_str());
    CopyString(page1_write.GetDataMut(), str1updated);

    ASSERT_EQ(1, bpm->GetPinCount(pageid0));
    ASSERT_EQ(1, bpm->GetPinCount(pageid1));
  }

  ASSERT_EQ(0, bpm->GetPinCount(pageid0));
  ASSERT_EQ(0, bpm->GetPinCount(pageid1));

  {
    auto page0_read_opt = bpm->CheckedReadPage(pageid0);
    ASSERT_TRUE(page0_read_opt.has_value());
    const auto page0_read = std::move(page0_read_opt.value());  // NOLINT
    EXPECT_STREQ(page0_read.GetData(), str0updated.c_str());

    auto page1_read_opt = bpm->CheckedReadPage(pageid1);
    ASSERT_TRUE(page1_read_opt.has_value());
    const auto page1_read = std::move(page1_read_opt.value());  // NOLINT
    EXPECT_STREQ(page1_read.GetData(), str1updated.c_str());

    ASSERT_EQ(1, bpm->GetPinCount(pageid0));
    ASSERT_EQ(1, bpm->GetPinCount(pageid1));
  }

  ASSERT_EQ(0, bpm->GetPinCount(pageid0));
  ASSERT_EQ(0, bpm->GetPinCount(pageid1));

  remove(db_fname);
  remove(disk_manager->GetLogFileName());
}

TEST(BufferPoolManagerTest, DISABLED_PagePinMediumTest) {
  auto disk_manager = std::make_shared<DiskManager>(db_fname);
  auto bpm = std::make_shared<BufferPoolManager>(FRAMES, disk_manager.get());

  // Scenario: The buffer pool is empty. We should be able to create a new page.
  const auto pid0 = bpm->NewPage();
  auto page0 = bpm->WritePage(pid0);

  // Scenario: Once we have a page, we should be able to read and write content.
  const std::string hello = "Hello";
  CopyString(page0.GetDataMut(), hello);
  EXPECT_STREQ(page0.GetData(), hello.c_str());

  page0.Drop();

  // Create a vector of unique pointers to page guards, which prevents the guards from getting destructed.
  std::vector<WritePageGuard> pages;

  // Scenario: We should be able to create new pages until we fill up the buffer pool.
  for (size_t i = 0; i < FRAMES; i++) {
    const auto pid = bpm->NewPage();
    auto page = bpm->WritePage(pid);
    pages.push_back(std::move(page));
  }

  // Scenario: All of the pin counts should be 1.
  for (const auto &page : pages) {
    const auto pid = page.GetPageId();
    EXPECT_EQ(1, bpm->GetPinCount(pid));
  }

  // Scenario: Once the buffer pool is full, we should not be able to create any new pages.
  for (size_t i = 0; i < FRAMES; i++) {
    const auto pid = bpm->NewPage();
    const auto fail = bpm->CheckedWritePage(pid);
    ASSERT_FALSE(fail.has_value());
  }

  // Scenario: Drop the first 5 pages to unpin them.
  for (size_t i = 0; i < FRAMES / 2; i++) {
    const auto pid = pages[0].GetPageId();
    EXPECT_EQ(1, bpm->GetPinCount(pid));
    pages.erase(pages.begin());
    EXPECT_EQ(0, bpm->GetPinCount(pid));
  }

  // Scenario: All of the pin counts of the pages we haven't dropped yet should still be 1.
  for (const auto &page : pages) {
    const auto pid = page.GetPageId();
    EXPECT_EQ(1, bpm->GetPinCount(pid));
  }

  // Scenario: After unpinning pages {1, 2, 3, 4, 5}, we should be able to create 4 new pages and bring them into
  // memory. Bringing those 4 pages into memory should evict the first 4 pages {1, 2, 3, 4} because of LRU.
  for (size_t i = 0; i < ((FRAMES / 2) - 1); i++) {
    const auto pid = bpm->NewPage();
    auto page = bpm->WritePage(pid);
    pages.push_back(std::move(page));
  }

  // Scenario: There should be one frame available, and we should be able to fetch the data we wrote a while ago.
  {
    const auto original_page = bpm->ReadPage(pid0);
    EXPECT_STREQ(original_page.GetData(), hello.c_str());
  }

  // Scenario: Once we unpin page 0 and then make a new page, all the buffer pages should now be pinned. Fetching page 0
  // again should fail.
  const auto last_pid = bpm->NewPage();
  const auto last_page = bpm->ReadPage(last_pid);

  const auto fail = bpm->CheckedReadPage(pid0);
  ASSERT_FALSE(fail.has_value());

  // Shutdown the disk manager and remove the temporary file we created.
  disk_manager->ShutDown();
  remove(db_fname);
}

TEST(BufferPoolManagerTest, DISABLED_PageAccessTest) {
  const size_t rounds = 50;

  auto disk_manager = std::make_shared<DiskManager>(db_fname);
  auto bpm = std::make_shared<BufferPoolManager>(1, disk_manager.get());

  const auto pid = bpm->NewPage();
  char buf[BUSTUB_PAGE_SIZE];

  auto thread = std::thread([&]() {
    // The writer can keep writing to the same page.
    for (size_t i = 0; i < rounds; i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      auto guard = bpm->WritePage(pid);
      CopyString(guard.GetDataMut(), std::to_string(i));
    }
  });

  for (size_t i = 0; i < rounds; i++) {
    // Wait for a bit before taking the latch, allowing the writer to write some stuff.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // While we are reading, nobody should be able to modify the data.
    const auto guard = bpm->ReadPage(pid);

    // Save the data we observe.
    memcpy(buf, guard.GetData(), BUSTUB_PAGE_SIZE);

    // Sleep for a bit. If latching is working properly, nothing should be writing to the page.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Check that the data is unmodified.
    EXPECT_STREQ(guard.GetData(), buf);
  }

  thread.join();
}

TEST(BufferPoolManagerTest, DISABLED_ContentionTest) {
  auto disk_manager = std::make_shared<DiskManager>(db_fname);
  auto bpm = std::make_shared<BufferPoolManager>(FRAMES, disk_manager.get());

  const size_t rounds = 100000;

  const auto pid = bpm->NewPage();

  auto thread1 = std::thread([&]() {
    for (size_t i = 0; i < rounds; i++) {
      auto guard = bpm->WritePage(pid);
      CopyString(guard.GetDataMut(), std::to_string(i));
    }
  });

  auto thread2 = std::thread([&]() {
    for (size_t i = 0; i < rounds; i++) {
      auto guard = bpm->WritePage(pid);
      CopyString(guard.GetDataMut(), std::to_string(i));
    }
  });

  auto thread3 = std::thread([&]() {
    for (size_t i = 0; i < rounds; i++) {
      auto guard = bpm->WritePage(pid);
      CopyString(guard.GetDataMut(), std::to_string(i));
    }
  });

  auto thread4 = std::thread([&]() {
    for (size_t i = 0; i < rounds; i++) {
      auto guard = bpm->WritePage(pid);
      CopyString(guard.GetDataMut(), std::to_string(i));
    }
  });

  thread3.join();
  thread2.join();
  thread4.join();
  thread1.join();
}

TEST(BufferPoolManagerTest, DeadlockTest) {
  auto disk_manager = std::make_shared<DiskManager>(db_fname);
  auto bpm = std::make_shared<BufferPoolManager>(FRAMES, disk_manager.get());

  const auto pid0 = bpm->NewPage();
  const auto pid1 = bpm->NewPage();

  auto guard0 = bpm->WritePage(pid0);

  // A crude way of synchronizing threads, but works for this small case.
  std::atomic<bool> start = false;

  auto child = std::thread([&]() {
    // Acknowledge that we can begin the test.
    start.store(true);

    // Attempt to write to page 0.
    const auto guard0 = bpm->WritePage(pid0);
  });

  // Wait for the other thread to begin before we start the test.
  while (!start.load()) {
  }

  // Make the other thread wait for a bit.
  // This mimics the main thread doing some work while holding the write latch on page 0.
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));

  // If your latching mechanism is incorrect, the next line of code will deadlock.
  // Think about what might happen if you hold a certain "all-encompassing" latch for too long...

  // While holding page 0, take the latch on page 1.
  const auto guard1 = bpm->WritePage(pid1);

  // Let the child thread have the page 0 since we're done with it.
  guard0.Drop();

  child.join();
}

TEST(BufferPoolManagerTest, EvictableTest) {
  // Test if the evictable status of a frame is always correct.
  const size_t rounds = 1000;
  const size_t num_readers = 8;

  auto disk_manager = std::make_shared<DiskManager>(db_fname);
  // Only allocate one frame of memory to the buffer pool manager.
  auto bpm = std::make_shared<BufferPoolManager>(1, disk_manager.get());

  for (size_t i = 0; i < rounds; i++) {
    std::mutex mutex;
    std::condition_variable cv;

    // This signal tells the readers that they can start reading after the main thread has already taken the read latch.
    bool signal = false;

    // This page will be loaded into the only available frame.
    const auto winner_pid = bpm->NewPage();
    // We will attempt to load this page into the occupied frame, and it should fail every time.
    const auto loser_pid = bpm->NewPage();

    std::vector<std::thread> readers;
    for (size_t j = 0; j < num_readers; j++) {
      readers.emplace_back([&]() {
        std::unique_lock<std::mutex> lock(mutex);

        // Wait until the main thread has taken a read latch on the page.
        while (!signal) {
          cv.wait(lock);
        }

        // Read the page in shared mode.
        const auto read_guard = bpm->ReadPage(winner_pid);

        // Since the only frame is pinned, no thread should be able to bring in a new page.
        ASSERT_FALSE(bpm->CheckedReadPage(loser_pid).has_value());
      });
    }

    std::unique_lock<std::mutex> lock(mutex);

    if (i % 2 == 0) {
      // Take the read latch on the page and pin it.
      auto read_guard = bpm->ReadPage(winner_pid);

      // Wake up all of the readers.
      signal = true;
      cv.notify_all();
      lock.unlock();

      // Allow other threads to read.
      read_guard.Drop();
    } else {
      // Take the read latch on the page and pin it.
      auto write_guard = bpm->WritePage(winner_pid);

      // Wake up all of the readers.
      signal = true;
      cv.notify_all();
      lock.unlock();

      // Allow other threads to read.
      write_guard.Drop();
    }

    for (size_t i = 0; i < num_readers; i++) {
      readers[i].join();
    }
  }
}

TEST(BufferPoolManagerTest, ConcurrentReaderWriterTest) {
  // Small pool to force evictions while multiple readers/writers are active.
  constexpr size_t kFrames = 16;
  constexpr size_t kNumPages = 64;
  constexpr size_t kNumWriters = 4;
  constexpr size_t kNumReaders = 8;
  constexpr size_t kWriterRounds = 1500;  // work per-writer
  constexpr size_t kReaderRounds = 1500;  // work per-reader

  auto disk_manager = std::make_shared<DiskManager>(db_fname);
  auto bpm = std::make_shared<BufferPoolManager>(kFrames, disk_manager.get());

  // Pre-allocate a universe of pages.
  std::vector<page_id_t> page_ids;
  page_ids.reserve(kNumPages);
  for (size_t i = 0; i < kNumPages; i++) {
    page_ids.push_back(bpm->NewPage());
  }

  // Shared epochs per page; writers bump after each successful write.
  std::vector<std::atomic<uint32_t>> epochs(kNumPages);
  for (auto &e : epochs) {
    e.store(0, std::memory_order_relaxed);
  }

  // Deterministic page payload helpers.
  auto write_pattern = [](char *dst, page_id_t pid, uint32_t epoch) {
    // encode pid + epoch in first 8 bytes and fill rest with a deterministic pattern
    std::memcpy(dst + 0, &pid, sizeof(pid));
    std::memcpy(dst + 4, &epoch, sizeof(epoch));
    for (size_t i = 8; i < BUSTUB_PAGE_SIZE; i++) {
      dst[i] = static_cast<char>((pid + epoch + i) & 0xFF);
    }
  };
  auto check_pattern_pid_only = [](const char *src, page_id_t expected_pid) -> bool {
    page_id_t got_pid{0};
    std::memcpy(&got_pid, src + 0, sizeof(got_pid));
    return got_pid == expected_pid;
  };

  // Barrier-style start signal using cv/mutex (to match your test style).
  std::mutex start_mu;
  std::condition_variable start_cv;
  bool start_flag = false;

  // Launch writers.
  std::vector<std::thread> writers;
  writers.reserve(kNumWriters);
  for (size_t t = 0; t < kNumWriters; t++) {
    writers.emplace_back([&, t]() {
      // wait for start
      {
        std::unique_lock<std::mutex> lk(start_mu);
        while (!start_flag) {
          start_cv.wait(lk);
        }
      }
      for (size_t it = 0; it < kWriterRounds; it++) {
        const size_t idx = (it + t) % kNumPages;
        const page_id_t pid = page_ids[idx];

        // Exclusive modify
        auto w = bpm->WritePage(pid, AccessType::Unknown);
        char *buf = w.GetDataMut();  // must set dirty internally
        // choose next epoch
        const uint32_t next_epoch = epochs[idx].load(std::memory_order_relaxed) + 1;
        write_pattern(buf, pid, next_epoch);
        // release (unpin)
        w.Drop();

        // publish epoch after bytes are written
        epochs[idx].store(next_epoch, std::memory_order_release);

        // occasionally flush to exercise safe flush path
        if ((it % 127) == 0) {
          (void)bpm->FlushPage(pid);
        }
      }
    });
  }

  // Launch readers.
  std::vector<std::thread> readers;
  readers.reserve(kNumReaders);
  for (size_t t = 0; t < kNumReaders; t++) {
    readers.emplace_back([&, t]() {
      // wait for start
      {
        std::unique_lock<std::mutex> lk(start_mu);
        while (!start_flag) {
          start_cv.wait(lk);
        }
      }
      // each reader does a bunch of random reads
      std::mt19937 rng(static_cast<uint32_t>(0xC0FFEE + t));
      std::uniform_int_distribution<size_t> dist(0, kNumPages - 1);

      for (size_t it = 0; it < kReaderRounds; it++) {
        const size_t idx = dist(rng);
        const page_id_t pid = page_ids[idx];

        // Shared read
        auto r = bpm->ReadPage(pid, AccessType::Unknown);
        const char *buf = r.GetData();

        // At minimum, the encoded pid must match (protects against stale mapping / torn I/O).
        // std::cout<<"till here";
        ASSERT_TRUE(check_pattern_pid_only(buf, pid)) << "Reader saw wrong pid in page header: expected " << pid;

        r.Drop();
      }
    });
  }

  // Start all threads together (same pattern as your EvictableTest).
  {
    std::unique_lock<std::mutex> lk(start_mu);
    start_flag = true;
    start_cv.notify_all();
  }

  for (auto &th : writers) th.join();
  for (auto &th : readers) th.join();

  // Final spot checks: read a few pages and ensure pid matches.
  for (size_t i = 0; i < kNumPages; i += 7) {
    auto g = bpm->ReadPage(page_ids[i], AccessType::Unknown);
    EXPECT_TRUE(check_pattern_pid_only(g.GetData(), page_ids[i]));
    g.Drop();
  }
  std::cout << "till here";
  disk_manager->ShutDown();
  remove(db_fname);
}

}  // namespace bustub
