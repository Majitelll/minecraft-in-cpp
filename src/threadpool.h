#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads) {
        for (size_t i = 0; i < numThreads; i++) {
            workers.emplace_back([this] {
                for (;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(mtx);
                        cv.wait(lock, [this] { return stop || !tasks.empty(); });
                        if (stop && tasks.empty()) return;
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    // Catch all exceptions so they don't call std::terminate
                    try {
                        task();
                    } catch (const std::exception& e) {
                        std::unique_lock<std::mutex> lock(errMtx);
                        lastError = e.what();
                    } catch (...) {
                        std::unique_lock<std::mutex> lock(errMtx);
                        lastError = "unknown exception in worker thread";
                    }
                }
            });
        }
    }

    ~ThreadPool() {
        { std::unique_lock<std::mutex> lock(mtx); stop = true; }
        cv.notify_all();
        for (auto& w : workers) w.join();
    }

    void enqueue(std::function<void()> task) {
        { std::unique_lock<std::mutex> lock(mtx); tasks.push(std::move(task)); }
        cv.notify_one();
    }

    // Call from main thread to check if any worker threw
    bool hasError() const {
        std::unique_lock<std::mutex> lock(errMtx);
        return !lastError.empty();
    }
    std::string getError() const {
        std::unique_lock<std::mutex> lock(errMtx);
        return lastError;
    }

    size_t queueSize() {
        std::unique_lock<std::mutex> lock(mtx);
        return tasks.size();
    }

private:
    std::vector<std::thread>          workers;
    std::queue<std::function<void()>> tasks;
    std::mutex                        mtx;
    std::condition_variable           cv;
    bool                              stop = false;

    mutable std::mutex errMtx;
    std::string        lastError;
};
