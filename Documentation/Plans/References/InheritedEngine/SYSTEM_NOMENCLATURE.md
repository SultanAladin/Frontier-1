# SYSTEM_NOMENCLATURE

🧩 The engine's living naming-governance record for the **target** tree (`Projects/Frontier`). Three
standing sections: **Exceptions** (rule carve-outs the user has authorized), **Suggestions** (proposed
names / conventions awaiting a ruling), and **Banned Words** (the local do-not-use list). The master
rules still live in `EngineDocs/AgenticInstuctions/SKILL-Naming.md`; this file records where the target
tree deviates, proposes, or reinforces on top of them. Newest entries on top within each section.

---

## 1 · Exceptions (authorized carve-outs)

Rulings that permit something the master naming rules would otherwise forbid. Each entry names the
**exact scope** — an exception is never a precedent beyond the item it names.

| # | Term / rule waived | Scope (where it is allowed) | Authority | Notes |
|---|---|---|---|---|
| E1 | `Provider` (banned §1.6 job-title) | **only** the folder `Internal/Platform/InputProvider/` | User, 2026-07-26 | Not a precedent. `Provider` stays banned for every other identifier, type, field, and folder. The component inside is `InputEncoder`; the exemption covers the folder name alone. |

## 2 · Suggestions (proposed, awaiting ruling)

Names / conventions put forward but not yet confirmed. Move an entry to §1 (if it becomes an
authorized exception) or into the codebase (if it just becomes the convention) once ruled on.

| # | Proposal | Rationale | Status |
|---|---|---|---|
| S1 | Edge/duration input query verbs `KeyPressed` / `KeyReleased` / `KeyHeldNow` / `KeyHeldDurationCount` | Avoids banned `Is` prefix, past-tense `Was`, and banned word `Frame`; reads as plain edge + level queries. Held duration counts in *integrations* (`…HeldDurationCount`). | ✅ adopted in `InputEncoder` |
| S2 | `InputPacket` as the per-poll decoded-input bundle (replaced `InputSnapshot`) | "Snapshot" undesired; "Packet" names the data bundle accurately without overclaiming (unlike `Tensor` / `Stream`). Packet-level level reads are `PacketKeyHeld` / `PacketButtonHeld`. | ✅ adopted across the input seam |
| S3 | Whole Platform seam wrapped in `namespace Frontier` (`InputPacket.h`, `PlatformWindow.h`, all backends) | CLAUDE.md: "everything in `namespace Frontier`". ⚠️ When the application / main loop is ported it must `using namespace Frontier;` or qualify the seam calls (`InitializePlatformWindow`, `PollPlatformEvents`, …) — they are no longer in the global namespace. | ✅ applied; app-side follow-up pending |

## 2.5 · Approved Names (governance-document / policy titles)

Reserved ALL-CAPS names for engine-wide governance, policy, and taxonomy documents. **These titles are
exempt from the §3 code banned-word rules** — they name policy documents, not code identifiers, so
`SYSTEM`, `GLOBAL`, and similar are permitted *here only*. A code identifier, type, folder, or field
may never borrow one of these words on the strength of this list.

| Approved name | Intended role |
|---|---|
| `SYSTEM_NOMENCLATURE.md` | This file — the naming-governance record (exceptions · suggestions · banned words · approved names). |
| `GLOBAL_IDENTIFIER_POLICY.md` | Engine-wide identifier rules (casing, prefixes, the PascalCase source of truth). |
| `ARCHITECTURAL_VOCABULARY.md` | The domain vocabulary — approved nouns/verbs grounded in geometry, topology, rendering. |
| `SKILL_NAMING_STANDARDS.md` | The naming-standards skill reference (target-tree companion to `SKILL-Naming.md`). |
| `GLOBAL_TAXONOMY` | The engine's classification scheme (pillars, subsystems, record classifications). |
| `DEVELOPMENT_DICTIONARY.md` | Term → definition glossary for contributors. |
| `GLOBAL_SIGNAL_ROUTER` | The engine-wide signal / event routing policy (was `GLOBAL_SIGNAL_R`). |
| `SYSTEM_FAULT` | The fault / diagnostic taxonomy (error classes, severities, reporting policy). |

> ⚠️ `SYSTEM` (in `SYSTEM_NOMENCLATURE` / `SYSTEM_FAULT`) and `GLOBAL` are otherwise **banned for code**
> per §3. Their appearance above is an authorized, document-title-only exemption — do not read it as
> lifting the code ban.

## 3 · Banned Words (local do-not-use list)

Words barred in the **target** tree, on top of the master `SKILL-Naming.md` blacklist. If a word here
duplicates the master list it is repeated deliberately because it slips through often. Never use these
as identifiers, filenames, folder names, comment concepts, or doc headings.

| Word | Why barred | Use instead |
|---|---|---|
| `Backend` | Generic "layer beneath" jargon; names nothing about what it ingests | `…Substrate` (raw ingest) / `Decode…` (the act); for the input folder, the authorized `InputProvider` (E1) |
| `Snapshot` | Vague point-in-time metaphor; user-disliked | `…Packet` (a bundled unit of state per poll) |
| `Frame` | Master-blacklisted; slips into unit comments (`per-frame`, `this frame`) | `integration` / `poll` (the actual cadence word for the thing) |
| `Is`-prefixed booleans | Master rule: booleans avoid `is` | state-noun form: `…HeldNow`, `…Enabled`, `…Condition` |
| `Was`-prefixed queries | Past-tense state word | plain edge verb: `Pressed` / `Released` |
| `Tick` (as a duration unit) | Ambiguous (clock tick vs. loop tick); tried and dropped here | `integration` — one `IntegrateInputPacket` advance |

---

### Maintenance

- One running file — append within the right section, prune entries that are superseded or landed.
- When an exception (§1) or banned word (§3) is added, cross-link the affected component's local `.md`
  back to the relevant row here (e.g. `InputEncoder.md` → E1).
- The master document (`SKILL-Naming.md`) always wins on anything not explicitly carved out here.
