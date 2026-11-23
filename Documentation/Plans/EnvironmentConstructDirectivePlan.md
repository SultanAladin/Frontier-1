# Environment Construct & Outliner Directive Parity Plan

> **TL;DR**: Branch synced to `unassignedinbox/Frontier@1828eae`. The pinned HTML reference (`eosclient0001-rgb/Frontier@2fd4c62`, `Experimental/ProjectZeroEditor`) was rendered live on `0.0.0.0:8092` with **9 browser screenshots** and exact DOM/CSS measurements saved under `Exhibits/Images/ExperimentalEditorUi/`. This plan defines the exact C++20 / `.slang` blueprint for constructing and cleanly removing **Sun**, **Clouds**, **Moon**, **Stars**, and **Atmosphere** into one default `Environment` folder, plus 1:1 HTML parity for the Construct modal and Outliner right-click directive sheet (unimplemented items visible & greyed out, zero HTML-preview strings).

---

## 1. Phase 0 Status — Rendered HTML Reference & Visual Evidence

🟢 **Live HTML Reference Running:** `http://0.0.0.0:8092/` (pinned `2fd4c6247b8697d18337669276da9ce88f6d6e00`, zero page errors).

| Capture File (`Exhibits/Images/ExperimentalEditorUi/`) | Verified Reference Scope |
| :--- | :--- |
| `01-EditorOverview.png` | Fresh 1440×900 three-column frame, folded Outliner (`World` open), `Construct Ctrl+A` pill, 3D viewport |
| `02-ConstructModalAll.png` | Construct modal (`860×760`, `#171717`, `1px #373737`, `12px` radius), 7 category rails (`138px`), `38px` search bar, `161×116` cards |
| `03-ConstructModalEnvironment.png` | `Environment` rail: `Atmosphere`, `Sun`, `God Rays`, `Moons`, `Stars`, `Rainbow` |
| `04-ConstructModalWeather.png` | `Weather` rail: `Wind`, `Height Fog`, `Atmospheric Fog`, `Local Fog`, `Clouds`, `Precipitation`, `Local Cloud` |
| `05-ConstructStep2Properties.png` | Step `02 Properties` for `Sun` (`< Entities` back button, editable name field, footer `Cancel` + `Add to scene +`) |
| `06-OutlinerContextMenuSun.png` | Right-click directive sheet on `Sun` (`220px` width, `#1a1a1a`, `14px` radius, `34px` rows, `#2c2c2c` hover, `#ff8a80` Delete) |
| `07-OutlinerContextMenuFolder.png` | Right-click on `World` folder (`Select children` active, `Delete Del` disabled at `0.4` opacity `#6b6b6b`) |
| `08-OutlinerContextMenuEditorCamera.png` | Right-click on `Editor Camera` (`Rename F2` disabled, `Hide` omitted, `Delete Del` disabled) |
| `09-OutlinerContextMenuMultiSelect.png` | Multi-select (`3 selected`, `Group into collection`, `Duplicate 3`, `Copy names`, `Delete 3 Del`) |
| `HtmlMeasurements.json` | Exact computed styles, dimensions, colors, opacities, and item lists for every captured state |

---

## 2. Decisions Required Before C++ Implementation

> **Rating Legend:** **Fidelity / Safety:** 🔴 = highest/best · 🚩 = medium · ✔️ = low | **Complexity:** ✔️ = lowest/best · 🚩 = medium · 🔴 = high | **Overall:** more ⭐ = better

### 2.1 Decision A — Default Folder Name & Empty-Folder Lifecycle

| Option | Behavior | Fidelity | Safety | Complexity | Score |
| :--- | :--- | :---: | :---: | :---: | :---: |
| 🥇 **A1 (Recommended)** | Lazy-create single **`Environment`** folder on first Sun/Clouds/Moon/Stars/Atmosphere addition; reuse for all 5; preserve empty folder when last entry is removed (explicitly deletable when empty or with confirmation) | 🔴 | 🔴 | ✔️ | ⭐⭐⭐⭐⭐ |
| 🥈 **A2** | Lazy-create **`World`** folder (matching HTML initial showcase name); auto-delete folder when last entry is removed | 🔴 | 🚩 | 🚩 | ⭐⭐⭐⭐ |
| 🥉 **A3** | Pre-create `Environment` folder in every blank world even before anything is constructed | 🚩 | 🔴 | ✔️ | ⭐⭐⭐ |

