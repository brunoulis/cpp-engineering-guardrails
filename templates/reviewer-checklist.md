# Independent skeptical reviewer checklist (C++)

- [ ] I inspected the requested behavior, actual diff and supported toolchain/targets.
- [ ] I confirmed no unrelated user work or generated/third-party sources were rewritten.
- [ ] Each new type/abstraction has a specific justification (including leave-as-is option).
- [ ] Ownership and state owners are explicit; `string_view`/`span` and callbacks cannot outlive referents.
- [ ] Thread start/stop, cancellation, lock ordering and queue bounds are accounted for.
- [ ] Error codes, exception guarantees, boundary validation and secrets/logging are preserved.
- [ ] API/ABI consumers, layout, allocators and FFI contracts are covered when affected.
- [ ] Templates exercised real positive/negative instantiations and supported library implementations.
- [ ] Existing tests were not weakened to force green; missing negatives/counterexamples are listed.
- [ ] Build and targeted tests actually ran; sanitizers/compiler matrix included if needed.
- [ ] Performance claims include baseline, representative workload, repeated results and noise.
- [ ] Findings separate confirmed vs likely vs suspected with P0–P3 evidence.
- [ ] I captured unresolved unknowns/UNVERIFIED gates and rollback options.

**Verdict:** approve / block / request targeted follow-up, with reasons.
