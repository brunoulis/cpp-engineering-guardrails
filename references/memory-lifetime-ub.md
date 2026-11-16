# C++ lifetime, memory safety and undefined behavior

**Rule:** ownership, lifetime and invalidation contracts must be understood before edits. A raw pointer may be valid as a non-owning view; a smart pointer is not automatically correct (cycles, destruction/allocator changes, ABI).

## Audit targets
- Dangling references, pointer-to-local escapes, `std::string_view`, `std::span`, `std::initializer_list`, temporaries, iterators invalidated by mutations, references held across suspension/callbacks.
- Manual `new`/`delete`, ownership through `void*`, mismatch allocation/deallocation, double-delete, custom allocators, destructors/partial construction, exception cleanup.
- Signed overflow, out-of-range indexing, use of moved-from object when invariant unspecified, strict aliasing/punning, alignment/placement-new/lifetime start, ODR violations and uninitialized reads.
- Exception safety: basic/strong/no-throw guarantees; destructors should not throw; partial updates and rollback.
- Undefined behavior from `memcpy`/`reinterpret_cast`/pointer arithmetic is contextual; flag for *inspection*, do not classify P0 from text alone.

## Safer defaults
- RAII, value types and clear ownership. `std::unique_ptr` for exclusive owning heap resources; `std::shared_ptr` only for actual shared lifetime; references/spans/string_views for appropriately bounded non-owning access.
- Prefer `std::vector`/`std::array`/`std::string`/`std::span` as appropriate. Check input sizes and integer conversions; make contract for an empty container clear.
- When bridging Rust: C ABI, explicit buffer lengths and lifetimes, documented allocator ownership, no unwinding across unsupported boundary.

## Verification
1. Reproducer / unit test for the actual suspect path.
2. Clang Static Analyzer and clang-tidy appropriate checks using `compile_commands.json`.
3. ASan+UBSan debug/instrumented build with representative execution; separate TSan for concurrency.
4. Consider Clang Lifetime Safety `-Wlifetime-safety-permissive` only if supported; **experimental** invalidation checks opt-in, reviewed for false positives.
5. Fuzz parser/serialization/FFI inputs with corpus and sanitizers when justified.

**Limit:** passing sanitizers does not prove absence of UB. Do not ship performance benchmarks from an ASan build as representative production speed.

For a property-level safety audit, use [safety-intelligence.md](safety-intelligence.md).
For borrows across opaque calls use [function-contracts.md](function-contracts.md);
for partial output and storage transitions use
[initialization-invalidation.md](initialization-invalidation.md). This file remains
the canonical source for the lifetime rules.
