# IMPLEMENT mode — smallest safe change

1. Read caller contract and usage tests. Determine C++ standard/toolchain; avoid unsupported library facilities (`std::expected` needs C++23, not C++20).
2. Define input/output, ownership, thread-safety, error behavior, exception safety, ABI/wire-format constraints.
3. Choose simple standard-library facilities before custom abstractions; do not blindly overuse inheritance, templates, macros or async.
4. Add or update tests for intended behavior; never replace a failing assertion with a weaker one to create green CI.
5. Implement a focused patch. Use const-correctness and narrow lifetimes; write comments for surprising invariants rather than narrating obvious code.
6. Build; run relevant tests. Add ASan/UBSan if changing memory code; TSan if changing concurrency; portable build matrix if public API/generic code; benchmark when claiming performance.
7. Independent review: correct semantics, error paths, no unapproved API/ABI changes, no accidental test degradation; report gaps.

Deliver [the existing report](../templates/report.md): identify base/candidate and exclusions, map each changed file to its reason, behavior, consumer and actual evidence, and record new state owners and closure. Compare the first solution with the candidate and resolve review findings using [reviewer-protocol.md](../references/reviewer-protocol.md). Use [quality-gate-matrix.md](../templates/quality-gate-matrix.md) for effective configurations, test counts and evidence freshness; a diff alone does not establish path coverage. A trivial change may cover applicable facts compactly with justified N/A.
