---
name: cpp-engineering-guardrails
description: >-
  Implement, audit and refactor C++ libraries, services and CMake projects. Covers
  ownership, lifetimes, templates, concurrency, API/ABI, FFI and measured performance.
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

Use the target's actual compiler, standard and callers. Preserve ownership, borrow validity and observable state on failure. Build and test the affected paths. Read related source together and expand check output on failure.

Consult the relevant section for [lifetime and iterator invalidation](references/memory-lifetime-ub.md) or [ABI and FFI](references/api-abi-ffi.md). Compiler audits include authored .inc/.inl files; use `--include-generated` for intentional generated coverage. See [MSVC context](tools/cpp-rails/MSVC.md) for evaluated targets. For audits, migrations, performance or wider refactors, select the workflow in the [engineering guide](engineering-guide.md). Audit read-only unless edits are requested. Report behavior, actual checks and material limits.
