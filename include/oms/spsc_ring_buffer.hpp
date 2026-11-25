#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace oms {

// Bounded single-producer/single-consumer ring buffer.
//
// Concurrency contract:
// - Exactly one thread may call try_push().
// - Exactly one thread may call try_pop().
// - The producer and consumer may be different threads.
//
// Progress guarantee:
// - try_push() and try_pop() are wait-free: they perform a fixed number of
//   steps and never loop or block.
//
// Memory ordering notes:
// - Producer loads head_ with acquire to observe the consumer's published slot
//   reclamation before overwriting that slot.
// - Producer stores tail_ with release after writing the slot, publishing the
//   element to the consumer.
// - Consumer loads tail_ with acquire to observe the producer's published write
//   before reading the slot.
// - Consumer stores head_ with release after consuming the slot, publishing slot
//   reclamation to the producer.
// - Producer-only tail_ loads and consumer-only head_ loads use relaxed ordering
//   because no other thread writes those indices.
template <typename T>
class SpscRingBuffer {
public:
    explicit SpscRingBuffer(std::size_t capacity)
        : capacity_(capacity), slots_(capacity + 1) {}

    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    [[nodiscard]] bool try_push(T value) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next_tail = increment(tail);
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false; // full
        }

        slots_[tail] = std::move(value);
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::optional<T> try_pop() {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) {
            return std::nullopt; // empty
        }

        std::optional<T> value = std::move(slots_[head]);
        slots_[head].reset();
        head_.store(increment(head), std::memory_order_release);
        return value;
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

private:
    [[nodiscard]] std::size_t increment(std::size_t index) const noexcept {
        return (index + 1) % (capacity_ + 1);
    }

    const std::size_t capacity_;
    std::vector<std::optional<T>> slots_;

    // Separate cache lines to reduce false sharing between producer and consumer.
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

} // namespace oms