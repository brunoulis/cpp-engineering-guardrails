# Adopt safety analysis and migrate unsafe boundaries

Use for incremental safety requirements or an ownership/FFI migration in any of
the four modes. Preserve existing behavior under R1 and use
[safety-intelligence.md](safety-intelligence.md) for the map and evidence states.

## Choose policy per component

| Reviewer policy | Required outcome |
|---|---|
| Inventory | Map owners, calls and uncovered properties; prioritize risk without inventing guarantees |
| Reviewed boundaries | Record current contracts for consequential calls, review unknown effects and track exceptions with owner/reason/review/expiry |
| Strict evidence | Select required properties and mechanisms before checking; leave acceptance unresolved for required UNVERIFIED evidence and block confirmed critical defects |

These are human/agent policy choices, not new CLI profiles. Record component,
properties, acceptance criteria and exception treatment in the evidence template.
No count of green checks is a memory-safety certificate. Exceptions preserve
their underlying FAIL/UNVERIFIED state and need explicit acceptance under the
project's policy. Never promote a heuristic P2 into a confirmed P0 to enforce it.

## Migrate one boundary at a time

1. Establish the existing error, numerical, ordering, allocation, destruction,
   API/ABI, threading and latency contracts. Characterize real consumers first.
2. Select a boundary from the safety map: opaque call, retained callback,
   allocator pair, raw buffer or storage-reuse region. Compare local validation,
   restricted effects, ownership adapter and leave-as-is with explicit debt.
3. Add tests for nonempty/empty, success/partial failure, no-reallocation/forced
   replacement, aliasing, exceptions and shutdown as applicable. Negative unsafe
   fixtures are review/compiler inputs; execute UB only in isolated diagnostic
   experiments with an expected sanitizer signal, never the normal test suite.
4. Change the smallest contract/ownership boundary. Keep adapters where needed;
   preserve allocation/deallocation pairing and destruction/thread timing.
   Copy-before-call, check-after-call and changing a borrow to ownership can each
   change behavior. State intentional differences and authorization explicitly.
5. Refresh summaries and property evidence for affected callers and configurations.
   Check consumers, relevant sanitizers and representative release performance.
   Compare scope/evidence against baseline; preserve a rollback checkpoint.

Use [refactor-plan.md](../templates/refactor-plan.md) and the deep-refactor ADR for
broad migrations. No mandatory smart-pointer rewrite, Rust imitation, new C++
standard or compiler replacement follows from this workflow.

## Optional external experiments

| Tool | Evaluate on an isolated, pinned experiment |
|---|---|
| [Fil-C](https://fil-c.org/) | An alternative C/C++ implementation using runtime checks, capabilities and concurrent GC. Check supported platform/ABI/FFI, dependencies, allocation/destruction interactions, latency, throughput and memory costs with real workloads |
| [scpptool](https://github.com/duneroadrunner/scpptool) | Experimental Clang-based subset checker associated with SaferCPlusPlus. Check required container/API changes, compiler compatibility, annotation coverage and exclusions; its authors warn against relying on complete prevention of invalid accesses |

Neither is a required dependency, an existing `cpp-rails` integration or a
certificate for another compiler/build. Record tool/library revisions, supported
subset, excluded/native boundaries, exact commands, diagnostic controls, consumer
results and measured tradeoffs. Failures are findings to investigate, not license
to rewrite the project or weaken its tests. Do not install or migrate merely to
make a report look more complete.

## Native-tool coverage and remaining roadmap

v1.4 implements optional Clang AST subprocess analysis with actual compile
commands and bounded intraprocedural contracts. See
[semantic coverage](../tools/cpp-rails/SEMANTIC_BACKEND.md). The `safety map`, `contracts`,
`boundaries` and `regressions` views expose the same conservative analysis;
`regressions` also requires a reviewed baseline. Unknown effects and unsupported
scope stay unknown. Remaining work includes:

- P0: explicit, revision-bound resource-effect summaries and property evidence;
  unknown/conflicting contracts must propagate rather than silently becoming safe.
- P1: optional Clang LibTooling semantic inventory using actual compile commands,
  reviewed diagnostic integration and positive/negative initialization/invalidation
  fixtures. Keep the core CLI dependency-free C++23. Pin the LLVM build's own
  language/toolchain requirements separately; newer LLVM may require newer C++.
- P2: guided migrations and optional Fil-C/scpptool compatibility experiments.

`cpp-rails-check --safety-profile strict` remains proposed. There is no `[safety]`
configuration parser, general cross-TU summary verifier or linked LibTooling
component. The optional backend invokes Clang and consumes its AST; no LLVM
development-library dependency is required. Consult the
[CLI README](../tools/cpp-rails/README.md) before selecting interfaces.
