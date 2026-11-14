# Single source of truth, state ownership and resource lifecycle

This imports Flutter's state/lifecycle guidance into C++ **without importing Flutter widget patterns**.

## Ownership map
For every mutable state, controller, observer, handle, buffer, file, socket, thread, cache and subscription, write down **who owns it**, who borrows it, who invalidates it and what executes on destruction. Multiple observers are fine; multiple independently mutable copies of a truth need an explicit reconciliation contract.

- Views and spans are **non-owning** (`std::span`, `std::string_view`); never return a view of a local or store it past the owner's lifetime.
- Avoid callbacks that capture a dangling raw `this`; inspect event-loop dispatch, timers, UI binding and async cancellation. Prefer explicit unsubscribe, weak handles or lifetime-managed tokens when appropriate.
- When changing state from a callback, check that the callback still belongs to the current operation and has not been superseded by a newer request (stale-result protection).
- Destruction/cleanup must be exception-safe and idempotent where repeated teardown is possible. RAII is necessary but design still must account for cycles and shutdown ordering.
- `std::shared_ptr` should represent intentional shared ownership, not a default answer to uncertain lifetime. Detect cycles and needless atomic refcount traffic in hot paths.
- Never introduce a global mutable cache or singleton just to avoid passing a dependency. Test initialization, invalidation and concurrency when global state is truly required.

## UI/event-loop adapters (Qt, ImGui, native, games)
Check thread affinity, event dispatch, focus/keyboard, accessibility and platform differences only if the project has user-facing UI. Keep side effects out of frequent rendering callbacks where possible. Do not force MVVM, BLoC, Provider, ECS or a UI framework migration to satisfy this skill.

**Verification:** tests for unsubscription, close/dispose during pending work, duplicate events, stale-result races, hot reload/re-initialization if applicable, and backend/UI boundaries.
