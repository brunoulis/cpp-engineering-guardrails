# C++ auditor roadmap implementation plan

> **For agentic workers:** Use superpowers:executing-plans, test-first changes and independent final review. The user selected all four phases of the supplied audit on 2026-11-16.

**Goal:** Deliver the four audit phases as usable, bounded native C++ capabilities with reproducible evidence and explicit coverage limits.

**Architecture:** Preserve the lexical CLI and its contracts. Add independent quality, Clang AST/safety, CMake architecture/policy, baseline/reporting and scalable lint modules. Clang is an optional executable backend; complete AST JSON gives real compiler types without a mandatory LLVM development-library dependency.

**Tech stack:** C++23, CMake 3.20+, existing native subprocess runner, optional Clang/clang-tidy, CTest; no Python/Rust tooling.

**Spec:** User-supplied “Auditoría de C++ Engineering Guardrails frente a Flutter”, all four phases approved. Existing C++23/version documentation corrections remain intact.

## Constraints and review focus

- Findings identify analyzer and evidence level; lexical matches remain suspicions.
- Six independent quality dimensions have evidence and units, without a global grade or automatic architecture prescription.
- Baseline differences must not call unscanned, capped or unsupported findings resolved.
- Missing backend/database, exhausted budgets, dynamic CMake and unknown effects remain UNVERIFIED.
- Target analysis never configures an untrusted project. Project rules load only through an explicit option and reject malformed policies.
- Lint processes deterministic batches without a 30-unit project cap. Cache keys include compilation configuration and dependency state; unknown dependency closure cannot be a verified cache hit.
- Preserve old baseline loading, existing output fields, default lightweight behavior, lower-standard fixtures and existing tests.
- Work in place on feat/cpp-auditor-roadmap after the worktree request was cancelled. No publishing, pushes or installation changes.

## Task 1 — Quality evidence (agent: quality)

- [x] Write/run failing `quality_tests.cpp` for duplicates, complexity, strings/comments, valid wrappers, empty input and test proximity.
- [x] Implement standalone `quality.hpp` / `quality.cpp`: `QualityReport quality_score(root, files)`, `quality_json`, `quality_markdown`.
- [x] Integrate `--score`, normalized production/test code lines and independent dimensions into all report formats; run tests.

## Task 2 — Semantic and bounded safety evidence (agent: semantic_safety)

- [x] Write/run failing `semantic_tests.cpp` for real type-sensitive positive/negative cases, compile flags, missing tools/database and exhausted budget.
- [x] Implement `analyze_semantic(const Options&, const fs::path&, const vector<fs::path>&, Report&)` in `semantic.cpp` with complete file-backed AST capture, safe argv compilation database replay and explicit coverage.
- [x] Expose `--semantic`, `--clang`, `--compilation-database`, `--analysis-budget` and `safety map|contracts|boundaries|regressions`; record intraprocedural effects/unknown boundaries without whole-program safety claims.
- [x] Run optional backend tests with an actual Clang; report unsupported scope.

## Task 3 — Architecture and explicit rules (agent: architecture)

- [x] Write/run failing `architecture_tests.cpp` for codemodel, literal cycles, dynamic input, valid/invalid policies and exceptions.
- [x] Implement `analyze_architecture(...)` and `apply_project_rules(...)` in `architecture.cpp`; File API evidence and heuristic fallback are distinguishable.
- [x] Integrate `topology --targets` / `--rules PATH`, structured target edges and include/API rule evidence; run tests.

## Task 4 — Evolution, review plans and scalable lint (root)

- [x] Write/run failing `roadmap_tests.cpp`: new/known/resolved/unobserved baseline IDs, partial scope, malformed CLI, normalized rates and structured minimal/structural/leave-as-is review alternatives.
- [x] Move baseline/reporting/process responsibilities out of the large core where interfaces are stable; preserve behavior under existing tests.
- [x] Implement v2 baseline metadata with v1 reader; explicit high-confidence regression policy; structured `plan` output.
- [x] Replace lint cap with `--lint-batch-size`, `--lint-max-units`, `--lint-budget-seconds`, optional conservative cache and analyzed/total/batch/cache coverage.
- [x] Run targeted and old regressions.

## Task 5 — Precision corpus, self-checks, portability and delivery (root)

- [x] Add native labeled corpus runner with per-rule TP/FP/FN, precision/recall, pinned real-project provenance and independent valid counterexamples. Keep compiler fixtures separate from detection tests.
- [x] Add version/doc consistency validation and version source of truth; synchronize v1.4.0 user documentation and actual capability limitations.
- [x] Extend existing GCC/Clang/macOS/MSVC CI with real semantic backend and optional analysis job; no claim of remote CI execution.
- [x] Build fresh MSVC tree, run entire CTest suite and real CLI JSON/Markdown/SARIF scenarios, self-audit and precision corpus. Record commands/results in `docs/audits/2026-11-16-roadmap.md`.
- [x] Request independent final review, reproduce/fix material defects and rerun affected checks. Deliver local files and explicit outstanding limits.
