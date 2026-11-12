# UV Workspace Audit & Windows Desktop Package

**Branch:** `arena/4c0111da-frontier-1` (source imported from `SultanAladin/Frontier` `arena/4c9015d5-frontier` @ `f73a5cf`)

**TL;DR**

- Banned vocabulary inside the UV workspace: **0 findings** after this pass (was 62 first-party hits). Gate: `python3 Automation/AuditBannedVocabulary.py`.
- Windows x64 package builds with MSVC on GitHub Actions: workflow `Windows desktop package` → artifact `FrontierUV-Windows-x64`.
- 🔴 The browser / SmartScreen "potentially unsafe" prompt **cannot be removed by any GitHub setting**. It needs an Authenticode certificate from a trusted authority. The workflow is wired for one (two repository secrets); until then the build ships with verifiable provenance instead. See §4.

---

## 1. Verification Status

| Check | Where it ran | Result |
| :--- | :--- | :---: |
| UV banned-vocabulary audit (43 files) | Local + both workflows | 🟢 0 findings, 5 advisories |
| MSVC x64 build, `Editor` profile, static runtime (`FrontierUV.exe`, `FrontierEditor.exe`) | `windows-2022` runner | 🟢 |
| Packaged-layout self-check (`--verify-packaged-layout`, run from a foreign working directory) | `windows-2022` runner | 🟢 13 shaders, geometry, typefaces found |
| Build provenance attestation | `windows-2022` runner | 🟢 |
| Zig 0.13 Windows cross-compile (`Automation/WindowsToolchain.cmake`) | Local sandbox | 🟢 links both executables |
| Linux CTest, 3 profiles | `ubuntu-24.04` runner | 🟢 all suites pass (`Editor`, `Development`, `Shipping`) |

⚠️ **Not verified:** the Windows executables were never launched with a window. GitHub's Windows runners have no Vulkan driver and the sandbox cannot run `.exe` files, so first launch on a real GPU is still untested. The download was also not opened in Chrome.

---

## 2. UV Workspace Audit

Scope: `Engine/Workspaces/UV/`, `Engine/GeometricRaster/UV/`, `Engine/Codecs/UVCodec.*`, `Engine/Lifecycle/FrontierUVLaunch.cpp`, the four UV `.slang` shaders, and the five `Exhibits/UV*Validation.cpp` (43 files, about 11,000 lines).

### 2.1 Banned Vocabulary — Fixed

| Barred form | Replacement | Files |
| :--- | :--- | :--- |
| `RecordUVRevisionStep` | `RegisterUVRevisionStep` | `UVWorkspaceSession.{h,cpp}` |
| `GPURelaxCornerRecord` / `RelaxCornerRecord` | `GPURelaxCornerLayout` / `RelaxCornerLayout` | `ProgressiveStretchRelaxer.cpp`, `StretchRelaxCompute.slang` |
| `UVCanvasVertexRecord` | `UVCanvasVertexLayout` | `UVTileRasterization.slang` |
| `Cached*` members, `RefreshCachedAtlasTelemetry`, `RefreshCachedIslandCount` | `Retained*`, `RefreshChartSheetTelemetry`, `RefreshRetainedIslandCount` | `UVOutlinerPanel`, `UVViewportHost`, `UVWorkspaceRasterizer` |
| `HasFoldedFace`, `HasNumeric`, `IsAct`, `IsActive`, `IsDot`, `IsSign` | `FoldedFaceDetected`, `NumericEntryPresent`, `TileActive`, `LayerActive` / `RailSlotActive`, `DecimalPointTyped`, `NegativeSignTyped` | Solvers, panels, `UVViewportInteractor` |
| `OverlapFlag` | `OverlapIndicator` | `UVTileRasterization.slang` |
| Comment terms: `horizon`, `kernel`, `evaluator`, `Pass`, `matrix`, `flag`, `sentinel`, `Member`, `frame`, `commit`, `State Machine`, `state`, `table`, `palette`, `Proof`, `Compose`, `Composite`, `object`, `binding`, `WIDGETS` | Reworded to the mechanism (`upper boundary`, `iteration`, `inspection`, `Rasterization`, `transform`, `marking`, …) | 20 files |

### 2.2 Banned Vocabulary — Open (owned by other modules)

