# Optional Clang AST backend

`analyze_semantic` consumes Clang's real JSON AST with the target translation
unit's compilation-database arguments and working directory. It uses native
argv subprocess APIs, not a command shell. The backend requires a working
Clang executable, matching target headers/toolchain and a compilation database.
It does not link LLVM development libraries; this keeps the default auditor
dependency-free. A future linked LibTooling backend could expose richer canonical
declarations/CFGs and avoid the JSON protocol's version dependence, at the cost
of an LLVM development dependency and its version/ABI integration.

`arguments` arrays are preferred. A conservative portable subset of quoted
`command` strings is supported; shell operators, unterminated quoting, response
files, frontend/plugin forwarding and hidden compiler config files are rejected.
Compilation output/dependency options are removed and the action is replaced
with syntax-only AST dumping. Unsupported flags and nonzero compiler exits leave
the corresponding translation unit **UNVERIFIED**. Compiler default config files
are disabled; explicit compilation flags remain the source of target context.

The backend captures complete stdout/stderr directly into private temporary
files. It never parses the core process runner's truncated output. The AST has
an explicit 1 GiB input limit, 512 parsed JSON nesting limit and 60-second
compiler timeout per invocation. Exceeding a limit reports **UNVERIFIED**. Header
AST internals are projected out before allocating the retained analysis tree;
Clang still parses the complete translation unit. `analysis_budget` counts AST
node visits across the retained analysis and safety passes, with zero meaning no
node-count limit. This is not a wall-clock timeout. Temporary captures are removed
on normal return and exceptions.

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
the source-content fingerprint, Clang version and compilation-command digest.
FNV-1a-64 fingerprints identify provenance; they are noncryptographic and are not
security identities. Whole-function safety remains **UNVERIFIED** in each summary.

`semantic_dependencies` asks the same compiler/context for `-M` output including
system headers, parses Make escaping and continuations, and requires every named
dependency to exist. Missing/ambiguous compile context or incomplete extraction
disables verified caching. Cache callers must additionally bind compiler/tool
identity, options, database content and dependency content, and defend against
files changing during a run.

## Verification

`semantic_tests.cpp` accepts the actual Clang executable as its first argument.
Without it, only missing-database/compiler protocol checks run; the program
prints that real-backend coverage is **UNVERIFIED**. Register real-backend tests
only when Clang and the target standard library are available, with a suggested
CTest timeout of 240 seconds (MSVC standard-library JSON dumps are large).

The runtime fixtures cover real standard string/span/vector types, macros,
static/parameter/literal/wrapper negatives, reassignment, destructor side effects,
direct release and invalidation contracts, compilation-command quoting, working
directories, complete compiler dependency extraction, missing backend/context,
bounded analysis and stable finding identity across inserted whitespace.

Protocol references: [Clang AST introduction](https://clang.llvm.org/docs/IntroductionToTheClangAST.html),
[compilation database format](https://clang.llvm.org/docs/JSONCompilationDatabase.html),
[Clang command-line reference](https://clang.llvm.org/docs/ClangCommandLineReference.html).
The local implementation was verified against Clang 21.1.8 with the MSVC 14.51
standard library and C++23; other compiler/library/platform configurations need
their own runtime checks.
