# Initialization and invalidation analysis

Use with [memory-lifetime-ub.md](memory-lifetime-ub.md) for object/storage reuse,
partial output, containers and calls that can end a borrow's validity. Record
per-path facts in [safety-evidence.md](../templates/safety-evidence.md).

## Inspect transitions

| Transition | Required review |
|---|---|
| Allocate storage → construct object | Size, alignment, lifetime start, initialized members/ranges and throwing constructors |
| Fill output → consume bytes | Actual written range on success/failure, capacity, length conversions and terminator requirements |
| Borrow → mutation/call → use | Which element/object is borrowed; all direct/indirect invalidation effects; revalidate after calls |
| Move/transfer → use/release | New owner, remaining source invariants, allocator behavior and outstanding aliases |
| Destroy → reuse storage | Exactly-once destruction for live objects, new lifetime, surviving aliases and any applicable pointer-reuse rules |
| Retain → async completion/cancel | Owner survives callbacks, cancellation joins/drains work, synchronization and teardown ordering |

Do not read uninitialized objects to "test" their validity. Distinguish storage
from a live object, and initialized bytes from a complete valid representation.
Zeroing storage does not construct arbitrary C++ objects or validate protocol
fields. `reserve()` gives capacity, not constructed vector elements. A moved-from
standard-library object remains valid with operation-specific postconditions;
do not label every use-after-move UB. Check custom types and actual operations.

For partial writes, track `[0, written)` separately from `[0, capacity)`, reject
impossible lengths, and define error-output semantics. Even initialized stale
bytes may leak earlier data or violate a message contract. A producer that
doesn't return a length needs another verified way to determine valid output.

For vector reallocation, all references/pointers/iterators to elements invalidate.
Other container operations have different rules: no reallocation is insufficient
if an element is erased/destroyed. Swap can preserve element references while
changing ownership/container association. Check operation-, allocator- and
version-specific rules instead of treating every mutation as either safe or UB.
Use [function-contracts.md](function-contracts.md) at call boundaries.

## Select verification, probe support

Use the project's real compile flags/includes/target. Before claiming a checker
is supported, compile a clean control and a known diagnostic-triggering example
with the selected compiler. Reject ignored/unknown flags and attributes; record
the actual diagnostic. Acceptance of a flag alone does not demonstrate analysis.
If unsupported, keep the property UNVERIFIED and choose another available check.
Standalone examples do not establish project translation-unit coverage.

| Mechanism | Scope and limits |
|---|---|
| Clang `-Wlifetime-safety-permissive` | Version-dependent lifetime diagnostics; current development docs distinguish experimental invalidation checks, which need separate opt-in and positive/negative controls |
| `-Wunsafe-buffer-usage` | Buffer-operation diagnostics; does not establish complete bounds or lifetime safety |
| `-Wthread-safety` | Checks supported annotated lock contracts; unannotated silence is not evidence of race freedom |
| MemorySanitizer (`-fsanitize=memory`) | Test-only initialized-memory diagnostics on supported platforms; separate build with dependencies appropriately instrumented/intercepted; not a substitute for ASan or TSan |
| ASan+UBSan; separate TSan | Execute representative empty/error/reallocation/teardown paths; unexecuted paths remain uncovered |
| `[[clang::nonallocating]]`, `[[clang::nonblocking]]` with `-Wfunction-effects` | Version-dependent performance effect contracts; validate bodies/indirect calls within documented limits; neither annotation proves memory safety or supplies `noexcept` automatically |

MSan is not supported on Windows in the currently consulted documentation. An
unavailable local MSan run remains UNVERIFIED; use a supported test environment
when required. `cpp-rails-check` currently accepts only `asan-ubsan` and `tsan` as
sanitizer modes. Its best-effort `--lifetime` does not expose every Clang check in
this table. Use reviewed project-native commands for additional checks; never
invent `--sanitizer msan` or assume a wrapper's success proves feature support.

[P4222R3](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/p4222r3.pdf)
and [P4418R0](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/p4418r0.pdf)
(2026-10-22) propose initialization and invalidation profiles. Proposed attributes
and profile syntax are not portable C++23. See [sources.md](sources.md) for
official tooling documentation; verify the selected release before applying it.
