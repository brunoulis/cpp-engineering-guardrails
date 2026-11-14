# Example FFI boundary contract (adapt, do not copy blindly)

For a C++ telemetry engine calling Rust and exposing results to Flutter, prefer a stable C boundary with opaque state and explicit ownership:

```cpp
extern "C" {
struct PfEngine;                   // opaque handle, constructed/destroyed by C++ side
struct PfBuffer { const unsigned char* data; size_t len; };
int pf_engine_read(PfEngine*, PfBuffer* out) noexcept; // contract: out borrows until next read
void pf_engine_destroy(PfEngine*) noexcept;
}
```

**Not a complete implementation.** Define exact exported API version, linker visibility, allocation/free ownership, null and error codes, thread affinity, callback lifetime and reentrancy, whether a `PfBuffer` borrow survives concurrent calls, and what happens on shutdown. `size_t` is architecture-dependent; use fixed-width types in versioned binary/wire contracts where appropriate. Bridge to Rust using matching C representation and explicit unwinding policy, then to Flutter through a controlled FFI layer.

Test buffer validity under rapid updates, consumer slowdowns, shutdown racing callbacks, handoff/copy cost and correct error reporting. A zero-copy design is acceptable only when lifetimes and synchronization are proven.
