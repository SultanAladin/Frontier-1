# 🧩 `Engine/Workspaces/Revision/` — Non-Linear Document Revision History

> **TL;DR**: `Engine/Workspaces/Revision/` implements Frontier's resolution-independent branching DAG revision engine enforcing the `215 MiB` per-document and `750 MiB` global memory ceilings, prefix folding, redo purge, and cross-tab sequence undo/redo.

---

## 🧩 1. Module Architecture

| File | Role |
| :--- | :--- |
| `ContentRegion.{h,cpp}` | Bounded binary/payload region snapshot backing revision deltas |
| `NonLinearHistory.{h,cpp}` | Branching DAG storage using index links (`EnclosingIndex`, `FirstNestedIndex`, `NextPeerIndex` — zero kinship nouns) |
| `OperationRecorder.{h,cpp}` | Records typed authoring operations with automatic `Redo Purge -> Prefix Fold` compaction |
| `CursorResolution.{h,cpp}` | Arbitrary-resolution cursor replay across branching checkpoints |
| `RevisionTracker.{h,cpp}` | Per-document revision ledger (`215 MiB` cap) wired into `WorkspaceDocument` and `UVWorkspace` |
| `RevisionExtension.{h,cpp}` | Designated lifecycle participant enforcing the `750 MiB` global ceiling and cross-tab sequence undo/redo |

---

## 🔍 2. C++ Naming Violations Audit (Read-Only)

⚠️ Scanned `12` files under `Engine/Workspaces/Revision/`. **Zero C++ edits were made.**

| Category | Count | Offending Examples (File & Symbol) | Compliant Replacement |
| :--- | :---: | :--- | :--- |
| **Prohibited `<K-suffix>`** | **0** | None detected in `Engine/Workspaces/Revision/` | 🟢 |
| **`Record`** | 162 | `OperationRecorder` & `RecordToken` / `RecordEntry` composite uses; any bare `Record` occurrences | Composite `RecordEntry` / `RecordToken` / `Ledger` (keep `Recorder` clean) |
| **`Base`** | 33 | `Base*` identifiers in `ContentRegion` / `NonLinearHistory` | `Origin` / `Root` / `Primary` |
