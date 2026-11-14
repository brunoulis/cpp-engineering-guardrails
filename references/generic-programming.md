# Generic algorithms and container semantics

Inspect iterator/range categories, mutability, constness, exception guarantees, iterator invalidation, asymptotic complexity, allocation and capacity requirements. Generic code that accepts a `range` is not automatically valid for all ranges.

- `std::array` is fixed-size and lacks `insert`; `std::vector`/`deque` may support insertion but have different iterator/reference invalidation rules.
- `std::inserter` builds an insertion iterator whose assignment semantics depend on the container. `std::back_inserter` requires `push_back`; check the *target* operation.
- Distinguish `output_range`, `input_range`, `forward_range`, `random_access_range` and `contiguous_range`; contiguous storage does not imply resizability.
- Verify handling of empty containers, move-only values, proxy references, sentinel/iterator differences and const ranges.
- Make algorithmic complexity claims under real constraints; check copying/moving per-element and error path cleanup.
- If using `std::ranges`, ensure chosen C++ standard and library fully support the used feature.

**Tests:** compile-time constraints plus actual instantiations, negative build tests with expected diagnostics, runtime boundary tests, and optionally fuzz input types.
