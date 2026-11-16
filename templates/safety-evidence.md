# Safety evidence — [component and revision]

Manual review artifact; not a memory-safety certificate or parsed CLI policy.

## Scope and policy

- Targets/TUs, source revision, headers and linked/loaded binary identities:
- Compiler/STL/standard, target/ABI, flags, build configuration and dependencies:
- Engineering profile; reviewer safety policy and required properties/mechanisms:
- Scope inventory: reviewed/total TUs, relevant boundaries/call targets and tests;
  exclusions, generated/vendor sources, unresolved calls and omitted configurations:

## Resource and boundary map

| Resource/boundary | Owner, aliases and mutation authority | Initialization/release/invalidation | Thread/FFI/callback contract | Contract evidence or unknown |
|---|---|---|---|---|

## Property evidence (required rows for a safety audit)

Use PASS/FAIL/UNVERIFIED/N/A per **scoped property**. Separate the check result
from the conclusion; explain N/A with code/contract evidence. A passing run can
leave the property UNVERIFIED. Include the supported subset and unexecuted paths.

| Property | Required? / acceptance criterion | Check/command, version and configuration | Check result / artifact | Property status and covered scope | Assumptions, gaps and next action |
|---|---|---|---|---|---|
| Lifetime | | | | UNVERIFIED | |
| Invalidation | | | | UNVERIFIED | |
| Initialization | | | | UNVERIFIED | |
| Bounds | | | | UNVERIFIED | |
| Thread safety | | | | UNVERIFIED | |

## Exceptions and regression comparison

- Exceptions: ID/path/property, owner, reason, reviewer/expiry, underlying status
  and accepted residual risk; never convert an exception into PASS:
- Baseline vs candidate identities, scope, tools, flags, tests and comparability:
- New confirmed defects vs suspected risks; lost/stale evidence, unknown calls,
  new exceptions and coverage exclusions (including PASS → UNVERIFIED):
- Acceptance disposition: satisfied / failed / unresolved, per required criterion:
- Independent review, remediation owner, checkpoints and rollback:

Do not call the component "memory-safe" from this matrix alone. A supported
sound guarantee needs a stated model covering all relevant operations and trust
boundaries; list assumptions and anything outside that model.
