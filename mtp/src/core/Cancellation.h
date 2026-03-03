#pragma once

#include <atomic>
#include <memory>

namespace proto {

class CancellationToken {
public:
    explicit CancellationToken(std::shared_ptr<std::atomic<bool>> state)
        : _state(std::move(state)) {}

    bool IsCancelled() const {
        return _state && _state->load();
    }

private:
    std::shared_ptr<std::atomic<bool>> _state;
};

class CancellationSource {
public:
    CancellationSource()
        : _state(std::make_shared<std::atomic<bool>>(false)) {}

    CancellationToken Token() const {
        return CancellationToken(_state);
    }

    void Cancel() {
        _state->store(true);
    }

private:
    std::shared_ptr<std::atomic<bool>> _state;
};

} // namespace proto
