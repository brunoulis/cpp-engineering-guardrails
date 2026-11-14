# Native UI and event-loop rails (Qt, Win32, ImGui, MFC/WTL)

Load this only when the project has a user-facing native UI or a message loop. It complements `state-resource-lifecycle.md`; it does **not** mandate MVVM, a UI framework migration or a specific architecture.

## 1. Thread affinity — one UI thread
- Respect each API's thread contract: Qt widgets stay on the main GUI thread; Win32 window procedures run on their creating thread, while documented cross-thread messaging is supported; ImGui context access needs the integration's synchronization. A thread-affinity violation is not by itself proof of a C++ data race. See [Qt thread rules](https://doc.qt.io/qt-6/threads-qobject.html) and [SendMessage](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendmessagew).
- Worker → UI handoff must go through the loop: Qt `QMetaObject::invokeMethod(obj, ..., Qt::QueuedConnection)` or a signal with a receiver; Win32 `PostMessage` with an owned payload (never a pointer to a stack/temporary); ImGui: a mutex- or queue-protected snapshot read at frame start.
- Shared state read in paint/render (`WM_PAINT`, `paintEvent`, ImGui frame) and written by a worker needs synchronization or a single-owner handoff. [ThreadSanitizer](https://clang.llvm.org/docs/ThreadSanitizer.html) detects races on exercised paths when its toolchain/platform is supported. A clean run does not prove race freedom; unavailable required instrumentation is **UNVERIFIED**.
- `SendMessage` across threads blocks and can deadlock if the UI thread waits on the worker; prefer `PostMessage`.

## 2. Ownership and destruction
- **Qt object tree:** `new T(..., parent)` transfers ownership to `parent`; this is idiomatic, not a leak. Verify `T` is a `QObject` and the parent outlives every use. Use `deleteLater()` for objects deleted while their signals may still be dispatching; never `delete` a sender inside its own slot.
- **Win32:** `GWLP_USERDATA` stores a value and does not transfer ownership. A stack object or external `unique_ptr` owner may lend its address while it outlives every window callback; do not add a second deletion. Invalidate the slot and stop asynchronous users during teardown. Only an explicitly self-owned heap object may delete itself once in `WM_NCDESTROY`, after handling reentrancy and surviving references. Handle messages before the pointer is installed in `WM_NCCREATE`. See the pinned Flutter counterexample in [code-evidence.md](code-evidence.md).
- `QPointer<T>` / weak handles detect deletion of `QObject`s; they do not make cross-thread access safe.
- Teardown order: stop timers, cancel/join workers, disconnect, then destroy UI objects. A detached worker capturing `this` is a use-after-free waiting for a slow task.

## 3. Callbacks, signals and stale results
- Qt `connect(sender, signal, lambda)` (3 arguments) has **no context object**: the connection outlives the receiver and runs on the sender's thread. Prefer `connect(sender, signal, context, lambda)`.
- Where overlapping requests follow a latest-result-wins contract, test that a slow older result cannot overwrite a newer one. Preserve an existing serialization or framework guarantee when sufficient; otherwise a generation ID or cancellation with checked completion can enforce the contract. Do not add these mechanisms to unrelated requests.
- Nested event loops (`processEvents()`, modal `exec()`, `DoModal`, `MessageBox` inside a handler) allow re-entrancy: the handler can run again, or `this` can be deleted before the call returns. Re-check invariants and liveness after them.
- Win32 timers, hooks and subclass procedures must be removed before the object they reference dies.

## 4. Win32 resource contracts (handle vs pointer, who owns it)
Scanner hints (`memcpy`, `reinterpret_cast`) land *near* these bugs, not on them: read the whole acquire → use → hand-off → release sequence against the API documentation.
- **Clipboard / `HGLOBAL`:** `GlobalAlloc(GMEM_MOVEABLE)` returns a handle; `GlobalLock` returns a pointer. `SetClipboardData` takes the **handle**, after `GlobalUnlock`; on success the system owns it (do not free it), on failure you still own it. Passing the locked pointer, or keeping it locked, contradicts the contract even if it happens to work.
- **GDI:** every `SelectObject` must restore the previous object before the DC is released; `GetDC`/`ReleaseDC` and `BeginPaint`/`EndPaint` pair on the same window; objects you create (`CreatePen`, `CreateFont`, `CreateCompatibleBitmap`) need `DeleteObject` once deselected. Stock objects must not be deleted.
- **COM:** each successful `CoInitializeEx` (`SUCCEEDED(hr)`, including `S_FALSE`) acquires exactly one `CoUninitialize` on the same thread. Failure, including `RPC_E_CHANGED_MODE`, acquires no cleanup obligation. Use a noncopyable, thread-affine guard activated only by success; do not uninitialize somebody else's apartment after failure. `ComPtr` / `CComPtr` manage interface references, not apartment initialization. Check HRESULTs. See [Microsoft's contract](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-coinitializeex) and the optional WIL example in [code-evidence.md](code-evidence.md).
- **Kernel handles:** `CloseHandle` exactly once; `INVALID_HANDLE_VALUE` and `nullptr` are different failure values depending on the API (`CreateFile` vs `CreateEvent`).
- **Strings from the OS:** `GetWindowTextLength` + 1 for the terminator; `W`-API sizes are in characters, `memcpy`/`GlobalAlloc` sizes are in bytes.
- Report such a mismatch as **suspected** with the documentation reference until a test or debugger run reproduces it.

## 5. Input, text and buffers
- Text read from controls (`GetWindowText`, `GetDlgItemText`, `QLineEdit::text`) is user input: size buffers from the reported length, use bounded APIs (`wcsncpy_s`, `StringCchCopyW`, `std::wstring`) and never copy into a smaller fixed array.
- Keep encodings explicit (UTF-16 on Win32 `W` APIs, `QString` ↔ UTF-8 at boundaries); do not mix `A` and `W` APIs.

## 6. Layout, DPI and accessibility (only when UI behavior is in scope)
- Hard-coded pixel geometry breaks at 125–200 % scaling. Check DPI awareness (manifest / `SetProcessDpiAwarenessContext`, Qt high-DPI defaults) and font-relative layout.
- Keyboard navigation (tab order, mnemonics, focus after dialogs), screen reader names (`QAccessible`, UIA names on custom controls) and contrast are behavior; preserve them in refactors (R1).

## 7. Performance in the loop
- Keep blocking I/O, heavy computation and allocations out of paint/render and high-frequency handlers; measure frame time or input latency before and after (R8).
- Coalesce invalidation (`update()` vs `repaint()`, `InvalidateRect` once per batch).

## Verification
- Unit-test state/model logic without a message loop; drive the loop in integration tests (Qt Test `QSignalSpy`/`QTest::qWait`, a hidden Win32 window pumping `PeekMessage`).
- Test: close/destroy while a worker is pending, out-of-order async completions, duplicate clicks, re-entrant handlers, and DPI/scaling where layout changed.
- `cpp-rails-audit --profile native-ui` adds lexical hints (`CPP-UI-*`, `CPP-ASYNC-CAPTURES-THIS`) that are **suspicions**; thread affinity, stale results and DPI still need manual review and runtime evidence.
