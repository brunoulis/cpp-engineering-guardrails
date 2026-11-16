# C++ Engineering Guardrails — Report

## Scope
Repository/branch/commit, requested change, compiler toolchain/standard, supported platforms and exclusions.

- Libraries touched: exact selected versions/revisions, headers/binaries/build-target evidence and unresolved discrepancies:
- Version-matched official documentation consulted (URLs or local paths), relevant API/default/deprecation facts:
- If updating: current → target, task justification, migration notes, compiler/standard-library/ABI/consumer compatibility, checks and rollback:

## Findings
| ID | Severity | Confidence | Path | Evidence | Recommendation |
|---|---|---|---|---|---|

## Decision record
Alternatives, tradeoffs, scope boundaries, API/ABI changes (intended or none), risk and rollback.

## Verification matrix
| Gate | Command/configuration | Result (PASS/FAIL/UNVERIFIED/N/A) | Evidence |
|---|---|---|---|

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

## Summary
Changes applied, confirmed fixes, unverified areas, next actions.
