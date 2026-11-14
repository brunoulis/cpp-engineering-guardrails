# Independent reviewer protocol

The reviewer must challenge the implementation *without merely paraphrasing the author*:

1. Reconstruct requested scope, before/after behavior and project support matrix.
2. Inspect diff and caller boundaries, including public API/ABI and data/wire formats.
3. Identify ownership, invalidation, exception, integer and concurrency edge cases.
4. Check each generic constraint against actual body and concrete instantiations.
5. Audit tests for deliberate/accidental weakening, missing negatives, untested error paths, false-positive mocks.
6. Inspect benchmark evidence if optimization was claimed; compare same build modes and distributions.
7. Verify tools/commands and statuses; mark absent runs UNVERIFIED.
8. Assign P0–P3 to evidence, name confidence and propose minimal corrective action.

**Stop conditions:** unexpected test failures, confirmed UB/races, silent public API/ABI breaks, contradictory assumptions, no rollback for major migration. A suggestion without evidence is not a blocker unless policy explicitly requires it.

**Reviewer output:** approval/block with reasons; findings; residual risks; requested tests; dispute resolution notes. Prefer small, actionable evidence rather than vague praise or blanket rejection.
