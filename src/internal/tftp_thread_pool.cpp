#include "internal/tftp_thread_pool.h"
#include "tftp/tftp_logger.h"
#include <algorithm>

namespace tftpserver {
namespace internal {

TftpThreadPool::TftpThreadPool(size_t num_threads)
    : stopping_(false), active_tasks_(0) {
    
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) {
            num_threads = 4; // Fallback to 4 threads
        }
    }
    
    // Clamp thread count to reasonable limits
    num_threads = std::max(size_t(1), std::min(num_threads, size_t(64)));
    
    TFTP_INFO("Creating thread pool with %zu worker threads", num_threads);
    
    workers_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back(&TftpThreadPool::WorkerThread, this);
    }
}

TftpThreadPool::~TftpThreadPool() {
    Shutdown();
}

void TftpThreadPool::Shutdown() {
    if (stopping_.exchange(true)) {
        return; // Already shutting down
    }
    
    TFTP_INFO("Shutting down thread pool with %zu workers", workers_.size());
    
    // Notify all workers to wake up and check stopping flag
    condition_.notify_all();
    
    // Wait for all workers to finish
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    // Clear any remaining tasks
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        std::queue<std::function<void()>> empty;
        tasks_.swap(empty);
    }
    
    TFTP_INFO("Thread pool shutdown completed");
}

bool TftpThreadPool::IsShuttingDown() const {
    return stopping_.load();
}

size_t TftpThreadPool::GetActiveTaskCount() const {
    return active_tasks_.load();
}

size_t TftpThreadPool::GetQueuedTaskCount() const {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    return tasks_.size();
}

size_t TftpThreadPool::GetThreadCount() const {
    return workers_.size();
}

void TftpThreadPool::WorkerThread() {
    TFTP_INFO("Worker thread started");
    
    while (!stopping_) {
        std::function<void()> task;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            condition_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
            
            if (stopping_ && tasks_.empty()) {
                break;
            }
            
            if (!tasks_.empty()) {
                task = std::move(tasks_.front());
                tasks_.pop();
            }
        }
        
        if (task) {
            active_tasks_.fetch_add(1);
            try {
                task();
            } catch (const std::exception& e) {
                TFTP_ERROR("Exception in worker thread: %s", e.what());
            } catch (...) {
                TFTP_ERROR("Unknown exception in worker thread");
            }
            active_tasks_.fetch_sub(1);
        }
    }
    
    TFTP_INFO("Worker thread finished");
}

} // namespace internal
} // namespace tftpserver