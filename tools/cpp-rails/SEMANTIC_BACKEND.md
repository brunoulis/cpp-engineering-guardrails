# Optional Clang AST backend

`analyze_semantic` consumes Clang's real JSON AST with the target translation
unit's compilation-database arguments and working directory. It uses native
argv subprocess APIs, not a command shell. The backend requires a working
Clang executable, matching target headers/toolchain and a compilation database.
It does not link LLVM development libraries; this keeps the default auditor
dependency-free. A future linked LibTooling backend could expose richer canonical
declarations/CFGs and avoid the JSON protocol's version dependence, at the cost
of an LLVM development dependency and its version/ABI integration.

`arguments` arrays are preferred. Windows command strings use CRT argument
quoting (including doubled backslashes before a closing quote), without invoking
`cmd.exe`; POSIX command strings use the documented conservative quoted subset.
Shell operators, unterminated quoting, response
files, frontend/plugin forwarding and hidden compiler config files are rejected.
Compilation output/dependency options are removed and the action is replaced
with syntax-only AST dumping. Unsupported flags and nonzero compiler exits leave
the corresponding translation unit **UNVERIFIED**. Compiler default config files
are disabled; explicit compilation flags remain the source of target context.

## MSVC-shaped compilation commands

`cl[.exe]` and `clang-cl[.exe]` are recognized by executable basename, including
absolute Windows executable paths. The selected `--clang` backend is invoked
with `--driver-mode=cl`; `/D`, `/U`, `/I`, `/external:I`, `/imsvc`, `/FI`, language
standard, exception/RTTI, target/ABI, CRT and supported optimization/FP switches
are preserved. CL-style `-I`, `-std:`, `-MD` and `-c` aliases are normalized;
`-MD` in CL mode selects the DLL CRT rather than GNU dependency generation.
Known output/debug/dependency switches are stripped, including separate `/Fo`
arguments and `/showIncludes`, so they neither write build artifacts nor pollute
AST JSON. An explicit closed vocabulary rejects other switches before replay;
fatal unknown/unused-option diagnostics also prevent Clang from silently ignoring
a listed switch. Arbitrary `/clang:` forwarding, compiler substitution, response
files and plugins are unsupported. Only explicit Windows targets and numeric
`/clang:-fms-compatibility-version=` values are accepted through that channel.

For original `cl.exe` commands, the original executable is resolved against the
entry working directory and current `PATH`, then inspected without executing it.
Its `Host<arch>/<target>/cl.exe` directory determines the target and its actual
Win32 FileVersion determines `_MSC_VER`/`_MSC_FULL_VER` compatibility; the toolset
directory's revision is not a substitute for compiler identity. Unknown identities
remain **UNVERIFIED** unless both target and compatibility version are explicit.
CL's current `INCLUDE` search paths are enumerated with `/imsvc` and `/X` disables
independent Clang MSVC/SDK discovery. A database's own `/X` suppresses that ambient
search path. Historical environment state absent from a database cannot be
reconstructed: use the MSBuild exporter with explicit target/version/includes
for reproducible analysis context.

An original `clang-cl.exe` is queried only when it is the same physical file as
the explicitly selected absolute backend. Otherwise its target and compatibility
version must be explicit; an untrusted compilation database must not execute its
own repository-local driver. Ambient `CL`, `_CL_` and `CCC_OVERRIDE_OPTIONS`
compiler-option variables are rejected before CL replay, including identity
probes. Compiler-specific macros/branches still describe Clang's compatible
parse, not an assertion that Clang and MSVC execute identical frontend behavior.

The backend captures complete stdout/stderr directly into private temporary
files. It never parses the core process runner's truncated output. The AST has
an explicit 1 GiB input limit, 512 parsed JSON nesting limit and 60-second
compiler timeout per invocation. Exceeding a limit reports **UNVERIFIED**. Only
requested project files are retained in the analysis tree, along with namespace
containers needed to reach them. SDK/header bodies and unrelated declarations
are projected out; physical filename-elision state is still tracked through the
complete JSON traversal, including range end and macro locations. Canonical path
keys are cached and retained-node source lookup avoids filesystem calls.
Clang still parses the complete translation unit and its raw JSON capture can
be large even though the retained tree excludes SDK bodies. `analysis_budget` counts AST
node visits across the retained analysis and safety passes, with zero meaning no
node-count limit. This is not a wall-clock timeout. Temporary captures are removed
on normal return and exceptions.
Trusted clang-cl identity/predefine probes have their own subprocess timeouts;
the coverage timeout field describes the AST invocation, not an aggregate
translation-unit budget including probes and JSON parsing.

Scanned `.inc`, `.inl`, `.ipp` and `.tpp` implementation fragments are analyzed
only when included by a selected real translation unit, including fragments
inside function bodies. They are never compiled standalone. Findings carry the
physical fragment path/line; function summaries identify the physical declaration
file and includer translation unit. Included files absent from the requested
project set cannot produce findings. Excluded statements inside retained
functions become opaque effect barriers: assignment/call effects cannot vanish
and manufacture ownership or invalidation proofs.

Coverage reports requested, observed and uncovered implementation fragments.
An unincluded, inactive, empty or otherwise unrepresented requested fragment
leaves semantic/safety coverage **UNVERIFIED**. An observed fragment means an
active retained AST location was seen in the selected compile contexts; it does
not certify all conditional branches, template instantiations or fragments in
other configurations. File snapshots are checked after compiler capture. The
per-function `source_fingerprint` currently binds its declaration file; for a
function whose body comes from another included file, the checked body snapshot
does not yet have a separate fingerprint in that function's summary.

