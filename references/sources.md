# External references (verify versions/tool flags before use)

Official/technical:
- C++ Core Guidelines: https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines
- clang-tidy: https://clang.llvm.org/extra/clang-tidy/
- Clang Lifetime Safety: https://clang.llvm.org/docs/LifetimeSafety.html (some flags experimental and unavailable in older Clang versions)
- ASan: https://clang.llvm.org/docs/AddressSanitizer.html
- UBSan: https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html
- TSan: https://clang.llvm.org/docs/ThreadSanitizer.html
- CMake compile_commands: https://cmake.org/cmake/help/latest/variable/CMAKE_EXPORT_COMPILE_COMMANDS.html
- Google Benchmark: https://github.com/google/benchmark
- libFuzzer: https://llvm.org/docs/LibFuzzer.html
- ABI compliance checker (example external tool): https://lvc.github.io/abi-compliance-checker/
- GCC optimization options: https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html
- LLVM vectorizers: https://llvm.org/docs/Vectorizers.html

Inspiration — Reddit discussions (community opinions are not normative rules):
- AI-generated C++ review practices: https://www.reddit.com/r/cpp/comments/1xpshkh/dealing_with_ai_slop_at_work/
- C++20 concepts/inserter trap: https://www.reddit.com/r/cpp/comments/1xl06jo/c20_concept_trap_of_stdinserter_and_stdarray/
- Byte-range equality/codegen performance: https://www.reddit.com/r/cpp/comments/1xlyti8/catastrophic_misoptimization_of_byterange/

Rules in this skill are engineering policy suggestions and should be adapted to project requirements. The upstream resources are authoritative for behavior *in their documented versions*; the Reddit threads are useful case studies and hypotheses.
