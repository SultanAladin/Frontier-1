# Source Formatting — Visual Rules for All Frontier Files

Applies to C++20 headers and translation units (`.h`, `.cpp`), Slang GPU shaders (`.slang`),
CMake/Python automation scripts, and Markdown documents (`.md`). Verify every file against these
rules before marking work complete.

---

## 1. File Header Banner (Required on Every Source & Shader File)

```cpp
/*============================================================================================================================================
                                                        INPUTEXTENSION.H
============================================================================================================================================*/
// 🧩 Input lifecycle coordinator: hardware intake polling and command routing across desktop and mobile surfaces
```

- Top and bottom `=` rulers span **140 columns** (`/*===...===*/` in C++ and `.slang`, `#===...===` in Python/CMake).
- Filename is **ALL CAPS** and **centred** on line 2.
- Immediately beneath the closing ruler: a single summary line prefixed with `// 🧩 `.
- Leave one blank line after the `🧩` summary.

---

## 2. Logical Section Headers

Divide every `.h`, `.cpp`, and `.slang` file into clear architectural sections:

```cpp
//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------
```

- `-` rulers span **120 columns** (`//` + 120 `-` characters).
- Section title is **ALL CAPS**, **centred**, with one blank line above and below.
- Standard section sequence in headers:
  `INCLUDES` → `CONCEPTS` → `CONSTANTS` → `ENUMERATIONS` → `STRUCTURES` → `FUNCTION PROTOTYPES` → `TEMPLATE IMPLEMENTATIONS`.

---

## 3. Aligned Unit-Tagged Comments

- Align trailing `//` comments vertically within each struct or block using **spaces only (never tabs)**.
- Prefix field comments with the physical or logical unit in brackets:
  - Spatial / physical: `[m]`, `[cm]`, `[mm]`, `[px]`, `[rad]`, `[°]`, `[s]`, `[ms]`, `[Hz]`, `[N]`, `[K]`
  - Memory / indexing: `[B]`, `[KiB]`, `[idx]`, `[count]`, `[mask]`, `[0-1]`, `[-]` (dimensionless / discrete)
- Place implementation notes (`// 📝 ...`), rationale (`// 💡 ...`), warnings (`// ⚠️ ...`), and critical invariants (`// 🔴 ...`) on the line above the statement they govern.
- Use circled numerals (`①`–`⑩`) for multi-step algorithmic sequences.

---

## 4. Mathematical & Physical Unicode Scalars (C++20 and `.slang`)

🔴 Name genuine mathematical/physical scalars (`β`, `γ`, `Δt`, `Δτ`, `θ`, `φ`, `ω`, `λ`, `σ`, `ρ`, `μ`)
with their real Unicode Greek symbols rather than degraded ASCII transliterations. Pair them with
full PascalCase geometric vector/structure identifiers:

```cpp
namespace Frontier
{
    struct AngularStepSample
    {
        double θ  = 0.0; // [rad] - Azimuthal angle around reference axis
        double ω  = 0.0; // [rad/s] - Instantaneous angular velocity
        double Δt = 0.0; // [s] - Integration timestep duration
    };
}
```

---

## 5. Emoji Governance

🔴 **`SKILL-Emoji.md` is the sole authority** on permitted glyphs and their semantics. Never introduce
any emoji outside `SKILL-Emoji.md`.

---

## 6. Column Alignment

- **Struct members:** align types, PascalCase field names, `=` default initializers, and `// [unit] - ...` comments into clean vertical columns.
- **Consecutive assignments / `constexpr` tables:** align `=` within each cohesive logical group.
- **Function prototypes with ≥4 parameters:** place one parameter per line with types and parameter names vertically aligned.

---

## 7. Braces, Indentation, and Namespace Layout

- **Brace style:** **Allman** (opening `{` and closing `}` on their own lines, aligned with the control/declaration keyword). Single-line guard clauses (`if (SlotCount == 0) return;`) are permitted when unambiguous.
- **Indentation:** **4 spaces** per level; **tabs are strictly forbidden**.
- **Namespaces:** Keep `namespace Frontier` (and nested domain namespaces such as `namespace Frontier::Input`) **unindented (flat)** so horizontal column budget is preserved for code and aligned comments.

```cpp
namespace Frontier::Input
{

bool ValidateSignalTimestamp(uint64_t MonotonicNanoseconds, uint64_t PreviousNanoseconds) noexcept
{
    if (MonotonicNanoseconds < PreviousNanoseconds)
    {
        return false;
    }
    return true;
}

} // namespace Frontier::Input
```

---

## 8. Blank Lines & Markdown Discipline

- **Blank lines:** 1 between functions; 1 above/below section headers; never more than 1 consecutive blank line inside a function; zero trailing whitespace.
- **Markdown:** Reserve `#` for the single document title (`##` / `###` beneath); always tag fenced code blocks (`cpp`, `slang`, `cmake`, `bash`, `text`); prefer tables over prose paragraphs per `SKILL-CompactOutputProtocol.md`.
