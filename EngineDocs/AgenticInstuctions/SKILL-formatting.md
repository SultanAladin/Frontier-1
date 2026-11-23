# Source Formatting — Visual Rules for All Files

Applies to C/C++ (`.h`, `.cpp`), shaders (`.vert`, `.frag`, `.comp`, `.glsl`), and Markdown (`.md`).
Formatting must stay consistent, structured, readable. Prioritise readability, structural clarity,
visual consistency, maintainability. Check a file against every rule here before calling it complete.

## 1. File Header — required on every source file

```cpp
/*====================================================================================================================================
                                                        DEVICE.CPP
====================================================================================================================================*/
// 🧩 Vulkan device provisioning and queue configuration
```
Ruler uses `=`, min width **140**, filename **centred**. Line 2 = single module description prefixed
`🧩`. One blank line after the header.

## 2. Section Headers — divide every file into logical sections

```cpp
//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCT DEFINITIONS
//------------------------------------------------------------------------------------------------------------------------
```
Rulers use `-`, width **120** (min 80). Titles **ALL CAPS**, **centred**, one blank line before/after.
Common: INCLUDES · CONSTANTS · TYPES · STRUCTS · FORWARD DECLARATIONS · PUBLIC FUNCTIONS · INTERNAL
FUNCTIONS · INPUT/OUTPUT STRUCTURES · PUSH CONSTANTS · MAIN.

## 3. Comments

- **Inline (aligned, unit-tagged):** align all `//`; unit tag first in `[]`. Format
  `[type] Name; // [unit] - description`. Units: `[cm] [px] [ms] [Hz] [rad] [°] [B] [idx] [0-1] [-]`.
- **Implementation note 📝** — above the code, short, explains non-obvious logic.
- **Markers:** governed by `SKILL-Emoji.md` — see §5.
- **Ordered steps:** one per line with `①–⑩`.

## 4. Math / Physics Variables — use Greek / Unicode symbols

🔴 Name math and physics quantities with their real Greek/Unicode symbols (β, γ, Δτ, 𝑣, 𝑐, θ, ω, …),
not ASCII transliterations. This is the one sanctioned exception to PascalCase identifiers.

```cpp
namespace Frontier
{
    // 📝 Relativistic sample: velocity ratio β and derived Lorentz factor γ. β satisfies |β| < 1; γ ≥ 1 always.
    struct LorentzSample
    {
        double β = 0.0;   // [-] - Velocity ratio v/c (dimensionless, |β| < 1)
        double γ = 1.0;   // [-] - Lorentz factor 1/√(1 − β²) (dimensionless, ≥ 1)
    };

    // Evaluate the Lorentz factor γ from a speed 𝑣 (m/s) against the speed of light 𝑐 (m/s). Clamps β so the radical stays real.
    inline LorentzSample EvaluateLorentzFactor(double 𝑣, double 𝑐)
    {
        constexpr double βCeiling = 0.999999999;                    // [-] - Keeps 1 − β² strictly positive at the limit
        double β = 𝑣 / 𝑐;                                           // [-] - Raw velocity ratio
        if (β >  βCeiling) β =  βCeiling;
        if (β < -βCeiling) β = -βCeiling;

        double γ = 1.0 / std::sqrt(1.0 - β * β);                    // [-] - Derived Lorentz factor
        return LorentzSample{ β, γ };
    }
}
```

## 5. Emoji

🔴 **`SKILL-Emoji.md` is the single authority** — which glyphs exist, what each means, and where each
may appear. Introduce **no** emoji outside it. Nothing about emoji is restated here.

## 6. Alignment

- **Assignments:** align `=` within a block; reset on a new logical block.
- **Struct fields:** align type names, field names, and comments into columns (spaces, never tabs).
- **Parameters:** ≥4 params → one per line, types and names aligned vertically.

## 7. Braces & Spacing — Allman

```cpp
void InitializeDevice(DeviceAssembly& Device, const InstanceAssembly& Instance)
{
    if (Result != Success)
    {
        throw std::runtime_error(Label);
    }
}
```
Single-line conditionals may omit braces: `if (Count == 0) return;`.

## 8. Blank Lines, Indentation, Markdown

- **Blank lines:** 1 between functions · 1 before/after section headers · 2 between major section
  groups · never >1 inside a function · no trailing whitespace.
- **Indentation:** 4 spaces per level · **tabs forbidden** · namespace contents stay **flat**.
- **Markdown:** `#` reserved for the file title only (use `##`/`###` below) · `-` bullets · align
  table columns with spaces · language tags on code blocks · max line width **120**.
