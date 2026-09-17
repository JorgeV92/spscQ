# spscQ.cpp23 

A header-only, bounded **single-producer, single-consumer FIFO queue for C++23 and later**. One thread constructs items; another reads and destroys them. The queue allocates its storage once, keeps the requested capacity, and uses acquire/release atomics to transfer ownership of occupied slots.

A pipeline often has a simple boundary: one thread produces work and one thread processes it. A fixed-capacity queue makes that boundary explicit, limits queued work, and lets the application decide what happens when the consumer falls behind. Examples include handing decoded records to a processing stage, moving owned jobs to a worker, or passing messages from one event loop to another.