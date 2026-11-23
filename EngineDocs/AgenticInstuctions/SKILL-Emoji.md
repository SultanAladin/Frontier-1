<!--PARSE:EMOJI-RULES v1.0
# Machine-readable emoji index. Stable keys; one rule per line: KEY = VALUE.
# Parsers: split on first " = ". VALUEs are literal; lists are comma-separated.
emoji.core.allowed       = 🧩,📝,💡,⚠️,🔴,🐞,🐛,🚧,🔍,🚩,✔️
emoji.dots.allowed       = 🔴,🟢
emoji.flags.allowed      = 🚩,🏳️,🏴,🏁,🎌
emoji.medals.allowed     = 🏆,🥇,🥈,🥉,🏅,🎖️
emoji.star.allowed       = ⭐
emoji.star.forbidden     = 🌟,✨
emoji.tag.allowed        = 🏷️
emoji.forbidden          = 🌟,✨
marker.module            = 🧩
marker.note              = 📝
marker.insight           = 💡
marker.warning           = ⚠️
marker.critical          = 🔴
marker.ok                = 🟢
marker.bug               = 🐞,🐛
marker.breaking          = 🚩
marker.tag               = 🏷️
rating.low               = ✔️
rating.medium            = 🚩
rating.high              = 🔴
rating.star              = ⭐
applies_to               = .h,.cpp,.vert,.frag,.comp,.glsl,.md
PARSE:END-->

# Emoji Skill Document

## Approved Emoji Set — Where, When, and How to Use Them

This document is the **single authority** on which emojis may appear anywhere in the project —
source comments, shader comments, Markdown docs, chat, plans, decision tables, filenames-context,
and doc headings. `SKILL-formatting.md` §5 defers to this file entirely.

🔴 **Only the emojis listed here may be used. Do not introduce any emoji outside this document.**

---

# 1. Core Set (usable everywhere)

These are the everyday markers for comments and documentation. Each carries a fixed meaning — use
the emoji for its meaning, not for decoration.

| Emoji | Name          | Meaning / when to use                                              | Where |
| ----- | ------------- | ------------------------------------------------------------------ | ----- |
| 🧩    | Puzzle        | File-header module description — the one line after the `=` banner | File headers only |
| 📝    | Memo          | Implementation note — explains non-obvious logic, placed above it  | Comments, docs |
| 💡    | Bulb          | Design insight / rationale — *why* it is done this way             | Comments, docs |
| ⚠️    | Warning       | Caution — a gotcha, fragile assumption, or ordering constraint     | Comments, docs |
| 🔴    | Red circle    | Critical / mandatory — hard requirement or blocking problem        | Comments, docs, chat |
| 🐞    | Ladybug       | Bug / defect marker                                                | Comments, docs |
| 🐛    | Caterpillar   | Bug / defect marker — interchangeable with 🐞                      | Comments, docs |
| 🚧    | Construction  | Work in progress / incomplete                                      | Comments, docs |
| 🔍    | Magnifier     | Inspection / lookup / debug concern                                | Comments, docs |
| 🚩    | Red flag      | Priority / breaking-change marker (also = medium rating, see §5)   | Comments, docs, tables |
| ✔️    | Check         | Validation success / done (also = low rating, see §5)              | Comments, docs, tables |

## 1.1 Usage in a file header

```cpp
/*====================================================================================================================================
                                                        DEVICE.CPP
====================================================================================================================================*/
// 🧩 Vulkan device provisioning and queue configuration
```

## 1.2 Usage in comments

```cpp
// 📝 Transpose inverse for correct non-uniform scale normal transform
// 💡 Mailbox provides triple-buffering on most drivers
// ⚠️ Initialization-time only — do not call during main loop
// 🔴 CRITICAL: Destroy resources in reverse creation order
// 🐛 Off-by-one on the final tile row — see Backlog
```

---

# 2. Status Dots (green / red only)

🔴 **Only red and green dots are permitted.** No amber/yellow/blue/other-colour dots.

| Emoji | Meaning                        |
| ----- | ------------------------------ |
| 🔴    | Bad / failing / critical / off |
| 🟢    | Good / passing / healthy / on  |

Use for pass–fail status lines and health summaries:

```
Build     🟢  passing
Tests     🔴  3 failing
Validation 🟢  clean
```

---

# 3. Flags (all coloured flags allowed)

Every flag glyph below is permitted. 🚩 keeps its dual role as a priority marker and as the
medium rating in decision tables (§5).

| Emoji | Name          | Typical use                          |
| ----- | ------------- | ------------------------------------ |
| 🚩    | Red flag      | Priority / breaking change / medium rating |
| 🏳️    | White flag    | Neutral marker / cleared             |
| 🏴    | Black flag    | Blocked / stop                       |
| 🏁    | Chequered     | Finished / milestone reached         |
| 🎌    | Crossed flags | Grouped milestone                    |

---

# 4. Medals, Badges, and the Tag

These are **no longer table-only** — they may be used anywhere (chat, docs, comments).

| Emoji | Name        | Typical use                          |
| ----- | ----------- | ------------------------------------ |
| 🏆    | Trophy      | Overall winner                       |
| 🥇    | Gold        | First / best                         |
| 🥈    | Silver      | Second                               |
| 🥉    | Bronze      | Third                                |
| 🏅    | Medal       | Notable mention                      |
| 🎖️    | Honour      | Distinguished mention                |
| 🏷️    | Tag / label | Labelling or tagging an item — **allowed** (previously banned) |

---

# 5. Ratings (decision tables)

Ratings use a fixed glyph vocabulary. In a decision table, more is worse for Cost/VRAM/Effort and
more is better for Accuracy — always state the direction in the legend.

| Glyph | Rating meaning                                                        |
| ----- | --------------------------------------------------------------------- |
| ✔️    | low                                                                   |
| 🚩    | medium                                                                |
| 🔴    | high (for Accuracy, 🔴 = **best** — state direction in the legend)    |
| ⭐     | quality star — more ⭐ = better (use the **plain** star only)         |

🔴 **Star rule:** ratings use the **plain ⭐ only**. The fancy stars 🌟 and ✨ are **forbidden** —
never use them anywhere.

## 5.1 Example comparison table

Ranking screen-space AO techniques. **Accuracy: 🔴 = best.** Cost = % of a 16.6 ms (60 fps) frame
@ 1440p, measured on the editor viewport.

| Option   | Cost (GPU) | VRAM | Accuracy | Effort | Quality  | Rank |
| -------- | ---------- | ---- | -------- | ------ | -------- | ---- |
| GTAO     | 🔴         | 🚩   | 🔴       | 🔴     | ⭐⭐⭐⭐⭐ | 🥇 🏆 |
| HBAO+    | 🚩         | 🚩   | 🚩       | 🚩     | ⭐⭐⭐⭐  | 🥈   |
| SSAO     | ✔️         | ✔️   | ✔️       | ✔️     | ⭐⭐⭐   | 🥉   |
| Disabled | ✔️         | ✔️   | —        | ✔️     | ⭐       | 🏁   |

⭐ **Recommendation:** GTAO — best accuracy, cost within budget at target resolution.
🏷️ *tagged for Phase 2 render work.*

---

# 6. Forbidden

🔴 **Never use:**

- 🌟 ✨ — fancy / sparkle stars (use the plain ⭐ instead).
- Any dot colour other than 🔴 / 🟢.
- Any emoji not listed in this document.

📝 The `PARSE:EMOJI-RULES` header at the top of this file is the quick reference — one line per
category, machine-readable. No prose table restates it.
