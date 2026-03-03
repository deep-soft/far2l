#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>

namespace proto {

class OpQueue {
public:
    OpQueue()
        : _worker([this]() { WorkerLoop(); }) {}

    ~OpQueue() {
        {
            std::lock_guard<std::mutex> lk(_mtx);
            _stopped = true;
        }
        _cv.notify_all();
        if (_worker.joinable()) {
            _worker.join();
        }
    }

    template <typename Fn>
    auto Submit(Fn&& fn) -> decltype(fn()) {
        using Ret = decltype(fn());
        auto promise = std::make_shared<std::promise<Ret>>();
        auto fut = promise->get_future();

        {
            std::lock_guard<std::mutex> lk(_mtx);
            _ops.push([promise, fn = std::forward<Fn>(fn)]() mutable {
                try {
                    promise->set_value(fn());
                } catch (...) {
                    promise->set_exception(std::current_exception());
                }
            });
        }
        _cv.notify_one();
        return fut.get();
    }

private:
    void WorkerLoop() {
        for (;;) {
            std::function<void()> op;
            {
                std::unique_lock<std::mutex> lk(_mtx);
                _cv.wait(lk, [this]() { return _stopped || !_ops.empty(); });
                if (_stopped && _ops.empty()) {
                    return;
                }
                op = std::move(_ops.front());
                _ops.pop();
            }
            op();
        }
    }

    std::mutex _mtx;
    std::condition_variable _cv;
    std::queue<std::function<void()>> _ops;
    bool _stopped = false;
    std::thread _worker;
};

} // namespace proto
