#include "io/ThreadPool.h"

#include <algorithm>
#include <thread>
#include <vector>

namespace lab2 {

int recommendedWorkerCount() {
    const unsigned int hardware = std::thread::hardware_concurrency();
    const int count = static_cast<int>(hardware == 0 ? 4u : hardware);
    return std::clamp(count, 2, 16);
}

ThreadPool::ThreadPool(int workers) {
    workers_ = workers > 0 ? std::clamp(workers, 1, 64) : recommendedWorkerCount();
}

void ThreadPool::cancel() { cancel_.store(true, std::memory_order_relaxed); }

void ThreadPool::clearCancel() { cancel_.store(false, std::memory_order_relaxed); }

bool ThreadPool::cancelled() const { return cancel_.load(std::memory_order_relaxed); }

void ThreadPool::run(std::size_t count, const std::function<void(std::size_t)>& task) {
    if (count == 0 || !task) {
        return;
    }
    std::atomic<std::size_t> next{0};
    const int workerCount = std::min(workers_, static_cast<int>(count));
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(workerCount));
    for (int i = 0; i < workerCount; ++i) {
        workers.emplace_back([&] {
            while (!cancel_.load(std::memory_order_relaxed)) {
                const std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
                if (index >= count) {
                    return;
                }
                task(index);
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
}

}