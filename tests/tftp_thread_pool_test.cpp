/**
 * @file tftp_thread_pool_test.cpp
 * @brief Unit tests for TftpThreadPool class
 */

#include <gtest/gtest.h>
#include "internal/tftp_thread_pool.h"
#include <chrono>
#include <atomic>
#include <vector>
#include <future>

using namespace tftpserver::internal;

class TftpThreadPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up any common resources for tests
    }

    void TearDown() override {
        // Clean up any common resources
    }
};

TEST_F(TftpThreadPoolTest, BasicConstruction) {
    // Test basic construction
    TftpThreadPool pool(4);
    EXPECT_EQ(pool.GetThreadCount(), 4);
    EXPECT_FALSE(pool.IsShuttingDown());
    EXPECT_EQ(pool.GetActiveTaskCount(), 0);
    EXPECT_EQ(pool.GetQueuedTaskCount(), 0);
}

TEST_F(TftpThreadPoolTest, AutomaticThreadCount) {
    // Test automatic thread count detection
    TftpThreadPool pool;
    EXPECT_GT(pool.GetThreadCount(), 0);
    EXPECT_LE(pool.GetThreadCount(), 64); // Should be clamped to max 64
}

TEST_F(TftpThreadPoolTest, SimpleTaskExecution) {
    TftpThreadPool pool(2);
    std::atomic<int> counter(0);

    // Submit a simple task
    auto future = pool.Submit([&counter]() {
        counter.fetch_add(1);
        return 42;
    });

    // Wait for completion and check result
    EXPECT_EQ(future.get(), 42);
    EXPECT_EQ(counter.load(), 1);
}

TEST_F(TftpThreadPoolTest, MultipleTaskExecution) {
    TftpThreadPool pool(4);
    std::atomic<int> counter(0);
    std::vector<std::future<void>> futures;

    // Submit multiple tasks
    const int task_count = 10;
    for (int i = 0; i < task_count; ++i) {
        futures.push_back(pool.Submit([&counter]() {
            counter.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }));
    }

    // Wait for all tasks to complete
    for (auto& future : futures) {
        future.get();
    }

    EXPECT_EQ(counter.load(), task_count);
}

TEST_F(TftpThreadPoolTest, TaskWithParameters) {
    TftpThreadPool pool(2);

    // Submit task with parameters
    auto future = pool.Submit([](int a, int b) {
        return a + b;
    }, 10, 20);

    EXPECT_EQ(future.get(), 30);
}

TEST_F(TftpThreadPoolTest, ExceptionHandling) {
    TftpThreadPool pool(2);

    // Submit task that throws exception
    auto future = pool.Submit([]() {
        throw std::runtime_error("Test exception");
        return 42;
    });

    // Exception should be propagated through future
    EXPECT_THROW(future.get(), std::runtime_error);
}

TEST_F(TftpThreadPoolTest, ConcurrentExecution) {
    TftpThreadPool pool(4);
    std::atomic<int> concurrent_count(0);
    std::atomic<int> max_concurrent(0);
    std::vector<std::future<void>> futures;

    // Submit tasks that track concurrency
    const int task_count = 10;
    for (int i = 0; i < task_count; ++i) {
        futures.push_back(pool.Submit([&concurrent_count, &max_concurrent]() {
            int current = concurrent_count.fetch_add(1) + 1;
            
            // Update max concurrent count
            int expected_max = max_concurrent.load();
            while (current > expected_max && 
                   !max_concurrent.compare_exchange_weak(expected_max, current)) {
                // Retry if another thread updated max_concurrent
            }
            
            // Simulate work
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            
            concurrent_count.fetch_sub(1);
        }));
    }

    // Wait for all tasks
    for (auto& future : futures) {
        future.get();
    }

    // Should have achieved some level of concurrency (at least 2 for 4 threads)
    EXPECT_GE(max_concurrent.load(), 2);
    EXPECT_EQ(concurrent_count.load(), 0);
}

TEST_F(TftpThreadPoolTest, ShutdownBehavior) {
    auto pool = std::make_unique<TftpThreadPool>(2);
    std::atomic<bool> task_executed(false);

    // Submit a long-running task
    auto future = pool->Submit([&task_executed]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        task_executed = true;
        return 42;
    });

    // Wait for task completion before shutdown
    EXPECT_EQ(future.get(), 42);
    EXPECT_TRUE(task_executed.load());

    // Shutdown should wait for running tasks
    pool->Shutdown();
    EXPECT_TRUE(pool->IsShuttingDown());

    // Submitting new tasks should fail
    EXPECT_THROW(
        pool->Submit([]() { return 1; }),
        std::runtime_error
    );
}

TEST_F(TftpThreadPoolTest, DestructorShutdown) {
    std::atomic<bool> task_completed(false);

    {
        TftpThreadPool pool(2);
        
        // Submit task before destruction
        auto future = pool.Submit([&task_completed]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            task_completed = true;
            return 99;
        });
        
        // Wait for task completion before destructor
        EXPECT_EQ(future.get(), 99);
        EXPECT_TRUE(task_completed.load());
    } // Pool destructor called here

    // Task should have completed before destructor
    EXPECT_TRUE(task_completed.load());
}

TEST_F(TftpThreadPoolTest, TaskCountAccuracy) {
    TftpThreadPool pool(2);
    std::vector<std::future<void>> futures;
    
    // Submit tasks that will block
    const int task_count = 5;
    std::atomic<int> ready_count(0);
    std::atomic<bool> release_tasks(false);
    
    for (int i = 0; i < task_count; ++i) {
        futures.push_back(pool.Submit([&ready_count, &release_tasks]() {
            ready_count.fetch_add(1);
            while (!release_tasks.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }));
    }

    // Wait for tasks to start
    while (ready_count.load() < std::min(task_count, 2)) { // Max 2 can run concurrently
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Check task counts
    size_t active = pool.GetActiveTaskCount();
    size_t queued = pool.GetQueuedTaskCount();
    
    EXPECT_GE(active, 1);
    EXPECT_LE(active, 2); // Should be <= thread count
    EXPECT_EQ(active + queued, task_count);

    // Release tasks
    release_tasks = true;
    
    // Wait for completion
    for (auto& future : futures) {
        future.get();
    }

    EXPECT_EQ(pool.GetActiveTaskCount(), 0);
    EXPECT_EQ(pool.GetQueuedTaskCount(), 0);
}