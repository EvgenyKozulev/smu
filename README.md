# SMU (Static Memory Unit) 🛡️

**SMU** is a high-performance, deterministic memory allocator based on a Red-Black Tree backend, engineered specifically for resource-constrained environments (Embedded, RTOS, Bare-metal).

## ✨ Key Features

*   **Zero Dynamic Allocation** — Operates entirely within a pre-allocated static pool. No `malloc`, no system `new`.
*   **Quantized Block Management** — Memory is divided into fixed-size quanta (`minBlockSize`), eliminating micro-fragmentation.
*   **Block-Chain Allocation** — Uses a unique contiguous block lookup strategy via RB-Tree iterators to find and extract sequential memory segments.
*   **Memory Safety** — Built-in **Integrity Checks** and **XOR-checksummed metadata** to detect and prevent memory corruption.
*   **Sterile Execution** — Zero STL dependencies, designed specifically for `no-std` and bare-metal C++20 environments.
*   **Introspection** — Real-time tracking of busy/free nodes, admin overhead, and internal tree balance.

## 🛠 Architecture

The system is split into two specialized layers to separate physical memory management from object tracking:
1.  **FMB (Free Memory Blocks)** — Manages the physical slicing, merging, and alignment of free memory quanta.
2.  **TAB (Tracker Table)** — An isolated bookkeeping layer that tracks active allocations, protected by checksums.

## 🚀 Quick Start

### Basic Allocation

```cpp
#include "smu.h"

// 1. Prepare a static pool
alignas(16) static std::byte pool[32768];

// 2. Initialize SMU (16-byte alignment, 64-byte min block)
Smu smu(16, 64, std::span<std::byte>(pool, sizeof(pool)));

// 3. Secure allocation
void* ptr = smu.allocate(1024);
if (ptr) {
    // ... do work ...
    smu.deallocate(ptr);
}
```

JSON Integration (RapidJSON)SMU is designed to handle heavy libraries like RapidJSON in environments where the system heap is forbidden:

```cpp
SmuRapidAllocator allocator;
rapidjson::GenericDocument<rapidjson::UTF8<>, SmuRapidAllocator> d(&allocator);

d.SetObject();
d.AddMember("status", "sterile", d.GetAllocator());
// All internal JSON nodes are now stored in your static SMU pool!
```

📊 Monitoring & DebuggingSMU provides granular control over memory health:busyBytes() — Total user data currently allocated.getAdminSpace() — Precise memory overhead used by RB-Tree nodes.checkIntegrity() — Instant validation of the internal tree structure.🏗 Build & TestThe library requires a C++20 compliant compiler (GCC 11+, Clang 13+, or MSVC 2022).

```console
mkdir build && cd build
cmake ..
cmake --build .
ctest --verbose

```