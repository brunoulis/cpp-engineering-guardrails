# REFACTOR mode — preserve behavior

A code smell is a *hypothesis*, not an instruction to rewrite. Before each change: identify constraints, affected callers, baseline tests and measurable reasons for improvement.

- **Class/function splitting:** assess cohesive responsibilities, dependency edges, invariants, header churn and build changes. 800 lines is not automatically wrong.
- **Reduction:** prefer deletion/merging of dead wrappers and duplication over multiplying interfaces and factories.
- **Ownership:** RAII adoption must preserve destruction timing, allocator/ABI behavior, exception guarantees and thread boundaries. Do not automatically turn every raw pointer into `unique_ptr` (some are non-owning).
- **Public contracts:** preserve signatures, symbol exports, layout and exception specifications where consumers depend on them.
- **Template code:** compile actual instantiations for supported type categories, not just concept `static_assert`s.
- **Verification:** protect behavior through characterisation tests, staged patches, a baseline diff and code review; include performance gates only when measurable impact is expected.

Deliver: issue → evidence → option comparison → change plan → steps → regression matrix → rollback. No automated large reformatting without request.