**Recommendation:** **A1** — matches your request (`+create 1 folder for all of them by default`) and `WorldAuthoringAssetDagPlan.md` §4 without surprise folder disappearance.

### 2.2 Decision B — Singleton vs Multi-Instance Policy (Audited Native Engine Limits)

| Entity | Native Engine Capacity (`WorldAtmosphereConfiguration`) | Option B1 (🥇 Recommended) | Option B2 |
| :--- | :--- | :--- | :--- |
| **Atmosphere** | 1× `AtmosphericMediumConfiguration` | Singleton — grey out in Construct & disable `Duplicate` once present (`Already in world`) | Allow multiple rows sharing 1 backend (🔴 misleading) |
| **Sun** | 1× `SolarCelestialConfiguration` | Singleton — grey out in Construct & disable `Duplicate` once present | Allow multiple rows (requires multi-sun shader redo) |
| **Clouds** | 1× `CloudDeckConfiguration` planetary deck | Singleton — grey out in Construct & disable `Duplicate` once present | Allow multiple rows (unsupported by 1-deck shader) |
| **Stars** | 1× `StarfieldConfiguration` celestial sphere | Singleton — grey out in Construct & disable `Duplicate` once present | Allow multiple rows (unsupported) |
| **Moon** | `std::array<LunarBodyConfiguration, 6>` (up to 6 slots) | Allow up to **6 Moon slots** (auto-assigns next free slot `0..5`; greyed out at `6/6`) **or** 1 `Moons` entry managing all 6 slots | Always 1 slot only |

**Recommendation:** **B1** — enforce true engine limits honestly (`Atmosphere`, `Sun`, `Clouds`, `Stars` = singleton; `Moon` = up to 6 real slots, or 1 `Moons` entry if you prefer exact HTML `Moons` naming). Never spawn fake duplicate rows that silently overwrite one another.

### 2.3 Decision C — Visual Output When `Atmosphere` Is Removed (While `Sun` / `Moon` / `Stars` / `Clouds` Remain)

| Option | Behavior When `Atmosphere` Is Absent | Physical Accuracy | Independence | Complexity | Score |
| :--- | :--- | :---: | :---: | :---: | :---: |
| 🥇 **C1 (Recommended)** | Rayleigh/Mie/Ozone scattering = `0` (true black vacuum sky, zero hardcoded blue `SkyRadiance` fallback); **Sun** disc + direct light, **Moon**, and **Stars** still render cleanly against black space; **Clouds** render lit only by direct Sun (if present) | 🔴 | 🔴 | 🚩 | ⭐⭐⭐⭐⭐ |
| 🥈 **C2** | Removing `Atmosphere` reverts to flat blue `SkyRadiance` gradient and hides `Sun`/`Moon`/`Stars`/`Clouds` (current broken coupling) | ✔️ | ✔️ | ✔️ | ⭐ |

**Recommendation:** **C1** — cleanly decouples all 5 components in both CPU (`TieredWorldRenderSequence.cpp`) and GPU (`WorldViewportRadiometricCompute.slang` + `VulkanWorldRenderSequence.cpp`) so removing `Atmosphere` never accidentally deletes or hides `Sun`, `Moon`, or `Stars`.

### 2.4 Decision D — Construct Modal Placement Flow (Step 01 vs Step 02)

| Option | UX Behavior in Native C++ | HTML Parity | Speed | Scope | Score |
| :--- | :--- | :---: | :---: | :---: | :---: |
| 🥇 **D1 (Recommended)** | Exact HTML `860×760` modal with 7 rails, search bar, `01 Entities > 02 Properties`, and Step 02 (`< Entities`, editable name, `Cancel`, `Add to scene +`). Active cards: **Sun**, **Clouds**, **Moon**, **Stars**, **Atmosphere** (plus existing native Geometry / Area Light / Camera if desired). Unimplemented HTML items stay visible & greyed out with tooltip. | 🔴 | 🔴 | 🚩 | ⭐⭐⭐⭐⭐ |
| 🥈 **D2** | Same HTML visual layout, 1-click immediate add on card click (skips Step 02 until native property inspectors are ported in Slice 3) | 🚩 | 🔴 | ✔️ | ⭐⭐⭐⭐ |

