# cpp-rails · Native C++17 audit / verification CLI

This is **real C++17 source code**, not a proposal or shell wrapper. Build it with CMake. It has **no external C++ library dependencies**. It invokes CMake/CTest and optionally compiler/lint/sanitizer/ABI tools already installed on the machine; no Rust/Python source or runtime is used.

## Build / test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

From `tools/cpp-rails/` these commands create `build/cpp-rails-audit`, `build/cpp-rails-check`; from the **skill root** use `build/tools/cpp-rails/`. Multi-config generators may put executables in `Release/`.

## CLI examples

```bash
BIN=/path/to/skill/build/tools/cpp-rails
"$BIN/cpp-rails-audit" audit --root . --profile general --format json --output report.json
"$BIN/cpp-rails-audit" triage --root . --baseline .cpp-rails/baseline.txt --changed
"$BIN/cpp-rails-audit" topology --root .
"$BIN/cpp-rails-audit" baseline --root . --output .cpp-rails/baseline.txt
"$BIN/cpp-rails-audit" templates --check --root . --source tests/positive_instantiation.cpp --compiler clang++
"$BIN/cpp-rails-audit" templates --check --root . --source tests/negative_instantiation.cpp --compiler clang++ --expect-error insert
"$BIN/cpp-rails-audit" perf analyze --root . --source src/hotloop.cpp --compiler clang++
"$BIN/cpp-rails-audit" perf compare --root . --results results.csv --min-samples 5 --max-regression 5
"$BIN/cpp-rails-check" --root . --build-dir build
"$BIN/cpp-rails-check" --root . --build-dir build --configure
"$BIN/cpp-rails-check" --root . --build-dir build --lint
"$BIN/cpp-rails-check" --root . --build-dir build --sanitizer asan-ubsan --compiler clang++
"$BIN/cpp-rails-check" --root . --build-dir build --sanitizer tsan --compiler clang++
"$BIN/cpp-rails-check" --root . --build-dir build --compiler-matrix clang++,g++
"$BIN/cpp-rails-check" --root . --build-dir build --profile library-sdk --abi-old old/lib.so --abi-new new/lib.so
"$BIN/cpp-rails-check" --root . --build-dir build --profile low-latency --results samples.csv
```

The `--source` template check is a **standalone compiler invocation**, without the project's compile database. Use actual build targets / consumer tests if source depends on project flags, includes or link-time behavior. `--expect-error` confirms a negative case failed with the requested diagnostic substring, not just any compiler failure.

## Implemented capabilities and limits

| Capability | Implementation status |
|---|---|
| Read-only lexical heuristic scanner with comment/string masking | Implemented, P2/P3 **suspicions only** |
| Stable finding ID, baseline comparison, reviewed suppressions with owner/path/expiry | Implemented |
| Triage and local-header include topology | Implemented; resolves via file dir, `--root`, `--include-dir`, `compile_commands.json` `-I` flags and ancestor include roots; reports unresolved includes; not a semantic call graph or full AST |
| JSON / Markdown / SARIF 2.1.0 reports; statuses PASS, FAIL, UNVERIFIED, N/A | Implemented; summary by rule, production hotspots, `scope` per finding, cap reporting |
| Test-scope classification (`--exclude-tests`) | Implemented by path convention (`tests/`, `testing/`, `*_unittests.cc`, `FooTest.cpp`); test hints are one level lower |
| Context-aware idioms | Byte/handle casts, wrapper-adopted `new`, leaky singletons → P3; `detach` only on thread objects |
| CMake build & CTest (`--configure` opt-in; zero effective tests is UNVERIFIED) | Implemented for CMake; `--config` (default `CMAKE_BUILD_TYPE`, else `Debug`) is passed to both `cmake --build` and `ctest -C`, so multi-config generators work |
| Native UI hints (`--profile native-ui`) | `CPP-UI-CONNECT-NO-CONTEXT`, `CPP-UI-NESTED-EVENT-LOOP`, `CPP-UI-WINDOW-USERDATA`; parent-owned `new` downgraded to P3 |
| Subprocesses | argv-based on all platforms: `fork`/`execvp` on POSIX, `CreateProcess` with CRT-compatible quoting on Windows; stdout+stderr captured (128 KiB cap) |
| clang-tidy from `compile_commands.json` (`--lint`, no auto-fix) | Implemented for at most 30 translation units per call; wider scope marked UNVERIFIED |
| ASan+UBSan or separate TSan instrumented CMake builds | Implemented, tool/platform dependent |
| Positive & negative real template compilation | Implemented for concrete source files |
| Clang/GCC opt remarks (standalone source) | Implemented; assembly scratch file in OS temp |
| CSV/TSV repeated benchmark comparison (median + MAD heuristic) | Implemented; not statistical proof |
| GCC/Clang compiler CMake matrix | Implemented opt-in; MSVC environments need native-supported configuration |
| Optional ABI check using `abidiff` | Implemented where installed |
| Lifetime warnings for a standalone source | Implemented as opt-in best-effort; flags vary and do not prove lifetime safety |
| Semantic Clang AST data-flow, project-wide race/UB proof, automated online compiler regression lookup | **Not implemented** |
| Source code refactoring, commits, test auto-updates | **Deliberately not implemented** |
| Python/Rust tool scripts | **None** |

