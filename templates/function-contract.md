# Function resource-effect contract — [qualified signature]

Review artifact only; not compiler syntax or an automatically loaded CLI format.

## Identity and validity

- Component/target, overload, source revision and selected headers:
- Linked/loaded binary path and version/hash; compiler/STL/ABI and relevant flags:
- Contract source, reviewer, review date; dependencies and invalidation triggers:
- Evidence freshness: current / stale / conflicting / unknown, with reason:

## Preconditions and outcomes

- Inputs: nullability, extent/alignment, initialized range, ownership and aliases:
- Synchronization, thread affinity and reentrancy:
- Success postconditions and returned/retained borrow lifetime:
- Failure/exception/cancellation postconditions, partial output and cleanup:

## Effects

One row per resource/effect/outcome. Use "unknown" explicitly rather than an
empty cell or inferring "none" from a signature.

| Argument/pointee/reachable object/global/return | Effect (read/write/initialize/retain/borrow/transfer/release/invalidate) | Condition and range | Claimed guarantee | Validation status (PASS/FAIL/UNVERIFIED/N/A), evidence and assumptions |
|---|---|---|---|---|

## Validation and consumers

- Body/callee/indirect-target coverage; opaque dependencies and trusted contracts:
- Actual diagnostic/test commands, configurations, results and artifacts:
- Callers/borrowed aliases affected if this claim is false or becomes stale:
- Unknowns, owner and next validation action:
- Accepted exception (if any): ID, scope, owner, reason, reviewer and expiry;
  retain the underlying validation status:
