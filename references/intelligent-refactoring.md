# Intelligent refactoring advisor

## Trigger analysis
Evidence can include: repeated bugs, duplicated invariants, tight coupling proven by change history, tests requiring extensive scaffolding, API confusion, measured build overhead or performance. Large file size and numbers of crates/classes are **not** defects by themselves.

## Decision table
| Option | Choose when | Avoid when |
|---|---|---|
| Local simplification | Duplicate wrappers, unnecessary levels, dead branches | Contracts are not yet understood |
| Split coherent responsibility | Separate change cadence/test boundary/dependency direction | Only reason is line count |
| Introduce facade/PImpl | Stable API/ABI must hide implementation changes | Extra heap/indirection outweighs benefit |
| Change ownership model | Lifetime errors or contract confusion, migration feasible | Public ABI/destruction semantics would break silently |
| Move to algorithm/ranges | Improves clarity and supported toolchain | Constraints/proxy/iterator semantics become harder |
| Keep as-is | Benefits uncertain and change risky | Confirmed defect exists |

## Method
For library-dependent cleanup or upgrades, apply **SKILL.md R0a** first: verify the version/revision selected by the actual build and consult its documentation. Do not replace working code solely to resemble newer API examples. For an authorized update, compare current/target documentation and migration notes, separate it from unrelated cleanup, and verify toolchain, API/ABI and consumer compatibility.

Create characterization tests, mark invariants, compute API/ABI impact, apply one reversible change, run targeted gates, review independently, repeat. Compare complexity and risk before/after rather than refactor-by-instinct. If no compelling improvement, **do not refactor**.
