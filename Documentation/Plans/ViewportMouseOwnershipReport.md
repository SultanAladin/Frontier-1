# World Viewport Mouse Ownership

Imported the requested `unassignedinbox/Frontier` workspace at `70fc5262a651b89651949a4ef727d9375780244f`
onto the active Arena branch. The world editor explicitly enabled both plain-LMB walking/WASD and RMB flight.
Plain LMB no longer navigates; navigation gestures cannot initiate selection or a gizmo drag.

| Input | Result |
| :--- | :--- |
| Plain LMB | Selection / gizmo drag only |
| RMB + mouse / WASD / Q/E | Look / flight; Shift boosts speed |
| LMB pressed during RMB flight | Continues flight; does not select, pan, or start a gizmo drag |
| MMB | Pan |
| Alt + LMB / RMB / MMB | Existing orbit / dolly / pan preserved |
| Wheel | Existing flight speed adjustment preserved |

## Verification

- `python3 Automation/ValidateViewportNavigation.py`: 4 source-level regression checks passed,
  including 64 modifier/button combinations for flight and cursor-lock predicates.
- `git diff --check`: passed.
- Native build and interactive GPU verification **not performed**: this sandbox has no `cmake`, and
  `PrepareDependencies.py --verify` reports the pinned ImGui dependency missing.
- No GitHub Actions requested or triggered. No shaders, renderer, shared gizmo geometry, or UV controls changed.

## Desktop Check Still Required

Select an object, drag each gizmo axis with LMB, release, then drag empty canvas with plain LMB: the camera
must remain stationary. Hold RMB and test mouse look, WASD, Q/E, Shift, and an additional LMB press;
release RMB and confirm the cursor is visible and gizmo dragging works again. Check Alt orbit and MMB pan.
