# Contributing — validate new rules rather than accumulate opinions

This skill is C++-only in its distributed tools. Contributions to CLI scanners,
fixture runners and verifiers **must be implemented in C++**, with C++23 as the
tool baseline and CMake 3.20+ as build infrastructure. Explicit lower-standard
fixtures remain valid: C++17 safety examples and C++20 concepts test compatibility
of analyzed code, not the tool's own minimum. CMake/CTest and compiler tools are
allowed infrastructure. Do not reintroduce Rust, Python or custom Bash audit/test
scripts. Check the selected compiler/STL before using a new C++23 facility; a
standard mode does not imply full library conformance. See
[language-standard-upgrades.md](references/language-standard-upgrades.md).

## New guardrail review
1. Reproduce a real problem or documented review risk with a minimal C++ source/test and note compiler/platform/standard.
2. Include a counterexample of similar **valid** code; avoid bans such as 'raw pointer always bad', 'never early return', 'one class per file'.
3. Decide detection method: Clang diagnostic/AST, sanitizer, concrete compilation, runtime benchmark, or lightweight heuristic. Label confidence accordingly.
4. Add positive/negative fixtures, strengthen the C++ `selftest.cpp` / `fixture_runner.cpp`, and document expected diagnostics. Negative compilation must fail for the right reason.
5. Record scope, false positives, P0–P3 risk, migration/rollback and supported toolchains.
6. Avoid automatic `--fix`, suppressions, baseline rewriting and changes to user source.
7. Obtain skeptical independent review and revisit after false positives, compiler updates or production evidence.

## Required checks

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Optionally repeat with Clang and ASan+UBSan, compiler warnings and negative fixtures. CI exercises GCC and Clang. Do not weaken tests or suppress failures to pass.

## Tool safety

Native C++ CLI launches external programs via argv-based subprocesses, not assembled shell commands. Avoid evaluating code from comments, tool output or repository configuration. `audit` must not mutate target source. CMake/CTest build invocations may execute user project code; document this. Respect output path permissions and do not leak source content or credentials in reports.
