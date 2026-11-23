import React, { useState, useEffect, useRef, useMemo } from "react";
import { Code2, X, Check, Copy } from "lucide-react";
import { sanitizePatternSVG } from "./patternImport.js";
const example = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 200">
  <defs>
    <linearGradient id="copper" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#e2ba85"/>
      <stop offset="1" stop-color="#8d513c"/>
    </linearGradient>
  </defs>
  <path d="M100 15 C170 45 185 120 100 185 C15 120 30 45 100 15Z"
        fill="url(#copper)" stroke="#263c48" stroke-width="4"/>
</svg>`;
export default function SVGSourceEditor({
  source,
  onApply,
  onClose,
  editing = false,
}) {
  const [draft, setDraft] = useState(source || example),
    [preview, setPreview] = useState(source || example),
    [message, setMessage] = useState(""),
    [valid, setValid] = useState(false);
  const input = useRef(null),
    root = useRef(null),
    previous = useRef(null);
  const src = useMemo(
    () => `data:image/svg+xml;charset=utf-8,${encodeURIComponent(preview)}`,
    [preview],
  );
  useEffect(() => {
    previous.current = document.activeElement;
    input.current?.focus();
    return () => previous.current?.focus();
  }, []);
  useEffect(() => {
    setValid(false);
    const t = setTimeout(() => validate(false), 250);
    return () => clearTimeout(t);
  }, [draft]);
  function validate(report = true) {
    try {
      const clean = sanitizePatternSVG(draft);
      setPreview(clean);
      setValid(true);
      setMessage(report ? "Valid, self-contained SVG." : "");
      return clean;
    } catch (e) {
      setMessage(e.message);
      setValid(false);
      return null;
    }
  }
  function apply() {
    const clean = validate();
    if (clean && onApply(clean) !== false) onClose();
  }
  function keys(e) {
    e.stopPropagation();
    if (e.key === "Escape") {
      e.preventDefault();
      onClose();
    }
    if ((e.ctrlKey || e.metaKey) && e.key === "Enter") {
      e.preventDefault();
      apply();
    }
    if (e.key === "Tab") {
      const all = [
          ...root.current.querySelectorAll("button,textarea,[tabindex='0']"),
        ].filter((n) => !n.disabled),
        first = all[0],
        last = all.at(-1);
      if (e.shiftKey && document.activeElement === first) {
        e.preventDefault();
        last.focus();
      } else if (!e.shiftKey && document.activeElement === last) {
        e.preventDefault();
        first.focus();
      }
    }
  }
  return (
    <div
      className="se-svg-backdrop"
      onPointerDown={(e) => {
        if (e.target === e.currentTarget) onClose();
      }}
    >
      <section
        ref={root}
        className="se-svg-dialog"
        role="dialog"
        aria-modal="true"
        aria-label="SVG source editor"
        onKeyDown={keys}
      >
        <header>
          <div>
            <span className="pe-kicker">EMBEDDED VECTOR SOURCE</span>
            <h2>
              <Code2 size={20} />
              {editing ? "Edit SVG artwork" : "Paste SVG artwork"}
            </h2>
          </div>
          <button aria-label="Close SVG source editor" onClick={onClose}>
            <X size={18} />
          </button>
        </header>
        <p className="pe-hint">
          Paste a complete SVG. Shapes, gradients, masks, clips, local
          definitions, text and presentation styles are supported. Executable
          scripts, events and external URLs are not.
        </p>
        <div className="se-svg-layout">
          <label>
            SVG markup
            <textarea
              ref={input}
              aria-label="SVG markup"
              spellCheck={false}
              maxLength={500001}
              value={draft}
              onChange={(e) => setDraft(e.target.value)}
            />
          </label>
          <div className="se-svg-preview">
            <img src={src} alt="Validated SVG artwork preview" />
            <small>{valid ? "Validated preview" : "Last valid preview"}</small>
          </div>
        </div>
        <p
          className={valid ? "se-svg-valid" : "pe-error"}
          role={valid ? "status" : "alert"}
        >
          {message ||
            `${draft.length.toLocaleString()} / 500,000 characters · static, self-contained SVG`}
        </p>
        <footer>
          <button onClick={() => validate()}>
            <Check size={14} />
            Validate SVG
          </button>
          <button
            onClick={() => {
              setDraft(example);
              setMessage("");
            }}
          >
            <Copy size={14} />
            Load example
          </button>
          <span>Ctrl/⌘ Enter to embed</span>
          <button onClick={onClose}>Cancel</button>
          <button className="pe-primary" onClick={apply}>
            {editing ? "Update SVG" : "Embed SVG"}
          </button>
        </footer>
      </section>
    </div>
  );
}
