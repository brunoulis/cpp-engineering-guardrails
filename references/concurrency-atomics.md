# Threads, atomics, cancellation and object lifetime

## Correctness first
- Identify each shared state owner and happens-before edge. Distinguish data race (UB), race condition (business logic), deadlock, starvation and contention.
- Use scoped locks / `std::scoped_lock`; establish lock ordering across components and avoid callbacks while holding locks unless contract explicitly requires it.
- `std::jthread` and stop tokens can improve cancellation, but callbacks, blocking I/O and destructor join semantics must be reviewed. A detached thread requires a **proven** lifetime/termination model.
- `memory_order_relaxed` is appropriate only when ordering is not needed; do not replace with stronger/weaker orders by intuition. Document atomic invariants and publication protocol.
- `shared_ptr`'s reference count being thread-safe does **not** make pointee mutation safe.
- Check initialization, shutdown and exceptions in worker threads; handle ownership across async callbacks, especially FFI.

## Performance
- Measure contention, false sharing and cache ping-pong, queue depth, tail latency and oversubscription. An atomic spin loop may waste CPU and reduce fairness.
- Keep critical sections as short as justified; correctness and predictable shutdown outrank speculative lock-free complexity.

## Verification
- Reproductions, stress tests, deterministically controlled interleavings where possible; TSan in **separate build** from ASan/UBSan, supported platforms only; shutdown/cancellation tests under load.
- Sanitizers have coverage and overhead limitations; no passing run constitutes a data-race proof.
