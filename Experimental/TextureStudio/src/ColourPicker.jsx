import React, { useEffect, useRef, useState } from "react";
import { Palette, Shuffle, Check } from "lucide-react";
import { Slider, palettes } from "./MaterialControls.jsx";
import { hexToHSV, hsvToHex } from "./colourConversion.js";
import { COLOUR_GENERATORS, generateColour } from "./colourGenerators.js";
import "./colourPicker.css";
export { hexToHSV, hsvToHex };
export default function ColourPicker({
  value,
  onChange,
  ariaLabel = "Colour",
  disabled = false,
  swatches = [],
  showPaletteAndGenerate = true,
}) {
  const hsv = hexToHSV(value),
    [hue, setHue] = useState(hsv.h),
    [hex, setHex] = useState(value),
    [panel, setPanel] = useState(null),
    pointer = useRef(null);
  useEffect(() => {
    setHex(value);
    const next = hexToHSV(value);
    if (next.s > 0.001 && next.v > 0.001) setHue(next.h);
  }, [value]);
  const pick = (s, v) => onChange(hsvToHex(hue, s, v));
  const sample = (e) => {
    const box = e.currentTarget.getBoundingClientRect();
    pick(
      Math.min(1, Math.max(0, (e.clientX - box.left) / box.width)),
      1 - Math.min(1, Math.max(0, (e.clientY - box.top) / box.height)),
    );
  };
  return (
    <section
      className="editor-colour-picker tp-shared-controls"
      aria-label={`${ariaLabel} picker`}
    >
      <header>
        <Palette size={12} />
        <span>Colour picker</span>
        <small>sRGB</small>
      </header>
      <div
        className="colour-picker-square"
        role="slider"
        tabIndex={disabled ? -1 : 0}
        aria-label={`${ariaLabel} saturation and brightness`}
        aria-disabled={disabled}
        aria-valuemin={0}
        aria-valuemax={100}
        aria-valuenow={Math.round(hsv.s * 100)}
        aria-valuetext={`${Math.round(hsv.s * 100)}% saturation, ${Math.round(hsv.v * 100)}% brightness`}
        style={{ "--hue-colour": hsvToHex(hue, 1, 1) }}
        onPointerDown={(e) => {
          if (disabled || e.button !== 0) return;
          e.preventDefault();
          e.currentTarget.focus();
          pointer.current = e.pointerId;
          e.currentTarget.setPointerCapture(e.pointerId);
          sample(e);
        }}
        onPointerMove={(e) => {
          if (pointer.current === e.pointerId && !disabled) sample(e);
        }}
        onPointerUp={() => (pointer.current = null)}
        onPointerCancel={() => (pointer.current = null)}
        onLostPointerCapture={() => (pointer.current = null)}
        onKeyDown={(e) => {
          if (
            disabled ||
            ![
              "ArrowLeft",
              "ArrowRight",
              "ArrowUp",
              "ArrowDown",
              "Home",
              "End",
            ].includes(e.key)
          )
            return;
          e.preventDefault();
          const step = e.shiftKey ? 0.1 : 0.01;
          pick(
            e.key === "Home"
              ? 0
              : e.key === "End"
                ? 1
                : Math.min(
                    1,
                    Math.max(
                      0,
                      hsv.s +
                        (e.key === "ArrowRight"
                          ? step
                          : e.key === "ArrowLeft"
                            ? -step
                            : 0),
                    ),
                  ),
            Math.min(
              1,
              Math.max(
                0,
                hsv.v +
                  (e.key === "ArrowUp"
                    ? step
                    : e.key === "ArrowDown"
                      ? -step
                      : 0),
              ),
            ),
          );
        }}
      >
        <i
          style={{
            left: `${hsv.s * 100}%`,
            top: `${(1 - hsv.v) * 100}%`,
            background: value,
          }}
        />
      </div>
      <div className="colour-picker-hue">
        <Slider
          label="Hue"
          ariaLabel={`${ariaLabel} hue`}
          value={hue}
          min={0}
          max={360}
          step={1}
          unit="°"
          disabled={disabled}
          onChange={(h) => {
            setHue(h);
            onChange(hsvToHex(h, hsv.s, hsv.v));
          }}
        />
      </div>
      <div className="colour-picker-hex">
        <i style={{ background: value }} />
        <input
          aria-label={ariaLabel}
          disabled={disabled}
          value={hex.toUpperCase()}
          maxLength={7}
          spellCheck={false}
          onChange={(e) => {
            setHex(e.target.value);
            if (/^#[\da-f]{6}$/i.test(e.target.value))
              onChange(e.target.value.toLowerCase());
          }}
          onBlur={() => {
            if (!/^#[\da-f]{6}$/i.test(hex)) setHex(value);
          }}
        />
        <span>HEX</span>
      </div>
      {!!swatches.length && (
        <div className="colour-picker-swatches">
          {swatches.map((c) => (
            <button
              key={c}
              aria-label={`Paint swatch ${c}`}
              aria-pressed={value.toLowerCase() === c.toLowerCase()}
              disabled={disabled}
              title={c}
              style={{ background: c }}
              onClick={() => onChange(c)}
            />
          ))}
        </div>
      )}
      {showPaletteAndGenerate && (
        <>
          <div className="colour-picker-source-tabs" role="group" aria-label={`${ariaLabel} colour source`}>
            <button
              type="button"
              aria-pressed={panel === "palette"}
              disabled={disabled}
              onClick={() => setPanel(panel === "palette" ? null : "palette")}
            >
              <Palette size={11} />
              Palettes
            </button>
            <button
              type="button"
              aria-pressed={panel === "generate"}
              disabled={disabled}
              onClick={() => setPanel(panel === "generate" ? null : "generate")}
            >
              <Shuffle size={11} />
              Generate
            </button>
          </div>
          {panel === "palette" && (
            <div className="colour-picker-palette" role="group" aria-label={`${ariaLabel} palette`}>
              {Object.keys(palettes).map((name) => (
                <div key={name} className="colour-picker-palette-group">
                  <small>{name}</small>
                  <div className="colour-picker-swatches">
                    {palettes[name].map((c) => (
                      <button
                        key={c}
                        aria-label={`${name} ${c}`}
                        aria-pressed={value.toLowerCase() === c.toLowerCase()}
                        disabled={disabled}
                        title={c}
                        style={{ background: c }}
                        onClick={() => onChange(c)}
                      >
                        {value.toLowerCase() === c.toLowerCase() && <Check size={10} />}
                      </button>
                    ))}
                  </div>
                </div>
              ))}
            </div>
          )}
          {panel === "generate" && (
            <div className="colour-picker-generate" role="group" aria-label={`${ariaLabel} generators`}>
              {COLOUR_GENERATORS.map((generator) => (
                <button
                  type="button"
                  key={generator.id}
                  disabled={disabled}
                  title={generator.note}
                  onClick={() => onChange(generateColour(generator.id, value, Date.now() + Math.random() * 1e9))}
                >
                  <span
                    className="colour-picker-generate-swatch"
                    style={{ background: generateColour(generator.id, value, generator.id.length * 97) }}
                  />
                  {generator.label}
                </button>
              ))}
            </div>
          )}
        </>
      )}
    </section>
  );
}
