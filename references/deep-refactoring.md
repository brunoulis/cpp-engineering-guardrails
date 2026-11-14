# Deep refactor planning template

**Prerequisites:** full build graph, artifacts, consumers, public surface, C++ version matrix, baseline tests/perf/ABI, deployment plan and rollback. If any are unknown, disclose them before asserting feasibility.

## Workstream plan
1. Map existing ownership/data flow and source-of-truth invariants.
2. List change boundaries and migration alternatives; quantify benefit, complexity, maintenance overhead and risk.
3. Stage compatibility seams and adapters; keep existing consumers operational where required.
4. Migrate lowest-risk module first, build+test each stage, capture public contract changes.
5. Verify new dependency direction and source/ABI compatibility; run concurrency and sanitizer checks in risky modules.
6. Run representative performance and shutdown tests; remove adapters only when safe.
7. Final code review and rollback test; publish migration decision record.

**Anti-patterns:** one giant PR; rewriting tests to match bugs; premature C++20 modules adoption; replacing dozens of raw pointers without ownership classification; cascading template type changes without real instantiations.
