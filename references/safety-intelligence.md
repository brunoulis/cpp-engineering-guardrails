# C++ Safety Intelligence — property-scoped evidence

Use for a safety audit, ownership migration or a request for safety guarantees.
Apply R2 using [memory-lifetime-ub.md](memory-lifetime-ub.md), the canonical lifetime
guide; this workflow adds contracts, evidence and adoption, not another rule set.

## Build the safety map

1. Scope components, translation units, selected compiler/STL/ABI, build flags,
   loaded binaries and source revision. Record generated/vendor exclusions,
   missing compile commands and code reached through plugins, callbacks or FFI.
2. Inventory resources: owner, borrowed aliases, mutation authority, release,
   invalidation events, synchronization and callers crossing the boundary.
   Label this a reviewed inventory unless semantic tooling actually produced it.
3. Follow acquire → initialize → borrow → call/mutate → use → release paths.
   Prioritize untrusted buffers, opaque calls, escaped pointers, manual storage
   and cross-thread ownership. Text matches locate candidates, not confirmed bugs.
4. Record effects at each consequential call using
   [function-contracts.md](function-contracts.md). Review partial construction,
   partial output and mutation paths with
   [initialization-invalidation.md](initialization-invalidation.md).
5. Fill [the safety evidence template](../templates/safety-evidence.md), including
   uncovered code and assumptions. Do not collapse independent properties into
   one green module status or a percentage described as "percent memory-safe".

## Interpret evidence

| State | Meaning for the stated property and scope |
|---|---|
| PASS | Evidence meets the stated scoped criterion under the recorded model and assumptions; state the mechanism and coverage limits |
| FAIL | A criterion was violated; link diagnostic, reasoning or reproducer and distinguish a diagnostic from a confirmed defect |
| UNVERIFIED | Missing, stale, conflicting, unsupported, skipped or insufficient evidence; include the next action |
| N/A | The property does not apply to the scoped component; justify with code/contract evidence |

Keep a check result separate from the property conclusion. For example, an ASan
test can PASS while lifetime across an opaque call remains UNVERIFIED. Dynamic
checks cover executed paths; a static result covers the analyzer's modeled
constructs and assumptions. An annotation or handwritten summary is a claim
until checked against the selected implementation. A reviewed exception records
accepted risk; it does not turn missing evidence or a defect into PASS.

Track lifetime, invalidation, initialization, bounds and thread safety separately;
add resource release or performance effects when relevant. Mark a demonstrated
counterexample FAIL; a plausible but unproven defect remains UNVERIFIED with its
confidence. Unsupported tooling is UNVERIFIED for a required applicable property,
not N/A. Silence from a lexical scan provides no semantic guarantee.

## Safety policies and regression review

Engineering profiles such as `low-latency` and `ffi` select risks. A **safety
policy** specifies required properties and acceptable evidence per component.
Use [safety-adoption.md](safety-adoption.md) to choose an incremental policy.
These are agent/reviewer policies, not WG21 profile implementations or currently
parsed CLI configuration. The CLI's existing `--strict` handles gate statuses;
it does not implement a strict memory-safety profile.

Compare baseline/candidate using equivalent component scope, property criteria,
tool versions, flags and tests; record each revision and binary identity.
Report new confirmed defects,
lost evidence (including PASS → UNVERIFIED), new unknown calls, added exceptions
and excluded coverage separately. A changed tool or scope needs reconciliation
before any regression claim. Existing lexical baseline IDs track hints, not
semantic safety. Never refresh a baseline just to accept a regression.

Block verified critical defects under R11. If required evidence is missing,
report an unresolved acceptance gate without relabeling the risk a confirmed
P0. Rerun affected checks after contract, implementation, dependency or build
changes. A collection of PASS checks does not certify a C++ program memory-safe.

Research context: [P3704R1](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/p3704r1.pdf)
discusses enforced profiles; [P4415R0](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/p4415r0.pdf)
reports experimental implementation experience. They do not establish support
in the user's compiler. See [sources.md](sources.md) for dated source status.
