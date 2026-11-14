# Testing and verification protocol

**Core:** record exact command, compiler/configuration, exit code and important failure output. Distinguish PASS, FAIL, UNVERIFIED, NOT-APPLICABLE. Build passing ≠ tests passing ≠ safety proven.

## Recommended test spectrum
- Positive behavior, boundary/empty/error paths, exception safety, integer limits, resources/shutdown.
- Concrete template instantiation for supported types and intentionally rejected types; negative tests compile-fail for an expected diagnostic.
- ASan+UBSan instrumented suite for memory/UB changes; separate TSan instrumented suite for concurrency on supported toolchains; CI stress/fuzzing when sensible.
- Public SDK ABI/API comparison and downstream consumer builds before breaking changes.
- Relevant GCC/Clang/MSVC matrix instead of assuming a successful local compile covers everyone.
- Performance: before/after identical environment + representative workload and repetitions; reject noise-driven conclusions.

## Anti-gaming rules
Do not delete, relax or replace useful tests just to pass the new code; any test change is reviewed as a separate semantic change. Do not disable warnings, lint rules, sanitizers, CI jobs or coverage thresholds to hide a failure. Tests that cannot run are UNVERIFIED, not passed.

## Evidence grading
- **Confirmed:** repeated runtime reproducer or actual compiler/test finding tied to a concrete contract.
- **Likely:** high-quality static analysis finding, reasoned counterexample, unverified runtime.
- **Suspected:** AST/text heuristic needing manual verification.

Use `templates/report.md` for the final summary and a separate reviewer check. Never claim universal memory safety or race freedom in C++ from a finite test suite.