The audit prints these as advisories instead of hiding them. Renaming them changes non-UV modules, so they are left for a ruling.

| Identifier | Barred word | Declared in | References |
| :--- | :--- | :--- | :---: |
| `SpaceExtensionCategory`, `ExtensionCategory` | `extension` (file-suffix sense, outside the §3.1 lifecycle exemption) | `Engine/Codecs/SpaceContainerCodec.h` | 53 repo-wide |
| `RecordParametricOperation` | bare `Record` | `Engine/Workspaces/Revision/RevisionTracker.h` | 13 repo-wide |

### 2.3 Rest of the Engine (not in scope, measured only)

`python3 Automation/AuditBannedVocabulary.py Engine Exhibits` reports **526 findings** outside the UV scope.

| Module | Findings | | Module | Findings |
| :--- | :---: | :--- | :--- | :---: |
| `Engine/Components` | 189 | | `Engine/GeometrySpace` | 20 |
| `Exhibits` (non-UV) | 106 | | `Engine/Configuration` | 14 |
| `Engine/GeometricRaster` (non-UV) | 49 | | `Engine/Windowing` | 10 |
| `Engine/Workspaces/Revision` | 46 | | `Engine/Threading` | 9 |
| `Engine/Codecs` | 39 | | `Engine/Editor` | 5 |
| `Engine/Interchange` | 25 | | `Engine/Input` | 0 |

Most frequent words: `record` (80), `extension` (74), `base` (35), `cache` / `cached` (58), `Has*` prefix (28), `emission` (27), `table` (26), `info` (21).

### 2.4 Other Governance Checks on the UV Scope

| Rule | Result | Detail |
| :--- | :---: | :--- |
| `CLAUDE.md` §2.1 — overlays on GPU `.slang`, ImGui only for panels | 🟢 | `ImGui::GetWindowDrawList()` appears only in the three panel roots (`UVInspectorPanel`, `UVOutlinerPanel`, `UVViewportHost` chrome) |
| `CLAUDE.md` §2.3 — no OS headers or platform `#if` in UV code | 🟢 | None in scope |
| File banners (`SKILL-Formatting`) | 🟢 | Present on all 43 files |
| 140-column limit | 🚩 | 183 lines over 140 columns, mostly `UVInspectorPanel.cpp` (35) and `UVWorkspaceRasterizer.cpp` (23) |
| `SKILL-Naming` §5 — no shorthand locals | 🚩 | `Idx` ×204, `Rec` ×24, `Cfg` ×4, `Val` ×3 — not renamed in this pass |
| Discouraged `atlas` | 🚩 | `UVBackdropPreset::TrimSheetAtlas` and three inspector labels (discouraged, not barred) |

### 2.5 Defects Found While Building for Windows

| Defect | Fix |
| :--- | :--- |
| 🐞 `WorkspaceAppearance.cpp` `ScaleAlpha` was `constexpr` but called `std::round`, which is not a constant expression in ISO C++20 (GCC accepts it as an extension; MSVC error `C3615`) | Round by adding `0.5f` before truncation |
| 🐞 Shader and content paths were compiled in as absolute build-machine paths, so a copied executable could not find `CompiledShaders/` or `EngineContent/` | `ResolvePackagedLayout` in `DesktopWorkspaceLaunch.cpp` reads them beside the executable when `PackageManifest.json` is present |
| 🐞 `PrepareShaderToolchain.py` pinned only the Linux Slang wheel | Windows hosts pin the `win_amd64` wheel of the same release (`slangc.exe`); SwiftShader stays Linux-only |
| 🐞 CTest failure annotation in `uv-workspace-ci.yml` escaped the two-character text `\n` instead of line breaks, so only the first log line was reported | Reports failing suite names and each suite's own output |

---

## 3. Windows Desktop Package

Workflow: `.github/workflows/windows-desktop-package.yml` (runs on pushes to `arena/**` and `main` that touch `Engine/`, `EngineContent/`, `Automation/`, `CMakeLists.txt`, or the workflow; also manual dispatch once the file is on the default branch).

