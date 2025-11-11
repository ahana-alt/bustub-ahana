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

TEST(BufferPoolManagerTest, DISABLED_DeadlockTest) {
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
  const size_t buffer_pool_size = 64;
  const size_t num_pages = 256;
  const size_t num_readers = 8;
  const size_t num_writers = 8;
  const size_t operations_per_thread = 100;

  auto disk_manager = std::make_shared<DiskManager>("test.db");
  auto bpm = std::make_shared<BufferPoolManager>(buffer_pool_size, disk_manager.get(), nullptr);

  // Initialize all pages with unique data
  for (size_t i = 0; i < num_pages; i++) {
    auto guard = bpm->WritePage(i);
    auto data = guard.GetDataMut();

    // Fill page with its page_id repeated
    for (size_t j = 0; j < BUSTUB_PAGE_SIZE / sizeof(page_id_t); j++) {
      reinterpret_cast<page_id_t *>(data)[j] = static_cast<page_id_t>(i);
    }
  }

  std::cout << "Initialized " << num_pages << " pages" << std::endl;

  std::atomic<bool> stop{false};
  std::atomic<size_t> errors{0};

  // Reader threads - read pages and verify consistency
  auto reader_func = [&](size_t thread_id) {
    std::random_device rd;
    std::mt19937 gen(rd() + thread_id);
    std::uniform_int_distribution<> dis(0, num_pages - 1);

    for (size_t i = 0; i < operations_per_thread && !stop; i++) {
      page_id_t page_id = dis(gen);

      try {
        auto guard = bpm->ReadPage(page_id);
        const auto *data = reinterpret_cast<const page_id_t *>(guard.GetData());

        // Verify all entries in the page match the expected page_id
        for (size_t j = 0; j < BUSTUB_PAGE_SIZE / sizeof(page_id_t); j++) {
          if (data[j] != page_id) {
            std::cerr << "Reader " << thread_id << " detected corruption! "
                      << "Page " << page_id << " position " << j << " has value " << data[j] << " (expected " << page_id
                      << ")" << std::endl;
            errors++;
            stop = true;
            break;
          }
        }
      } catch (const std::exception &e) {
        std::cerr << "Reader " << thread_id << " exception: " << e.what() << std::endl;
        errors++;
        stop = true;
      }
    }
  };

  // Writer threads - write to pages
  auto writer_func = [&](size_t thread_id) {
    std::random_device rd;
    std::mt19937 gen(rd() + thread_id + 1000);
    std::uniform_int_distribution<> dis(0, num_pages - 1);

    for (size_t i = 0; i < operations_per_thread && !stop; i++) {
      page_id_t page_id = dis(gen);

      try {
        auto guard = bpm->WritePage(page_id);
        auto *data = guard.GetDataMut();

        // Write page_id to all positions
        for (size_t j = 0; j < BUSTUB_PAGE_SIZE / sizeof(page_id_t); j++) {
          reinterpret_cast<page_id_t *>(data)[j] = page_id;
        }
      } catch (const std::exception &e) {
        std::cerr << "Writer " << thread_id << " exception: " << e.what() << std::endl;
        errors++;
        stop = true;
      }
    }
  };

  // Launch all threads
  std::vector<std::thread> threads;

  for (size_t i = 0; i < num_readers; i++) {
    threads.emplace_back(reader_func, i);
  }

  for (size_t i = 0; i < num_writers; i++) {
    threads.emplace_back(writer_func, i);
  }

  // Wait for all threads to complete
  for (auto &thread : threads) {
    thread.join();
  }

  std::cout << "Test completed with " << errors.load() << " errors" << std::endl;
  EXPECT_EQ(errors.load(), 0);

  disk_manager->ShutDown();
  remove("test.db");
}