**Recommendation:** **D1** — preserves exact HTML 2-step layout (`05-ConstructStep2Properties.png`) with editable name & `Add to scene +` (displaying default readouts in Step 02 until Slice 3 ports full inspector cards).

---

## 3. Audited Native C++ & `.slang` Defect Inventory (Found in Source)

Every defect below was verified directly in the current repository and **must** be fixed for real add/remove world registration:

| # | File & Location | Current Defect | Planned Fix |
| :---: | :--- | :--- | :--- |
| 1 | `Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h` | No `AtmosphereToken` or explicit registration tracking; `WorldFolderToken` hardcodes pre-populated rows | Add `EnvironmentFolderToken`, `AtmosphereToken`, `MoonSlotTokens[6]`, and authoritative registration booleans (`AtmosphereRegistered`, `SolarRegistered`, `CloudsRegistered`, `StarsRegistered`, `MoonSlotRegistered[6]`) |
| 2 | `Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.cpp:1761-1825` | **Zombie deletion bug:** `if (FindEntry(Token))` skips syncing when a row is deleted — deleting `Sun`/`Moon`/`Stars`/`Clouds` leaves them **stuck ON** in the renderer forever | Unregister cleanly on missing token: zero `SolarIrradiance`, disable `SolarEnabled`/`CloudsEnabled`/`CloudShadow`/`StarfieldEnabled`/`Moons[i].Visible`/`AtmosphereEnabled`, clear token, mark GPU + CPU sky/cloud LUTs dirty |
| 3 | `Engine/Lifecycle/EditorLifecycle.cpp:367-385, 2347-2386` | `ViewportDisplaySettings` overwrites authored atmosphere on load & couples `Atmo.Solar.SolarEnabled = DisplayCfg.CelestialStarsEnabled` | Decouple `Sun` from `Stars`; make world registration + Outliner visibility the single authority (Render Settings toggles reflect/mutate world state instead of stomping it) |
| 4 | `TieredWorldRenderSequence.cpp:189`, `VulkanWorldRenderSequence.cpp:4886`, `WorldViewportRadiometricCompute.slang:464` | `!AtmosphereEnabled` returns flat blue `SkyRadiance` early and **kills** Sun disc, Moons, Stars, and Clouds | Separate `AtmosphereEnabled` (Rayleigh/Mie/Ozone scattering + SkyView LUT) from `SolarEnabled`, `Moons`, `StarfieldEnabled`, and `CloudsEnabled` on both CPU and GPU `.slang` |
| 5 | `Engine/Components/Controls/DropdownSelector.{h,cpp}` | `DropdownDirectiveEntry` lacks `Disabled` / `DisabledReason` support | Add `bool Disabled = false;` and `const char* DisabledReason = "";` (render at `0.40f` alpha, suppress hover fill/clicks, display `ControlTooltip` on hover); align `#1a1a1a` surface, `14px` radius, `#2c2c2c` hover, `#ff8a80` danger |
| 6 | `Engine/Components/Panes/ViewportPanel.{h,cpp}` | Construct modal is `532×346` with 3 generic rails (`All`, `Items`, `Organization`) and no rendered search field | Upgrade `RenderConstructDialog` to match HTML `ConstructPanel` (`860×760`, `138px` 7-rail sidebar, `38px` search box, `01 Entities > 02 Properties`, `155px+` card grid, greyed-out unsupported items, zero HTML-preview copy) |

---

## 4. Exact HTML → Native Outliner Right-Click Directive Sheet Parity

Matched 1:1 against `06-OutlinerContextMenuSun.png`, `07-OutlinerContextMenuFolder.png`, `08-OutlinerContextMenuEditorCamera.png`, `09-OutlinerContextMenuMultiSelect.png`, and `HtmlMeasurements.json` (`220px` min-width, `#1a1a1a`, `14px` radius, `8px` padding, `34px` buttons, `12px` text, `10px` right-aligned shortcut hints, `#ff8a80` danger):

