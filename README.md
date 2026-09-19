# spscQ.cpp23

This repo builds up the C++23 `spsc23::SPSCQueue` implementation piece by piece. The intended result is a header-only, bounded single-producer, single-consumer FIFO queue. The current stage implements **construction, destruction, `try_emplace`, `try_push`, `emplace`, `push`, and `front`**.

## What currently works

- `SPSCQueue<T>(capacity)` allocates storage once without constructing any `T` objects. The requested capacity is the exact number of items you can insert.
- `try_emplace(args...)` forwards arguments to construct a `T` directly in a queue slot and returns `true` on success. It supports move-only and immovable types when the supplied arguments can construct them.
- `try_push(value)` inserts a single value through `try_emplace`, copying an lvalue or moving an rvalue when the element type supports it.
- Both `try_emplace` and `try_push` return `false` on a full queue without constructing an item or moving from the supplied arguments.
- `emplace(args...)` retries insertion, yielding to the scheduler while the queue is full. It returns `void` after successfully constructing the item.
- `push(value)` forwards a single value to `emplace` and has the same waiting behavior.
- `front()` is a consumer operation that returns a pointer to the oldest item, or `nullptr` when empty. It lets you read or modify that item without removing it. Repeated calls return the same pointer; at this stage, it remains valid until queue destruction.
- If an item's constructor throws, the exception propagates and the queue's occupied slots stay unchanged.
- The queue's destructor destroys all inserted items and releases the storage.

Capacity zero throws `std::invalid_argument`; capacities beyond the storage limit throw `std::length_error`. The queue itself cannot be copied or moved. Element types must be non-array objects without `const` or `volatile` qualification, and their destructors must not throw.

Removal operations such as `pop` and `try_pop` have not been added yet. Once filled, the queue stays full until destruction; calling `front()` does not free a slot. For this stage, call `emplace` and `push` only when space remains: calling either on a full queue would wait forever because there is no consumer operation to free a slot. Transferring items between producer and consumer threads comes later.

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
        std::cout << *item << '\n'; // Reads "hello" without removing it.
    }

    // Leaving this scope destroys all four stored strings and frees the storage.
}
```

Output:

```text
true
true
false
hello
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

## Compile and run the example

After saving the example above, run from the repository root:

```sh
c++ -std=c++23 -Wall -Wextra -Wpedantic -Iinclude example.cpp -o example
./example
```
