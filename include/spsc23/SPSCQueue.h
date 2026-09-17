#pragma once

#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <version>

#if !defined(__cpp_lib_allocate_at_least) || __cpp_lib_allocate_at_least < 202302L
#error "spsc23 requires a C++23 standard library with std::allocator_traits::allocate_at_least"
#endif

namespace spsc23 {

template <typename T>
concept queue_element = std::is_object_v<T> && !std::is_array_v<T> &&
                        std::same_as<T, std::remove_cv_t<T>> &&
                        std::is_nothrow_destructible_v<T>;

template <queue_element T, std::size_t CacheLineSize = 64>
class SPSCQueue final {
    using allocator_type = std::allocator<T>;
    using allocator_traits = std::allocator_traits<allocator_type>;
    static constexpr std::size_t padding_slots = (CacheLineSize - 1) / sizeof(T) + 1;
public:
    using value_type = T;
    using size_type = std::size_t;

    static constexpr size_type cache_line_size = CacheLineSize;
    static constexpr bool is_always_lock_free = std::atomic<size_type>::is_always_lock_free;

    /// Allocates storage once, without constructing any T objects.
    /// Capacity is exact, even if allocate_at_least provides more storage.
    explicit SPSCQueue(size_type capacity) {
        if (capacity == 0) {
        throw std::invalid_argument("spsc23 capacity must be greater than zero");
        }

        // Bound both the byte extent and pointer arithmetic before adding guards/slack.
        const auto allocator_limit = allocator_traits::max_size(allocator_);
        const auto pointer_limit = static_cast<size_type>(
                                    std::numeric_limits<std::ptrdiff_t>::max()) /
                                sizeof(T);
        const auto limit = allocator_limit < pointer_limit ? allocator_limit : pointer_limit;
        if (padding_slots > limit / 2 || capacity >= limit - 2 * padding_slots) {
            throw std::length_error("spsc23 capacity exceeds the storage limit");
        }

        ring_size_ = capacity + 1; // One unused slot distinguishes full from empty.
        const auto allocation = allocator_traits::allocate_at_least(
            allocator_, ring_size_ + 2 * padding_slots);
        allocation_ = allocation.ptr;
        allocation_count_ = allocation.count;
        slots_ = allocation_ + padding_slots;
    }

    /*
        Requires external synchronization: the producer and consumer must have
        stopped, and no borrowed pointers may remain in use.
        Relaxed loads are sufficient because no queue operation may run concurrently
        with destruction.

        Threads are already synchronized externally, so these loads only retrieve
        the final indexes; no acquire/release synchronization is needed here.
    */
     ~SPSCQueue() noexcept {
        auto position = read_index_.load(std::memory_order_relaxed);
        const auto end = write_index_.load(std::memory_order_relaxed);
        while (position != end) {
            std::destroy_at(slots_ + position);
            position = advance(position);
        }
        allocator_traits::deallocate(allocator_, allocation_, allocation_count_);
    }

    SPSCQueue(const SPSCQueue&) = delete;                       // copy ctor 
    SPSCQueue& operator=(const SPSCQueue&) = delete;            // copy assignment operator
    SPSCQueue(SPSCQueue&&) = delete;                            // move ctor 
    SPSCQueue& operator=(SPSCQueue&&) = delete;                 // move assignment operator 

    /// Producer only. A full queue returns false without constructing or moving T.
    /// If construction throws, the slot is not published and the queue is unchanged.
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    [[nodiscard]] bool try_emplace(Args&&... args)
            noexcept(std::is_nothrow_constructible_v<T, Args...>) {
        const auto position = write_index_.load(std::memory_order_relaxed);
        const auto next = advance(position);
        if (next == cached_read_index_) {
            // Acquire the consumer's destruction before reusing its released slots.
            cached_read_index_ = read_index_.load(std::memory_order_acquire);
            if (next == cached_read_index_) {
                return false;
            }
        }

        std::construct_at(slots_ + position, std::forward<Args>(args)...);
        // Publish only a fully constructed object to the consumer.
        write_index_.store(next, std::memory_order_release);
        return true;
    }

private:
    [[nodiscard]] size_type advance(size_type index) const noexcept {
        ++index;
        return index == ring_size_ ? 0 : index;
    }

    [[no_unique_address]] allocator_type allocator_;
    T* allocation_ = nullptr;
    T* slots_ = nullptr;
    size_type allocation_count_ = 0;
    size_type ring_size_ = 0;

    alignas(CacheLineSize) std::atomic<size_type> write_index_{0};
    alignas(CacheLineSize) size_type cached_read_index_ = 0;
    alignas(CacheLineSize) std::atomic<size_type> read_index_{0};
    alignas(CacheLineSize) size_type cached_write_index_ = 0;
};

} 