| Order | Label | Shortcut | Visibility Rule (Exact HTML) | Slice 1 Native Status |
| :---: | :--- | :---: | :--- | :--- |
| 0 | `<Name>` or `<N> selected` | — | Always (header caption, `9px`, `#777`) | 🟢 **Active** |
| 1 | `Rename` | `F2` | Single selection only | 🔴 **Greyed out** (`Inline rename pending`; always disabled on `Editor Camera`) |
| 2 | `Show` | — | When any selected row is hidden | 🟢 **Active** (restores world & renderer visibility) |
| 3 | `Hide` | — | When any selected non-`Editor Camera` row is visible | 🟢 **Active** (hides in world & renderer without removing) |
| 4 | `Isolate` | — | Always | 🟢 **Active** (hides all other rows, preserves enclosing folders & `Editor Camera`) |
| 5 | `Focus in viewport` | — | When selection contains geometry | 🟢 **Active** (wires to existing `FocusCameraOnSelectedEntry`) |
| 6 | `Inspect in workspace` | — | Single selection only | 🔴 **Greyed out** (`Workspace zoom mode pending`) |
| 7 | `Enable fracture` / `Disable fracture` | — | Single geometry selection only | 🔴 **Greyed out** (`Fracture link pending`) |
| — | *Divider* | — | Always | 🟢 **Active** |
| 8 | `Select children` *(C++ enum: `SelectNested`)* | — | When any selected row contains nested rows | 🟢 **Active** (expands & selects nested rows) |
| 9 | `Move into new collection` / `Group into collection` | — | When selection contains non-`Editor Camera` row | 🟢 **Active** (wires to `GroupSelectionIntoFolder`) |
| 10 | `Duplicate` / `Duplicate N` | — | Always (except locked sub-components) | 🟢 **Active** for multi-instance entries · 🔴 **Greyed out** for singleton environment entries (`Only one per world`) |
| 11 | `Copy name` / `Copy names` | — | Always | 🟢 **Active** (`ImGui::SetClipboardText`) |
| 12 | `Undo delete` | `Ctrl+Z` | When last deletion snapshot exists | 🟢 **Active** (restores deleted entity + world registration + folder) |
| — | *Divider* | — | Always | 🟢 **Active** |
| 13 | `Delete` / `Delete N` | `Del` | Always (`.danger` `#ff8a80`) | 🟢 **Active** (clean unregister + GPU sync + undo snapshot) · 🔴 **Greyed out** for `Editor Camera` & protected collections |

> 📝 **Excluded HTML-only items:** `Export HTML UI state…`, `Import HTML UI state…`, `Analytical preview only · no native object is created`, and `HTML preview` captions are intentionally stripped.

---

## 5. Exact Construct Modal Parity Matrix (Slice 1)

| Category Rail | Active 🟢 Entries (Real World Registration) | Greyed Out 🔴 Entries (Visible, Disabled + Tooltip) |
| :--- | :--- | :--- |
| **All** | Union of all active entries below | Union of all greyed-out entries below |
| **Environment** | **Atmosphere**, **Sun**, **Moon** *(up to 6 slots)*, **Stars** | `God Rays`, `Rainbow` |
| **Weather** | **Clouds** *(planetary volumetric deck)* | `Wind`, `Height Fog`, `Atmospheric Fog`, `Local Fog`, `Precipitation`, `Local Cloud` |
| **Cameras** | *(Optional: existing `Scene Camera`)* | `Main Camera`, `Cine Camera`, `Motion Blur`, `Lens Flare`, `Post Process`, post effects |
| **Geometry** | *(Optional: existing `Cube`, `Sphere`, `Cylinder`, `Torus`, `Cone`)* | Any unimplemented mesh presets |
| **Lighting** | *(Optional: existing `Area Light`)* | `Environment Light`, `Key Spot`, `Rim Point`, `Fill Point`, `ECE Low Beam`, `Softbox`, `Studio Tube`, `LED Emitter`, `LED Strip`, `IES Downlight` |
| **Vehicle** | — | `Controlled Vehicle`, vehicle components, `Off-road Vehicle`, `Pickup Truck` |

### 5.1 Environment Add / Remove World Registration Contract

