# Memory layout, ownership cost and predictable hot paths

From Rust's measured allocations/SoA/generational arenas and Flutter's frame-sensitive work. No blanket zero-allocation mandate.

## Before changing representations
Use representative profiling to locate hot functions, cache misses, allocation frequency, heap fragmentation, CPU time, peak RAM, p95/p99 and tail latency. Separate **user-visible performance** from microbenchmark wins. Compare build flags, CPU affinity/power state, compiler versions and realistic data distributions.

## Options to assess with evidence
- `std::vector`/contiguous storage vs node-based containers; stable references/iterator invalidation; small-buffer and SSO behavior are implementation dependent.
- AoS vs SoA vs hybrid layouts; tradeoffs include cache locality, ergonomics and conversion overhead.
- Scoped arena/pool reuse vs general allocator; inspect memory growth, peak usage, fragmentation, thread contention and invalidation.
- Move vs copy, `std::span` borrow vs owned buffer, zero-copy vs cross-thread/FFI lifetime; never sacrifice correctness for fewer copies.
- Branch prediction and early exits vs SIMD and fixed-size reductions; vectorization remarks are **not** runtime evidence.
- False sharing, producer/consumer locality, `atomic` contention and NUMA are specialized topics requiring targeted profiling.

## C++/Rust/Flutter crossings
For every handoff verify stable C ABI, buffer ownership and free function, allocator on each side, alignment, encoding, lifetimes, callback threading, exception/panic containment and version compatibility. Profile *full pipeline*, not only the native stage.

**Anti-slop:** fewer allocations, lines or branches are not necessarily better. Preserve maintainability and measure before/after in comparable builds.
