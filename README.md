# spscQ.cpp23

This repo builds up the C++23 `spsc23::SPSCQueue` implementation piece by piece. It is a header-only, bounded single-producer, single-consumer FIFO queue. The current stage implements **construction, destruction, `try_emplace`, `try_push`, `emplace`, `push`, `front`, `pop`, and `try_pop`**.

## What currently works

- `SPSCQueue<T>(capacity)` allocates storage once without constructing any `T` objects. The requested capacity is the exact number of items you can insert.
- `try_emplace(args...)` forwards arguments to construct a `T` directly in a queue slot and returns `true` on success. It supports move-only and immovable types when the supplied arguments can construct them.
- `try_push(value)` inserts a single value through `try_emplace`, copying an lvalue or moving an rvalue when the element type supports it.
- Both `try_emplace` and `try_push` return `false` on a full queue without constructing an item or moving from the supplied arguments.
- `emplace(args...)` retries insertion, yielding to the scheduler while the queue is full. It returns `void` after successfully constructing the item.
- `push(value)` forwards a single value to `emplace` and has the same waiting behavior.
- `front()` is a consumer operation that returns a pointer to the oldest item, or `nullptr` when empty. It lets you read or modify that item without removing it. The pointer remains valid until that item is removed by `pop()` or `try_pop()`, or the queue is destroyed.
- `pop()` destroys the oldest item and frees its slot. Call it only after `front()` returns a non-null pointer for that item. It returns `void` and supports immovable elements.
- `try_pop()` moves the oldest item into a `std::optional<T>` and removes it from the queue. An empty queue returns `std::nullopt`. This method requires `T` to be nothrow move-constructible; the returned value owns its object independently of the queue.
- If an item's constructor throws, the exception propagates and the queue's occupied slots stay unchanged.
- The queue's destructor destroys all inserted items and releases the storage.

Capacity zero throws `std::invalid_argument`; capacities beyond the storage limit throw `std::length_error`. The queue itself cannot be copied or moved. Element types must be non-array objects without `const` or `volatile` qualification, and their destructors must not throw.

Removed slots can now be reused, including when the indexes wrap around the ring. Use exactly one producer thread for insertion and one consumer thread for `front`, `pop`, and `try_pop`. Blocking `emplace` and `push` calls need consumer progress when the queue is full; in a single-threaded example, remove an item before inserting into a full queue. Stop and join any worker threads before destroying the queue.

## Example

Save this as `example.cpp` in the repository root:

```cpp
#include <spsc23/SPSCQueue.h>

#include <iostream>
#include <string>
#include <utility>

int main() {
    // Reserve room for four strings; no strings are constructed yet.
    spsc23::SPSCQueue<std::string> queue(4);

    std::cout << std::boolalpha;
    std::cout << queue.try_emplace("hello") << '\n';
    const std::string message = "world";
    std::cout << queue.try_push(message) << '\n'; // Copies message into a slot.
    queue.emplace(3, '!'); // Constructs "!!!" in place; space is available.
    std::string last = "last";
    queue.push(std::move(last)); // Moves last into the final available slot.
    std::cout << queue.try_push("full") << '\n'; // Returns false immediately.
    if (auto* item = queue.front()) {
        std::cout << *item << '\n'; // Reads "hello".
        queue.pop(); // Destroys "hello"; item must no longer be used.
    }
    queue.push("again"); // Reuses the space released by pop().

    while (auto item = queue.try_pop()) {
        std::cout << *item << '\n'; // Owns each extracted string.
    }
    std::cout << queue.try_pop().has_value() << '\n'; // The queue is empty.

    // Leaving this scope frees the queue's storage.
}
```

Output:

```text
true
true
false
hello
world
!!!
last
again
false
```

## Build and run the tests

Requirements:

- CMake 3.25 or newer.
- A C++23 compiler and standard library that support `std::allocator_traits::allocate_at_least`. Enabling C++23 alone is insufficient if the standard library lacks this feature.

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

CMake enables C++23 and builds the `spsc23_tests` executable.

The `front()` tests cover empty queues, the first inserted value, pointer stability after further insertions, mutation through the returned pointer, move-only and immovable elements, object lifetimes, construction failures, and alignment. Compile-time checks verify its pointer return type and `noexcept` guarantee.

The `pop()` and `try_pop()` tests cover FIFO order, repeated filling and draining, slot reuse and wraparound, destruction of removed and remaining items, move-only extraction, and values that outlive the queue. Compile-time checks verify the return types, `noexcept` guarantees, and the element types accepted by `try_pop()`. A producer/consumer test transfers 25,000 items using both removal methods and the blocking insertion methods.

## Compile and run the example

After saving the example above, run from the repository root:

```sh
c++ -std=c++23 -Wall -Wextra -Wpedantic -Iinclude example.cpp -o example
./example
```