## Safety and trust model

- `audit`, `triage` and `topology` never edit target source; `baseline` and `--output` write **only specified paths**. `perf analyze` writes a temporary assembly file outside the analyzed repository and removes it.
- `check` runs **project-controlled builds/tests**. Only run it for repositories you trust to compile; CMake scripts may perform arbitrary configured build steps. Separate sanitized and compiler-matrix trees are under `.cpp-rails/`.
- `--changed` requires Git, compares against `HEAD` including untracked paths, skips generated/vendor/symlinks and refuses incomplete output. Max individual scanned file size 2 MB; scanner caps 20 findings per rule/file.
- Profiles use built-in defaults; target `.cpp-rails` policies are **not automatically loaded**. Use `--profile-file` only after review. Exceptions, if present, can only suppress P2/P3 suggestions and must have `id`, `path`, `owner`, `reason` and unexpired `expires`.
- The source checker does not guarantee soundness or lack of memory errors. Compiler/lint/sanitizer availability and incomplete test coverage are reported clearly.
- Flags for `--sanitizer`, `--lifetime` and GCC/Clang remarks depend on compiler/toolchain versions. MSVC compiler matrix/diagnostics and `abidiff` need appropriate supported environment.

## Benchmark format

CSV or TSV, each row is a **separate measured run** in nanoseconds for the same benchmark:

```text
benchmark,variant,run_ns
read_telemetry,baseline,100
read_telemetry,baseline,101
read_telemetry,baseline,99
read_telemetry,baseline,102
read_telemetry,baseline,98
read_telemetry,candidate,94
read_telemetry,candidate,95
read_telemetry,candidate,96
read_telemetry,candidate,93
read_telemetry,candidate,95
```

The comparison reports median and MAD. An observed difference smaller than the 3×MAD heuristic is marked **UNVERIFIED**, never claimed as a win. Using aggregated means as samples cannot establish p95/p99; only per-operation observations support those claims.

## Exit codes

- `0`: no failed/unverified **required** gates in requested run (an audit scan alone does not certify quality).
- `1`: at least one gate FAIL.
- `2`: incorrect argument, runtime error or any mandatory gate UNVERIFIED; `--strict` also makes optional UNVERIFIED block completion.

See skill `references/quality-gates-and-ci.md` and `templates/report.md` for interpretation and reporting.

CTest gates (regular, sanitizer and compiler matrix) read fresh dashboard Test.xml, supported by CMake 3.16, from the build directory. PASS requires at least one passed test and no failed, skipped, disabled or unknown results. Partial omissions remain UNVERIFIED with counts; project owners can evaluate optional omissions separately. No dashboard submission occurs. Dashboard evidence requires exclusive access to that build tree during the run; use separate trees for concurrent checks.

Changed scope uses Git NUL-delimited UTF-8 paths relative to the repository, normalized to --root. Tracked edits, untracked files and rename destinations are scanned; deleted/missing paths are counted but cannot be scanned. Paths outside --root are excluded. A working HEAD and Git are required.

Negative compiler fixtures check a missing insert member on std::array. MSVC uses diagnostic C2039 plus symbol/type; GCC/Clang use primary missing-member diagnostics (English output required). Unknown localized formats fail verification rather than accepting arbitrary errors.