```text
Construct ("Add to scene +")
  └─► WorldEnvironmentRegistry::RegisterEntry(Category, Title)
        ① Ensure single default "Environment" folder exists (reuse if present)
        ② Validate singleton / slot-capacity rule (reject if already at limit)
        ③ Populate validated defaults in WorldSceneSpecification::Atmosphere
        ④ Create OutlinerRecordEntry under EnvironmentFolderToken (with category & subtitle)
        ⑤ Mark CPU SkyView LUT + GPU Uniforms/Clouds/Stars/LensFlare dirty & re-sync
        ⑥ Select newly registered entry & close Construct modal

Right-Click → "Delete" (or Del key)
  └─► WorldEnvironmentRegistry::UnregisterEntry(Token)
        ① Snapshot removed entry + its Atmosphere config for Undo (Ctrl+Z)
        ② Remove OutlinerRecordEntry & clear selection / gizmo anchor safely
        ③ Clear bundle token & reset owned Atmosphere subsystem to zero contribution:
           - Sun: SolarEnabled = false, Scene.Sun.SolarIrradiance = {0,0,0}
           - Clouds: CloudsEnabled = false, CloudShadow.ShadowEnabled = false
           - Moon[slot]: Moons[slot].Visible = false, release slot
           - Stars: StarfieldEnabled = false
           - Atmosphere: Medium.AtmosphereEnabled = false (scattering = 0; Sun/Moon/Stars survive!)
        ④ Synchronize CPU + GPU renderer (drain/invalidate sky & cloud passes cleanly)
```

---

## 6. Modular Architecture & Verification Plan

### 6.1 Files Touched (Reuse-First, Editor-Only UI Isolation)

| Layer | Existing Files Extended | New Focused Unit (Only If Needed) |
| :--- | :--- | :--- |
| **Reusable Controls** | `Engine/Components/Controls/DropdownSelector.{h,cpp}` | None (extend `DropdownDirectiveEntry`) |
| **Outliner & Construct UI** | `Engine/Components/Panes/OutlinerPanel.{h,cpp}`, `Engine/Components/Panes/ViewportPanel.{h,cpp}`, `Engine/Components/Panes/WorkspaceAppearance.{h,cpp}` | None (extend existing panels; all Editor-only) |
| **World Registration** | `Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.{h,cpp}`, `Engine/Lifecycle/EditorLifecycle.{h,cpp}` | `Engine/Editor/WorldEnvironmentRegistry.{h,cpp}` *(typed add/remove/undo adapter, Editor-only target)* |
| **Renderer Independence** | `Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.cpp`, `Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.cpp`, `Engine/Shaders/GeometricRaster/WorldViewportRadiometricCompute.slang` | None |
| **Validation & Visual Proof** | `CMakeLists.txt` (Editor-only target membership + Shipping isolation check) | `Exhibits/EnvironmentConstructValidation.cpp` → `Exhibits/Images/EnvironmentConstruct/*.png` |

### 6.2 Mandatory Verification Gates (Post-Approval Implementation)

| Gate | Automated Proof |
| :--- | :--- |
| **UI Visual Parity** | Capture C++ offscreen `.png` of Construct modal (`All`, `Environment`, `Weather`, Step 02) & Outliner right-click directive sheet (Sun, Environment folder, Editor Camera, multi-select) and visually compare side-by-side against `Exhibits/Images/ExperimentalEditorUi/02..09.png` |
| **Add / Remove / Undo Matrix** | Construct all 5 (`Sun`, `Clouds`, `Moon`, `Stars`, `Atmosphere`) → verify 1 shared `Environment` folder, real pixel radiance changes in rendered frames → delete each individually → verify **zero residual radiance** (no zombie state), other 4 keep rendering → `Ctrl+Z` undo restores exact config + folder |
| **Atmosphere Removal Isolation** | Remove `Atmosphere` while `Sun` + `Stars` + `Moon` exist → verify black space sky (zero Rayleigh/blue fallback) with visible Sun disc, Moon, and Stars |
| **Repeated Stress & Singleton** | Add → delete → re-add 10 cycles with zero token leaks or crashes; verify duplicate singleton Construct/Duplicate controls stay greyed out |
| **Build Scope & Shipping Isolation** | Build & test only `Frontier` + `EnvironmentConstructValidation` (+ `WorkspacePanesValidation` regression); verify `FRONTIER_PROFILE=Shipping` excludes all editor-only UI/registry files |

