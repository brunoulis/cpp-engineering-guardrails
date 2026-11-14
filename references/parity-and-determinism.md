# Behavior parity, determinism, migrations and replay

From Rust parity/differential-testing and Flutter preservation of UX/state. Refactors must preserve *observable behavior* even when implementation changes.

## Characterize before changing
Capture representative golden outputs/replays, error codes, serialization, timing-sensitive ordering, FFI wire formats, state transitions and resource-cleanup behavior. When deterministic behavior matters (simulation, telemetry normalization, encoding), record platform/compiler/flags and floating-point assumptions.

## Numerical correctness
- Avoid undefined signed overflow; define truncation, rounding, saturation and wraparound explicitly.
- In cross-platform numerical code, control assumptions about IEEE754, fused multiply-add, `-ffast-math`, NaNs, infinities, denormals, locale, endianness, alignment and data layout.
- SIMD/parallel reductions can reorder floating-point operations, producing different results. Distinguish exact equality from tolerance-based equivalence.
- Avoid changing iteration order in associative containers when output ordering is observable.

## Verification ladder
1. Characterization fixtures on the old implementation.
2. Unit and boundary tests for the new code; genuine negative cases where possible.
3. **Differential tests** comparing old/new on identical inputs, including malformed input and stress cases.
4. Property/metamorphic tests for invariants; fuzzing for parsers, decoders and boundary inputs.
5. Mutation testing selectively to find weak assertions. Mutation score is not proof of correctness.
6. Production-like replay/consumer compatibility tests where cross-module behavior matters.

Don't fake parity by updating expected values to match the new implementation. If behavior intentionally changes, label it and define migration, rollout and rollback.
