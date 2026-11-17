---
name: cpp-engineering-guardrails
description: >-
  Evidence-based, risk-adaptive C++ engineering guardrails for implementation, audit,
  intelligent refactoring, deep migration, safe ownership/lifetimes, templates/concepts,
  concurrency, FFI/API/ABI, compiler optimization and measurable performance. Activates
  for native libraries, game/telemetry engines, backend C++, CMake, legacy systems,
  code review, unsafe code and C++↔Rust/Flutter interfaces. Four modes: audit,
  implement, refactor and deep-refactor. Native C++23 tools; no Python/Rust runtime.
license: MIT
compatibility: A compiler supporting the tools in C++23 mode and CMake 3.20+ to build the optional CLI; target-project standards and external tools are discovered, never assumed.
metadata:
  version: "1.5.0"
  family: "engineering-guardrails"
  hermes:
    tags:
      - cpp
      - guardrails
      - audit
      - refactoring
      - performance
      - memory-safety
---

# C++ engineering guardrails

Check the affected callers, build target, C++ standard and compiler before choosing an API. For a local change, preserve who owns each resource, how long borrowed data remains valid, and which state and errors the caller observes on failure. Exercise the changed paths with the project's build and tests.

- For ownership or iterator invalidation decisions, consult [memory and lifetime](references/memory-lifetime-ub.md). For public layout, linkage or language boundaries, consult [API, ABI and FFI](references/api-abi-ffi.md).
- For compiler-backed audits, include authored .inc and .inl fragments in the source inventory. Use `--include-generated` for intentional generated coverage. For evaluated Visual Studio targets see [MSVC context](tools/cpp-rails/MSVC.md).
- For audit, refactor, migration, dependency or toolchain changes, performance work and cross-target changes, select the matching workflow in the [engineering guide](engineering-guide.md). Audit the source read-only unless edits are requested.

For an implementation, state the observable change, actual checks and material limits of the tested configuration. For an audit, lead with the finding and its consequence. Answer questions at the requested level of detail.
