# DEEP REFACTOR mode — migration with checkpoints

**Entry condition:** new architecture, major dependency inversion, modularization, broad ownership/FFI changes, ABI breaks or pervasive build restructuring. Explicitly define what is in/out of scope. Document developer authorization for deliberate breaking changes.

1. **System map:** build dependency graph, public symbols, consumers, feature flags, data models, ownership across threads/FFI, latency budgets and platform support.
2. **Baseline:** run representative tests, characterize behavior and capture optional ABI dumps, compiler matrix, perf distributions and memory costs.
3. **Alternatives:** compare incremental cleanup vs a migration. Justify boundaries by cohesion, coupling, independent build/test lifecycles and actual deployment needs.
4. **Migration:** introduce compatibility seams/adapters if needed; split into compilation-safe, reversible steps. Do not attempt a big-bang rewrite without a rollback path.
5. **Per-step gates:** build and targeted tests plus API/ABI/concurrency/sanitizer gates where applicable. Keep a change log of contracts intentionally changed.
6. **Cutover:** run full integration, consumers and production-like benchmarks; remove temporary adapters only after proving migration.
7. **Independent review:** ensure no hidden contract changes, no races, no changed tests that mask failures, and documentation remains accurate.

Not a license for wholesale class splitting, newer-standard upgrades, `module` adoption or micro-optimizations. Each must have a reason.

For safety-driven migrations use `references/safety-adoption.md`: migrate one
unsafe boundary per checkpoint, keep revision-bound function contracts and compare
property evidence with `templates/safety-evidence.md`. New unknown calls, stale
summaries, lost coverage and exceptions are regressions to review even when no
new sanitizer diagnostic appears. LLVM, Fil-C and scpptool remain optional;
neither changing toolchains nor mimicking Rust supplies a universal borrow checker.
