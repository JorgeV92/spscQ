# spscQ.cpp23

This repo builds up the C++23 `spsc23::SPSCQueue` implementation piece by piece. The intended result is a header-only, bounded single-producer, single-consumer FIFO queue. The current stage implements **construction, destruction, and `try_emplace`**.

## What currently works

- `SPSCQueue<T>(capacity)` allocates storage once without constructing any `T` objects. The requested capacity is the exact number of items you can insert.
- `try_emplace(args...)` forwards arguments to construct a `T` directly in a queue slot and returns `true` on success. It supports move-only and immovable types when the supplied arguments can construct them.
- A full queue returns `false` without constructing an item or moving from the supplied arguments.
- If an item's constructor throws, the exception propagates and the queue's occupied slots stay unchanged.
- The queue's destructor destroys all inserted items and releases the storage.

Capacity zero throws `std::invalid_argument`; capacities beyond the storage limit throw `std::length_error`. The queue itself cannot be copied or moved. Element types must be non-array objects without `const` or `volatile` qualification, and their destructors must not throw.

Consumer operations such as `front`, `pop`, and `try_pop` have not been added yet. Once filled, the queue stays full until destruction. This stage lets you test allocation and insertion; transferring items between producer and consumer threads comes later.

## Example

Save this as `example.cpp` in the repository root:

```cpp
#include <spsc23/SPSCQueue.h>

#include <iostream>
#include <string>

int main() {
    // Reserve room for two strings; no strings are constructed yet.
    spsc23::SPSCQueue<std::string> queue(2);

    std::cout << std::boolalpha;
    std::cout << queue.try_emplace("hello") << '\n';
    std::cout << queue.try_emplace(3, '!') << '\n'; // Constructs "!!!" in place.
    std::cout << queue.try_emplace("full") << '\n';

    // Leaving this scope destroys both stored strings and frees the storage.
}
```

Output:

```text
true
true
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

## Compile and run the example

After saving the example above, run from the repository root:

```sh
c++ -std=c++23 -Wall -Wextra -Wpedantic -Iinclude example.cpp -o example
./example
```
