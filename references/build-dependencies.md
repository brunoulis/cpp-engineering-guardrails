# Build, toolchain, dependencies, portability

Start by detecting actual build system (CMake, Meson, Bazel, Visual Studio), C++ standard and supported compilers. `compile_commands.json` from CMake supports Make/Ninja generators; it can behave poorly with unity builds and may not be produced for Visual Studio generators. Never assume every project is CMake.

- Prefer using the repository's existing build commands/test runner and exact compile flags.
- Third-party/generated code is outside normal auto-rewrite scope; assess the vendor/update path instead.
- Public headers influence compilation time, ABI and dependency propagation. Avoid adding heavy includes or implicit global state casually.
- MSVC, GCC, Clang and their standard libraries differ: verify feature macros, exception/RTTI settings, visibility and warnings.
- Sanitizer support differs by platform and compiler; use dedicated build directories and report unsupported tools explicitly.
- Do not run untrusted build scripts without assessing side effects and the user's permissions.

For MSVC semantic analysis, distinguish a real compiler command from a reconstructed
analysis context. Preserve the selected configuration, platform, VC toolset, SDK,
standard, CRT, defines and per-file includes. Clang needs its CL driver for MSVC
options. If the Visual Studio build has no compilation database, use the explicit
[MSBuild evaluated-context export](../tools/cpp-rails/MSVC.md); ordinary audit does
not evaluate project files. Unsupported contexts remain UNVERIFIED.

Count `.inc`/`.inl` implementation fragments in the authored-source inventory.
Analyze them through their actual including translation units rather than inventing
standalone compiler flags. Separate generated-source exclusions, unreadable files
and unobserved AST fragments when comparing results or baselines. Include generated
output explicitly only when reviewing the generator/output itself.

## Effective dependency and artifact selection

When vcpkg is used, record its selected revision/builtin baseline, manifest and resolved versions, target/host triplets, overlays, overrides and local patches with reasons. Compare these with the actual CMake cache/toolchain, imported target include/link paths and configuration-specific binaries; a manifest or lock alone does not prove selection. Apply the same evidence discipline to other package managers without adding vcpkg to projects that do not use it.

Trace source/dependency revisions → effective build configuration (architecture, compiler, standard library, CRT, defines) → built/linked/runtime-loaded artifacts → test consumer and release package. Record identities such as hashes or build IDs, not filename equality. A Debug consumer cannot verify a different Release DLL; unresolved selection or stale identity remains UNVERIFIED. For distributed products, attach SBOM/provenance/signing only when required by product or policy. A local edit without distribution records N/A and its reason in [report.md](../templates/report.md).

**CMake quick start:**
```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

**Quality check:** don't mark "no tests found" as passing tests. Inspect CTest output; if there are zero discovered tests, report UNVERIFIED and add an appropriate plan.
