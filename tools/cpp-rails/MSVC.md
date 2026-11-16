# Evaluated Visual Studio analysis context

Windows users can explicitly export a Clang compilation database from an existing,
trusted Visual Studio C++ project:

```powershell
cpp-rails-audit compdb --msvc-project path/to/project.vcxproj --config Release --platform x64 --output compile_commands.json
cpp-rails-audit audit --root path/to/source --semantic --compilation-database compile_commands.json
```

The exporter discovers installed `MSBuild.exe` on the absolute PATH entries or
standard Visual Studio installation roots. It never downloads or installs tools.
It requests evaluated `ClCompile` items, `ProjectConfiguration` items and selected
properties using `-getItem`, `-getProperty` and `-getResultOutputFile`. It disables
automatic response files and node reuse, and requests no targets or restore.
Ordinary audit does not evaluate projects. Only evaluate projects you trust:
MSBuild evaluation loads imports and can execute property functions.

The JSON contains **reconstructed Clang analysis arguments**, with `clang-cl` as
the driver. These are not captured MSVC build invocations. Export provenance is
recorded in the command's report, without adding nonstandard compilation-database
keys. Each entry preserves the evaluated per-file definitions, undefinitions,
forced includes, include paths, language standard, runtime library, exceptions,
RTTI, supported conformance, calling convention, packing and architecture options.
The selected `cl.exe` version resource supplies the compiler compatibility version;
the toolset folder's version can differ from the executable's patch version.
The target triple and SDK/toolset includes are explicit. Per-file excluded items
are omitted. Existing output is replaced atomically only after complete validation.

The export requires supported native MSVC toolsets, an installed selected compiler
and SDK/UCRT headers, existing sources and a declared configuration/platform pair.
Supported platforms are `Win32`, `x64` and `ARM64`. MSBuild must support all three
query switches; older installations report UNVERIFIED instead of falling back to
static project XML parsing. Evaluation is limited to 60 seconds and 64 MiB of
result/diagnostic data. No generated sources are produced and target-time changes
to items or metadata are outside this evaluation snapshot.

Unsupported evaluated metadata or additional compiler options fail closed. This
includes PCH creation/use, modules/header units, managed C++, WinRT, OpenMP,
active MSVC runtime checks, arbitrary response files and compiler plugins. Debug
configurations that enable `/RTC` therefore require another suitable existing
configuration or a real build-produced database. The exporter never changes the
source project to accommodate analysis. Code-generation, linker, warning and
output-file settings are not presented as compile-context equivalence guarantees.
Nonempty `CL`/`_CL_` environment options are rejected because they alter compiler
arguments outside the evaluated metadata snapshot.
An exported database still needs a working Clang backend; successful export alone
does not establish successful semantic analysis or whole-program coverage.

Protocol references: [MSBuild evaluation queries](https://learn.microsoft.com/en-us/visualstudio/msbuild/evaluate-items-and-properties),
[CL task metadata](https://learn.microsoft.com/en-us/visualstudio/msbuild/cl-task),
and the [official MSBuild result-file implementation](https://github.com/dotnet/msbuild/blob/main/src/MSBuild/XMake.cs).
