# spscQ.cpp23 

A attempt at a modern cpp version of single producer single consumer queue. 


```bash
Producer thread                      Consumer thread
      │                                    │
      │ push(A)                            │
      │ push(B)                            │
      │ push(C)                            │
      │                                    │
      └───────> [ A ][ B ][ C ] ──────────>│
                                           │ pop A
                                           │ pop B
                                           │ pop C
```

Only one thread is allowed to add elements, and only one thread is allowed to remove elements. 

Because there is only one writer on each side, the queue can avoid 

```cpp
std::mutex
std::lock_guard 
std::condition_variable
```

and instead coordinate using just a couple of atomic indices. 