## Bounded contracts

Findings have `analyzer=clang-ast`, `evidence_level=verified-structure`, P2
severity and structural evidence. These labels describe compiler-confirmed
declarations, types and expression structure; they do not claim execution of a
failing path or a whole-program safety proof.

- `SEM-DANGLING-VIEW`: a returned standard string view/span structurally borrows
  local automatic string/vector/array storage, directly or through a modeled view
  initializer. Static/thread-local storage, borrowed parameters, literals and opaque custom
  wrapper conversions are not treated as local automatic owners. Mutation and
  opaque effects discard view aliases conservatively.
- `SEM-DELETE-MISMATCH`: a directly acquired primitive-element local pointer has
  conflicting scalar/array `new` and `delete` forms in a straight-line body.
- `SEM-DOUBLE-DELETE`: a primitive-element allocation is released repeatedly
  without intervening reassignment, aliasing, opaque calls or control flow.
  User destructors are outside this model because they can change pointer aliases.
- `SEM-INVALIDATED-VIEW`: a modeled standard view's element access follows
  `std::vector::clear` on its local owner in the modeled straight-line scope.
- `SEM-DISCARDED-LOCK-GUARD` reports a standalone temporary `std::lock_guard` or
  nonempty `std::scoped_lock` followed by another statement in the same block.
  The temporary releases its mutexes at the semicolon. A named guard can cover
  subsequent work when the locking contract requires it. The check follows
  direct construction and leaves deliberate void casts and compound expressions
  to review.

Opaque wrappers/calls, unmodeled aliases/mutations, exceptions/branches,
cross-translation-unit effects, concurrency, allocator protocols, ownership
cycles and other iterator invalidation remain unknown. A clean finding list
cannot prove safety. The `semantic` gate describes successful AST coverage;
the `safety` gate describes execution of the selected bounded contracts, with
findings reported separately. Neither gate is a universal safety certificate.

Standard string owners and views accept both canonical template types and the
exact standard aliases (`std::string`, `std::string_view` and their character
variants). Apple Clang may preserve these aliases without `desugaredQualType`.
String-to-view conversions are identified by their standard owner/result types,
independently of the conversion operator's displayed typedef spelling. Custom
wrappers remain opaque. Host-independent AST protocol fixtures cover both forms;
the real standard-library assertions remain identical across the CI matrix.

## Evidence sections

`semantic-coverage` reports selected/analyzed translation units, retained AST
visits, functions, unknown call boundaries and limits. `safety-contracts` names
the supported properties and unsupported boundaries. `safety-function-effects`
records actual per-function structural acquisition/release/invalidation counts,
opaque boundaries, local owners and recognized borrow relations. Summaries carry
the declaration-source fingerprint, Clang version, original compilation-command
digest and effective replay-command digest. The `clang-ast` analyzer context is
bound to the actual backend version, selected contract mode, replayed target/CRT/
compatibility/include arguments and relevant compiler environment, so resolving
bare `cl.exe` to a different target or toolset changes baseline context.
FNV-1a-64 fingerprints identify provenance; they are noncryptographic and are not
security identities. Whole-function safety remains **UNVERIFIED** in each summary.

`semantic_dependencies` asks the same compiler/context for `-M` output including
system headers, parses Make escaping and continuations, and requires every named
dependency to exist. Missing/ambiguous compile context or incomplete extraction
disables verified caching. Cache callers must additionally bind compiler/tool
identity, options, database content and dependency content, and defend against
files changing during a run. CL-mode dependency actions are forwarded with
`/clang:` and `/showIncludes` is removed just as for AST capture.

## Verification

`semantic_tests.cpp` accepts the actual Clang executable as its first argument.
Without it, only missing-database/compiler protocol checks run; the program
prints that real-backend coverage is **UNVERIFIED**. Register real-backend tests
only when Clang and the target standard library are available, with a suggested
CTest timeout of 240 seconds (MSVC standard-library JSON dumps are large).

Windows runtime tests require a native Visual Studio developer environment and
real x64/x86 `cl.exe` installations. They use the configured real Clang and real
MSVC string/span/vector headers, CL-shaped argv and Windows command strings,
CRT/defines/includes, trailing quoted backslashes, x86 ABI selection from both
absolute driver paths and `PATH`, actual installed compiler FileVersions,
trusted clang-cl identity and explicit reconstructed context, dependency closure,
unsupported flags/plugins/response files and hidden compiler environment options.

The runtime fixtures also cover real standard string/span/vector types, macros,
static/parameter/literal/wrapper negatives, reassignment, destructor side effects,
direct release and invalidation contracts, compilation-command quoting, working
directories, complete compiler dependency extraction, missing backend/context,
bounded analysis and stable finding identity across inserted whitespace. Fragment
regressions cover declarations and statements, complete included function bodies,
Unicode source paths, physical path/line evidence, excluded effect barriers and
explicitly uncovered inactive/unincluded fragments.

Protocol references: [Clang AST introduction](https://clang.llvm.org/docs/IntroductionToTheClangAST.html),
[compilation database format](https://clang.llvm.org/docs/JSONCompilationDatabase.html),
[Clang command-line reference](https://clang.llvm.org/docs/ClangCommandLineReference.html).
The local implementation was verified against Clang 21.1.8 with the MSVC 14.51
standard library and C++23; other compiler/library/platform configurations need
their own runtime checks.
