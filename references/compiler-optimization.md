# Compiler-aware optimization: observe, do not guess

The same C++ behavior can compile to different machine code under different compilers, flags, standards, target CPUs and library implementations. Source-level simplicity does not guarantee vectorization.

## Procedure
1. Identify verified hotspot (profiling, actual workload) and baseline code/flags/hardware.
2. Use compiler optimization remarks and inspect assembly/LLVM IR *as supporting evidence*, not a substitute for runtime measurement.
3. Test equivalent alternatives: explicit loop, standard algorithm/operator, reordered bounds checks, buffered processing; preserve observable behavior and error timing.
4. Verify equivalence with tests and sanitizer builds; separately measure release builds using representative distributions (early mismatch vs equal data, sizes, alignment, cache state).
5. Compare multiple compilers and versions if supported; search upstream issues before adding a permanent workaround. Remeasure new compiler releases.
6. Do not convert every loop to intrinsics or eliminate early returns globally. Trade branch exits versus SIMD throughput based on data distribution.

**Clang example:**
```bash
clang++ -std=c++20 -O3 -Rpass=loop-vectorize -Rpass-missed=loop-vectorize -Rpass-analysis=loop-vectorize -c file.cpp
```
**GCC example:**
```bash
g++ -std=c++20 -O3 -fopt-info-vec-optimized -fopt-info-vec-missed -c file.cpp
```
Use actual compilation database/build flags for project code. Clang/LLVM IR and assembly are unstable across versions. Do not conflate missed vectorization with a confirmed performance defect.
