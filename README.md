# 🚀 Custom In-Memory Data Store (Redis-like DB in C/C++)

A high-performance in-memory database engine written **from scratch in C/C++**, inspired by Redis-like architectures. This project implements the fundamentals of distributed systems, low-level network programming, and advanced data structures without external dependencies.

---

## ✨ Key Features & Architecture

1. **Non-Blocking Network & Event Loop**
   - Asynchronous TCP server based on Linux's `poll()` system call.
   - Fine-grained handling of non-blocking file descriptors (`O_NONBLOCK`) and signal prevention (`EINTR`).
   - Pipelining support to handle massive query streams per loop iteration.

2. **Binary Communication Protocol (TLV)**
   - Custom protocol based on length prefixes and a **Tag-Length-Value (TLV)** serialization format for fast, unambiguous decoding of types (Strings, Integers, Floats, Arrays).

3. **Advanced Intrusive Data Structures**
   - **Custom Hash Table**: Built-in implementation featuring *progressive rehashing* (incremental rehashing in small batches) to eliminate latency spikes during mass insertions.
   - **AVL Trees**: Integrated self-balancing binary search trees for sorting management.
   - **Sorted Sets (ZSET)**: Combination of a hash table and an AVL tree for optimal lookups by name or score range ($\mathcal{O}(\log N)$).

4. **Time Management & Expiration (TTL)**
   - **Idle Timeout**: Automatic closure of inactive connections via a doubly linked list bound to the monotonic clock (`CLOCK_MONOTONIC`).
   - **Min-Heap**: Efficient lifecycle management for keys with TTL to automatically purge expirations without overloading the event loop.

5. **Multithreading & Asynchronous Tasks**
   - **Thread Pool**: Implementation of a producer-consumer model secured by mutexes and condition variables (`pthread`) to offload heavy destructions (such as purging massive structures) to the background without freezing the event loop.

6. **Clé-Valeur de base (GET, SET, DEL)**
   - An integrated textual mini-parser enabling interaction with the server using `GET`, `SET`, and `DEL` queries.

---

## 📁 Project Structure

```text
├── server.cpp          # Server core, event loop, and request router
├── hashtable.cpp/.h    # Intrusive hash table with progressive rehashing
├── avl.cpp/.h          # Self-balancing AVL tree
├── zset.cpp/.h         # Sorted Sets implementation (Hash Table + AVL)
├── heap.cpp/.h         # Min-Heap for TTL management
├── thread_pool.cpp/.h  # Multithreaded thread pool (mutexes & condvars)
└── client.py       # Interactive testing and administration client
```

##▶️ PROGRAM EXECUTION :
1. In a Linux terminal, run the global compilation command:

```code
g++ -Wall -Wextra -O2 -g server.cpp hashtable.cpp avl.cpp zset.cpp heap.cpp thread_pool.cpp -o server -pthread
```
2. Start the server:

```code
./server
```
3. To test the program, you need a client file such as client.py. In another terminal , run:

 ```code
python3 client.py
```


<img width="532" height="180" alt="image" src="https://github.com/user-attachments/assets/7dfffe27-7f14-41cf-bd51-56bbd620b071" />


#### 🎯 Educational Objective

This project was designed following Richard Feynman's philosophy: "What I cannot create, I do not understand". By bypassing modern framework abstractions and relying on standard tooling only for basic primitives, it demonstrates how to design high-performance, resilient, and production-ready system software.




 
 

