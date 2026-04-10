# SMU (Static Memory Unit) 🛡️

[![CI Build](https://github.com/EvgenyKozulev/smu/actions/workflows/cmake.yml/badge.svg)](https://github.com/EvgenyKozulev/smu/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)]()

**SMU** is a high-performance, deterministic memory allocator based on a **Red-Black Tree** backend, engineered specifically for resource-constrained environments (Embedded, RTOS, Bare-metal).

## ✨ Key Features

*   **Zero Dynamic Allocation** — Operates entirely within a pre-allocated static pool. No `malloc`, no system `new`.
*   **Quantized Block Management** — Memory is divided into fixed-size quanta (`minBlockSize`), eliminating micro-fragmentation.
*   **Block-Chain Allocation** — Uses a unique contiguous block lookup strategy via RB-Tree iterators to find and extract sequential memory segments.
*   **Memory Safety** — Built-in **Integrity Checks** and **XOR-checksummed metadata** to detect and prevent memory corruption.
*   **Sterile Execution** — Zero STL dependencies, designed specifically for `no-std` and bare-metal C++20 environments.
*   **Introspection** — Real-time tracking of busy/free nodes, admin overhead, and internal tree balance.

## 🛠 Architecture

SMU uses a **dual Red-Black Tree** approach to separate concerns:

1. **SmuTree** — Core Red-Black Tree implementation 
   - Self-balancing BST with color properties
   - Iterator support for in-order traversal
   - Generic key-value storage via `std::span<std::byte>`

2. **SmuFmb (Free Memory Blocks)** — Inherits SmuTree, manages free memory
   - Best-fit allocation: finds contiguous block sequences
   - `extract(size)` — retrieves free blocks from pool
   - `release(nodes)` — returns allocated blocks to free pool

3. **SmuTab (Tracker Table)** — Inherits SmuTree, tracks active allocations
   - XOR-checksummed metadata for corruption detection
   - `push(nodes)` — stores active allocation info
   - `pop(key)` — safely retrieves with integrity validation

4. **Smu** — High-level coordinator
   - Aligns user pointers to requested boundary
   - Integrity checks after every operation
   - Real-time statistics (free/busy nodes, admin overhead)

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

## 📦 JSON Integration (RapidJSON)

SMU is designed to handle heavy libraries like RapidJSON in environments where the system heap is forbidden:

```cpp
SmuRapidAllocator allocator;
rapidjson::GenericDocument<rapidjson::UTF8<>, SmuRapidAllocator> d(&allocator);

d.SetObject();
d.AddMember("status", "sterile", d.GetAllocator());
// All internal JSON nodes are now stored in your static SMU pool!
```

## 📊 Monitoring & Debugging

SMU provides granular control over memory health:

- `busyBytes()` — Total user data currently allocated
- `freeBytes()` — Available memory in free pool
- `getAdminSpace()` — Precise memory overhead used by RB-Tree nodes
- `checkIntegrity()` — Instant validation of internal tree structure and stats
- Tree color balance tracking (red/black node counts)

## 🧪 Testing

Comprehensive test suite with **74 unit and integration tests** covering:

- ✅ **Core Operations** (test0): RB-Tree insert/remove/search, dual-stack memory layout
- ✅ **Allocator** (test1): Block extraction, best-fit search, fragmentation patterns
- ✅ **Integration** (test2): FMB↔TAB coordination, metadata validation, security scenarios
- ✅ **API** (test3): High-level Smu interface, exhaustion, chessboard fragmentation, corruption isolation
- ✅ **Stress** (test4): RapidJSON integration, deep recursion (256 levels), chaos monkey (500 ops)
- ✅ **Edge Cases** (test5): Boundary conditions, alignment verification, double-free detection, cyclic patterns

All tests pass on:
- Linux (GCC 11+, Clang 13+)
- Windows (MSVC 2022)
- macOS (Clang)

Run time: **~350ms** for full suite

## � Performance Benchmarks

### Fair Comparison: Limited Heap Scenario
Realistic embedded-style benchmarks with constrained memory pools (no unlimited system heap advantage):

**JSON Processing Benchmark (256KB pool, 200 objects)**
- **SMU Allocator**: 3585μs ⚡
- **System malloc**: 108μs
- **Ratio**: SMU ~33x slower (expected for safety features)

**Embedded Stress Test (16KB pool, 100 allocations)**
- **Allocations**: 100/100 successful (no pool exhaustion)
- **Time**: 239μs for 100 alloc/dealloc cycles
- **Pool Efficiency**: 39.3% free after cleanup (excellent fragmentation resistance)

### Memory Efficiency
- **Admin Overhead**: ~30-50% (RB-Tree metadata + alignment padding)
- **Fragmentation**: Zero micro-fragmentation (quantized blocks)

## �📋 Requirements & Configuration

| Parameter | Recommended | Notes |
|-----------|-------------|-------|
| **C++ Standard** | C++20 | Requires `std::span`, atomics, constexpr support |
| **Compiler** | GCC 11+, Clang 13+, MSVC 2022 | Full C++20 support required |
| **Alignment** | 8–64 bytes | Depends on data type alignment needs |
| **Min Block Size** | 64–512 bytes | Smaller = higher overhead, less fragmentation |
| **Pool Size** | 4KB–256KB+ | Must accommodate: `(blockCount * (NodeSize + blockSize))` |
| **Use Case** | Embedded, RTOS, no-heap | Sterile execution without dynamic memory |

### Memory Layout Example
```
Pool: 32768 bytes, alignment=16, minBlockSize=64

[nil Node: 48B] [SmuTab: ~80B] [SmuFmb: ~64B] [RB-Nodes: N×48B] [Data: N×64B]
     ↓                ↓              ↓                ↓               ↓
    8B             metadata       allocator        tree nodes    user storage
```

## 🏗 Build & Test

The library requires a C++20 compliant compiler (GCC 11+, Clang 13+, or MSVC 2022).

```console
mkdir build && cd build
cmake ..
cmake --build .
ctest --verbose
```

To run individual test suites:
```console
./SMU_TEST --gtest_filter=SmuTreeTest.*          # Core tree tests
./SMU_TEST --gtest_filter=SmuFmbTest.*           # Allocator tests
./SMU_TEST --gtest_filter=SmuIntegrationTest.*   # Integration tests
./SMU_TEST --gtest_filter=SmuInterfaceTest.*     # API tests
./SMU_TEST --gtest_filter=SmuJsonTest.*          # RapidJSON stress
./SMU_TEST --gtest_filter=SmuEdgeCasesTest.*     # Edge cases & negative paths
```

## 📄 License

MIT License — See [LICENSE](LICENSE) for details