# VehicleHMI demo reference

Copied at the owner's request from:
https://github.com/SultanAladin/Frontier/blob/6afb6f00b5f25c45f7cda92c7da65b64845768f3/Experimental/VehicleHmiPrototype/index.html

Branch: arena/e9bd7db5-frontier. `index.html` is a byte-identical copy (git blob
hash verified at copy time). It is the **port source of truth** for the editor
shell layout: Outliner column, Viewport column, Inspector column, status bar.

Only the HTML was copied, per instruction. The demo's JavaScript (`Source/`,
138 modules), stylesheets (`Style/`, 13 CSS files + DM Sans fonts), WGSL
shaders (`Shader/`, 80 files, including the ReSTIR reservoir set), facet
archives and celestial assets remain on the upstream branch. They are
inventoried in `Docs/VehicleHmiEnginePlan.md` and will be ported to C++ /
Vulkan / Slang / Jolt — not copied. Without those siblings this file renders
the static shell only; it is not a runnable demo. When this folder is served
statically, the root path therefore shows that unstyled shell — open
`LogStreamPanel.html` to view the Log Stream design.

Design references for new editor panels live upstream at
`SourceImages/General UI/` on branch arena/db2f9953-frontier
(revision 676eceb74b01399e71a7664d464a80b243016189). `LogStreamPanel.html` in
this folder is the first new panel designed from those images; it becomes
C++ only after owner approval.

Project file notes: `OpeningScene` names the intended exported Proving
Grounds scene (the demo builds its level procedurally; the glTF export is
plan Phase 3). `InterfaceFingerprint` reuses the Project-Networking value
because both projects target the same host interface; regenerate it when the
host SDK tooling issues one for this project.
