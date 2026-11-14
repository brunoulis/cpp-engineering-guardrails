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
