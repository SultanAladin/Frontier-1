import React, { useState } from "react";
import { Palette, Shuffle, Check } from "lucide-react";
import { ColorField, palettes } from "./MaterialControls.jsx";
import { COLOUR_GENERATORS, generateColour } from "./colourGenerators.js";
import "./colourSourceField.css";

// Reusable solid/palette/generator colour source picker. Wraps the existing compact ColorField
// swatch and hex input so every call site keeps emitting a flat hex string — no document schema
// change. The same palette set and generators back the full ColourPicker overlay, so a value
// built from a palette swatch or a generator suggestion here looks identical there.
export function ColourSourceField({
  label,
  ariaLabel,
  value,
  onChange,
  disabled = false,
  paletteNames = Object.keys(palettes),
  generators = COLOUR_GENERATORS,
}) {
  const [open, setOpen] = useState(null);
  return (
    <div className="colour-source-field">
      <ColorField label={label} ariaLabel={ariaLabel} value={value} onChange={onChange} disabled={disabled} />
      <div className="colour-source-tabs" role="group" aria-label={`${label} colour source`}>
        <button
          type="button"
          aria-pressed={open === "palette"}
          disabled={disabled}
          onClick={() => setOpen(open === "palette" ? null : "palette")}
        >
          <Palette size={11} />
          Palette
        </button>
        <button
          type="button"
          aria-pressed={open === "generate"}
          disabled={disabled}
          onClick={() => setOpen(open === "generate" ? null : "generate")}
        >
          <Shuffle size={11} />
          Generate
        </button>
      </div>
      {open === "palette" && (
        <div className="colour-source-palette" role="group" aria-label={`${label} palette`}>
          {paletteNames.map((name) => (
            <div key={name} className="colour-source-palette-group">
              <small>{name}</small>
              <div className="colour-source-swatches">
                {palettes[name].map((hex) => (
                  <button
                    type="button"
                    key={hex}
                    aria-label={`${name} ${hex}`}
                    aria-pressed={value?.toLowerCase() === hex.toLowerCase()}
                    style={{ background: hex }}
                    disabled={disabled}
                    onClick={() => onChange(hex)}
                  >
                    {value?.toLowerCase() === hex.toLowerCase() && <Check size={10} />}
                  </button>
                ))}
              </div>
            </div>
          ))}
        </div>
      )}
      {open === "generate" && (
        <div className="colour-source-generate" role="group" aria-label={`${label} generators`}>
          {generators.map((generator) => (
            <button
              type="button"
              key={generator.id}
              disabled={disabled}
              title={generator.note}
              onClick={() => onChange(generateColour(generator.id, value, Date.now() + Math.random() * 1e9))}
            >
              <span
                className="colour-source-generate-swatch"
                style={{ background: generateColour(generator.id, value, generator.id.length * 97) }}
              />
              {generator.label}
            </button>
          ))}
        </div>
      )}
    </div>
  );
}
