# Language-standard upgrades — tools versus analyzed projects

Use when upgrading C++, adopting a newer standard-library facility, or evaluating
C++26 features. Apply R0a: identify the actual compiler, STL, language flags,
target, headers/binaries and consumers before changing the build.

## Current tool baseline

The bundled CLI and runners build in **C++23** mode with **CMake 3.20+**. This is
a build requirement, not a claim that every compiler implements all C++23
features. The standalone safety examples remain C++17 and concept fixtures are
syntax-checked as C++20; they intentionally exercise older target-project code.
No Rust/Python source or runtime is required by the tools.

As checked on 2026-11-16, [the published ISO standard is C++23](https://isocpp.org/std/the-Standard),
with C++26 still under development. Keep that status separate from the presence
of an individual feature in a released compiler. Recheck official status before
using this date-bound statement for another upgrade.

CMake requires `CXX_STANDARD_REQUIRED` and disables compiler extensions in the
tool build; an explicit standard below 23 is rejected. A newer standard may be
selected through `CMAKE_CXX_STANDARD` only if that CMake/compiler combination
supports it. Failure to configure is not authorization to silently lower it.

The validated local combination is MSVC 19.51.36260.0 with CMake 4.3 on Windows
x64. That CMake version maps C++23 to `/std:c++latest`; MSVC also offers a
`/std:c++23preview` switch. **Neither name establishes complete C++23 conformance
or ABI stability.** Inspect actual generated commands and relevant feature-test
macros. Newer MSVC/CMake releases may select different flags. Other compiler/OS
combinations require their own build/test evidence; CI configuration is not a
record that CI ran.

## Plan a target-project upgrade

1. Separate the tool's build standard from the analyzed project's contracts.
   `cpp-rails` does not edit target standards. Its standalone `--std` default
   remains `c++20`; select the actual target standard explicitly when appropriate.
   Project-native build/test commands remain authoritative for real target flags.
2. Record current → intended standard and exact compiler/STL matrix. Check
   language and library support independently, dependencies, configuration macros,
   generated code, supported deployment platforms and consumer ABI/CRT.
3. Compile before refactoring to reveal actual migration breaks. In this
   repository, C++17 → C++23 exposed `char8_t`: `path::u8string()` and `u8` literals
   no longer implicitly feed `std::string`. Preserve UTF-8 bytes at process,
   report and path boundaries; do not disable `char8_t` to hide the mismatch or
   replace UTF-8 paths with locale-dependent `path::string()`.
4. Make focused compatibility changes. New `expected`, ranges, formatting or
   modules are choices to justify using real code, not a mandatory rewrite.
   Check deprecated/removed APIs and changed overload resolution against actual
   consumers. Keep error, lifetime, numerical and concurrency contracts intact.
5. Run concrete positive/negative instantiations, Unicode paths and output,
   consumer/FFI tests and other risk-specific gates. Record actual compile flags,
   compiler/STL, results, untested platforms and rollback.

## C++26 evaluation

Use an isolated build and documented compiler-specific flags. CMake's recognition
of standard value `26` is distinct from support by the selected compiler. Accepting
`-std=c++26`, `/std:c++latest` or an annotation is not proof that the requested
feature is implemented; use a positive control and an expected failure or
diagnostic control. Missing support is UNVERIFIED. Do not infer WG21 Safety
Profile enforcement or memory-safety guarantees from a C++26 mode.

Primary sources (verify selected versions):

- [CMake CXX_STANDARD](https://cmake.org/cmake/help/latest/prop_tgt/CXX_STANDARD.html)
  and [known language features](https://cmake.org/cmake/help/latest/prop_gbl/CMAKE_CXX_KNOWN_FEATURES.html).
- [MSVC language-standard switches](https://learn.microsoft.com/en-us/cpp/build/reference/std-specify-language-standard-version).
- [Clang language conformance](https://clang.llvm.org/cxx_status.html),
  [GCC C++ status](https://gcc.gnu.org/projects/cxx-status.html).
