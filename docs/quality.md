# Quality evidence: six independent dimensions

`cpp-rails-audit audit --score --root PATH` measures the supplied source inventory and
reports six independent dimensions. JSON and Markdown contain file/line evidence,
production and test code-line denominators, raw signal counts, and explicit unknowns.
There is no combined grade, pass threshold, defect severity, or automatic refactoring.

The library API is in `tools/cpp-rails/include/quality.hpp`. `quality_score(root,
files)` reads only the supplied paths; relative paths resolve against `root`.
Duplicate lexically normalized paths are measured once. Unreadable inputs throw
rather than producing an apparently complete partial report. The caller owns source
discovery, exclusions, changed-file filtering, and error presentation.

## Reading a report

All dimension statuses are `UNVERIFIED`: these are reproducible lexical indicators,
not semantic judgments. `score`, when numeric, means **signals / production KLOC**:
`signals * 1000 / production_lines`. It is not a 0–100 score; a larger value does not
automatically mean worse code. Duplication, complexity, and dependencies expose this
density. Responsibilities, indirection, and test proximity use `null`, because those
inventories do not support a defensible numeric quality score. An empty production
scope makes every score `null`; it never establishes zero risk or a passing gate.

`code_lines = production_lines + test_lines`. A code line contains a non-whitespace
token character, excluding comments. Multiline literal content counts on its actual
nonblank physical lines. A string is an opaque token for structural analysis; its
contents cannot create a branch, class, include, or brace. Literal values remain part
of duplicate equality, so differing strings are not exact duplicate tokens.

Test-like directory names (`tests`, `test`, `unittests`, `fixtures`, `mocks`, etc.) and
conventional test filename suffixes/prefixes identify a separate test scope. This is
a naming heuristic, and unconventional test paths can be classified as production.
Production measurements exclude recognized test sources. Scope classification and
changed-file scope affect all denominators and comparisons.

| Dimension | Measured evidence | Limitations and valid counterexamples |
|---|---|---|
| duplication | Exact contiguous windows of six code-bearing token-start lines and at least 30 tokens; whitespace/comments ignored, literal payloads preserved; repeated windows overlapping the latest reported copy are coalesced. Same-file overlapping windows are excluded. Each additional non-overlapping copy is one signal. | A window is a lexical match, not proof that extraction is desirable. Small snippets, renamed/parameterized code, one-line bodies, generated variants, and semantic equivalence can be missed. Distinct necessary protocol/ABI implementations may legitimately repeat code. |
| complexity | Function-like braced bodies with at least 10 branch tokens (`if`, `for`, `while`, `case`, `catch`, `&&`, `||`, `?`) or brace nesting at least four; branch count, nesting, name, and location included. | This is not cyclomatic/cognitive complexity or AST parsing. Braces may be scopes/initializers; nested lambdas contribute to the containing body. Trailing-return types, constructor initializers, operators, function-try-blocks, macro-generated functions, and complex declarators can be missed. A compact dispatch table or nested parser can be justified. |
| responsibilities | Class/struct surface candidates with at least 20 direct method-like parenthesis sites or physical span over 400 lines; counts and location included. | Method calls/default initializers/macros may affect the estimate. Domains, cohesion, ownership, and number of actual responsibilities remain unknown. A cohesive SDK, facade, ABI boundary, or generated binding may need a broad surface; this is a review location and carries no numeric penalty. |
| indirection | Factual inventory of `virtual` keywords, where present. | Necessity, call graph, template instantiation cost, dispatch overhead, PImpl, and abstraction depth require compiler/caller/ABI evidence. Zero signals and null score are intentional: a facade, forwarding call, interface, or PImpl is never penalized by its name or shape. |
| dependencies | Unique resolved include edges between supplied production files; per-file fanout; iterative DFS cycle back-edges with source locations. Each unique source/target edge is one signal. | Resolution tries the including directory then the root. No configured include search paths, generated files, macros, compiler preprocessing, module imports, transitive build costs, or link edges are inferred. Unresolved/external directives are counted separately. Include guards and conditional compilation can make lexical cycles harmless. |
| test-proximity | A production file with one or more recognized same-stem test paths is one signal; matched paths and unmatched production paths are shown. | Filename proximity establishes neither execution, assertions, behavior coverage, nor code coverage. Missing same-stem tests may be covered through integration tests, header consumers, another target, or external suites. No coverage score or absence-of-tests quality penalty is produced. |

The tokenizer handles ordinary, prefixed, escaped, and raw string/character literals,
line/block comments, continued line comments, and numeric digit separators. It is not
a full C++ translation-phase implementation: conditional preprocessing, token pasting,
universal character identifiers, trigraphs, malformed source, and escaped newlines in
general tokens require the real compiler. All branches of conditional source are
measured. No tool-specific source suppression is applied inside this module.

Measurements are deterministic for the same root, path inventory, and source bytes.
Compare reports only with equivalent scope and exclusions. Lexical path deduplication
does not establish filesystem identity across symlinks or differently cased aliases.
The implementation uses standard C++23 facilities and no subprocesses, Python, Rust,
external parser, telemetry, or network access.

## Verification

`tools/cpp-rails/tests/quality_tests.cpp` runs native fixtures for positive duplication,
short/different-literal negative cases, raw/escaped literal and comment exclusions,
digit separators followed by actual nested control flow, branch-heavy functions,
include fanout/cycles and fake includes, facade/PImpl neutrality, large class surfaces,
same-stem test proximity, empty scope, unreadable files, path deduplication, and report
escaping. These are behavioral fixture tests; they do not establish semantic accuracy
or compiler portability outside the executed build matrix.

Build the repository normally and run `ctest --test-dir BUILD --output-on-failure`.
The CMake quality test target is `cpp-rails-quality-tests` and its CTest name is
`cpp-rails-quality`.