---

## 7. Implementation & Verification Results

🟢 **Environment Batch Complete — All Targets Built, Validated & Visually Verified**

| Verification Gate | Result | Artifact / Evidence |
| :--- | :---: | :--- |
| **Build (`Frontier`, `WorkspacePanesValidation`, `EnvironmentConstructValidation`)** | 🟢 PASS | Zero errors, zero warnings across C++20 + compiled `.slang` SPIR-V |
| **Regression (`WorkspacePanesValidation`)** | 🟢 PASS | 100% green (`OutlinerPanel` default templates & `ViewportPanel` empty-registry fallback intact) |
| **Functional & Radiometric (`EnvironmentConstructValidation`)** | 🟢 PASS | Single `Environment` folder holds all 5 (`Atmosphere`, `Sun`, `Clouds`, `Moon`[0..5], `Stars`); Full Sky `meanLum=0.3255, blueRatio=0.5257`; Clean Removal `< 0.02` dark vacuum; Decoupled Hide/Show; Protected Camera; `Ctrl+Z` Undo Restore |
| **Native C++ Visual Proof (`Exhibits/Images/EnvironmentConstruct/`)** | 🟢 PASS | `01-NativeConstructAll.png`, `02-NativeConstructStep2Moon.png`, `03-NativeOutlinerSingleSheet.png`, `04-NativeOutlinerFolderSheet.png`, `05-NativeOutlinerEmptySheet.png`, `06-WorldSkyAtmosphereSunClouds.png`, `07-WorldSkyEmptyRemoved.png` |
| **Naming & Vocabulary Audit** | 🟢 PASS | Zero `Kind`/`KIND`, zero banned identifiers, `WorldEnvironmentRegistry` gated inside `FRONTIER_PROFILE STREQUAL "Editor"` |

---

## 8. Dedicated Environment Inspectors (Follow-Up Batch — Complete)

🟢 **All 5 Environment Inspectors + 3-Pane Validation Exhibits Built, Wired Live & Verified**

| Component | Summary | Evidence / Binding |
| :--- | :--- | :--- |
| **`Engine/Editor/WorldEnvironmentInspector.{h,cpp}`** | Editor-only dedicated Inspectors for **Atmosphere** (5 pages), **Sun** (4 pages), **Clouds** (5 pages), **Moon** (3 pages, 6 slots), and **Stars** (3 pages), reusing `ScalarSlider`, `WorkspaceAppearance`, and `InspectorPanel::ConfigureFullOverrideCallback` | Gated under `FRONTIER_PROFILE STREQUAL "Editor"` in `CMakeLists.txt`; zero fake controls; every tile, slider, and preset chip binds directly to `WorldAtmosphereConfiguration` |
| **`Engine/Lifecycle/EditorLifecycle.{h,cpp}`** | Wired `WorldEnvironmentInspector::RenderIfEnvironment` into `Panes->Inspector.ConfigureFullOverrideCallback(...)` with `EnvironmentInspectorMutated` dirty latch triggering immediate CPU/GPU sky, solar ephemeris, and cloud LUT sync | Selecting `Environment` folder or non-environment entries cleanly falls through (`return false`) to built-in `InspectorPanel` Folder / Standard Inspectors |
| **3-Pane Visual Exhibits (`Exhibits/Images/EnvironmentConstruct/`)** | Upgraded `EnvironmentConstructValidation.cpp` to dock `Outliner` \| `Viewport` \| `Inspector` at `1440×900` and capture live exhibits + assert interactive Quick Control mutation & page switching | `08-InspectorAtmosphere.png`, `09-InspectorSun.png`, `10-InspectorClouds.png`, `11-InspectorMoon.png`, `12-InspectorStars.png`, `13-InspectorEnvironmentFolder.png`, `14-InspectorAtmosphereScattering.png` (+ HTML references `Exhibits/Images/ExperimentalEditorUi/10..14.png`) |
| **Audit & Tests** | `Frontier`, `WorkspacePanesValidation`, and `EnvironmentConstructValidation` built with zero warnings and passed 100%; `ZERO_BANNED_WORDS_FOUND` & `ZERO_KIND_FOUND` | All 14 native exhibits visually verified side-by-side against HTML reference |