TEST(BufferPoolManagerTest, StaircaseLoadTest) {
  // Tiny buffer to force eviction; plenty of pages to thrash.
  constexpr size_t kFrames = 8;
  constexpr size_t kNumPages = 64;

  struct Stage {
    size_t writers;
    size_t readers;
    size_t writer_rounds;
    size_t reader_rounds;
  };

  // Gradually increase pressure across stages.
  // const std::vector<Stage> kStages = {
  //     {2, 4, 300, 600}, {4, 6, 600, 900}, {6, 8, 900, 1200}, {8, 10, 1200, 1500},  // heaviest stage
  // };

  const std::vector<Stage> kStages = {
      {1, 1, 300, 600}  // heaviest stage
  };

  auto disk_manager = std::make_shared<DiskManager>(db_fname);
  auto bpm = std::make_shared<BufferPoolManager>(kFrames, disk_manager.get());

  // Allocate page ids once and reuse across stages.
  std::vector<page_id_t> page_ids;
  page_ids.reserve(kNumPages);
  for (size_t i = 0; i < kNumPages; i++) {
    page_ids.push_back(bpm->NewPage());
  }

  // Per-page version counters; writers bump these.
  std::vector<std::atomic<uint64_t>> page_versions(kNumPages);
  for (auto &v : page_versions) {
    v.store(0, std::memory_order_relaxed);
  }

  // Seed each page with version 0 pattern.
  for (size_t i = 0; i < kNumPages; i++) {
    const page_id_t pid = page_ids[i];
    auto guard = bpm->WritePage(pid, AccessType::Unknown);
    char *data = guard.GetDataMut();

    const uint64_t ver = 0;
    const uint64_t checksum = static_cast<uint64_t>(pid) ^ ver;

    std::memcpy(data, &pid, sizeof(page_id_t));           // [0..7] page_id
    std::memcpy(data + 8, &ver, sizeof(uint64_t));        // [8..15] version
    std::memcpy(data + 16, &checksum, sizeof(uint64_t));  // [16..23] checksum
    for (size_t j = 24; j < BUSTUB_PAGE_SIZE; j++) {
      data[j] = static_cast<char>((ver + j) & 0xFF);
    }
    guard.Drop();
  }

  auto run_stage = [&](const Stage &S) {
    std::atomic<size_t> wrong_page_reads{0};
    std::atomic<size_t> version_mismatches{0};
    std::atomic<size_t> torn_reads{0};

    std::atomic<bool> start_flag{false};
    std::atomic<size_t> ready_count{0};

    auto writer_func = [&](size_t tid) {
      ready_count.fetch_add(1, std::memory_order_relaxed);
      while (!start_flag.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      std::mt19937 rng(static_cast<uint32_t>(tid * 1337u + 4242u));
      std::uniform_int_distribution<size_t> dist(0, kNumPages - 1);

      for (size_t r = 0; r < S.writer_rounds; r++) {
        const size_t idx = dist(rng);
        const page_id_t pid = page_ids[idx];

        auto guard = bpm->WritePage(pid, AccessType::Unknown);

        const uint64_t new_version = page_versions[idx].fetch_add(1, std::memory_order_acq_rel) + 1;

        char *data = guard.GetDataMut();
        const uint64_t checksum = static_cast<uint64_t>(pid) ^ new_version;

        std::memcpy(data, &pid, sizeof(page_id_t));
        std::memcpy(data + 8, &new_version, sizeof(uint64_t));
        std::memcpy(data + 16, &checksum, sizeof(uint64_t));
        for (size_t i = 24; i < BUSTUB_PAGE_SIZE; i++) {
          data[i] = static_cast<char>((new_version + i) & 0xFF);
        }

        if ((r % 127) == 0) {
          guard.Flush();  // exercise disk path
        }
        guard.Drop();

        if ((r % 19) == 0) {
          std::this_thread::yield();
        }
      }
    };

    auto reader_func = [&](size_t tid) {
      ready_count.fetch_add(1, std::memory_order_relaxed);
      while (!start_flag.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      std::mt19937 rng(static_cast<uint32_t>(tid * 98765u + 123u));
      std::uniform_int_distribution<size_t> dist(0, kNumPages - 1);

      for (size_t r = 0; r < S.reader_rounds; r++) {
        const size_t idx = dist(rng);
        const page_id_t expected_pid = page_ids[idx];

        auto guard = bpm->ReadPage(expected_pid, AccessType::Unknown);
        const char *data = guard.GetData();

        page_id_t read_pid;
        uint64_t read_ver, read_sum;
        std::memcpy(&read_pid, data, sizeof(page_id_t));
        std::memcpy(&read_ver, data + 8, sizeof(uint64_t));
        std::memcpy(&read_sum, data + 16, sizeof(uint64_t));

        if (read_pid != expected_pid) {
          wrong_page_reads.fetch_add(1, std::memory_order_relaxed);
          guard.Drop();
          continue;
        }

        const uint64_t exp_sum = static_cast<uint64_t>(read_pid) ^ read_ver;
        if (read_sum != exp_sum) {
          version_mismatches.fetch_add(1, std::memory_order_relaxed);
          guard.Drop();
          continue;
        }

        bool pattern_ok = true;
        for (size_t j = 24; j < BUSTUB_PAGE_SIZE; j += 101) {
          const char expected_byte = static_cast<char>((read_ver + j) & 0xFF);
          if (data[j] != expected_byte) {
            pattern_ok = false;
            break;
          }
        }
        if (!pattern_ok) {
          torn_reads.fetch_add(1, std::memory_order_relaxed);
        }

        if ((r % 100) == 0) {
          std::this_thread::sleep_for(std::chrono::microseconds(80));
        }

        guard.Drop();
      }
    };

    std::vector<std::thread> threads;
    threads.reserve(S.writers + S.readers);
    for (size_t i = 0; i < S.writers; i++) threads.emplace_back(writer_func, i);
    for (size_t i = 0; i < S.readers; i++) threads.emplace_back(reader_func, i + S.writers);

    while (ready_count.load(std::memory_order_relaxed) < (S.writers + S.readers)) {
      std::this_thread::yield();
    }
    start_flag.store(true, std::memory_order_release);

    for (auto &t : threads) t.join();

    // Stage diagnostics
    std::cout << "[Staircase] writers=" << S.writers << " readers=" << S.readers << " wrong=" << wrong_page_reads.load()
              << " mismatch=" << version_mismatches.load() << " torn=" << torn_reads.load() << std::endl;

    EXPECT_EQ(wrong_page_reads.load(), 0ULL);
    EXPECT_EQ(version_mismatches.load(), 0ULL);
    EXPECT_EQ(torn_reads.load(), 0ULL);

    // Verify each page matches its final version & pattern.
    for (size_t i = 0; i < kNumPages; i++) {
      const page_id_t pid = page_ids[i];
      const uint64_t final_ver = page_versions[i].load(std::memory_order_acquire);

      auto guard = bpm->ReadPage(pid, AccessType::Unknown);
      const char *data = guard.GetData();

      page_id_t read_pid;
      uint64_t read_ver, read_sum;
      std::memcpy(&read_pid, data, sizeof(page_id_t));
      std::memcpy(&read_ver, data + 8, sizeof(uint64_t));
      std::memcpy(&read_sum, data + 16, sizeof(uint64_t));

      EXPECT_EQ(read_pid, pid);
      EXPECT_EQ(read_ver, final_ver);
      EXPECT_EQ(read_sum, (static_cast<uint64_t>(pid) ^ read_ver));

      bool body_ok = true;
      for (size_t j = 24; j < BUSTUB_PAGE_SIZE; j += 113) {
        const char exp = static_cast<char>((read_ver + j) & 0xFF);
        if (data[j] != exp) {
          body_ok = false;
          break;
        }
      }
      EXPECT_TRUE(body_ok) << "Body torn pid=" << pid << " ver=" << read_ver;

      guard.Drop();
    }

    // No pin leaks after this stage.
    for (auto pid : page_ids) {
      auto pin = bpm->GetPinCount(pid);
      ASSERT_TRUE(pin.has_value());
      EXPECT_EQ(*pin, 0UL) << "Pin leak on pid=" << pid;
    }

    // Optional: make durable between stages.
    bpm->FlushAllPages();
  };

  for (const auto &st : kStages) {
    run_stage(st);
  }

  disk_manager->ShutDown();
  remove(db_fname);
}

TEST(BufferPoolManagerTest, DISABLED_ConcurrentWritersOnlyTest) {
  // Tiny buffer pool to force frequent evictions & flushes.
  constexpr size_t kFrames = 8;
  constexpr size_t kNumPages = 64;
  constexpr size_t kNumWriters = 12;
  constexpr size_t kWriterRounds = 1500;

  auto disk_manager = std::make_shared<DiskManager>(db_fname);
  auto bpm = std::make_shared<BufferPoolManager>(kFrames, disk_manager.get());

  // Pre-allocate page ids.
  std::vector<page_id_t> page_ids;
  page_ids.reserve(kNumPages);
  for (size_t i = 0; i < kNumPages; i++) {
    page_ids.push_back(bpm->NewPage());
  }

  // Per-page version counters that writers will increment.
  std::vector<std::atomic<uint64_t>> page_versions(kNumPages);
  for (auto &v : page_versions) {
    v.store(0, std::memory_order_relaxed);
  }

  // Barrier for synchronized start.
  std::atomic<bool> start_flag{false};
  std::atomic<size_t> ready_count{0};

  auto writer_func = [&](size_t tid) {
    ready_count.fetch_add(1, std::memory_order_relaxed);
    while (!start_flag.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }

    std::mt19937 rng(static_cast<uint32_t>(tid * 1337 + 4242));
    std::uniform_int_distribution<size_t> dist(0, kNumPages - 1);

    for (size_t round = 0; round < kWriterRounds; round++) {
      const size_t idx = dist(rng);
      const page_id_t pid = page_ids[idx];

      // Exclusive write guard
      auto guard = bpm->WritePage(pid, AccessType::Unknown);

      // Increment per-page version first, then write it.
      const uint64_t new_version = page_versions[idx].fetch_add(1, std::memory_order_acq_rel) + 1;

      char *data = guard.GetDataMut();

      // [0..7]: page_id
      std::memcpy(data, &pid, sizeof(page_id_t));
      // [8..15]: version
      std::memcpy(data + 8, &new_version, sizeof(uint64_t));
      // [16..23]: checksum = pid ^ version
      const uint64_t checksum = static_cast<uint64_t>(pid) ^ new_version;
      std::memcpy(data + 16, &checksum, sizeof(uint64_t));
      // Body pattern dependent on version
      for (size_t i = 24; i < BUSTUB_PAGE_SIZE; i++) {
        data[i] = static_cast<char>((new_version + i) & 0xFF);
      }

      // Occasionally flush while holding the guard to stress disk path.
      if ((round % 127) == 0) {
        guard.Flush();
      }

      // Explicitly drop to exercise pin count transitions.
      guard.Drop();

      // Encourage interleaving.
      if ((round % 19) == 0) {
        std::this_thread::yield();
      }
    }
  };

  // Launch writers
  std::vector<std::thread> threads;
  threads.reserve(kNumWriters);
  for (size_t i = 0; i < kNumWriters; i++) {
    threads.emplace_back(writer_func, i);
  }

  // Start all together
  while (ready_count.load(std::memory_order_relaxed) < kNumWriters) {
    std::this_thread::yield();
  }
  start_flag.store(true, std::memory_order_release);

  for (auto &t : threads) {
    t.join();
  }

  // Final verification: every page's header and body must match the final version.
  for (size_t i = 0; i < kNumPages; i++) {
    const page_id_t expected_pid = page_ids[i];
    const uint64_t final_version = page_versions[i].load(std::memory_order_acquire);

    auto guard = bpm->ReadPage(expected_pid, AccessType::Unknown);
    const char *data = guard.GetData();

    page_id_t read_pid;
    std::memcpy(&read_pid, data, sizeof(page_id_t));
    EXPECT_EQ(read_pid, expected_pid) << "Page ID header corrupted for pid=" << expected_pid;

    uint64_t read_version;
    std::memcpy(&read_version, data + 8, sizeof(uint64_t));
    EXPECT_EQ(read_version, final_version) << "Version mismatch for pid=" << expected_pid;

    uint64_t read_checksum;
    std::memcpy(&read_checksum, data + 16, sizeof(uint64_t));
    EXPECT_EQ(read_checksum, (static_cast<uint64_t>(expected_pid) ^ read_version))
        << "Checksum mismatch for pid=" << expected_pid;

    bool pattern_ok = true;
    for (size_t j = 24; j < BUSTUB_PAGE_SIZE; j += 113) {
      const char expected_byte = static_cast<char>((read_version + j) & 0xFF);
      if (data[j] != expected_byte) {
        pattern_ok = false;
        break;
      }
    }
    EXPECT_TRUE(pattern_ok) << "Body pattern torn for pid=" << expected_pid << " version=" << read_version;

    guard.Drop();
  }

  // Ensure no pin leaks: every resident page should report pin_count = 0.
  for (auto pid : page_ids) {
    auto pin = bpm->GetPinCount(pid);
    ASSERT_TRUE(pin.has_value());
    EXPECT_EQ(*pin, 0UL) << "Pin leak on pid=" << pid;
  }

  disk_manager->ShutDown();
  remove(db_fname);
}

TEST(BufferPoolManagerTest, BetterConcurrentReaderWriterTest) {
  constexpr size_t kFrames = 8;
  constexpr size_t kNumPages = 32;
  constexpr size_t kNumWriters = 4;
  constexpr size_t kNumReaders = 8;
  constexpr size_t kWriterRounds = 500;
  constexpr size_t kReaderRounds = 1000;

  auto disk_manager = std::make_shared<DiskManager>(db_fname);
  auto bpm = std::make_shared<BufferPoolManager>(kFrames, disk_manager.get());

  // Pre-allocate pages
  std::vector<page_id_t> page_ids;
  page_ids.reserve(kNumPages);
  for (size_t i = 0; i < kNumPages; i++) {
    page_ids.push_back(bpm->NewPage());
  }

  // Each page has a version counter that gets incremented by writers
  std::vector<std::atomic<uint64_t>> page_versions(kNumPages);
  for (auto &v : page_versions) {
    v.store(0, std::memory_order_relaxed);
  }

  // Track errors
  std::atomic<size_t> version_mismatches{0};
  std::atomic<size_t> torn_reads{0};
  std::atomic<size_t> wrong_page_reads{0};

  // Barrier for synchronized start
  std::atomic<bool> start_flag{false};
  std::atomic<size_t> ready_count{0};

  // Writer threads: increment version and write pattern
  auto writer_func = [&](size_t thread_id) {
    ready_count.fetch_add(1);
    while (!start_flag.load()) {
      std::this_thread::yield();
    }

    std::mt19937 rng(thread_id * 12345);
    std::uniform_int_distribution<size_t> dist(0, kNumPages - 1);

    for (size_t round = 0; round < kWriterRounds; round++) {
      size_t idx = dist(rng);
      page_id_t pid = page_ids[idx];

      // Get write guard
      auto guard = bpm->WritePage(pid, AccessType::Unknown);

      // Increment version atomically
      uint64_t new_version = page_versions[idx].fetch_add(1, std::memory_order_release) + 1;

      // Write pattern: [page_id][version][checksum][repeating pattern]
      char *data = guard.GetDataMut();

      // Write page_id in first 8 bytes
      std::memcpy(data, &pid, sizeof(page_id_t));

      // Write version in next 8 bytes
      std::memcpy(data + 8, &new_version, sizeof(uint64_t));

      // Calculate checksum of page_id + version
      uint64_t checksum = static_cast<uint64_t>(pid) ^ new_version;
      std::memcpy(data + 16, &checksum, sizeof(uint64_t));

      // Fill rest with deterministic pattern based on version
      for (size_t i = 24; i < BUSTUB_PAGE_SIZE; i++) {
        data[i] = static_cast<char>((new_version + i) & 0xFF);
      }

      // Explicitly drop to test pin count handling
      guard.Drop();

      // Occasionally yield to encourage interleaving
      if (round % 10 == 0) {
        std::this_thread::yield();
      }
    }
  };

  // Reader threads: verify consistency
  auto reader_func = [&](size_t thread_id) {
    ready_count.fetch_add(1);
    while (!start_flag.load()) {
      std::this_thread::yield();
    }

    std::mt19937 rng(thread_id * 54321);
    std::uniform_int_distribution<size_t> dist(0, kNumPages - 1);

    for (size_t round = 0; round < kReaderRounds; round++) {
      size_t idx = dist(rng);
      page_id_t expected_pid = page_ids[idx];

      // Get read guard
      auto guard = bpm->ReadPage(expected_pid, AccessType::Unknown);
      const char *data = guard.GetData();

      // Read page_id
      page_id_t read_pid;
      std::memcpy(&read_pid, data, sizeof(page_id_t));

      // Read version
      uint64_t read_version;
      std::memcpy(&read_version, data + 8, sizeof(uint64_t));

      // Read checksum
      uint64_t read_checksum;
      std::memcpy(&read_checksum, data + 16, sizeof(uint64_t));

      // CRITICAL CHECKS:

      // 1. Page ID must match
      if (read_pid != expected_pid) {
        wrong_page_reads.fetch_add(1);
        std::cout << "[READER-" << thread_id << "] ERROR: Expected page " << expected_pid << " but read page "
                  << read_pid << std::endl;
        continue;
      }

      // 2. Checksum must be consistent with page_id and version
      uint64_t expected_checksum = static_cast<uint64_t>(read_pid) ^ read_version;
      if (read_checksum != expected_checksum) {
        version_mismatches.fetch_add(1);
        std::cout << "[READER-" << thread_id << "] ERROR: Checksum mismatch on page " << expected_pid << " version "
                  << read_version << std::endl;
        continue;
      }

      // 3. Pattern must be consistent with version (check a few spots)
      bool pattern_valid = true;
      for (size_t i = 24; i < BUSTUB_PAGE_SIZE; i += 100) {
        char expected_byte = static_cast<char>((read_version + i) & 0xFF);
        if (data[i] != expected_byte) {
          pattern_valid = false;
          break;
        }
      }

      if (!pattern_valid) {
        torn_reads.fetch_add(1);
        std::cout << "[READER-" << thread_id << "] ERROR: Torn read detected on page " << expected_pid << " version "
                  << read_version << std::endl;
      }

      // Sleep briefly while holding the lock to increase chance of contention
      if (round % 50 == 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
      }

      guard.Drop();
    }
  };

  // Launch all threads
  std::vector<std::thread> threads;

  for (size_t i = 0; i < kNumWriters; i++) {
    threads.emplace_back(writer_func, i);
  }

  for (size_t i = 0; i < kNumReaders; i++) {
    threads.emplace_back(reader_func, i + kNumWriters);
  }

  // Wait for all threads to be ready
  while (ready_count.load() < (kNumWriters + kNumReaders)) {
    std::this_thread::yield();
  }

  std::cout << "All threads ready, starting test..." << std::endl;
  start_flag.store(true);

  // Join all threads
  for (auto &t : threads) {
    t.join();
  }

  // Report results
  std::cout << "Test complete!" << std::endl;
  std::cout << "Wrong page reads: " << wrong_page_reads.load() << std::endl;
  std::cout << "Version mismatches: " << version_mismatches.load() << std::endl;
  std::cout << "Torn reads: " << torn_reads.load() << std::endl;

  // All should be zero for correct implementation
  EXPECT_EQ(wrong_page_reads.load(), 0);
  EXPECT_EQ(version_mismatches.load(), 0);
  EXPECT_EQ(torn_reads.load(), 0);

  // Verify final state - read each page and check consistency
  for (size_t i = 0; i < kNumPages; i++) {
    auto guard = bpm->ReadPage(page_ids[i], AccessType::Unknown);
    const char *data = guard.GetData();

    page_id_t read_pid;
    std::memcpy(&read_pid, data, sizeof(page_id_t));

    EXPECT_EQ(read_pid, page_ids[i]) << "Final verification failed for page " << page_ids[i];
  }

  disk_manager->ShutDown();
  remove(db_fname);
}

}  // namespace bustub
