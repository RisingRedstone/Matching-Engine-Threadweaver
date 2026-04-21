# ThreadWeaver: High-Performance Matching Engine

ThreadWeaver is a specialized, ultra-low latency **Matching Engine** built in C++23. The project is designed with a hardware-first philosophy, prioritizing cache-line alignment, lock-free data structures, and zero-copy memory management to achieve maximum throughput for high-frequency financial operations.

---

## 🚀 Core Philosophy

- **Mechanical Sympathy**: Data structures are aligned to 64-byte boundaries to prevent false sharing.
- **Lock-Free Primitives**: Utilization of x86 atomic bit-manipulation (`lock bts`) and C++20 concepts for compile-time safety.
- **Tracing & Observability**: Integrated with LTTng-UST for non-blocking, high-performance execution tracing and URCU (Userspace RCU) for safe memory reclamation.
- **Reproducible Environment**: Fully managed development via Nix for consistent builds across GCC and Clang.

---

## 🛠 Progress So Far

We have laid the groundwork for the core messaging and memory layers:

### 1. Advanced Memory Layouts

Implemented two distinct ring buffer strategies located in `src/buffer/layout/`:

- **Three-Pointer Approach**: A traditional lock-free MPSC (Multiple Producer, Single Consumer) layout using `write_head`, `read_head`, and `commit_head`.
- **Cell-Lockable Approach**: A high-throughput design where synchronization is offloaded to individual memory cells. Initial tests show this approach handling **~80 million ops/sec** across 8 producers.

### 2. High-Perf Allocators

- **One-Time Shared Memory Allocator**: A POSIX `mmap` based allocator featuring Atomic Reference Counting (ARC). This allows memory segments to be shared across process boundaries with safe cleanup.

### 3. Hardware-Aware Containers

- **CacheLinePacked**: Union-based containers that ensure data structures fit perfectly within CPU cache lines, optimizing fetch performance.

---

## 🏗 Development Environment

This project uses **Nix** to manage dependencies. This ensures that LTTng, URCU, and the specific compiler versions (GCC 13 / Clang 18) are always available.

### Setup

1.  Enter the development shell:
    ```bash
    nix-shell
    ```
2.  The shell provides several pre-configured aliases (see `shell.nix` for details).

---

## 🏃 Running the Engine

Currently, the project is in the architectural validation phase. You can run the test suites to verify the integrity of the ring buffers and allocators.

### Build and Test

Use the built-in aliases for a streamlined workflow:

```bash
# 1. Configure for Debug or Release
config_debug    # or config_release

# 2. Compile the project
run_build

# 3. Execute the test suite
run_test
```

### Manual Execution

If you prefer running specific test binaries manually:

```bash
./bin/debug/ring_buffer_test
./bin/debug/buffer_general_test
```

---

## 📈 Roadmap

### Phase 1: Benchmarking (Current)

Before expanding the protocol, we are performing rigorous stress tests on the `Three-Pointer` vs. `Cell-Lockable` layouts.

- Generate latency distribution graphs (heatmaps).
- Analyze cache-miss behavior using `perf` and `valgrind` (via `perf_prof` and `valgrind_prof` aliases).

### Phase 2: The Disruptor Pattern

- Implementation of **SCSP (Single-Consumer, Single-Producer)** buffers inspired by the LMAX Disruptor.
- Adding "wait strategies" (BusySpin, Yielding, Sleeping) to the `ReadLockLoopExtension`.

### Phase 3: The Order Book

- Implementation of **Lock-Free Skip-Lists** to serve as the core Order Book data structure, allowing for $O(\log n)$ insertions and deletions while maintaining thread safety.

---

## 📚 Documentation

Detailed API documentation is generated via Doxygen.
To build and view the docs:

```bash
build_docs
view_docs
```

---

> **Note**: The `ring_buffer_low_cas_test` is currently flagged as failing in `CMakeLists.txt` and is omitted from `ctest`. This is part of the current debugging cycle for LTTng provider integration.
