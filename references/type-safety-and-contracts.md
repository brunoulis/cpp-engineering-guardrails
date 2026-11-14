# Type safety, valid states, error contracts (from Rust guardrails)

## Translate *principles*, not syntax
Rust's ownership types and typestate inspire design questions; C++ does **not** acquire borrow-checker guarantees by imitating their names. Prefer making invalid states hard to represent where it lowers total complexity.

- **Strong domain types:** consider `struct UserId { std::uint64_t value; };`, explicit constructors and validation at boundaries. Avoid mixing bytes, positions, millimeters, times, handles and counts. Do not create a wrapper around every primitive; choose boundaries where unit confusion or invalid state has caused bugs.
- **Validated values:** parse/untrusted input once into a constrained type; constructors/factories should establish invariants and document how errors are returned. Avoid throwing from destructors.
- **Typestate:** compile-time protocols (`Disconnected` → `Connected` → `Closed`) can prevent illegal sequencing, but only when lifecycle complexity justifies template/API proliferation. Alternatives: a small runtime enum with invariant checks.
- **Generational handles:** if callbacks retain indexes to mutable pools, consider `{slot,generation}` IDs to reject stale references after reuse. Verify wraparound, concurrency and invalidation. A stable index alone isn't safe.
- **Error semantics:** maintain existing exceptions/error codes. C++23 `std::expected<T,E>` is optional only on supported toolchains; use an established project result type, `optional`, or typed exceptions as appropriate. Never silently swallow failures or turn errors into success defaults.
- **Contracts:** record preconditions, postconditions, exception guarantees and thread-safety level; test invalid input and boundary values. `assert` may be disabled in release, so do not rely on it for essential public validation.
- **Exception safety:** identify basic/strong/no-throw guarantee where meaningful. Favor RAII for rollback and resource cleanup, commit/swap for state replacement when needed. Validate partial initialization and cancellation paths.

## Decision process
1. Identify the invalid state, source of user-visible failures and frequency of modification.
2. Compare a plain struct + validation, a strong type, an enum state machine and typestate.
3. Select the least complex representation that meaningfully prevents defects; preserve ABI if public layout/signatures are exposed.
4. Test negative paths, copy/move behavior, object lifetime and downstream consumers.

**Anti-slop:** never recommend stronger type systems purely because Rust uses them. C++ interop/layout constraints may make an explicit runtime contract superior.
