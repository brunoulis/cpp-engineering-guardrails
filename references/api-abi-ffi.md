# API, ABI, plugins and C++↔Rust↔Flutter interoperability

## Public ABI risk
- Symbol exports/mangling, calling conventions, vtable layout, struct alignment/padding, library/compiler ABI, STL type ABI, exception spec, inline functions, RTTI, allocator and destruction ownership, visibility and versioning.
- `std::vector`, `std::string`, exceptions and compiler-specific C++ classes should not casually cross a stable cross-language ABI. Prefer `extern "C"` functions with opaque handles, explicitly versioned POD structs and `uint8_t* + len` buffers.
- Rust `#[repr(C)]` only addresses representation of compatible types; it does not fix allocator ownership, layout of arbitrary C++ types or unwinding.
- Define who allocates, who frees, which thread invokes callbacks, how long borrowed data remains valid, and how errors are represented. Provide a paired free function if buffers cross allocations.
- Keep C++ exceptions and Rust panics on their own side of boundaries unless a deliberately supported, tested unwinding ABI is used.
- Never hand Flutter/Rust a pointer to stack-owned C++ memory; manage stream/buffer lifetime, backpressure and orderly shutdown.

## Review protocol
1. List all exported/public symbols and consumers.
2. Before refactor: capture API headers and ABI baseline with an appropriate ABI tool if public binaries exist.
3. Change using an adapter/migration plan if binary compatibility cannot be maintained.
4. Test integration across correct target architectures, runtime/toolchains and link modes.
5. For hot paths, measure copy vs zero-copy vs batching **end-to-end**; zero-copy may increase synchronization or lifetime complexity.

See profile `profiles/library-sdk.toml` and `examples/ffi-contract.md`.
