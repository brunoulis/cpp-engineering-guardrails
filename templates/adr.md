# ADR-[number]: [C++ boundary or migration]

- Status: Proposed / Accepted / Superseded
- Date, owner, affected package/service(s)
- Context: observed problem, invariants, current C++ and toolchains, constraints.
- Decision drivers: correctness/safety, build cost, ABI/API, ownership, latency, testability.
- Alternatives: stay as-is, minimal change, deeper restructure with explicit tradeoffs.
- Decision and justification: evidence, not just desired architecture.
- Consequences: migration plan, breakages/consumer updates, compile/link costs, memory/resource semantics.
- Verification: source/ABI/consumer tests, compilation matrix, sanitizer/TSan/profiling as relevant.
- Rollback and cleanup criteria: versioned checkpoints and condition for deleting adapters.
