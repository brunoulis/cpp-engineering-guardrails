# C++ Engineering Guardrails — Report

## Scope
Repository/branch/commit, requested change, compiler toolchain/standard, supported platforms and exclusions.

- Base and candidate revisions, dirty diff identity, compared range and relevant local changes:
- Observed inventory and exclusions; inferred execution paths and unresolved scope (distinguish these):

- Libraries touched: exact selected versions/revisions, headers/binaries/build-target evidence and unresolved discrepancies:
- Version-matched official documentation consulted (URLs or local paths), relevant API/default/deprecation facts:
- If updating: current → target, task justification, migration notes, compiler/standard-library/ABI/consumer compatibility, checks and rollback:

## Findings
| ID | Severity / confidence | Path | Trigger and consequence | Attribution (introduced / pre-existing / unknown) | Evidence | Disposition and reason |
|---|---|---|---|---|---|---|

Use corrected/verified, legitimate use, accepted in-scope limit, or investigation pending; link supporting evidence. A reviewer label alone does not establish severity or attribution.

## Change surface
| Existing/new file | Why changed | Behavior/contract before → after | Affected consumer/path | Verification evidence or gap |
|---|---|---|---|---|

- New/changed mutable state or resource → owner/mutator → invalidation/reset/cancel/join/disposal; necessity and failure/shutdown behavior (or justified N/A):
- Affected scenario inventory and denominator, if established; paths inferred from inspection versus paths actually exercised:
- Bug regression that fails before/passes after, with both identities and evidence; if unavailable/unsafe, reason and limit. Characterization/new-feature checks state their different objective:
- Feature-off, conditional compilation and other supported platform/target behavior: evidence, intentional authorized change, or UNVERIFIED:

## Decision record
Alternatives, tradeoffs, scope boundaries, API/ABI changes (intended or none), risk and rollback.

First solution → current candidate: added/removed states, dependencies, abstractions and lifecycle rules; which review findings caused them and why they remain necessary. Compare simplification, local correction and structural change; LOC or review count alone is not justification. If no earlier candidate exists, identify the starting proposal; do not invent revision history.

## Verification matrix
| Contract/scenario | Dimension (built / tested / integration / release) | Config/feature/target/runtime and artifact identity | Exact command/job | Selected / executed / passed / failed / skipped / disabled | Result (PASS/FAIL/UNVERIFIED/N/A), reason | Evidence location, date and candidate freshness |
|---|---|---|---|---|---|---|

Apply [quality-gate-matrix.md](quality-gate-matrix.md). Use N/A for counts on non-test rows; unknown counts stay unknown. Preserve runner failures as FAIL while unmet behavioral verification stays UNVERIFIED. Zero, all-skipped, stale or unavailable tests do not verify a contract. Record partial coverage and outstanding required checks separately.

## Distribution (when applicable)
Selected dependency revisions, baseline/triplet/overlays/overrides/local patches and reasons; effective CMake selection, toolchain/CRT/configuration; built, linked and runtime-loaded binary/package identities; consumer that exercised that artifact. Connect test and release identities or mark mismatches UNVERIFIED. SBOM/signing evidence is required only by the product/policy. Local changes without distribution use justified N/A.

## Safety evidence (when in scope)
Attach [safety-evidence.md](safety-evidence.md) and relevant
[function-contract.md](function-contract.md) entries. Report each scoped property,
check result, assumptions, binary/summary freshness and uncovered paths. Include
unknown effects, accepted exceptions and lost evidence versus baseline; passing
tools alone do not establish component memory safety.

## Performance (when claimed)
Dataset, machine, compiler/flags, baseline vs candidate median/p95/p99, repeats, spread/noise, confidence and tradeoffs.

## Independent review
Reviewer conclusion, disputed findings, test integrity, remaining uncertainties.

Reviewer/source and reviewed candidate; dispositions from Findings with evidence; unresolved required checks and accepted limits. If review was only self-review or documentary inspection, say so; do not imply independent execution.

## Summary
Changes applied, confirmed fixes, unverified areas, next actions.
