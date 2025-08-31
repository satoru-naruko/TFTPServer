/**
 * @file tftp_thread_pool.h
 * @brief Thread pool implementation for TFTP server
 */

#ifndef TFTP_THREAD_POOL_H_
#define TFTP_THREAD_POOL_H_

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <atomic>
#include <vector>
#include <future>

namespace tftpserver {
namespace internal {

class TftpThreadPool {
public:
    explicit TftpThreadPool(size_t num_threads = std::thread::hardware_concurrency());
    ~TftpThreadPool();
    
    TftpThreadPool(const TftpThreadPool&) = delete;
    TftpThreadPool& operator=(const TftpThreadPool&) = delete;
    TftpThreadPool(TftpThreadPool&&) = delete;
    TftpThreadPool& operator=(TftpThreadPool&&) = delete;
    
    template<typename F, typename... Args>
    auto Submit(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type>;
    
    void Shutdown();
    bool IsShuttingDown() const;
    size_t GetActiveTaskCount() const;
    size_t GetQueuedTaskCount() const;
    size_t GetThreadCount() const;

private:
    void WorkerThread();
    
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    
    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stopping_;
    std::atomic<size_t> active_tasks_;
};

template<typename F, typename... Args>
auto TftpThreadPool::Submit(F&& f, Args&&... args) 
    -> std::future<typename std::invoke_result<F, Args...>::type> {
    
    using return_type = typename std::invoke_result<F, Args...>::type;
    
    if (stopping_) {
        throw std::runtime_error("Cannot submit task to stopped thread pool");
    }
    
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    
    std::future<return_type> result = task->get_future();
    
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (stopping_) {
            throw std::runtime_error("Cannot submit task to stopped thread pool");
        }
        
        tasks_.emplace([task](){ (*task)(); });
    }
    
    condition_.notify_one();
    return result;
}

} // namespace internal
} // namespace tftpserver

#endif // TFTP_THREAD_POOL_H_