# Manually labeled precision corpus

This small corpus measures selected **review candidates**, not proven C++ defects.
Positive means a candidate merits review under the label's stated contract;
negative means the stated concern is absent or justified. Labels and rationale are
reviewable independent inputs. They are not inferred from detector output. A
complex geometric routine can correctly be a positive review candidate without
having any defect or needing a rewrite.

Run the native C++23 executable:

```text
cpp-rails-corpus --self-test
cpp-rails-corpus PATH/TO/tests/corpus results.json
cpp-rails-corpus --check PATH/TO/tests/corpus checked-results.json
```

The second command writes JSON to stdout and optionally to its second argument.
No Python, Rust, downloads, external project builds, or compiler processes run at
measurement time. CMake registers `corpus-metrics-selftest` and
`labeled-precision-corpus` as separate CTest checks.

## Manifest and metric contract

`manifest.tsv` is UTF-8 TSV with the exact header:

```text
case_id detector rule path label profile provenance rationale
```

Those eight fields are separated by literal tabs. Blank lines and `#` comment lines
are allowed. Fields must be nonempty and contain no tabs/newlines. `case_id` is
unique. Each `detector/rule/path/profile` unit appears once; repeated detections
within a file count once. Relative paths use `/`, stay within this corpus, and must
identify readable source files. `label` is `positive` or `negative`; profiles are
`general` or `native-ui`. Detector `lexical` uses actual `CPP-*` findings from
`scan`; detector `quality` currently supports `duplication` and `complexity` from
`quality_score`. Other semantic dimensions and compiler-backed semantic rules have
no labeled precision estimate here and remain **UNVERIFIED**.

Each rule/profile reports TP, FP, FN, TN, `precision = TP / (TP + FP)` and
`recall = TP / (TP + FN)`. A zero denominator produces JSON `null`; a negative-only
sample cannot establish precision or recall. The JSON also records every case's
expected label, observed detection, outcome, provenance, and rationale. There is no
pooled quality grade or assertion that a small sample establishes general accuracy.
Unlabeled rule/path combinations are excluded rather than silently relabeled as
true negatives. Incomplete scans, unreadable/missing labels, invalid rows, duplicate
units, and unsupported detectors fail execution.

The metric self-test deliberately supplies all four outcomes and checks precision
and recall are 0.5. It separately checks null denominators, zero precision, JSON
escaping, malformed manifests, duplicate units, and path traversal. A measurement-only
run succeeds when measurement completes. `--check` additionally enforces the reviewed
per-rule regression policy described below; **it does not require perfect precision
or recall**, and does not conceal known detection limits by changing labels.

The initial measurement contains 35 labeled units. It includes an accidental exact
six-line clone, a justified independently versioned ABI clone (known duplication
FP), a shorter accidental clone (known FN), a branch-heavy routine, a complex
trailing-return routine (known complexity FN), and nested aggregate data (known
complexity FP). Those explicit counterexamples explain why lexical candidates are
never semantic quality gates. Current counts should be read from the generated
report, because detector changes may improve or worsen these outcomes.

## Regression policy

`bounds.tsv` has the literal-tab header `detector rule profile min_tp max_fp max_fn`
and exactly one row for every measured detector/rule/profile. `--check` fails with
exit 1 if any rule's TP falls below its minimum, FP exceeds its maximum, or FN
exceeds its maximum; invalid/incomplete policy or inputs exit 2. JSON retains the
observed confusion matrices and every policy bound, with independent rule statuses.
Missing policy rows cannot silently disable a check. Measurement without `--check`
reports the regression check as `UNVERIFIED`.

The reviewed fixture contract requires at least one detected positive for each
lexical rule with positive labels, zero missed positives, and zero false positives
on its labeled negatives. Native-UI raw allocation has only valid negative cases,
so its minimum TP is zero and precision/recall stay undefined. Complexity requires
the synthetic branch routine and the pinned Qt/Godot positives (minimum TP three),
while preserving the documented maximum one FP and one FN. Duplication requires
the exact accidental clone (minimum TP one), while allowing the documented maximum
one FP and one FN. These bounds permit improvements and reject regressions; they
are not thresholds for project quality or claims of population accuracy. Changes
to labels or policy require independent review of the fixture contracts.

The native self-test demonstrates that removing all positive predictions fails the
minimum-TP policy even with the known noise allowance; it also rejects one added FP
and one added FN. A separate development mutation check compiled a temporary runner
with its actual detection result forced to `false`: `--check` exited 1 and eight
rule/profile checks failed. The real runner passed all nine checks. CTest's
`labeled-precision-corpus` runs with `--check` so detector removal cannot pass merely
by continuing to print metrics.

## Real sources and reproducibility

`real/` contains small excerpts copied from official project source at fixed Git
revisions. Each directory includes `provenance.json` with the exact upstream source
path, pinned raw-source URL, original source SHA-256, copied original line ranges,
excerpt SHA-256, and license SHA-256. Source lines are unmodified except LF line
ending normalization and explicit separation comments for non-contiguous excerpts.
Line evidence in reports refers to the **local excerpt**; provenance maps it back
to the original source. Excerpts intentionally omit surrounding declarations and
includes and are scanned as data, not compiled or linked into the CLI.

| Project and role | Fixed revision | Source ranges |
|---|---|---|
| google/benchmark, numerical infrastructure | `cf4d7fedd59473f99042806fcba407ea767c5327` | `src/statistics.cc`, 1–108 |
| fmtlib/fmt, template library | `0e72a3ecec32d46ea012914ea04856bdd343403a` | `include/fmt/format.h`, 1–31 and 350–447 |
| qt/qtbase, Qt framework | `f6a1e0449f6645d814bfb076df68c6e6dbb87457` | `src/corelib/tools/qversionnumber.cpp`, 1–14 and 238–350 |
| godotengine/godot, game engine | `53166eb0eb0457457d0e41836ac99a2dc9e18996` | `core/math/aabb.cpp`, 1–182 |
| drogonframework/drogon, backend framework | `571161281fb14bc583a33d84b527985cb7b084cf` | `lib/src/HttpUtils.cc`, 1–103 |

The benchmark original and license hashes were verified against the existing
2026-11-14 local pinned audit inventory before excerpting. The other four were
retrieved directly from their official GitHub repositories and pinned before
labeling or detector execution. No upstream test suite was run; these cases do not
validate the upstream projects or establish their overall quality.

Original copyright/license notices are retained. The benchmark sample is Apache
2.0; fmt, Godot, and Drogon samples retain their supplied permissive licenses. The
Qt sample is redistributed under its offered **GPL-3.0-only** option with the
corresponding license; this third-party test data is not relicensed under the
guardrails repository's license. Each `real/*/LICENSE` belongs to its source project.

`synthetic/` cases are authored fixtures, explicitly identified in both path and
manifest. They model backend, Qt, engine, and template patterns without claiming to
be real extracted application code. Broader project diversity, semantic-rule
coverage, independent label review, inter-reviewer agreement, and statistically
representative accuracy remain **UNVERIFIED**.
