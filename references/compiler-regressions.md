# Compiler version and optimization regression tracker

1. Record compiler ID/version (`clang++ --version`, `g++ --version`, `cl`) and full relevant flags, target triple, standard library, target CPU and linker options.
2. If codegen differs, minimize the reproducer while retaining representative hot-path conditions; isolate frontend vs optimizer vs standard library changes.
3. Compare old/current/new versions when available, with equivalent target hardware and flags.
4. Search official compiler issue trackers; verify that an issue or patch applies to the observed version; do not infer a fix from issue status alone.
5. Prefer removing workarounds when a supported compiler fixes the problem. Add a regression test/benchmark to protect hot paths.
6. Distinguish compile-time warnings, miscompilation (wrong semantics) and missed optimization (correct but slower); severity differs enormously.

**Sources:** GCC Bugzilla/patch tracker, LLVM GitHub issues and MSVC developer community. Never claim that a known bug is present in a user's compiler without confirming version/reproducer.
