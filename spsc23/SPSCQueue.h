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

};

} 