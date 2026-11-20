# Parametric Sketcher Overhaul, Frontier Interchange/Exchange & Multi-Workspace Input Plan

## Goals
1. **Replace `ParametricPolygonBridge` with Frontier Interchange / Exchange**
   - Remove `Engine/GeometrySpace/Parametric/ParametricPolygonBridge.{h,cpp}` and its `CMakeLists.txt` entry.
   - Integrate `ModelPartition` / `PolygonComplex` -> `Frontier::GeometrySpace::PolygonNetwork` and `ConstructedSampleAsset` conversion directly into `Frontier::Interchange::GeometryInterchange` (`Engine/Interchange/GeometryInterchange.{h,cpp}`), unifying parametric CAD tessellation with Frontier's `.obj`/`.gltf` interchange pipeline and `FacePartitionRegister`.
   - Update `Exhibits/ParametricSketcher/SketcherKernelValidation.cpp` to validate parametric-to-interchange conversion through `GeometryInterchange`.

2. **Fix `InputExchange`, `PlatformWindowDesktop`, `InputExtension`, `ImGuiInputAdapter`, and `DesktopWorkspaceLaunch` Across All Workspaces**
   - Expand `InputLogicalKey` in `Engine/Input/InputExchange.h` with distinct `Minus`, `Equal`, `LeftBracket`, `RightBracket`, `Backslash`, `Apostrophe`, `GraveAccent`, `CapsLock`, and `Keypad0..9` / `KeypadDecimal` / `KeypadDivide` / `KeypadMultiply` / `KeypadSubtract` / `KeypadAdd` / `KeypadEqual`.
   - Fix `TranslateGlfwLogicalKey` in `Engine/Windowing/PlatformWindowDesktop.cpp` so numpad keys map to `Keypad*` (instead of colliding with `Digit0..9` or dropping to `Unassigned`) and punctuation/navigation keys (`Minus`, `Equal`, `LeftBracket`, `RightBracket`) propagate cleanly.
   - Add GLFW window focus-loss handling (`glfwSetWindowFocusCallback`) to emit clean release/reset state so `Ctrl`, `Shift`, `Alt`, and `Super` never stick across window focus transitions.
   - Fix `InputExtension::ApplyPacketToEditorSnapshot` so modifier release packets recompute modifier booleans from `HeldLogicalKeys` rather than stale platform `ModifierBitmask` bits.
   - Map all new logical keys in `Engine/Input/ImGuiInputAdapter.cpp`.
   - Fix `DesktopWorkspaceLaunch.cpp` for `SketcherWorkspaceSurface`:
     - Enable hosted overlay/popup presentation (`EndFrame(Dt, true)` with hosted floating overlay flag so Frontier's dock status bar isn't duplicated while popups, modals, drawing options, curve guide, matcap picker, and context menus render properly).
     - Guard Sketcher viewport pointer intake against `WorkspaceAppearance::QueryOverlayShieldContains` so Content Browser drawers and Control Centre never leak clicks into the 3D viewport.
     - Route OS drag-and-drop `DroppedPaths` for `SketcherWorkspaceSurface` (`.solidarc`, `.step`/`.stp`/`.iges`/`.igs`, `.ImageSpace`, `.png`/`.jpg`/`.jpeg`/`.bmp`).

