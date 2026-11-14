# Pinned code evidence and counterexamples

Read only for window ownership, conditional cleanup or statistical sample validity.
These examples establish narrow contracts; they do not certify entire repositories
or require adopting their dependencies. Revisions inspected on 2026-11-14.

| Contract | Pinned implementation and related test | Decision and limit |
|---|---|---|
| Window user data can borrow | Flutter [`main.cpp.tmpl`, automatic Window](https://github.com/flutter/flutter/blob/e8ac49ab53c7a5db0c64f8d3664977de6c19f94b/packages/flutter_tools/templates/app/windows.tmpl/runner/main.cpp.tmpl#L27), [`win32_window.cpp`, WndProc](https://github.com/flutter/flutter/blob/e8ac49ab53c7a5db0c64f8d3664977de6c19f94b/packages/flutter_tools/templates/app/windows.tmpl/runner/win32_window.cpp#L158) | `GWLP_USERDATA` does not own the stored pointer. Preserve a stack/external owner that outlives callbacks. No upstream dedicated test was collected; local `windows-resource-contracts` exercises automatic, unique_ptr and explicitly self-owned objects. Flutter's unchecked COM initialization is **not** the reference for COM handling. |
| COM cleanup follows successful initialization | WIL [`resource.h`, CoInitializeEx helper](https://github.com/microsoft/wil/blob/abb1bd3509b6e5db53ba29738a4180d857adfd40/include/wil/resource.h#L5327), [`ResourceTests.cpp`](https://github.com/microsoft/wil/blob/abb1bd3509b6e5db53ba29738a4180d857adfd40/tests/ResourceTests.cpp#L533) | The helper checks failure before constructing the cleanup guard. Include `S_FALSE`, exclude failures such as `RPC_E_CHANGED_MODE`, and retain same-thread/nonduplicated cleanup. WIL is optional. The upstream success test alone does not cover changed mode; local Windows tests cover all three results. |
| Scope guards encode one cleanup obligation | WIL [`scope_exit`](https://github.com/microsoft/wil/blob/abb1bd3509b6e5db53ba29738a4180d857adfd40/include/wil/resource.h#L538), [counter tests](https://github.com/microsoft/wil/blob/abb1bd3509b6e5db53ba29738a4180d857adfd40/tests/ResourceTests.cpp#L75) | Verify observable destruction/release/reset counts. Reuse an existing suitable wrapper; a new generic guard hierarchy is not necessary. |
| Skipped runs are not statistical samples | Google Benchmark [`ComputeStats`](https://github.com/google/benchmark/blob/cf4d7fedd59473f99042806fcba407ea767c5327/src/statistics.cc#L110), [repetition tests](https://github.com/google/benchmark/blob/cf4d7fedd59473f99042806fcba407ea767c5327/test/repetitions_test.cc#L215) | It counts non-skipped reports before aggregation. Preserve raw samples and separate aggregates/omissions. This does not establish significance for the CLI's median/MAD heuristic, nor justify adding Google Benchmark everywhere. |

Primary API contracts: [Microsoft CoInitializeEx](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-coinitializeex),
[CTest 3.16 dashboard Test step](https://cmake.org/cmake/help/v3.16/manual/ctest.1.html#ctest-test-step),
[CTest skip semantics](https://cmake.org/cmake/help/latest/prop_test/SKIP_REGULAR_EXPRESSION.html).
The tool reads fresh dashboard XML; zero effective tests or any omission remains
UNVERIFIED. This is a conservative gate policy, not a claim that every project's
optional test must run on every platform.

The external suites were inspected, not executed. Original files, licenses and
SHA-256 manifests belong to the separate audit corpus (`CPP-C01` through `CPP-C04`);
the pinned links keep this skill portable without copying upstream implementations.
