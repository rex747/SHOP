// rate_limiter.h
#pragma once

#include <string>
#include <unordered_map>
#include <deque>
#include <mutex>
#include <chrono>

class RateLimiter {
private:
    std::mutex mtx_;
    std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>> requests_;
    size_t max_requests_;
    std::chrono::seconds window_;

public:
    RateLimiter(size_t max_requests, std::chrono::seconds window)
        : max_requests_(max_requests), window_(window) {
    }

    bool allow(const std::string& key) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto now = std::chrono::steady_clock::now();
        auto& dq = requests_[key];

        while (!dq.empty() && now - dq.front() > window_) {
            dq.pop_front();
        }

        if (dq.size() >= max_requests_) {
            return false;
        }

        dq.push_back(now);
        return true;
    }
};