| Step | What it does |
| :--- | :--- |
| Audit | `Automation/AuditBannedVocabulary.py` — build stops on any UV finding |
| Recover | Pinned packages, fonts, `slangc.exe` (all SHA-256 verified) |
| Build | MSVC x64, `Editor` profile, `Release`, static C runtime (no Visual C++ redistributable needed) |
| Collect | `Automation/PackageDesktopBinaries.py --collect` → executables, `CompiledShaders/`, `EngineContent/`, `ThirdPartyNotices/`, `README.txt`, `PackageManifest.json` |
| Self-check | Each executable runs `--verify-packaged-layout` from another directory |
| Sign | Authenticode, only when the certificate secrets exist |
| Bundle | `SHA256SUMS.txt` + `.zip` |
| Attest | Sigstore build provenance for both executables and the `.zip` |
| Upload | Artifact `FrontierUV-Windows-x64` (30 days) |

**Download:** GitHub → Actions → `Windows desktop package` → latest green run → Artifacts → `FrontierUV-Windows-x64`.

**Verify:** `gh attestation verify FrontierUV.exe --repo SultanAladin/Frontier-1`

📝 The `Editor` profile is packaged because `FrontierUV` and `FrontierEditor` are not built in `Shipping` (`CMakeLists.txt` excludes the UI sources and both executables there). The console window is kept so Vulkan start-up diagnostics stay visible in this preview.

---

## 4. "This file is potentially unsafe" — What Actually Fixes It

Chrome Safe Browsing and Windows SmartScreen decide from two things: **who signed the executable** and **how often that exact file or publisher has been downloaded**. A fresh unsigned `.exe` has neither. GitHub build provenance (what this workflow adds) proves where the binary came from, but browsers and SmartScreen do not read it.

| Rank | Option | Cost | Removes the prompt | What is needed from you |
| :---: | :--- | :--- | :---: | :--- |
| 🥇 | **SignPath Foundation** (free certificate for open-source projects) | Free | 🟢 after approval | Apply at signpath.org; the repository needs an open-source licence file (none exists yet) |
| 🥈 | **Azure Artifact Signing** (formerly Trusted Signing) | About US$10 / month | 🟢 | Azure account + identity validation; availability depends on country and on whether you sign as an organisation or an individual — check eligibility for South Africa first |
| 🥉 | **OV / EV certificate** from a certificate authority (cloud-HSM, e.g. SSL.com eSigner, DigiCert KeyLocker) | About US$200–500 / year | 🟢 (EV immediately, OV after some downloads) | Business or identity validation |
| 🏅 | **No certificate** (current state) | Free | 🔴 prompt can remain | Nothing; download the `.zip` from github.com, verify with `gh attestation verify`, then *Keep* / *More info → Run anyway* |

**Recommendation:** apply to SignPath Foundation (🥇) — it is the only free route that produces a trusted signature.

⚠️ A self-signed certificate does not help: browsers and SmartScreen treat it the same as unsigned.

### Turning signing on with a `.pfx` certificate

1. Repository → Settings → Secrets and variables → Actions → add `WINDOWS_SIGNING_CERTIFICATE_BASE64` (the `.pfx` file, base64-encoded) and `WINDOWS_SIGNING_CERTIFICATE_PASSWORD`.
2. Re-run the workflow. The sign step runs `signtool` with SHA-256 and an RFC 3161 timestamp, then `signtool verify /pa`; `PackageManifest.json` records `"AuthenticodeSigned": true`.

🚧 Certificates issued since mid-2023 keep their private key on a hardware or cloud module and cannot be exported as `.pfx`. SignPath, Azure, and cloud-HSM certificates each need their own signing step in place of the `signtool` call; that step is not written yet because it needs your account details (`BKL-010`).

---

## 5. Repository Notes

| Item | State |
| :--- | :--- |
| `Exhibits/Images/` (242 MB of rendered `.png`) | Not copied into this repository; only empty domain folders. The images remain on `SultanAladin/Frontier` |
| `01a1c57c-3f3e-7d7d-8334-c6944c9015d5.patch` (12 MB, 565 new `Experimental/` files) | Left untouched and **not applied** — its `.gitignore` hunk targets a different base than `f73a5cf`, and it does not touch the UV workspace |
| `.gitattributes` (`* -text`) | Added so Windows checkouts keep patch files and digest-pinned inputs byte-identical |
