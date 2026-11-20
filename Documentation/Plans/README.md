# 📝 `Documentation/Plans/` — Architectural Plans, Reports & Markdown Studio

> **TL;DR**: `Documentation/Plans/` stores architectural specifications, audit reports, and validation evidence for this branch.

---

## 🧩 1. Architecture & System Plans

| Plan Document | Domain |
| :--- | :--- |
| [WorldGiShadowPathTracerPlan.md](WorldGiShadowPathTracerPlan.md) | GTX GI candidates (2), deterministic path tracer (GTX + RTX), solar shadow set, SDF retirement, measured candidate evidence |

---

## 🧩 2. Validation Evidence

| Artifact | Purpose |
| :--- | :--- |
| [`tools/LightingCandidateBench/`](../../tools/LightingCandidateBench/README.md) | Runnable measurement harness: shadow and GI candidate RMSE versus a deterministic path-traced reference on real triangle geometry |
| [`tools/LightingCandidateBench/results/`](../../tools/LightingCandidateBench/results) | Comparison sheets, CSV metrics, JSON summary, and determinism hashes |