3. **Plasticity / Blender CAD Controls, Context-Aware Snapping & Snap Filter Menu in Parametric Sketcher**
   - **`Ctrl`**: Point / important-feature snap (`Endpoint`, `Midpoint`, `Quarter`, `Centre`, `Intersection`, `Tangent`, `Perpendicular`), plus Grid snap toggle in Snap menu.
   - **`Alt`**: Snap / slide along curves & edges (`CurveEnabled`).
   - **`Shift`**: Straight orthogonal line / axis constraint while drawing lines, arcs, slots, and polylines.
   - **`Ctrl + E`**: Open context menu at the cursor showing Extrude types:
     - `Standard / Normal Extrude`
     - `Extrude Along Normals`
     - `Extrude Manifold`
     - `Extrude Individual Faces`
     - `Extrude Symmetric`
     - (Plus `Export…` moved to `Ctrl + Shift + E` and File menu.)
   - **`E`**: Extrude immediately using the last selected extrude type from the `Ctrl + E` context menu (or sketch profile extrude when a sketch profile is focused with no faces selected).
   - **`Ctrl + B` / `C`**: `Ctrl + B` (and `B`) starts Bevel/Fillet; `C` on selected edges/corners (or `Ctrl + Shift + B`) starts Chamfer directly; inside Bevel `D` = Fillet and `C` = Chamfer (`C` with no edge/corner selection still performs Cut).
   - **`G`, `R`, `S`**: Blender-style Move / Rotate / Scale modal transforms working consistently across bodies, faces, edges, vertices, sketches, sketch points, curve poles, and reference image planes, with `X`/`Y`/`Z` axis lock, `Shift+X/Y/Z` plane lock, numeric entry, and `Ctrl` snapping.
   - **Context-Aware Snapping & Snap Filter Menu**:
     - Viewport toolbar `Snap` button + dropdown popover allowing individual toggles for `Snap Enabled`, `Context-Aware`, `Grid`, `Endpoints / Points`, `Midpoints`, `Quarter Points`, `Centres`, `Intersections`, `Tangent`, `Perpendicular`, `Curves / Edges`, `Surfaces / Faces`, plus quick presets (`All On`, `Curves`, `Solids`).
     - Context-aware snapping biases curve/point/intersection snaps when sketching/editing curves (`Curve`, `Arc`, `Line`, `ControlPoint` pick mode, or `Alt`) and biases endpoints/centres/edges/faces when modeling solids/transforms.

4. **`.ImageSpace` Integration for Reference Image Import & Drag-and-Drop**
   - Wire `Engine/Codecs/ImageCodec.{h,cpp}` into `SketcherImagePlane.{h,cpp}`:
     - Decode `.ImageSpace` files directly into `SketcherPicture` (`ImageCodec::Decoder::DecodeFromFile`).
     - Provide `EncodePictureAsImageSpace(...)` (`ImageCodec::Encoder::EncodeToFile`) so imported PNG/JPG/BMP/GIF images can be persisted/exchanged as `.ImageSpace` containers seamlessly.
     - Support `.ImageSpace` and standard image extensions in `AddImagePlane`, the File/Import modal, `BrowseSketcherPicture`, and `DesktopWorkspaceLaunch` drag-and-drop.
     - Allow selecting/focusing and `G`/`R`/`S` transforming reference image planes in the viewport.

5. **Comprehensive Parametric Sketcher Test Matrix & Verification**
   - **`SketcherKernelValidation`**: 2D constraint solver, solid booleans, `GeometryInterchange` parametric -> `PolygonNetwork` & `ConstructedSampleAsset` (replacing `ParametricPolygonBridge`), STEP/STL/OBJ/glTF roundtrips, mass properties, edge fillets/chamfers.
   - **`SketcherDocumentValidation`**: Feature tree rebuild, extrude modes (`Standard`, `AlongNormals`, `Manifold`, `IndividualFaces`, `Symmetric`), `.ImageSpace` encode/decode & `SketcherImagePlane` validation, undo/redo, `.solidarc` save/load.
   - **`SketcherViewportValidation`**: Offscreen Vulkan + SwiftShader rendering, GPU pick buffer, `SketcherSnapFilter` & context-aware snapping (`Ctrl` points, `Alt` curve slide, filter toggles).
   - **`SketcherWorkspaceValidation`**:
     - `InputExchange` -> `ImGuiInputAdapter` tests covering numpad `0..9`, `+`/`-`, `[`/`]`, modifier release cleanup, and `WorkspaceAppearance` overlay shield blocking.
     - CAD control tests: `Shift` straight-line constraint, `Ctrl` point snap, `Alt` curve snap, `Ctrl+E` extrude-type menu + `E` last-mode extrude, `Ctrl+B` bevel + `C` chamfer, `G`/`R`/`S` Move/Rotate/Scale on bodies, faces, and `.ImageSpace` image planes, Snap filter menu toggles, and hosted multi-pane UI smoke screenshot.
   - **`ProjectBrowserValidation`** & **`FrontierSketcher` / `Frontier --workspace=sketcher`**: Full build + runtime verification.
