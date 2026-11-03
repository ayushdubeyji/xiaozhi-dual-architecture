#pragma once
#include <atomic>
#include <cstdint>
#include <array>
#include <optional>

// Cache-line aligned Lockless SPSC Ring Buffer (C++20)
// Prevents thread contention between Camera Ingestion and GUI Render loop
template <typename T, size_t Capacity = 4>
class alignas(64) LocklessRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
public:
    LocklessRingBuffer() : write_idx_(0), read_idx_(0) {}

    bool push(const T& item) noexcept {
        const uint64_t w = write_idx_.load(std::memory_order_relaxed);
        const uint64_t r = read_idx_.load(std::memory_order_acquire);
        if (w - r >= Capacity) {
            // Buffer full: drop oldest frame to maintain ultra-low latency real-time feed
            read_idx_.store(w - Capacity + 1, std::memory_order_release);
        }
        buffer_[w & (Capacity - 1)] = item;
        write_idx_.store(w + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& item) noexcept {
        const uint64_t r = read_idx_.load(std::memory_order_relaxed);
        const uint64_t w = write_idx_.load(std::memory_order_acquire);
        if (r >= w) {
            return false; // Empty
        }
        item = buffer_[r & (Capacity - 1)];
        read_idx_.store(r + 1, std::memory_order_release);
        return true;
    }

    bool peek_latest(T& item) noexcept {
        const uint64_t w = write_idx_.load(std::memory_order_acquire);
        const uint64_t r = read_idx_.load(std::memory_order_relaxed);
        if (w == 0 || r >= w) return false;
        item = buffer_[(w - 1) & (Capacity - 1)];
        read_idx_.store(w, std::memory_order_release);
        return true;
    }

private:
    alignas(64) std::array<T, Capacity> buffer_{};
    alignas(64) std::atomic<uint64_t> write_idx_{0};
    alignas(64) std::atomic<uint64_t> read_idx_{0};
};
