# C++ Engineering Guardrails v1.5.0

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
![C++23](https://img.shields.io/badge/tools-C%2B%2B23-blue.svg)
![Agents](https://img.shields.io/badge/agents-Claude%20Code%20%7C%20Codex%20%7C%20Hermes-555.svg)

An installable **agent skill** for Claude Code, Codex and Hermes that guides coding agents to **write, audit, refactor and migrate C++** with evidence instead of opinions: real builds and tests, explicit ownership and lifetimes, measured performance and honest verification status.

It distills lessons from the sibling skills `rust-engineering-guardrails` and `flutter-engineering-guardrails`, adapted to what C++ actually guarantees (and doesn't).

> Español: [README.es.md](README.es.md)

**C++-only tooling.** The bundled helpers (`cpp-rails-audit`, `cpp-rails-check` and
their runners) build in **C++23** mode. Explicit C++17 safety and C++20 concept
fixtures exercise older analyzed code. No Rust, Python, Cargo, `pip` or shell
audit scripts are shipped. CMake/CTest are build/test infrastructure;
GCC/Clang/MSVC, sanitizers, `clang-tidy` and `abidiff` are optional external tools
that are discovered, never assumed.

## What's inside

| Path | Purpose |
|---|---|
| `SKILL.md` | Lightweight entry point: 14 rails (R0–R13), mode selection, progressive reference routing, severity model and handoff contract |
| `modes/` | `AUDIT`, `IMPLEMENT`, `REFACTOR`, `DEEP-REFACTOR` — staged, verifiable, reversible workflows |
| `references/` | Memory/lifetime/UB, types and contracts, state and resource lifecycle, native UI and event loops, cancellation/backpressure, concurrency, templates/concepts, security, API/ABI/FFI, determinism, testing, architecture, data layout, compiler optimization, reviewer protocol |
| `profiles/` | `general`, `low-latency`, `library-sdk`, `legacy-code`, `engine`, `service`, `native-ui`, `ffi`, `prototype` — risk hypotheses, not rigid styles |
| `policies/` | P0–P3 severities, change-dependent quality gates, expiring suppressions |
| `tools/cpp-rails/` | C++23 library + CLIs: heuristic scanner, baseline, triage, include topology, JSON/Markdown reports, CMake/CTest runner, `clang-tidy`, sanitizers, compiler matrix, template instantiation checks, CSV/TSV benchmark comparison |
| `tests/` | C++17 safety examples and positive/negative C++20 template fixtures driven by CTest |
| `templates/`, `examples/` | Report, ADR, refactor plan, finding, reviewer checklist, project profile, FFI contract and ready-to-use prompts |

## The four modes

| Mode | Use it for | What it requires |
|---|---|---|
| **Audit** | Debt, defects, quality, security and performance review | Read-only, P0–P3 findings with evidence and confidence, remediation plan |
| **Implement** | New feature or bug fix | Respect existing contracts, smallest coherent patch, tests |
| **Refactor** | Improve existing code without changing behavior | Options including *leave-as-is*, characterization tests, regression checks, rollback |
| **Deep-refactor** | Architecture, boundaries, ownership or ABI migration | ADR, consumer map, staged migration, compatibility plan, independent reviewer |

Combine a mode with a profile, e.g. *"audit with the `native-ui` profile"*. Example prompts live in [`examples/AGENT_PROMPTS.md`](examples/AGENT_PROMPTS.md).

## Safety Intelligence (skill v1.4.0)

Use the [safety workflow](references/safety-intelligence.md) to map resources and
boundaries, review [function effects](references/function-contracts.md), inspect
[initialization/invalidation](references/initialization-invalidation.md) and plan
[incremental adoption](references/safety-adoption.md). Templates record current
binary/contract identities and evidence for lifetime, invalidation, initialization,
bounds and thread safety, including unknowns, exceptions and lost coverage.

All 14 rails and four modes remain. v1.4 adds optional compiler-backed
`safety map|contracts|boundaries|regressions`, using Clang ASTs and actual compile
commands for bounded local lifetime/release/invalidation evidence. Opaque effects
and unsupported scope remain unknown. Passing tools is not a whole-project
memory-safety guarantee. See [the CLI guide](tools/cpp-rails/README.md).

## Auditor v1.5

v1.5 adds [real MSVC context handling](tools/cpp-rails/MSVC.md), explicit MSBuild
evaluation for existing Visual Studio projects, generated-code exclusions with
scope accounting, and `.inc`/`.inl` analysis through their including translation
units. `--include-generated` restores generated source; repeatable `--exclude`
patterns narrow scope. Missing compiler/fragment coverage remains UNVERIFIED.

- `audit --score`: six independent quality indicators with evidence and rates.
- `audit --semantic`: optional real Clang AST; no mandatory LLVM library dependency.
- `topology --targets`: existing CMake File API target graph, with a labeled heuristic fallback.
- `--rules PATH`: explicit dependency/API constraints and reviewed exceptions.
- Baselines: new/known/disappeared/unobserved, rule deltas and code-line rates.
- `plan`: structured contract, cause, minimal/structural/leave-as-is alternatives and verification.
- Lint: deterministic batches, configurable budgets and opt-in dependency-aware caching.
- Native labeled precision corpus, version consistency checks and compiler/backend CI.

[`VERSION`](VERSION) is the build version source; CTest checks documentation and
SARIF metadata for drift. See [quality evidence](docs/quality.md),
[target/rule boundaries](tools/cpp-rails/ARCHITECTURE.md) and
[semantic coverage](tools/cpp-rails/SEMANTIC_BACKEND.md).

## Install the skill

See [INSTALL.md](INSTALL.md) for agent-specific paths, portable folder/skills CLI installation, language-scoped activation, updates and verification. Installing the instructions is separate from building the optional C++ tools.

Keep the **whole folder** and place it in the skills directory your agent reads:

| Agent | Project scope | User scope |
|---|---|---|
| Claude Code | `<project>/.claude/skills/cpp-engineering-guardrails/` | `~/.claude/skills/cpp-engineering-guardrails/` |
| Codex | `<project>/.agents/skills/cpp-engineering-guardrails/` | `~/.agents/skills/cpp-engineering-guardrails/` |
| Hermes Agent | `<project>/.hermes/skills/cpp-engineering-guardrails/` | `~/.hermes/skills/cpp-engineering-guardrails/` (or a configured skills dir) |

For teams, keep one versioned copy and symlink it where the agent supports symlinks. The modes and references work as plain instructions even if the CLI is never built.

## Build the optional CLI (C++23)

Requirements: CMake 3.20+ and a compiler/STL able to build the tools in C++23 mode.
The default tool build requires C++23, with no silent fallback. This does not
upgrade the projects being audited or imply full C++23 conformance. Local
validation used MSVC 19.51.36260.0 and CMake 4.3, whose generated C++23 flag is
`/std:c++latest`. See [standard/toolchain upgrade notes](references/language-standard-upgrades.md)
for the C++26 development status, platform limits and lower-standard fixtures.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Binaries land in `build/tools/cpp-rails/` (multi-config generators such as Visual Studio add a `Release/` subfolder and need `ctest -C Release`). No prebuilt binaries are shipped, so nothing is tied to one OS/architecture.

Historical toolchain claims (GCC/Clang on Linux, Apple Clang on macOS, MSVC 19.50 with Ninja/Visual Studio) lack bundled reproducible logs and remain unverified here. Run the supplied CTest suite on each supported toolchain; a successful run on one platform does not verify the others.

## Use it on a C++ project

```bash
SKILL=/path/to/cpp-engineering-guardrails
BIN="$SKILL/build/tools/cpp-rails"

# Read-only analysis
"$BIN/cpp-rails-audit" audit    --root . --profile general
"$BIN/cpp-rails-audit" triage   --root . --format json --output review.json
"$BIN/cpp-rails-audit" audit    --root . --exclude-tests --format sarif --output cpp-rails.sarif
"$BIN/cpp-rails-audit" topology --root . --include-dir include
"$BIN/cpp-rails-audit" baseline --root . --output .cpp-rails/baseline.txt
"$BIN/cpp-rails-audit" audit    --root . --baseline .cpp-rails/baseline.txt --changed
"$BIN/cpp-rails-audit" templates --check --root . --source tests/concrete_instances.cpp --compiler clang++
"$BIN/cpp-rails-audit" perf compare --root . --results benchmarks.csv

# Verification gates (only on an already configured CMake build)
"$BIN/cpp-rails-check" --root . --build-dir build
# Opt-in: configure if the build tree is missing
"$BIN/cpp-rails-check" --root . --build-dir build --configure
# Multi-config builds (Visual Studio, Xcode): choose the configuration to build and test
"$BIN/cpp-rails-check" --root . --build-dir build --config Release
# GUI code: Qt/Win32/ImGui rules
"$BIN/cpp-rails-audit" audit --root . --profile native-ui
# Each sanitizer runs in its own isolated build
"$BIN/cpp-rails-check" --root . --build-dir build --sanitizer asan-ubsan --compiler clang++
"$BIN/cpp-rails-check" --root . --build-dir build --sanitizer tsan --compiler clang++
```

### Exit codes and statuses

| Exit | Meaning |
|---|---|
| `0` | All required gates PASS |
| `1` | At least one gate FAILED |
| `2` | A required gate is UNVERIFIED (missing tool, skipped, not run) — or any UNVERIFIED gate with `--strict` — or a CLI error |

Gate statuses are `PASS`, `FAIL`, `UNVERIFIED` and `N/A`. A skipped or missing check is **never** reported as PASS.

### Safety model

- `audit` never edits source. `baseline` writes only the requested inventory file.
- `check` **runs the target project's build and tests**, which executes project code — use it only on code you trust.
- Target-repository policies are never auto-loaded; only an explicitly passed `--profile-file` changes gate defaults.
- Scanner findings are **suspicions** (P2/P3). Text matching never proves UB, memory safety, data races or ABI compatibility.

## Lessons carried over from Rust and Flutter

| Lesson | How it applies to C++ |
|---|---|
| Ownership, newtypes, typestate, generational IDs | RAII, lifetime contracts, validated types and safe handles where they pay off |
| Sans-IO, cancellation, backpressure, bounded queues | Worker design, orderly shutdown, bounded queues and saturation tests |
| SoA/AoS, allocations, determinism | Hot-path profiling, numerical precision, replay/parity tests |
| State owner, lifecycle, stale async results | Single source of truth, callback cancellation, clean teardown |
| Adaptive architecture, no gratuitous layers | Evidence before splitting classes, modules, targets, PImpl or plugins |
| Intelligent / deep refactoring | Minimal fix vs structural change vs leave-as-is, checkpoints, reversibility |
| Skeptical reviewer + CI + fixtures | Positive/negative tests, no false green, evidence-backed findings |
| Progressive context, hierarchical memory | On-demand references and verified, scoped project notes |

Flutter-specific concepts (widgets, Provider) are not transplanted into C++ libraries without UI, and C++ is never assumed to give Rust's borrow-checker guarantees.

## Limitations

The default scanner and quality indicators are lexical hints. Optional Clang
AST analysis covers selected local patterns using actual types; it does not
implement full interprocedural data flow, prove race/UB freedom, research compiler
regressions online or support every build configuration. Unknown dependencies,
opaque effects, unsupported flags and exhausted analysis budgets remain
UNVERIFIED. The median/MAD benchmark check is a prioritization heuristic, not
statistical inference.

### Profiles in the CLI

`legacy-code`, `prototype`, `library-sdk` and `low-latency` change CLI gate defaults; `native-ui` enables the GUI scanner rules below. `profiles/*.toml` are read only when passed with `--profile-file`, and their `[context]` keys guide the agent, not the CLI.

### Native UI (`--profile native-ui`)

Adds lexical hints for Qt/Win32/ImGui code — `CPP-UI-CONNECT-NO-CONTEXT` (3-argument Qt `connect` to a lambda), `CPP-UI-NESTED-EVENT-LOOP` (`processEvents`), `CPP-UI-WINDOW-USERDATA` (raw pointer in `GWLP_USERDATA`) — and downgrades parent-owned `new T(..., this|parent)` (Qt object tree) from P2 `CPP-RAW-ALLOC` to P3 `CPP-UI-PARENT-OWNED-NEW`. Thread affinity, stale async results, races and DPI scaling still need the agent review described in [`references/native-ui-event-loop.md`](references/native-ui-event-loop.md).

### Reading results on large codebases

Findings are ordered production P2 → production P3 → test. The Markdown report starts with a **Summary by rule** and a **Review first** list of production hotspots; JSON carries the same under `summary`, and every finding has a `scope` (`production` / `test`).

- Test, fixture and mock sources (`tests/`, `testing/`, `*_unittests.cc`, `FooTest.cpp`, …) are reported one level lower (P2 → P3). Use `--exclude-tests` to skip them entirely.
- Idioms are reported as P3 instead of P2: casts to byte/void pointers or integer handles (`LPARAM`, `LONG_PTR`, `intptr_t`), `new` adopted on the spot by an owning wrapper (`shared_ptr<T>(new T)`, `p.reset(new T)`), and function-local `static T* x = new T` singletons. `CPP-DETACHED-THREAD` only fires on objects that are actually threads.
- The 20-hints-per-rule-per-file limit is reported (`findings_capped`), never silent.
- `--format sarif` emits SARIF 2.1.0 for GitHub code scanning or the VS Code SARIF viewer (P2 → `warning`, P3 → `note`, stable `partialFingerprints`).
- `topology` resolves quoted includes relative to the file, `--root`, `--include-dir` (repeatable), `-I` flags in `compile_commands.json` and ancestor include roots (`"flutter/shell/x.h"` when `--root` is `shell/`), and reports how many includes it could not resolve.

Historical Flutter-engine timing and hint-count comparisons are unverified: the package does not contain the pinned corpus, before/after versions, environment and raw results needed to reproduce them. Fewer hints alone do not establish higher precision.

### Changelog (unreleased)

- Real-code tuning: test scope, context-aware `detach`/cast/`new` rules, summary + hotspots, cap reporting, SARIF output, include-root resolution for `topology`, regex prechecks (historical speedup unverified).
- `references/native-ui-event-loop.md`: Win32 resource contracts (HGLOBAL vs locked pointer, GDI, COM, kernel handles); AUDIT mode: hints are entry points, review hotspots first.
- Windows: subprocesses now use `CreateProcess` with a pipe, so output is captured (`--changed` used to scan 0 files and report success) and arguments with spaces/quotes are quoted correctly.
- `cpp-rails-check` passes the same configuration to `cmake --build --config` and `ctest -C` (new `--config NAME`; default `CMAKE_BUILD_TYPE`, else `Debug`), fixing false CTest failures with Visual Studio / multi-config generators.
- New rules: `CPP-ASYNC-CAPTURES-THIS` (all profiles), the `CPP-UI-*` rules above; `CPP-UNBOUNDED-STRING` now covers `wcscpy`, `wcscat`, `sprintf`, `lstrcpy`, `gets` and friends; `CPP-SHELL-CALL` no longer matches `filesystem(`.
- New reference: `references/native-ui-event-loop.md`.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). New rules need a reproducible problem, a valid counterexample, positive/negative C++ fixtures and documented false positives. Tooling contributions must stay in C++.

## License

Released under the [MIT License](LICENSE). Copyright (c) 2026 Contributors.

You may use, copy, modify, merge, publish, distribute, sublicense and/or sell copies of this skill and its tools, provided the copyright notice and permission notice are included in all copies or substantial portions. The software is provided "as is", without warranty of any kind.
