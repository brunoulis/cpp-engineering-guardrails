# Target topology and explicit project constraints

`cpp-rails-audit topology --targets --root PROJECT --build-dir build --config Debug`
reads an existing CMake File API codemodel v2 reply. It follows the newest
`index-*.json` to the referenced codemodel and target objects, checks the source
root, and selects the requested configuration. It does not run CMake, configure
the target project, create query files, build targets, or evaluate project code.
Configure a project with a codemodel-v2 query through your normal trusted build
workflow before using this command. A multi-configuration reply requires
`--config`; a reply containing one configuration selects it automatically.

The JSON report's `target-topology` analysis section contains target names,
IDs, types, sources, dependency edges, evidence, and a transitive dependent
target count. The inventory covers the configured project even when source
scanning selects changed files. The count is a **recompilation fan-out
heuristic**, not a measured rebuild cost or a count of translation units using
a particular header. CMake utility targets can occur in the graph.

Codemodel dependencies are build dependencies. Their PUBLIC/PRIVATE/INTERFACE
link visibility is **unknown** because this API member does not supply it.
Header exposure uses a source's `fileSetIndex` and the corresponding file-set
visibility when supplied (codemodel 2.5+). Some CMake distributions omit the
source-to-file-set mapping: a header under a public file-set base directory is
then only a heuristic candidate. Internal/detail/private path names express
a review hint, not a proof that a public contract is wrong. Cycles are P2 review
findings because static library cycles can be intentional.

A missing, malformed, mismatched, unsupported or unsafe reply falls back to
root `CMakeLists.txt` literal target declarations and target dependencies.
The fallback is always UNVERIFIED and heuristic. It does not evaluate variables,
generator expressions, conditions, functions, macros, includes, subdirectories,
or arbitrary CMake commands. Inactive literal declarations may appear and real
targets may be missing. Literal link visibility is labeled as such in evidence.
Codemodel success verifies the stored structure, not snapshot freshness,
current build correctness, all exported APIs, or architectural intent.
Because `--targets` explicitly requests this analysis, an UNVERIFIED topology
gate produces exit status 2.

## Opt-in source constraints

`cpp-rails-audit audit --root PROJECT --rules path/to/project-rules.toml`
applies the supplied policy to the selected C/C++ files. No project policy is
discovered or enabled automatically. A policy can express project-specific
dependency allow/deny lists, explicit forbidden upward layer edges, and API
identifier restrictions without imposing a universal architecture.

The format is a deliberately restricted TOML-like subset, not general TOML:

```toml
version = 1

[[rule]]
id = "domain-avoids-ui"
kind = "layer"
from = "src/domain/**"
to = "ui/**"

[[rule]]
id = "public-avoids-internal"
kind = "dependency-deny"
from = "include/public/**"
to = "internal/**"

[[rule]]
id = "wrapper-uses-only-supported-headers"
kind = "dependency-allow"
from = "src/wrapper.cpp"
to = "supported/**"

[[rule]]
id = "avoid-legacy-entrypoint"
kind = "api-deny"
path = "src/**"
pattern = "legacy::open"
```

Each dependency/layer rule matches a root-relative source path (`from`) and the
literal spelling in a `#include` directive (`to`). `dependency-deny` and `layer`
forbid matching includes; `dependency-allow` requires every include from its
source scope to match `to`. Each rule is an independent constraint, so overlapping
allow rules are cumulative requirements. `layer` declares one forbidden upward
edge chosen by the project; it does not infer or mandate a hierarchy.
Paths use `/`; `*` matches within one path component, `**` across components,
and `?` matches one non-slash character. All other characters are literal.

`api-deny` matches an identifier or `::`-qualified identifier sequence, allowing
whitespace around `::`, outside comments, character literals and strings.
Matches include declarations and references; they do not establish which
overload is called. API restrictions and include rules are lexical constraints,
with P2 heuristic evidence. A declared violation fails the explicit project
rules gate; it is not escalated to a confirmed security or runtime defect.
An unresolved macro or continued include in a dependency rule's scope yields
UNVERIFIED unless a known violation already fails the gate. Conditions, macro
expansion, aliases, include search order and semantic resolution are not proven.

## Reviewed exceptions

An exception must name a declared project rule and one exact source path:

```toml
[[exception]]
rule = "avoid-legacy-entrypoint"
path = "src/migration.cpp"
owner = "native-platform-team"
reason = "Temporary adapter while the old producer is replaced"
review = "architecture-review-42"
expires = "2027-03-01"
```

All six fields are required and nonempty. Dates must be valid `YYYY-MM-DD`
values, unexpired on the current UTC day. Wildcard exception paths, unknown
rules, missing fields, duplicate keys/IDs, unknown sections/keys/kinds,
unsupported versions, invalid strings and security-disabling switches reject
the entire policy with FAIL before any exemptions are applied. Policy strings
use double quotes without escapes; trailing `#` comments are supported.
Rule IDs have 1–100 ASCII letters, digits, dots, underscores or hyphens and begin
with a letter or digit. At most 256 rules and 1024 exceptions are accepted.

Policy exceptions affect only newly evaluated `project.<id>` findings. They
cannot suppress built-in findings or turn off security analysis. Exception
evidence retains the owner, reason, review reference and expiration in the
report. At most 100 findings per rule and source file are retained; additional
matches still fail the gate and increment the report's capped count. Inputs
larger than 16 MiB and graphs over 4096 targets or 65536 edges remain unverified
or fail evaluation as appropriate, rather than silently producing PASS.

The regression executable covers literal/dynamic CMake, a synthetic exact
codemodel, a real configured tiny C++ CMake project, cycle and exposure evidence,
unsafe reply references, include allow/deny/layer rules, API token boundaries,
macro includes, rejected policies, and reviewed expiration constraints.

References: [CMake File API 4.2](https://cmake.org/cmake/help/v4.2/manual/cmake-file-api.7.html)
and [CMake File API 3.31](https://cmake.org/cmake/help/v3.31/manual/cmake-file-api.7.html).
