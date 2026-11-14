# Architecture, boundaries, cohesion and build complexity

**Inspect:** dependency direction, API ownership, modules/libraries/executables, public headers, cross-TU behavior, initialization/destruction order, error channels and tests. C++ boundaries may be expensive because headers, templates and ABI are compile-time and binary interfaces.

**Decision framework:**
- Split a component only when it has separate cohesive responsibilities, change cadence, testing concerns, dependency boundaries or binary ownership. Measure impact on includes, build times, extra indirections and review complexity.
- Avoid factories/interfaces/DI solely to satisfy an abstract architecture template. Prefer direct composition when there is only one implementation and no real seam.
- Consider PImpl for binary interface stability **when justified**: it incurs allocation/indirection and complexity.
- C++20 modules do not automatically fix a bad dependency graph; verify build-system and compiler support.
- Prevent static-initialization-order problems across TUs; inspect globals, service-locators and singletons for shutdown/lifetime hazards.
- Treat large APIs as consumer contracts: defaults, inlining, exception specs, overload sets, type layout and symbol visibility matter.

**Measurements:** number of public headers, inclusion/compile-time, dependencies at each layer, churn, test setup complexity, number of actual call sites. Prefer a small verified refactor over speculative abstractions.

**Reviewer questions:** Does this make a common change easier? Is behavior easier to understand? Is the migration reversible? Have dependencies been moved across a stable boundary or just shuffled?
