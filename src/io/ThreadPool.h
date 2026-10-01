#pragma once

#include <atomic>
#include <cstddef>
#include <functional>

namespace lab2 {

class ThreadPool {
public:
    explicit ThreadPool(int workers = 0);

    int size() const { return workers_; }

    void cancel();
    void clearCancel();
    bool cancelled() const;

    void run(std::size_t count, const std::function<void(std::size_t)>& task);

private:
    std::atomic<bool> cancel_{false};
    int workers_ = 1;
};

int recommendedWorkerCount();

}