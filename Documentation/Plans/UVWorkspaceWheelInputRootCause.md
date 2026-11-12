# UV Workspace — Mouse Wheel Root Cause (2026-11-12)

**Symptom:** Inspector / Outliner scroll and 2D / 3D viewport zoom did nothing, regardless of the pane-level changes made in earlier sessions.

| Layer | Finding |
| ----- | ------- |
| GLFW scroll callback (`PlatformWindowDesktop.cpp`) | Correct — enqueues a `ScrollWheel` packet. |
| `DesktopPlatformWindow::PollOperatingEvents` | Moves packets into `InputExtension`'s exchange queue only. |
| `DesktopWorkspaceLaunch.cpp` frame loop | **Bug** — never called `InputCoordinator.TickExchange()`, so `ScrollDeltaY` stayed `0.0` and the 1024-slot ring filled and dropped everything. |
| `Io.AddMouseWheelEvent(0, 0)` | ImGui therefore never saw a wheel event; `Io.MouseWheel` was always `0`. |
| Consumers (`CaptureImGuiPaneInput` → `UVInspectorPanel`, `UVOutlinerPanel`, `UVViewportInteractor`) | Logic is fine; they only ever received `WheelDeltaY == 0`. |

**Fix:** `InputCoordinator.TickExchange()` is now called right after `PollOperatingEvents` and before the wheel is forwarded to ImGui.

**Verification:** a headless harness (real `InputExtension` + real ImGui `3bae66c`) pushing one wheel notch through the same sequence gives `Io.MouseWheel = 0.0` before the fix and `1.0` after.
