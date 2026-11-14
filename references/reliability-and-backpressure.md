# Async reliability, bounded work, cancellation and shutdown

Adapted from Rust's cancellation/backpressure/Sans-IO lessons and Flutter's async lifecycle lessons. **Correct ownership and graceful shutdown are quality gates.**

## Map task ownership
For every thread, task, future, callback, scheduler and external resource, answer:
- Who starts it? Who stops it? Who awaits/joins it? What happens on exceptions and shutdown?
- Can results arrive after owning object destruction? Are callbacks holding borrowed references or `this`? Are weak ownership tokens appropriate?
- Is the queue bounded? What happens when a producer outruns a consumer? Define drop, coalesce, reject, block or backpressure policy; don't default to unlimited buffers.
- What controls retries, idempotency, timeouts, deadlines and cancellation propagation? Is re-entry safe?

## C++ implementation choices
- Prefer structured ownership of threads (`std::jthread` with stop token where available). `std::thread` with explicit scope/join is fine for older standards. Detached workers require documented lifecycle proof.
- `std::future` from `std::async` has destructor/wait semantics that can surprise; don't fire-and-forget by immediately discarding futures.
- A `condition_variable` wait needs a predicate to handle spurious wakeups. Check lock ordering and callbacks invoked under mutex.
- Bounded producer/consumer queues should publish measurable queue depth, rejection/drop metrics and shutdown drain behavior.
- Keep I/O separate from state machine where possible (**Sans-IO** principle): pure-ish parsing/state transitions are easier to replay deterministically. Do not introduce layers unless it improves testing, reliability or portability.
- Never substitute a lock-free queue for a mutex-backed queue without proving correctness under the target memory model **and** measuring representative throughput/latency.

## Tests
- Shutdown while callbacks are pending; slow consumer; producer burst; cancellation during I/O; exceptions during task completion.
- Repeat under TSan in a separate build on supported platforms and run deterministic interleaving/unit tests when possible.
- Record p95/p99, queue occupancy and dropped-work counts only when using real workloads; performance is not synonymous with avoiding mutexes.
