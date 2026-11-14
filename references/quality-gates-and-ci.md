# Quality gates, reproducible proof and CI

From both skills: checks are selected by risk, not uniformly mandated for every edit. **Build success is the floor, not a proof of maintainability, security or correctness.**

## Minimum verification evidence
- Identify actual project commands: CMake + CTest, Meson, Bazel, Make, Visual Studio, platform SDK. Do not invent commands. A non-CMake project must use its native verified build/test commands.
- Build and targeted tests for typical source edits, then extend based on changed boundaries.
- Memory/FFI: ASan+UBSan if supported; negative and fuzz tests as appropriate.
- Concurrency: shutdown/stress tests + separate TSan build where available. Do **not** combine TSan and ASan in one build.
- Concepts/templates: compile real positive and deliberately negative instantiations, verify negative diagnostic rather than treating an arbitrary compiler failure as success.
- Public library: check API, symbol/ABI and downstream consumer matrix; `abidiff` is platform-dependent, not sufficient alone.
- Performance: baseline workload, compilers/flags, repeated samples with dispersion; treat small deltas within noise as inconclusive.
- Integration/web/native UI: run relevant platform/UI/UX tests only where the project has such interfaces; don't add Flutter-specific checks to pure C++ libraries.

## CI policy
Use test matrices where it matters: GCC/Clang/Linux; MSVC/Windows; Apple Clang/macOS. Test product toolchains actually supported, not every release ever shipped. Capture failing/skipped checks and link evidence to report. Avoid auto-fix and auto-suppression in CI.

**Exit statuses:** `0` means no requested *mandatory* failed/unverified gate (audit scan is not proof); `1` means gate FAILED; `2` means input/error or mandatory gate UNVERIFIED. A check without tests because none exist must not appear green.

## Avoid 'fake green'
Never update snapshots, fixtures, mocks or expected results solely to mask behavior changes. Preserve old contracts or explicitly authorize and document a migration. A negative compile test passes only for its expected reason. Separate true defects and improvement suggestions.
