# Iterator compatibility check

The original `move-only-join` acceptance test covers a move-only input iterator and a repeatable view over ordinary copyable iterators. This additional consumer calls the iterator overload with two lvalues of a type that explicitly deletes its move constructor. The same view must still format twice. The historical parent accepts this caller. The upstream fix and all four patches from the first pilot reject it during compilation with MSVC.

Configure the consumer with a clean fmt checkout or the `source/` directory produced by the blind grader. From the guardrails repository root:

```sh
cmake -S tests/agent-pilot/followup/copy-only -B build/copy-check -DFMT_SOURCE=<absolute-path-to-fmt> -DCMAKE_BUILD_TYPE=Release
cmake --build build/copy-check --config Release
ctest --test-dir build/copy-check -C Release --output-on-failure --no-tests=error
```

Use a fresh build directory per candidate. Score this check alongside the original acceptance and regression results. The original report uses its own frozen criteria. [Comparison protocol](../../../docs/verification/cpp-skill-efficiency-2026-11-18/protocol.md)
