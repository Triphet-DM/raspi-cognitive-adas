# Debug Report — GIL Investigation
**Date:** 2026-06-05
**Project:** raspi_project v11 (version_2.2_cls_roi_debug)
**Scope:** Static analysis + code fix. No runtime testing performed (target hardware: Raspberry Pi 5).

---

## Problem Investigated

Static architecture review (`architecture_issues_v2.2.txt`) identified a **Python GIL violation** as Bug #2 (severity: HIGH).

`CameraThread` is a raw `std::thread` that calls `Picamera2Camera::read()` in a loop. `read()` calls Python C API functions (`PyObject_CallMethod`, `PyArg_ParseTuple`, `Py_DECREF`) without acquiring the GIL and without a registered `PyThreadState`.

This violates the CPython specification, which requires any thread calling Python C API to:
1. Have a `PyThreadState` registered with the interpreter.
2. Hold the GIL.

---

## GIL Violation Findings

### Call chain (before fix)

```
Py_Initialize()              [main thread — GIL acquired here, never released]
std::thread(&run, this)      [CameraThread spawned — no PyThreadState, no GIL]
  camera_.read()             [called from CameraThread]
    PyObject_CallMethod()    [Python C API — no GIL — VIOLATION]
    PyArg_ParseTuple()       [Python C API — no GIL — VIOLATION]
    Py_DECREF()              [Python C API — no GIL — VIOLATION]
```

### Why it did not crash immediately

After `Py_Initialize()`, the main thread holds the GIL but never uses Python again in normal operation. The camera thread became the **accidental sole user** of Python state — no concurrent access in the common case, masking the bug.

The violation manifests under specific triggers:

| Trigger | Effect |
|---|---|
| Python cyclic GC fires automatically | Data race on object graph traversal |
| `Py_DECREF` drops refcount to zero | Enters `tp_dealloc` / `pymalloc` without GIL |
| SIGINT received | Python signal handler races with camera thread |
| `Py_Finalize()` in destructor | Main thread re-enters Python while camera thread may still be in `read()` |

On ARM (Raspberry Pi 5), weaker memory ordering widens the race window compared to x86.

### CPython documentation basis

From [CPython docs — Non-Python created threads](https://docs.python.org/3/c-api/init.html#non-python-created-threads):

> *"When threads are created using means other than the Python thread module, they don't have a thread state associated with them. [...] It is possible for these threads to use the Python C API, but before they do so, they must create a thread state."*

---

## Code Changes Applied

### Change 1 — `src/camera/Picamera2Camera.h`

Added `PyThreadState* save_` member to store the result of `PyEval_SaveThread()`.

```diff
 private:
-    PyObject* camera_ = nullptr;
+    PyObject*      camera_ = nullptr;
+    PyThreadState* save_   = nullptr;
```

### Change 2 — `src/camera/Picamera2Camera.cpp` — `read()`

Wrapped all Python C API calls with `PyGILState_Ensure` / `PyGILState_Release`. Applied to all 4 return paths to guarantee matched Ensure/Release on every exit.

```diff
 bool Picamera2Camera::read(cv::Mat& frame_bgr) {
+    PyGILState_STATE gstate = PyGILState_Ensure();
     PyObject* result = PyObject_CallMethod(camera_, "read", nullptr);
     if (!result) {
         PyErr_Print();
+        PyGILState_Release(gstate);
         return false;
     }
     ...
     if (!PyArg_ParseTuple(...)) {
         PyErr_Print();
         Py_DECREF(result);
+        PyGILState_Release(gstate);
         return false;
     }
     ...
     if (buffer_size != expected_size) {
         Py_DECREF(result);
+        PyGILState_Release(gstate);
         return false;
     }
     ...
     Py_DECREF(result);
+    PyGILState_Release(gstate);
     return true;
 }
```

### Change 3 — `src/camera/Picamera2Camera.cpp` — constructor

Added `PyEval_SaveThread()` as the last line of the constructor. This releases the GIL after all Python initialization is complete, allowing other threads to acquire it via `PyGILState_Ensure`.

```diff
     if (!camera_) {
         PyErr_Print();
         throw std::runtime_error("Failed to create Picamera2 object");
     }
+    save_ = PyEval_SaveThread();
 }
```

### Change 4 — `src/camera/Picamera2Camera.cpp` — destructor

Added `PyEval_RestoreThread(save_)` as the first line of the destructor. This re-acquires the GIL for the main thread before calling `close()`, `Py_DECREF`, and `Py_Finalize()`. The `if (save_)` guard prevents calling `RestoreThread(nullptr)` if the constructor threw before reaching `SaveThread`.

```diff
 Picamera2Camera::~Picamera2Camera() {
+    if (save_) PyEval_RestoreThread(save_);
     if (camera_) {
```

---

## Build Results

**Not yet performed.** Changes were written on Windows development machine. Build and runtime testing must be done on the Raspberry Pi 5 target.

---

## Async Camera Deadlock Discovery

After applying Change 2 (GIL fix to `read()`) alone and before applying Changes 3–4, `--async-camera` mode hung immediately after:

```
[CameraThread] started (double buffer)
```

### Root cause

`PyGILState_Ensure()` in `read()` blocks waiting to acquire the GIL. The GIL was held by the main thread since `Py_Initialize()` and **never released** — `PyEval_SaveThread()` was not yet in the constructor. Neither thread could proceed:

```
Main thread:    holds GIL → spinning in capture_frame_async busy-wait
                → waiting for slot.valid == true
                → never releases GIL

Camera thread:  blocked in PyGILState_Ensure()
                → waiting for GIL
                → never completes read()
                → never sets slot.valid = true
```

Sync camera mode was unaffected because `read()` is called from the main thread, which already holds the GIL — `PyGILState_Ensure` returned immediately without blocking.

### Resolution

Changes 3 and 4 (`PyEval_SaveThread` in constructor, `PyEval_RestoreThread` in destructor) complete the fix. The full GIL lifecycle is now:

```
Constructor:  Py_Initialize() → [main holds GIL] → PyEval_SaveThread() → [GIL free]
read():       PyGILState_Ensure() → [caller holds GIL] → PyGILState_Release() → [GIL free]
Destructor:   PyEval_RestoreThread() → [main holds GIL] → Py_Finalize()
```

---

## Current Hypothesis

All four changes together correctly implement the CPython multi-threaded embedding pattern. Both the original GIL violation and the subsequent deadlock are addressed. The fix is consistent with CPython documentation and should resolve:

- Potential crash from cyclic GC racing with camera thread.
- Potential crash from `Py_DECREF` triggering `tp_dealloc` without GIL.
- Hang in `--async-camera` mode from `PyGILState_Ensure` blocking indefinitely.

**Not yet verified at runtime.** Must build and test on Pi before confirming.

---

## Next Steps

1. **Build on Raspberry Pi 5** and confirm `--async-camera` no longer hangs.
2. **Run both modes** to confirm sync camera still works.
3. **Test SIGINT shutdown** (`Ctrl+C`) to verify `Py_Finalize()` path is clean.
4. **Address Bug #1** — data race in `CameraThread` double buffer. This requires a design decision: mutex (simpler) vs triple buffer (lock-free). That investigation is next.
