# Risk-based decision matrix and topology analysis

Imported from Rust's adaptive crate boundaries and Flutter's project-first state architecture.

## Compare options
For any nontrivial structural change, explicitly compare:

| Candidate | When to choose | Costs to measure |
|---|---|---|
| Keep in same class/module | Responsibilities cohesive, change cadence aligned | Complexity, tests, readability |
| Extract helper / internal namespace | Repeated logic, clear invariant, no new public interface | Call graph, compilation, tests |
| Extract static library / target | Separate test/build ownership, real dependency boundary | Public headers, link cost, build graph, API |
| Dynamic library / plugin / PImpl | ABI isolation or independent deployment demonstrated | Binary compatibility, allocation, indirection, versioning |
| Rework ownership/state model | Confirmed dangling reference, repeated invalid state | API migration, test impact, semantics |
| Keep as-is | Evidence of improvement weak | Debt item and review date, if warranted |

Never impose a crate count, file count, package count, ECS, hexagonal architecture or universal directory layout. Clang modules and C++20 modules require validated compiler, build and dependency support.

## Prioritize candidates
Use impact × confidence × change risk rather than line count. P0/P1 require verified evidence; heuristic scanner candidates are P2/P3 until confirmed. High-complexity plans need characterization tests, downstream consumers and rollback strategy.

Use `cpp-rails-audit topology --root .` only as a **direct include graph hint**, not a semantic dependency map. Human inspection of CMake targets, generators and callers remains necessary.
