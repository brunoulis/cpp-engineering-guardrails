# Quality gate selector — by changed contract, not by style

| Changed surface | Minimum | Add when applicable | Not implied |
|---|---|---|---|
| Local implementation | compile + targeted tests + diff review | clang-tidy/negative cases | Full benchmark for every edit |
| Ownership/memory | compile + targeted boundary tests | ASan+UBSan, fuzz/replay, experimental lifetime checks | UB-free certification |
| Concurrency/tasks | shutdown and concurrency tests | TSan **separate build**, stress/queue depth | Lock-free by default |
| Templates/concepts | concrete positive/negative compile | compiler + stdlib matrix | `requires` alone suffices |
| Public API/ABI/FFI | consumer tests, compat review | `abidiff`, multi-platform builds | FFI safe because types match |
| Low-latency | release benchmark baseline and after | p50/p95/p99, profile, allocation/CPU/queue metrics | Optimization inferred from assembly |
| Deep migration | ADR, characterized behavior, staged builds | ABI snapshots, compatibility adapters, rollback | Mandatory modularization |

Unavailable tools and skipped tests are UNVERIFIED. CTest with zero tests is not a PASS. An unverified P0 risk is not automatically a confirmed defect; seek evidence.

## Effective evidence record

For each changed contract/scenario, populate the matrix in [report.md](report.md):

- Base/candidate and artifact identity, exact configuration/feature/target/runtime, command/job, evidence path/date and freshness against the candidate.
- Separate built, tested, integration and release validation. A Debug build, cross-build or sanitizer run does not exercise a different Release artifact or another platform.
- Test counts: selected, executed, passed, failed, skipped and disabled; distinguish a registered test from its internal subcases. Report unavailable counts explicitly. A disabled test proves none of its behavior.
- PASS requires relevant current evidence in the reported scope. Empty/all-skipped selection, stale logs, missing consumer or unexecuted feature-off paths leave the affected contract UNVERIFIED; preserve an actual runner failure as FAIL. Partial execution describes its covered and uncovered scope.
- For a bug, link failing baseline and passing candidate checks where feasible; explain unavailable/unsafe reproduction. A newly passing test alone proves only the candidate. A diff inventory alone does not prove path coverage.
- N/A needs a reason tied to this change. Trivial local edits may use compact prose with these applicable facts; do not add infrastructure to fill inapplicable fields.
