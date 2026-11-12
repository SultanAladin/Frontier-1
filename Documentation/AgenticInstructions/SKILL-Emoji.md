<!--PARSE:EMOJI-RULES v1.1
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
applies_to               = .h,.cpp,.slang,.md
PARSE:END-->

# Emoji Skill Document

## Approved Emoji Set — Where, When, and How to Use Them

This document is the **single authority** on which emojis may appear anywhere in **Frontier** —
C++20 headers/sources (`.h`, `.cpp`), Slang shaders (`.slang`), Markdown docs (`.md`), chat,
plans, and decision tables. `SKILL-Formatting.md` §5 defers to this file entirely.

🔴 **Only the emojis listed here may be used. Do not introduce any emoji outside this document.**

---

## 1. Core Set (Usable Everywhere)

Each marker carries a fixed technical meaning — use it for its meaning, never for decoration.

| Emoji | Name         | Meaning / When to Use                                              | Where                  |
| ----- | ------------ | ------------------------------------------------------------------ | ---------------------- |
| 🧩    | Puzzle       | File-header summary — the single line beneath the `=` banner       | File headers only      |
| 📝    | Memo         | Implementation note — explains non-obvious logic above the code    | Comments, docs         |
| 💡    | Bulb         | Design rationale — *why* a mechanism is structured this way        | Comments, docs         |
| ⚠️    | Warning      | Caution — ordering constraint, platform nuance, or lifetime hazard | Comments, docs         |
| 🔴    | Red circle   | Critical / mandatory — hard invariant or blocking defect           | Comments, docs, chat   |
| 🐞    | Ladybug      | Defect / bug marker                                                | Comments, docs         |
| 🐛    | Caterpillar  | Defect / bug marker — interchangeable with 🐞                      | Comments, docs         |
| 🚧    | Construction | Incomplete / deferred work (cross-reference `Backlog.md`)          | Comments, docs         |
| 🔍    | Magnifier    | Inspection, profiling, or diagnostic concern                       | Comments, docs         |
| 🚩    | Red flag     | Priority / breaking-change marker (also medium rating, see §5)     | Comments, docs, tables |
| ✔️    | Check        | Verified / compliant (also low rating, see §5)                     | Comments, docs, tables |

### 1.1 File Header Usage

```cpp
/*============================================================================================================================================
                                                        INPUTEXTENSION.H
============================================================================================================================================*/
// 🧩 Input lifecycle coordinator: hardware intake polling and command routing across desktop and mobile surfaces
```

### 1.2 Comment Usage

```cpp
// 📝 Normalize pointer coordinates into [0, 1] surface space before DPI scaling
// 💡 Fixed-capacity ring storage avoids heap allocation during high-frequency touch bursts
// ⚠️ Initialization-time registration only — never mutate during active tick
// 🔴 CRITICAL: Destroy GPU resources in reverse allocation order
// 🐛 Deferred Wayland relative-pointer warp — tracked in Documentation/Backlog.md
```

---

## 2. Status Dots (Green / Red Only)

🔴 **Only red and green dots are permitted.** No amber, yellow, blue, or other colour dots.

| Emoji | Meaning                        |
| ----- | ------------------------------ |
| 🔴    | Failing / critical / disabled  |
| 🟢    | Passing / verified / enabled   |

---

## 3. Flags

| Emoji | Name          | Typical Use                                |
| ----- | ------------- | ------------------------------------------ |
| 🚩    | Red flag      | Priority / breaking change / medium rating |
| 🏳️    | White flag    | Neutral marker / cleared                   |
| 🏴    | Black flag    | Blocked / halted                           |
| 🏁    | Chequered     | Milestone / baseline complete              |
| 🎌    | Crossed flags | Grouped milestone                          |

---

## 4. Medals, Honours, and Tag

| Emoji | Name   | Typical Use                        |
| ----- | ------ | ---------------------------------- |
| 🏆    | Trophy | Overall winner                     |
| 🥇    | Gold   | First / recommended                |
| 🥈    | Silver | Second                             |
| 🥉    | Bronze | Third                              |
| 🏅    | Medal  | Notable mention                    |
| 🎖️    | Honour | Distinguished mention              |
| 🏷️    | Tag    | Labelling or tagging an item       |

---

## 5. Decision Table Ratings

In decision tables, state metric polarity in the legend (e.g., **Cost/Complexity: ✔️ = lowest/best**; **Portability/Safety: 🔴 = highest/best**).

| Glyph | Rating Meaning                                              |
| ----- | ----------------------------------------------------------- |
| ✔️    | Low                                                         |
| 🚩    | Medium                                                      |
| 🔴    | High (for positive metrics, 🔴 = **best** — state in legend) |
| ⭐     | Quality star — more ⭐ = better (**plain** `⭐` only)        |

🔴 **Star rule:** Use the **plain `⭐` only**. Fancy stars `🌟` and `✨` are **strictly forbidden**.

---

## 6. Forbidden

🔴 **Never use:**
- `🌟`, `✨` — sparkle/fancy stars.
- Any dot colour other than `🔴` or `🟢`.
- Any emoji outside this document.
