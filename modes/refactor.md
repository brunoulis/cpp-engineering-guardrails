# REFACTOR mode — preserve behavior

A code smell is a *hypothesis*, not an instruction to rewrite. Before each change: identify constraints, affected callers, baseline tests and measurable reasons for improvement.

- **Class/function splitting:** assess cohesive responsibilities, dependency edges, invariants, header churn and build changes. 800 lines is not automatically wrong.
- **Reduction:** prefer deletion/merging of dead wrappers and duplication over multiplying interfaces and factories.
- **Ownership:** RAII adoption must preserve destruction timing, allocator/ABI behavior, exception guarantees and thread boundaries. Do not automatically turn every raw pointer into `unique_ptr` (some are non-owning).
- **Safety boundaries:** when changing borrow/call order, partial output or ownership effects, use `references/safety-adoption.md`. Refresh affected summaries and property evidence; copying before mutation and reacquiring afterwards may change results or error behavior.
- **Public contracts:** preserve signatures, symbol exports, layout and exception specifications where consumers depend on them.
- **Template code:** compile actual instantiations for supported type categories, not just concept `static_assert`s.
- **Verification:** protect behavior through characterisation tests, staged patches, a baseline diff and code review; include performance gates only when measurable impact is expected.

Deliver: issue → evidence → option comparison → change plan → steps → regression matrix → rollback. No automated large reformatting without request.

Use [report.md](../templates/report.md) to connect each file/reason to affected behavior, consumers, state ownership/closure and verification. Compare the first solution and current candidate, including complexity accumulated through review; necessary new state is permitted with a justified lifecycle. Record review attribution and dispositions, effective configurations, counts and freshness using [reviewer-protocol.md](../references/reviewer-protocol.md) and [quality-gate-matrix.md](../templates/quality-gate-matrix.md). Mark inaccessible consumers and unexecuted variants UNVERIFIED; a trivial edit may report applicable facts compactly with justified N/A.
