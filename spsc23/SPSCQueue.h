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

private:
    [[no_unique_address]] allocator_type allocator_;
    T* allocation_ = nullptr;
    T* slots_ = nullptr;
    size_type allocation_count_ = 0;
    size_type ring_size_ = 0;
};

} 