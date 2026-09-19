#include <spsc23/SPSCQueue.h>

#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace {

[[noreturn]] void fail(const char* expression, const char* file, int line) {
  std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", file, line, expression);
  std::abort();
}


#define CHECK(expression) \
  ((expression) ? static_cast<void>(0) : fail(#expression, __FILE__, __LINE__))

template <class Exception, class F>
void check_throws(F&& function) {
  bool caught = false;
  try {
    std::forward<F>(function)();
  } catch (const Exception&) {
    caught = true;
  } catch (...) {
    CHECK(false && "unexpected exception type");
  }
  CHECK(caught);
}

using IntQueue = spsc23::SPSCQueue<int>;

struct Immovable {
  explicit Immovable(int initial) : value(initial) {}
  Immovable(const Immovable&) = delete;
  Immovable(Immovable&&) = delete;
  int value;
};

struct ThrowingDestructor {
  ~ThrowingDestructor() noexcept(false) {}
};

struct ThrowingMove {
  ThrowingMove() = default;
  ThrowingMove(ThrowingMove&&) noexcept(false) {}
};

template <class Q>
concept HasTryPop = requires(Q& queue) { queue.try_pop(); };

template <class T>
concept CanStore = requires { typename spsc23::SPSCQueue<T>; };

template <class Q, class... Args>
concept CanEmplace = requires(Q& queue, Args&&... args) {
  { queue.try_emplace(std::forward<Args>(args)...) } -> std::same_as<bool>;
};

template <class Q, class U>
concept CanTryPush = requires(Q& queue, U&& value) {
  { queue.try_push(std::forward<U>(value)) } -> std::same_as<bool>;
};

template <class Q, class... Args>
concept CanBlockingEmplace = requires(Q& queue, Args&&... args) {
  { queue.emplace(std::forward<Args>(args)...) } -> std::same_as<void>;
};

template <class Q, class U>
concept CanPush = requires(Q& queue, U&& value) {
  { queue.push(std::forward<U>(value)) } -> std::same_as<void>;
};

static_assert(!std::is_default_constructible_v<IntQueue>);
static_assert(!std::is_convertible_v<std::size_t, IntQueue>);
static_assert(!std::is_copy_constructible_v<IntQueue>);
static_assert(!std::is_copy_assignable_v<IntQueue>);
static_assert(!std::is_move_constructible_v<IntQueue>);
static_assert(!std::is_move_assignable_v<IntQueue>);
static_assert(!CanStore<void>);
static_assert(!CanStore<int&>);
static_assert(!CanStore<const int>);
static_assert(!CanStore<volatile int>);
static_assert(!CanStore<int[2]>);
static_assert(!CanStore<ThrowingDestructor>);
static_assert(std::is_nothrow_destructible_v<IntQueue>);
static_assert(IntQueue::is_always_lock_free);
static_assert(IntQueue::cache_line_size == 64);
static_assert(spsc23::SPSCQueue<int, 128>::cache_line_size == 128);
static_assert(CanEmplace<spsc23::SPSCQueue<Immovable>, int>);
static_assert(!CanEmplace<spsc23::SPSCQueue<Immovable>>);
static_assert(!CanEmplace<IntQueue, std::unique_ptr<int>>);
static_assert(noexcept(std::declval<IntQueue&>().try_emplace(1)));
static_assert(!noexcept(std::declval<spsc23::SPSCQueue<Immovable>&>().try_emplace(1)));
static_assert(CanTryPush<IntQueue, int&>);
static_assert(CanPush<IntQueue, const int&>);
static_assert(!CanTryPush<IntQueue, std::unique_ptr<int>>);
static_assert(!CanPush<IntQueue, std::unique_ptr<int>>);
static_assert(CanBlockingEmplace<spsc23::SPSCQueue<Immovable>, int>);
static_assert(!CanBlockingEmplace<spsc23::SPSCQueue<Immovable>>);
static_assert(CanTryPush<spsc23::SPSCQueue<std::unique_ptr<int>>, std::unique_ptr<int>>);
static_assert(CanPush<spsc23::SPSCQueue<std::unique_ptr<int>>, std::unique_ptr<int>>);
static_assert(!CanTryPush<spsc23::SPSCQueue<std::unique_ptr<int>>, std::unique_ptr<int>&>);
static_assert(!CanPush<spsc23::SPSCQueue<std::unique_ptr<int>>, std::unique_ptr<int>&>);
static_assert(noexcept(std::declval<IntQueue&>().try_push(1)));
static_assert(noexcept(std::declval<IntQueue&>().emplace(1)));
static_assert(noexcept(std::declval<IntQueue&>().push(1)));
static_assert(!noexcept(std::declval<spsc23::SPSCQueue<Immovable>&>().try_push(1)));
static_assert(!noexcept(std::declval<spsc23::SPSCQueue<Immovable>&>().emplace(1)));
static_assert(!noexcept(std::declval<spsc23::SPSCQueue<Immovable>&>().push(1)));
static_assert(noexcept(std::declval<IntQueue&>().front()));
static_assert(std::same_as<decltype(std::declval<IntQueue&>().front()), int*>);
static_assert(std::same_as<decltype(std::declval<spsc23::SPSCQueue<Immovable>&>().front()),
                           Immovable*>);
static_assert(HasTryPop<IntQueue>);
static_assert(HasTryPop<spsc23::SPSCQueue<std::unique_ptr<int>>>);
static_assert(!HasTryPop<spsc23::SPSCQueue<Immovable>>);
static_assert(!HasTryPop<spsc23::SPSCQueue<ThrowingMove>>);
static_assert(noexcept(std::declval<IntQueue&>().pop()));
static_assert(noexcept(std::declval<IntQueue&>().try_pop()));
static_assert(std::same_as<decltype(std::declval<IntQueue&>().pop()), void>);
static_assert(std::same_as<decltype(std::declval<IntQueue&>().try_pop()), std::optional<int>>);

void test_pop_and_try_pop_fifo() {
  for (const std::size_t capacity : {1, 2, 3, 7, 16, 31, 128}) {
    IntQueue queue(capacity);
    CHECK(!queue.try_pop());
    CHECK(!queue.try_pop());
    int value = 0;
    for (int round = 0; round != 100; ++round) {
      const int first = value;
      for (std::size_t index = 0; index != capacity; ++index) {
        CHECK(queue.try_push(value++));
      }
      CHECK(!queue.try_push(-1));
      for (std::size_t index = 0; index != capacity; ++index) {
        const int expected = first + static_cast<int>(index);
        if (round % 2 == 0) {
          auto* const item = queue.front();
          CHECK(item != nullptr && *item == expected);
          queue.pop();
        } else {
          const auto item = queue.try_pop();
          CHECK(item && *item == expected);
        }
      }
      CHECK(queue.front() == nullptr);
      CHECK(!queue.try_pop());
    }

    // Reuse one released slot at a time while crossing the ring boundary.
    for (std::size_t index = 0; index != capacity; ++index) {
      CHECK(queue.try_emplace(static_cast<int>(index)));
    }
    for (int expected = 0; expected != 1000; ++expected) {
      const auto item = queue.try_pop();
      CHECK(item && *item == expected);
      CHECK(queue.try_push(expected + static_cast<int>(capacity)));
      CHECK(!queue.try_push(-1));
    }
    for (std::size_t index = 0; index != capacity; ++index) {
      const auto item = queue.try_pop();
      CHECK(item && *item == 1000 + static_cast<int>(index));
    }
    CHECK(queue.front() == nullptr);
    CHECK(!queue.try_pop());
  }
}

void test_front() {
  for (const std::size_t capacity : {1, 2, 3, 7, 16, 31, 128}) {
    IntQueue queue(capacity);
    CHECK(queue.front() == nullptr);
    CHECK(queue.front() == nullptr);
    CHECK(queue.try_emplace(10));
    auto* const first = queue.front();
    CHECK(first != nullptr);
    CHECK(*first == 10);
    CHECK(queue.front() == first);

    for (std::size_t index = 1; index != capacity; ++index) {
      CHECK(queue.try_emplace(static_cast<int>(index) + 10));
      CHECK(queue.front() == first);
      CHECK(*queue.front() == 10);
    }
    CHECK(!queue.try_emplace(-1));
    CHECK(queue.front() == first);
    CHECK(*queue.front() == 10);
    *first = 42;
    CHECK(queue.front() == first);
    CHECK(*queue.front() == 42);
    // Reading or modifying the front does not remove it or free a slot.
    CHECK(!queue.try_push(-1));
  }
}

void test_construction_and_capacity() {
  check_throws<std::invalid_argument>([] { IntQueue queue(0); });
  check_throws<std::length_error>([] {
    IntQueue queue(std::numeric_limits<std::size_t>::max());
  });
  check_throws<std::length_error>([] {
    IntQueue queue(std::numeric_limits<std::size_t>::max() / sizeof(int));
  });

  for (const std::size_t capacity : {1, 2, 3, 7, 16, 31, 128}) {
    IntQueue queue(capacity);
    for (std::size_t index = 0; index != capacity; ++index) {
      CHECK(queue.try_emplace(static_cast<int>(index)));
    }
    CHECK(!queue.try_emplace(-1));
    CHECK(!queue.try_emplace(-1));
  }
}

void test_immovable_and_move_only() {
  spsc23::SPSCQueue<Immovable> queue(2);
  CHECK(queue.try_emplace(10));
  CHECK(queue.try_emplace(20));
  CHECK(!queue.try_emplace(30));
  auto* const item = queue.front();
  CHECK(item != nullptr);
  CHECK(item->value == 10);
  item->value = 11;
  CHECK(queue.front() == item);
  CHECK(queue.front()->value == 11);
  queue.pop();
  CHECK(queue.front() != nullptr && queue.front()->value == 20);
  queue.pop();
  CHECK(queue.front() == nullptr);

  spsc23::SPSCQueue<std::unique_ptr<int>> pointers(1);
  CHECK(pointers.front() == nullptr);
  auto first = std::make_unique<int>(7);
  auto* const pointee = first.get();
  CHECK(pointers.try_emplace(std::move(first)));
  CHECK(first == nullptr);
  auto* const stored = pointers.front();
  CHECK(stored != nullptr);
  CHECK(stored->get() == pointee);
  CHECK(**stored == 7);
  auto second = std::make_unique<int>(8);
  CHECK(!pointers.try_emplace(std::move(second)));
  CHECK(second != nullptr && *second == 8);
  CHECK(pointers.front() == stored);
  CHECK(stored->get() == pointee);
  const auto first_result = pointers.try_pop();
  CHECK(first_result && first_result->get() == pointee && **first_result == 7);
  CHECK(pointers.front() == nullptr);
  pointers.push(std::move(second));
  CHECK(second == nullptr);
  const auto second_result = pointers.try_pop();
  CHECK(second_result && *second_result && **second_result == 8);
  CHECK(!pointers.try_pop());
  // An occupied slot containing a null pointer still produces an engaged optional.
  CHECK(pointers.try_emplace(nullptr));
  const auto null_result = pointers.try_pop();
  CHECK(null_result.has_value());
  CHECK(*null_result == nullptr);
  CHECK(!pointers.try_pop());
}


void test_try_push() {
  for (const std::size_t capacity : {1, 2, 3, 7, 16, 31, 128}) {
    IntQueue queue(capacity);
    for (std::size_t index = 0; index != capacity; ++index) {
      const int value = static_cast<int>(index);
      CHECK(queue.try_push(value));
    }
    CHECK(!queue.try_push(-1));
    CHECK(!queue.try_push(-1));
  }

  spsc23::SPSCQueue<std::unique_ptr<int>> pointers(1);
  auto first = std::make_unique<int>(7);
  CHECK(pointers.try_push(std::move(first)));
  CHECK(first == nullptr);
  auto second = std::make_unique<int>(8);
  CHECK(!pointers.try_push(std::move(second)));
  CHECK(second != nullptr && *second == 8);
}

void test_push_and_emplace() {
  // Provide room for every item so neither blocking call waits forever.
  for (const std::size_t capacity : {1, 2, 3, 7, 16, 31, 128}) {
    IntQueue queue(capacity);
    for (std::size_t index = 0; index != capacity; ++index) {
      int value = static_cast<int>(index);
      if (index % 2 == 0) {
        queue.push(value);
      } else {
        queue.emplace(value);
      }
    }
    CHECK(!queue.try_push(-1));
  }

  spsc23::SPSCQueue<Immovable> immovable(1);
  immovable.emplace(20);
  CHECK(!immovable.try_emplace(30));

  spsc23::SPSCQueue<std::unique_ptr<int>> pointers(2);
  auto first = std::make_unique<int>(7);
  pointers.push(std::move(first));
  CHECK(first == nullptr);
  auto second = std::make_unique<int>(8);
  pointers.emplace(std::move(second));
  CHECK(second == nullptr);
  auto third = std::make_unique<int>(9);
  CHECK(!pointers.try_push(std::move(third)));
  CHECK(third != nullptr && *third == 9);
}

struct Tracked {
  static inline int constructed = 0;
  static inline int destroyed = 0;
  static inline int alive = 0;
  static inline std::array<int, 64> destroyed_ids{};

  explicit Tracked(int initial) : value(initial) {
    ++constructed;
    ++alive;
  }
  Tracked(const Tracked&) = delete;
  Tracked(Tracked&& other) noexcept : value(std::exchange(other.value, -1)) {
    ++constructed;
    ++alive;
  }
  ~Tracked() noexcept {
    if (value >= 0) {
      ++destroyed_ids[static_cast<std::size_t>(value)];
    }
    ++destroyed;
    --alive;
  }
  int value;
};

void test_lifetime_and_full_queue() {
  {
    spsc23::SPSCQueue<Tracked> empty(3);
    CHECK(empty.front() == nullptr);
    CHECK(Tracked::constructed == 0);
  }
  CHECK(Tracked::destroyed == 0);

  {
    spsc23::SPSCQueue<Tracked> queue(3);
    CHECK(Tracked::constructed == 0);
    for (int value = 0; value != 3; ++value) {
      CHECK(queue.try_emplace(value));
    }
    CHECK(Tracked::alive == 3);
    CHECK(!queue.try_emplace(42));
    CHECK(Tracked::constructed == 3);
    auto* const first = queue.front();
    CHECK(first != nullptr);
    CHECK(first->value == 0);
    CHECK(queue.front() == first);
    CHECK(Tracked::constructed == 3);
    CHECK(Tracked::destroyed == 0);
    CHECK(Tracked::alive == 3);
  }
  CHECK(Tracked::alive == 0);
  CHECK(Tracked::destroyed == 3);
  for (int value = 0; value != 3; ++value) {
    CHECK(Tracked::destroyed_ids[static_cast<std::size_t>(value)] == 1);
  }
  CHECK(Tracked::destroyed_ids[42] == 0);
}

void test_push_and_emplace_lifetime() {
  CHECK(Tracked::alive == 0);
  Tracked::constructed = Tracked::destroyed = 0;
  Tracked::destroyed_ids.fill(0);
  {
    spsc23::SPSCQueue<Tracked> queue(3);
    CHECK(queue.try_push(10));
    queue.emplace(20);
    queue.push(30);
    CHECK(Tracked::constructed == 3);
    CHECK(Tracked::alive == 3);
    CHECK(!queue.try_push(42));
    CHECK(Tracked::constructed == 3);
  }
  CHECK(Tracked::alive == 0);
  CHECK(Tracked::destroyed == 3);
  // Destruction observes the values constructed through all three methods.
  CHECK(Tracked::destroyed_ids[10] == 1);
  CHECK(Tracked::destroyed_ids[20] == 1);
  CHECK(Tracked::destroyed_ids[30] == 1);
  CHECK(Tracked::destroyed_ids[42] == 0);
}

void test_pop_lifetime_and_wraparound() {
  CHECK(Tracked::alive == 0);
  Tracked::constructed = Tracked::destroyed = 0;
  Tracked::destroyed_ids.fill(0);
  {
    spsc23::SPSCQueue<Tracked> queue(3);
    for (int value = 0; value != 3; ++value) {
      CHECK(queue.try_emplace(value));
    }
    for (int value = 0; value != 2; ++value) {
      CHECK(queue.front() != nullptr && queue.front()->value == value);
      queue.pop();
      CHECK(Tracked::alive == 2);
      CHECK(Tracked::destroyed_ids[static_cast<std::size_t>(value)] == 1);
      CHECK(queue.try_emplace(value + 3));
    }
    // The remaining live objects straddle the ring boundary at destruction.
    CHECK(Tracked::alive == 3);
  }
  CHECK(Tracked::alive == 0);
  CHECK(Tracked::constructed == 5);
  CHECK(Tracked::destroyed == 5);
  for (int value = 0; value != 5; ++value) {
    CHECK(Tracked::destroyed_ids[static_cast<std::size_t>(value)] == 1);
  }
}

void test_try_pop_lifetime() {
  CHECK(Tracked::alive == 0);
  Tracked::constructed = Tracked::destroyed = 0;
  Tracked::destroyed_ids.fill(0);
  {
    const auto result = [] {
      spsc23::SPSCQueue<Tracked> queue(1);
      CHECK(!queue.try_pop());
      CHECK(Tracked::constructed == 0);
      CHECK(queue.try_emplace(5));
      auto item = queue.try_pop();
      CHECK(item && item->value == 5);
      CHECK(queue.front() == nullptr);
      CHECK(!queue.try_pop());
      CHECK(Tracked::alive == 1);
      CHECK(Tracked::destroyed_ids[5] == 0);
      CHECK(queue.try_emplace(6));
      return item;
    }();
    CHECK(result && result->value == 5);
    CHECK(Tracked::alive == 1);
    CHECK(Tracked::destroyed_ids[5] == 0);
    CHECK(Tracked::destroyed_ids[6] == 1);
  }
  CHECK(Tracked::alive == 0);
  CHECK(Tracked::constructed == Tracked::destroyed);
  CHECK(Tracked::destroyed_ids[5] == 1);
}

struct ConstructionError {};

struct ThrowOnConstruction {
  static inline int alive = 0;
  explicit ThrowOnConstruction(int initial) : value(initial) {
    if (initial < 0) {
      throw ConstructionError{};
    }
    ++alive;
  }
  ThrowOnConstruction(const ThrowOnConstruction&) = delete;
  ThrowOnConstruction(ThrowOnConstruction&&) = delete;
  ~ThrowOnConstruction() noexcept { --alive; }
  int value;
};

void test_exceptions() {
  {
    spsc23::SPSCQueue<ThrowOnConstruction> queue(2);
    CHECK(queue.front() == nullptr);
    check_throws<ConstructionError>([&] { static_cast<void>(queue.try_emplace(-1)); });
    CHECK(queue.front() == nullptr);
    CHECK(ThrowOnConstruction::alive == 0);
    CHECK(queue.try_emplace(1));
    auto* const first = queue.front();
    CHECK(first != nullptr);
    CHECK(first->value == 1);
    check_throws<ConstructionError>([&] { static_cast<void>(queue.try_emplace(-1)); });
    CHECK(queue.front() == first);
    CHECK(queue.front()->value == 1);
    CHECK(ThrowOnConstruction::alive == 1);
    // Both slots remain usable after failed construction.
    CHECK(queue.try_emplace(2));
    CHECK(!queue.try_emplace(-1));
    CHECK(ThrowOnConstruction::alive == 2);
    CHECK(queue.front() == first);
    CHECK(queue.front()->value == 1);
    queue.pop();
    CHECK(queue.front() != nullptr && queue.front()->value == 2);
    queue.pop();
    CHECK(queue.front() == nullptr);
    CHECK(ThrowOnConstruction::alive == 0);
    check_throws<ConstructionError>([&] { static_cast<void>(queue.try_emplace(-1)); });
    CHECK(queue.front() == nullptr);
    CHECK(queue.try_emplace(3));
    CHECK(queue.front() != nullptr && queue.front()->value == 3);
  }
  CHECK(ThrowOnConstruction::alive == 0);
}

void test_push_and_emplace_exceptions() {
  {
    spsc23::SPSCQueue<ThrowOnConstruction> queue(3);
    check_throws<ConstructionError>([&] { static_cast<void>(queue.try_push(-1)); });
    CHECK(ThrowOnConstruction::alive == 0);
    CHECK(queue.try_push(1));
    check_throws<ConstructionError>([&] { queue.emplace(-1); });
    CHECK(ThrowOnConstruction::alive == 1);
    queue.emplace(2);
    check_throws<ConstructionError>([&] { queue.push(-1); });
    CHECK(ThrowOnConstruction::alive == 2);
    queue.push(3);
    // Failed construction leaves each slot available; a full queue never
    // invokes the throwing constructor through try_push.
    CHECK(ThrowOnConstruction::alive == 3);
    CHECK(!queue.try_push(-1));
    CHECK(ThrowOnConstruction::alive == 3);
  }
  CHECK(ThrowOnConstruction::alive == 0);
}

struct alignas(256) OverAligned {
  explicit OverAligned(std::uint64_t initial) : value(initial) {
    CHECK(reinterpret_cast<std::uintptr_t>(this) % alignof(OverAligned) == 0);
  }
  std::uint64_t value;
};

void test_over_alignment_and_custom_cache_line() {
  spsc23::SPSCQueue<OverAligned, 128> queue(3);
  for (std::uint64_t first = 0; first != 300; first += 3) {
    CHECK(queue.front() == nullptr);
    CHECK(queue.try_emplace(first));
    CHECK(queue.try_emplace(first + 1));
    CHECK(queue.try_emplace(first + 2));
    CHECK(!queue.try_emplace(first + 3));
    for (std::uint64_t offset = 0; offset != 3; ++offset) {
      auto* const item = queue.front();
      CHECK(item != nullptr);
      CHECK(reinterpret_cast<std::uintptr_t>(item) % alignof(OverAligned) == 0);
      CHECK(item->value == first + offset);
      CHECK(queue.front() == item);
      if (offset % 2 == 0) {
        queue.pop();
      } else {
        const auto result = queue.try_pop();
        CHECK(result && result->value == first + offset);
      }
    }
    CHECK(queue.front() == nullptr);
    CHECK(!queue.try_pop());
  }
}

void test_blocking_producer() {
  constexpr int count = 25'000;
  IntQueue queue(1);
  std::atomic<int> completed{0};
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
  std::jthread producer([&] {
    for (int value = 0; value != count; ++value) {
      if (value % 2 == 0) {
        queue.push(value);
      } else {
        queue.emplace(value);
      }
    }
    completed.fetch_add(1, std::memory_order_release);
  });
  std::jthread consumer([&] {
    for (int expected = 0; expected != count; ++expected) {
      if (expected % 2 == 0) {
        std::optional<int> item;
        while (!(item = queue.try_pop())) {
          std::this_thread::yield();
        }
        CHECK(*item == expected);
      } else {
        int* item = nullptr;
        while ((item = queue.front()) == nullptr) {
          std::this_thread::yield();
        }
        CHECK(*item == expected);
        queue.pop();
      }
    }
    completed.fetch_add(1, std::memory_order_release);
  });
  while (completed.load(std::memory_order_acquire) != 2) {
    CHECK(std::chrono::steady_clock::now() < deadline);
    std::this_thread::yield();
  }
  producer.join();
  consumer.join();
  CHECK(queue.front() == nullptr);
  CHECK(!queue.try_pop());
}

}  // namespace

int main() {
  test_front();
  test_pop_and_try_pop_fifo();
  test_construction_and_capacity();
  test_immovable_and_move_only();
  test_try_push();
  test_push_and_emplace();
  test_lifetime_and_full_queue();
  test_push_and_emplace_lifetime();
  test_pop_lifetime_and_wraparound();
  test_try_pop_lifetime();
  test_exceptions();
  test_push_and_emplace_exceptions();
  test_over_alignment_and_custom_cache_line();
  test_blocking_producer();
  std::puts("All queue tests passed.");
}
