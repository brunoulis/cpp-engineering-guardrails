# SIMD/vectorization review

Look for hot loops with predictable bounds, contiguous access, loop-carried dependencies, early exits, aliasing and alignment constraints. Note that auto-vectorization can be blocked for sound reasons or can be slower for small/unpredictable data.

**Checklist:** data distribution, branch predictability, input lengths, alignment, cache residency, CPU ISA support, dispatch path, fallbacks, SIMD width, code size and power. Verify bounded access (masked loads, tails) and portability.

A reduction like `result &= lhs[i] == rhs[i]` eliminates early exit, can help vectorization but may run the full loop when the first element differs. Compare to standard equality and `memcmp` where correct for the chosen types; `memcmp` is **not** automatically a valid replacement for arbitrary object equality (padding/representations).

Use compile remarks and assembly to form a hypothesis, then production-like benchmarks and p50/p95/p99 to accept/reject the change. Do not turn a performance tip into a style rule.
