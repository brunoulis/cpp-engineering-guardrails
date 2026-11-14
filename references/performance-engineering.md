# Performance guardrails: measurable, distribution-aware

**Baseline:** label compiler/flags, build mode, CPU, governor/power state, machine load, input size/distribution, commit and number of repeats; inspect noise and warm-up. Match release LTO/PGO settings where practical. Sanitizer-enabled measurements are for defect discovery, **not** production comparisons.

- Profile before selecting target (wall time vs CPU, I/O vs allocation, caches, lock contention, p99 tails).
- Changes to algorithms and data layouts can dominate hand-tuned loops. Consider end-to-end cost including FFI transfers, allocations and serialization.
- Prefer `std::span` for non-owning view only when lifetime is proven; avoid false "zero-copy" claims.
- Inspect `std::move`/copy elision and actual generated code; forcing `std::move` on a local return can inhibit NRVO in some cases.
- Evaluate batch sizes, pool costs, allocator fragmentation, false sharing and NUMA when relevant.
- Run A/B with repeated samples and representative populations. Use median and spread (e.g., MAD), compute p95/p99 for *per-operation* timings; reject inconclusive wins.
- Performance budgets are project-specific, never hardcode a 5% threshold for everyone.

**Low-latency mode:** focus on fixed-size buffers, bounded allocations, predictable cancellation, bounded queues, backpressure, jitter, FFI costs and exact memory-ownership paths. The goal is latency/correctness measured in the real pipeline, not blindly removing allocations.
