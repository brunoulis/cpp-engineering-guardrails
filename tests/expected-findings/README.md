# Expected C++ fixture behavior

- Positive `concepts_positive.cpp` must compile as C++20 and instantiate an actual insertion.
- Negative `concepts_negative.cpp` **must not compile**; the **native C++ fixture runner verifies that the diagnostic concerns insertion** (not an unrelated compiler failure on GCC/Clang).
- Positive `lifetime_positive.cpp` must compile; `lifetime_negative.cpp` is a deliberately unsafe example for manual/Clang lifetime review, not a guaranteed compiler failure.
- Scanner heuristics report suspected P2/P3 candidates only, never pretend a text match proves a memory bug.

## Safety contract examples (C++17)

`safety_contracts_positive.cpp` is compiled and run by the CTest
`safety-contract-examples` target. Active Release checks exercise in-place updates,
storage replacement, empty-before/after-call behavior and partial/full/failed
output with written-length validation. The callback example deliberately defines
an after-call result; it is not a drop-in behavior-preserving fix for all callers.

`compiler-fixtures` syntax-checks both new files as C++17.
`safety_contracts_negative.cpp` is **never linked or executed** in normal tests.
No portable compiler diagnostic or lexical finding is required for these cases:

| Negative case | Expected semantic review conclusion |
|---|---|
| `unknown_call` | Nonempty precondition needs evidence; opaque-call invalidation remains UNVERIFIED, not confirmed UAF |
| `unchecked_empty` | Empty input violates `front()`'s precondition unless excluded by a verified caller contract |
| `destroyed_element` | Final read accesses an element after its lifetime ended; capacity preservation does not rescue it |
| `replaced_storage` | Final read uses freed original storage; swap followed by destruction guarantees replacement without relying on growth |
| `partial_initialization` | Final sum reads an uninitialized int |
| `escaped_stack` | Post-return use is unsafe if the callee retains the address; retention is unknown from the declaration |

These are examples for agents/reviewers and future analyzer controls, **not tests
that the current scanner detects these defects**. A green compiler or CTest
result establishes only example compilation and the executed positive behavior.
Review procedure and evidence states live in
[`references/safety-intelligence.md`](../../references/safety-intelligence.md).

After rules change, edit the C++ `cpp-rails-selftest` and `cpp-rails-fixtures`, not Rust/Python unit tests. Do not create or regenerate baselines just to hide regressions.
