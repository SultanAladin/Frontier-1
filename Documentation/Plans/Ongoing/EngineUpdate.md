# EngineUpdate

## Objective

Establish agent rules and folder ownership, then audit and repair inherited violations with evidence.
Approved source: `SultanAladin/Frontier-` commit `338293863d7df4e585dec0c4acae45fe9c524ed5`.
The session checkout is a different repository. Only the selected UI foundation files have been imported.

## Current authorised scope

The user authorised selective engine/UI source porting after the documentation phase.
Include existing ImGui GPU UI drawing, all six patches, SVG artwork, and a lifecycle shared by UI and scene code.
Do not import or implement the scene renderer. Leave `Engine/Render/` blank apart from its Git retention marker.
Proof image generation and local checks are authorised. Commit/push are authorised, but GitHub Actions are not.
Windows remains the delivery target; local host verification tools are not Linux product deliverables.

First batch: interface foundations, lifecycle contract, UI-only Windows exhibit, dependency setup, and CPU evidence.
Full engine migration, whole-editor integration, compliance repair, and Windows runtime verification remain ongoing.

## Sequence

| Step | Work                                                     | Completion condition                  |
|------|----------------------------------------------------------|---------------------------------------|
| 1    | Establish documentation and agent rules                  | Consistency checks and review.        |
| 2    | Inventory linked baseline and map safe reconciliation     | Explicit path/build dependency map.   |
| 3    | Audit naming, formatting, ownership, duplication, evidence| Findings in the single tracker.       |
| 4    | Review exact source moves and build/launcher changes     | Approved migration map.               |
| 5    | Repair small, reviewable batches                         | Relevant tests and retained evidence. |
| 6    | Deliver verified Windows binaries when in scope          | Build and runtime status reported.    |

Do not replace the checkout wholesale or rewrite the entire engine without a reviewed scope.
No Linux binary delivery unless requested. No macOS setup now. No GitHub Actions without permission.
Keep this plan ongoing until work and evidence support completion, then move it to `../Completed/`.

## Control Centre batch

Ported the theme/typography layouts and font discovery, excluding renderer and GI settings.
Added the quick panel, stable font catalogue, licensed font recovery, and persistent applied preferences.
Six Debug and sanitizer tests pass. A Windows x64 Release binary cross-builds; runtime/GPU checks remain pending.
See `Exhibits/ControlCentre/README.md` for commands and evidence. No Actions are authorised.

Corrected the drawer to the archived trapezoid/gesture reference, migrated content into EngineContent,
and integrated GLFW. Native font discovery reads TOML metadata and uses Unicode-capable file access.
