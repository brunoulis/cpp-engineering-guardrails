# Refactor plan — [component]

## Goal and why now
- Problem, reproduction or maintenance cost; risk P0–P3 and confidence.
- What should get better; what should *not* change.

## Discovery and baseline
- Supported C++ standard, compilers, platforms, build targets, modified user files.
- Current responsibilities/callers/ownership graph and public header/ABI/symbol surface.
- Characterization tests, real fixtures, profiling/build metrics if relevant.

## Alternatives (required)
| Option | Benefit | Risk | Test/effort | Decision |
|---|---|---|---|---|
| Minimal local fix | | | | |
| Structural refactor | | | | |
| Leave as-is | | | | |

## Contracts and scope
Behavior, serialization, errors, ordering, resource lifetime, concurrency, exception guarantees, API/ABI, supported platforms, generated/vendor exclusions.

For safety-driven changes: boundary/resource map, current and intended function
effects (including unknowns), initialized ranges, affected borrows and the
property evidence to preserve. Use [function-contract.md](function-contract.md)
and [safety-evidence.md](safety-evidence.md); define how changed identities expire
summaries and how copying/reacquiring can affect observable results.

## Checkpoints and rollback
1. Add characterization/negative tests, ensure current behavior is understood.
2. Mechanical extraction (no behavior change), compile/test.
3. Ownership/interface improvement, compile/test + risk-based gates.
4. Optional measured performance changes, separate before/after verification.
5. Remove legacy only after consumers pass; retain rollback marker.

## Verification matrix
| Gate | Exact command | Baseline | After | Status |
|---|---|---|---|---|

## Skeptical reviewer
Questions, independent findings, rejected options, unresolved risk, debt backlog, accepted/deferred candidate rules.
