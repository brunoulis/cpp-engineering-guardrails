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

For a fix, search once for the symbol, its caller and its test. Read the matching regions together. Preserve ownership, borrow validity and observable failure behavior. When extending a template, keep its old minimum type requirements: copying may be valid even when moving is explicitly deleted.

Use the target compiler and standard. Exercise the reported failure and the existing caller most exposed by the change, then run the nearest regression target. Capture verbose output to a file; surface the exit status, test count or first diagnostic. Once these checks pass on the final patch, report the result. Rebuild or widen the test selection only for a changed input, a required project check or a concrete uncovered risk.

Consult [lifetimes](references/memory-lifetime-ub.md) or [ABI/FFI](references/api-abi-ffi.md) for an unresolved boundary. Use the [engineering guide](engineering-guide.md) for audits, migrations, performance and wider refactors. Audit read-only unless edits are requested. Compiler audits cover authored .inc/.inl files; see `--include-generated` and [MSVC context](tools/cpp-rails/MSVC.md) for additional scope.
