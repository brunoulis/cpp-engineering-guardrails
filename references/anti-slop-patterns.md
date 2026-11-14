# Anti-slop guardrails: challenge AI-generated code without banning valid designs

AI can generate code that compiles and even passes narrow tests while being expensive to maintain. Treat code review as verification of **contracts, clarity, necessity and evidence**, not a count of modern C++ features.

## Red flags and counterexamples

| Risk signal | What to check | What is **not** automatically wrong |
|---|---|---|
| Unrequested rewrite across many directories | Scope expansion, reviewability, lost user edits, hidden behavior/ABI changes | Large migration backed by an approved staged plan |
| Interfaces/factories for one concrete implementation | Actual swap/test boundary, extra layers, reasoning cost | Stable SDK plugin boundary with multiple consumers |
| Massive hierarchy/template metaprogramming | Simpler value/overload/composition alternative, compile cost | Proven generic public library with type-level constraints |
| Repeated `shared_ptr`/`weak_ptr` as default | Ownership graph, cycles, refcount overhead | Truly shared async lifetime |
| Blanket `std::move`, `std::forward`, `const` or `inline` tweaks | Semantics, NRVO, readability, profiler evidence | Correct local move or forwarding implementation |
| Replacing mutex with lock-free | Happens-before, memory order, ABA, reclamation, stress tests | Verified low-latency requirement with measured gain |
| Catch-all swallowing / default success on failure | Error semantics, telemetry, recovery path | Documented best-effort boundary with observability |
| Duplicated caches / state | Single source of truth, invalidation, stale callbacks | Replication with explicit consistency contract |
| Changed tests that suddenly pass | Deleted assertions, loosened snapshot, mocks matching implementation | Legitimate specification change with independently approved migration |
| Unmeasured “faster” claim | Baseline machine, compiler flags, workload distribution, repeated timings | Explicitly exploratory optimization hypothesis |
| Extracted library/class because a file is long | Cohesion, boundaries, testability, build/ABI cost | Evidence-driven target extraction |
| Overloaded design doc that doesn't match changed code | Callers, actual exports, run commands and tracked debt | Detailed ADR for a broad migration |

## Reviewer protocol for AI changes

1. Split mechanical and behavior-changing diffs. Look at changed public interfaces, resource ownership, callback lifetime and test modifications first.
2. Reconstruct intended behavior from original requirements and consumers — not from the new implementation's happy path.
3. Ask whether the code can be **deleted** or simplified rather than abstracted into more layers.
4. Run the real compiler/test gates; the agent should not explain away errors by changing snapshots or disabling lints.
5. Seek a credible counterexample for each new static rule; a reviewer preference is not a universal safety requirement.
6. Track repeat findings, but promote a new guardrail only after positive and negative fixtures and reviewer acceptance. Retire conflicting or high-noise rules.

**Core invariant:** a smaller diff is preferred when equally correct and maintainable. Large diffs are acceptable when migration evidence warrants them; verbose outputs and green but ineffective tests are not quality.
