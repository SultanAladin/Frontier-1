# 🧩 `Engine/Editor/` — Editor Explorer

> **TL;DR**: `Engine/Editor/` (compiled **only** under `FRONTIER_PROFILE_EDITOR=1`; excluded from `Development` and `Shipping`) houses **`Explorer/`** (`ProjectBrowser` and `ContentBrowser`).

---

## 🧩 1. Module Architecture

| Directory | Files | Responsibilities |
| :--- | :--- | :--- |
| **`Explorer/`** | `ProjectBrowser.{h,cpp}`, `ContentBrowser.{h,cpp}` | Launcher/project hub (`ProjectBrowser`: theme toggle, search, sort, revision/zoom/fit bar, project thumbnails) and editor wrapper around `ContentBrowserPanel` |

---

## 🏷️ 2. UI Controls (`Explorer/`)

| Module | Controls | Function |
| :--- | :--- | :--- |
| **`ProjectBrowser`** | Theme segmented toggle · `TextInputField` search · `DropdownSelector` sort · View / Revision / Zoom / Fit bar · Project tiles | Create, open, filter, sort, and inspect `.ProjectSpace` projects |
| **`ContentBrowser` / `ContentBrowserPanel`** | Origin Scope tabs (`All` / `Project` / `Imported` / `Engine`) · Category rail · Search · Layout switcher · Thumbnail size slider · Colour filters · Drag-and-drop zones | Browse `.Space` assets and materials |

---

## 📝 3. Hotkeys & Mouse Controls

| Context | Input | Action |
| :--- | :--- | :--- |
| **`ProjectBrowser`** | `Esc` / `Enter` | Dismiss modal / Open selected project |
| **`ContentBrowserPanel`** | `Ctrl+Space` / `Ctrl+F` / `Esc` | Toggle drawer / Focus search / Clear search or close |
| **`ContentBrowserPanel`** | `Left` / `Right` / `Home` / `End` / `Enter` / `Del` / `Backspace` | Navigate & open/delete assets |

---

## 🔍 4. C++ Naming Violations Audit (Read-Only)

⚠️ Scanned `4` files under `Engine/Editor/Explorer/`. **Zero C++ edits were made.**

| Directory | Prohibited `<K-suffix>` | Offending Counts & Examples | Compliant Replacement |
| :--- | :---: | :--- | :--- |
| **`Explorer/`** | 0 | `Get/Set` (`17`), `Palette` (`15`), `Data` (`13`), `Item` (`6`), `Frame` (`6`), `Context` (`1`) | `Query*`/`Assign*`, `ColorCodex`, `Payload`, `Entry`, `Extent`, `Session` |
