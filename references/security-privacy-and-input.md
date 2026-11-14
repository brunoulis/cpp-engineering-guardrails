# Security and privacy rails (from Flutter and Rust)

- Treat remote JSON, IPC buffers, FFI pointers, file formats and untrusted memory as hostile input. Check lengths *before* constructing views/reading fields, enforce protocol versions and handle partial frames.
- Never log plaintext tokens, session cookies, API credentials, personal data, memory dumps or full payloads by default. Redact in debug logs and automated reports.
- Avoid `system()`/shell command construction with untrusted input; prefer argument-vector subprocess APIs, narrow permissions and validated executable paths.
- A path supplied by a caller is not authorization to scan outside the requested repository; avoid following symlinks during heuristic scans. Respect generated code and vendor boundaries.
- Use bounds-safe, typed containers where possible. Pointer arithmetic, `reinterpret_cast`, manual serialization and C interfaces require explicit preconditions and validation.
- Do not weaken existing authentication, permission checks, certificate validation or audit logs in the name of simplifying architecture or improving throughput.
- Supply threat model and error handling for network, firmware, game server or backend-facing code when relevant. Differentiate static *suspicion* from demonstrated vulnerability.
- For security fixes, avoid publishing sensitive sample data in reports; use synthetic fixtures and sanitized evidence.

Check how failures propagate: rejected inputs must not silently become valid default values or fall back to insecure paths.
