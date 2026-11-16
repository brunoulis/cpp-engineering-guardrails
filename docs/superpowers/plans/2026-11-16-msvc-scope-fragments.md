# MSVC context and analysis scope

Implement the three gaps reproduced in the preceding ProyectTelemetryGui audit.
Preserve the target application's source and C++17 configuration. The optional
auditor remains native C++23 and does not install a compiler or other runtime.

1. Add failing native regressions for MSVC database replay, generated-code scope,
   and included `.inc`/`.inl` bodies and locations. Keep negative/unsupported cases.
2. Replay real MSVC commands through Clang's CL driver, retaining preprocessing,
   language and ABI context. Reject unsupported context; never report it as covered.
3. Provide an explicit MSBuild evaluated-context export for existing Visual Studio
   projects. Evaluation is opt-in, uses the chosen configuration/platform and does
   not invoke build targets. Record reconstructed-context limitations.
4. Exclude recognized generated files by default, expose counts and reasons,
   provide explicit inclusion/exclusion controls, and include authored fragments
   in lexical/topology/quality scope. Keep absent baseline evidence unobserved.
5. Integrate tests into CTest and Windows CI. Update the skill and guides to teach
   scope accounting and the real MSVC workflow, with explicit coverage limits.
6. Run targeted tests, a clean MSVC build and the full native suite. Review changes
   independently, resolve actionable findings, then repeat the three telemetry
   scans, native contract tests and real compiler-context analysis. Write results
   outside the telemetry repository and compare generated noise/fragment coverage.

Rollback: discard this feature branch's changes; no application migration or
source changes are involved. Missing tools, unsupported flags and fragments not
observed in an analyzed translation unit remain UNVERIFIED, never safety proofs.
