# Build, toolchain, dependencies, portability

Start by detecting actual build system (CMake, Meson, Bazel, Visual Studio), C++ standard and supported compilers. `compile_commands.json` from CMake supports Make/Ninja generators; it can behave poorly with unity builds and may not be produced for Visual Studio generators. Never assume every project is CMake.

- Prefer using the repository's existing build commands/test runner and exact compile flags.
- Third-party/generated code is outside normal auto-rewrite scope; assess the vendor/update path instead.
- Public headers influence compilation time, ABI and dependency propagation. Avoid adding heavy includes or implicit global state casually.
- MSVC, GCC, Clang and their standard libraries differ: verify feature macros, exception/RTTI settings, visibility and warnings.
- Sanitizer support differs by platform and compiler; use dedicated build directories and report unsupported tools explicitly.
- Do not run untrusted build scripts without assessing side effects and the user's permissions.

**CMake quick start:**
```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

**Quality check:** don't mark "no tests found" as passing tests. Inspect CTest output; if there are zero discovered tests, report UNVERIFIED and add an appropriate plan.
