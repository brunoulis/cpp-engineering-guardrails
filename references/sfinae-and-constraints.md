# SFINAE, constraints and substitution behavior

- SFINAE applies to substitution failures in the immediate context; ill-formed code outside it (e.g., a required function template body instantiated later) can still be a hard compile error.
- `requires` and concepts improve syntax and diagnostics but do **not** instantiate arbitrary function bodies merely because their calls are present in a requires-expression.
- Constrained overload sets can change overload resolution; add tests for qualifying and nonqualifying types and ambiguous cases.
- Use `if constexpr` to avoid instantiating invalid branches, but verify both branches under appropriate types.
- Prefer standard concepts when their actual contract matches the algorithm; add local concepts for project-specific semantics and write positive/negative compile checks.
- Public templates are part of source-level compatibility: changing constraints/defaults can silently redirect overloads or break consumers.

**Review checklist:** What precise expression is checked? Does the runtime operation require more? Are constraints tested with actual instantiated bodies? Do error messages remain intelligible? Which standard/library versions are supported?
