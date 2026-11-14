# Expected C++ fixture behavior

- Positive `concepts_positive.cpp` must compile as C++20 and instantiate an actual insertion.
- Negative `concepts_negative.cpp` **must not compile**; the **native C++ fixture runner verifies that the diagnostic concerns insertion** (not an unrelated compiler failure on GCC/Clang).
- Positive `lifetime_positive.cpp` must compile; `lifetime_negative.cpp` is a deliberately unsafe example for manual/Clang lifetime review, not a guaranteed compiler failure.
- Scanner heuristics report suspected P2/P3 candidates only, never pretend a text match proves a memory bug.

After rules change, edit the C++ `cpp-rails-selftest` and `cpp-rails-fixtures`, not Rust/Python unit tests. Do not create or regenerate baselines just to hide regressions.
