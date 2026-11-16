# AUDIT mode — read-only, evidence-first

**Goal:** create a prioritized, verifiable improvement backlog; do not modify production source, test assertions, suppressions, policies, CMake or generated artifacts. A requested audit may create a report in an explicitly authorized output path.

1. Determine supported C++ standards/compilers, modules, ownership boundaries, critical execution paths, test maturity and codegen/generated areas.
2. Inspect public API and ABI constraints; label unverified assumptions.
3. Inspect architecture and targeted change history before tools; do not assume every inheritance chain is bad.
4. Run relevant static/build/test tools when permitted, collect outputs (compiler and sanitizer evidence), then add manual risk-focused review.
   - With `cpp-rails-audit`, start from the **Summary by rule** and **Review first** hotspots; review production P2 before P3 and test-scope hints. On large trees use `--exclude-tests` or `triage`, and never paste hundreds of raw hints into the report.
   - A hint is an **entry point, not the finding**: read the surrounding acquire → use → hand-off → release sequence against the API contract. In real-code trials the actual defect candidate sat next to a correct `memcpy`, not on it.
5. For every finding: `id, rule, severity, confidence, file:line, evidence, impact, counterexample, fix, validation`.
6. Group findings by P0–P3; rank confirmed reproducible defects before hypotheses. Avoid mass-generated low-priority noise.
7. Propose staged remediation with effort/risk and acceptance tests; never claim that a heuristic proves UB.

**Specific review:** lifetime and invalidation, `std::string_view`/`span`, exception safety, unchecked arithmetic, iterator categories, threads+destructors, FFI, test changes, concrete template instantiations, codegen surprises, unused abstractions, compile-time dependency graph.

When safety is in scope, follow `references/safety-intelligence.md` and attach
`templates/safety-evidence.md` to the report. Review consequential call effects
using `references/function-contracts.md`; a missing body or stale binary summary
leaves dependent properties UNVERIFIED. Preserve tool results separately from
property conclusions and justify uncovered/N/A scope.

**Deliver:** summary, scope, supported platforms, files sampled, test/tool coverage, evidence-backed top findings, baseline status, and risk-ordered proposal. Use `templates/report.md`.
