# Function contracts and resource effects

Use when a call can affect a borrow, ownership, initialized output or concurrency,
especially across translation units, dynamic dispatch, plugins, callbacks or FFI.
Use [the contract template](../templates/function-contract.md) as a review artifact;
it is not compiler syntax or a machine-checked summary format.

## Record effects precisely

- Identify the qualified function, overload/signature, target, source revision,
  selected headers and linked/loaded library identity. Match build defines and
  ABI. A signature or `const` parameter alone cannot establish effects through
  other aliases, globals, callbacks or interior mutable state.
- List preconditions: valid/non-null objects, extent and alignment, initialized
  ranges, nonempty containers, ownership, alias restrictions and locks.
- For each argument, pointee, reachable object, return and relevant global,
  record read, write, initialize (which range and on which outcomes), retain,
  return-borrow, transfer-ownership, release and invalidate effects. Distinguish
  "does not" from "unknown" and conditional "may" from unconditional "will".
- Include indirect calls, reentrancy, asynchronous retention/cancellation,
  exceptions and partial failure. State the lifetime of retained/returned borrows
  and invalidation conditions. For output buffers, capacity is not bytes written.
- Track each effect's provenance: implementation review, versioned API contract,
  analyzer result, annotation, test observation or unverified testimony. A test
  of one branch cannot validate an all-path no-retention claim.

## Validate and consume summaries

Check the summary against the selected body where available, including callees
and exceptional exits. Record how far analysis went and where dependencies are
assumed. A vendor's version-matched contract can be used as an explicit trusted
assumption; it is not evidence that this audit verified the binary's body.

If a body/effect is unknown, keep dependent facts UNVERIFIED and identify which
reachable aliases could be affected. Do not invent a pure, no-escape or
no-invalidation contract from the name or declaration. Virtual targets/function
pointers require all feasible targets to satisfy the used effects; unresolved
targets remain unknown. Resolve conflicting summaries before relying on them.

Invalidate summary evidence when the body, callee dependencies, linked/loaded
binary, relevant flags/defines, annotations or compiler model changes. Reuse
only with matching identities and assumptions; timestamp alone is insufficient.
Never treat an old handwritten summary as valid for a newly deployed plugin.

## A call between borrow and use

```cpp
void normalize(std::vector<double>& values); // body unavailable
double get_first(std::vector<double>& values) {
    double& first = values.front();
    normalize(values);
    return first;
}
```

Review two independent obligations: `values` must be nonempty at `front()`, and
the element must remain alive at the final read. A no-invalidation summary must
cover structural mutation, element destruction and indirect effects, not just
"writes values". With no current contract the latter obligation is UNVERIFIED.
A caller precondition may establish nonemptiness; without one, empty input is a
counterexample. Demonstrate actual invalidation before claiming a confirmed UAF.

Possible changes depend on intended behavior: restrict the callee to in-place
updates, or call it before borrowing and check the resulting container. Copying
before the call returns the old value; reacquiring afterwards may select a
different element or find an empty container. These are not automatically
behavior-preserving fixes. Coordinate API/ABI, error and exception contracts.

The C++17 fixtures `safety_contracts_positive.cpp` and
`safety_contracts_negative.cpp` under `tests/fixtures/` distinguish these cases.
They exercise examples, not a semantic analyzer.

Research: [P4422R0 — Function Summaries for Scaling Code Analysis of C++](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/p4422r0.pdf)
(2026-10-23) is an informational proposal about modular analysis, not an adopted
standard contract facility or an implemented feature of `cpp-rails`.
