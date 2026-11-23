import React, { useLayoutEffect, useRef, useState } from "react";
import { Bold } from "lucide-react";
import ColourPicker from "./ColourPicker.jsx";
import {
  STAMP_FONTS,
  formatStampRuns,
  normalizeStampRuns,
  replaceStampText,
} from "./stampDocument.js";

function textOffset(root, node, offset) {
  const range = document.createRange();
  range.selectNodeContents(root);
  try {
    range.setEnd(node, offset);
    return range.toString().length;
  } catch {
    return 0;
  }
}
function restoreSelection(root, start, end) {
  const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
  const nodes = [];
  for (let node; (node = walker.nextNode());) nodes.push(node);
  if (!nodes.length) return;
  const point = (offset) => {
    for (const node of nodes) {
      if (offset <= node.textContent.length) return [node, Math.max(0, offset)];
      offset -= node.textContent.length;
    }
    return [nodes.at(-1), nodes.at(-1).textContent.length];
  };
  const range = document.createRange();
  range.setStart(...point(start));
  range.setEnd(...point(end));
  const selection = window.getSelection();
  selection.removeAllRanges();
  selection.addRange(range);
}
export default function StampRichTextEditor({
  runs,
  onChange,
  disabled = false,
}) {
  const host = useRef(null),
    saved = useRef({ start: 0, end: 0 }),
    pendingSelection = useRef(null),
    composing = useRef(false);
  const [selection, setSelection] = useState({ start: 0, end: 0 });
  const [error, setError] = useState("");
  const signature = JSON.stringify(runs),
    rendered = useRef("");
  const capture = () => {
    const range = window.getSelection()?.rangeCount
      ? window.getSelection().getRangeAt(0)
      : null;
    if (
      range &&
      host.current?.contains(range.startContainer) &&
      host.current.contains(range.endContainer)
    ) {
      const result = {
        start: textOffset(
          host.current,
          range.startContainer,
          range.startOffset,
        ),
        end: textOffset(host.current, range.endContainer, range.endOffset),
      };
      saved.current = result;
      setSelection(result);
    }
    return saved.current;
  };
  useLayoutEffect(() => {
    if (!host.current || rendered.current === signature) return;
    const focused = document.activeElement === host.current;
    const range = pendingSelection.current || (focused ? capture() : null);
    const spans = runs.map((run, index) => {
      const span = document.createElement("span");
      span.dataset.run = String(index);
      span.style.fontFamily = run.fontFamily;
      span.style.fontSize = `${run.fontSize}px`;
      span.style.fontWeight = String(run.fontWeight);
      span.style.color = run.color;
      span.textContent = run.text || "";
      return span;
    });
    host.current.replaceChildren(...spans);
    rendered.current = signature;
    if (range) {
      restoreSelection(host.current, range.start, range.end);
      saved.current = range;
      setSelection(range);
    }
    pendingSelection.current = null;
  }, [signature]);
  const active = (() => {
    let offset = 0;
    for (const run of runs) {
      offset += run.text.length;
      if (selection.start < offset) return run;
    }
    return runs.at(-1);
  })();
  function revertDOM(range) {
    const spans = runs.map((run, index) => {
      const span = document.createElement("span");
      span.dataset.run = String(index);
      Object.assign(span.style, {
        fontFamily: run.fontFamily,
        fontSize: `${run.fontSize}px`,
        fontWeight: String(run.fontWeight),
        color: run.color,
      });
      span.textContent = run.text;
      return span;
    });
    host.current.replaceChildren(...spans);
    rendered.current = signature;
    pendingSelection.current = null;
    restoreSelection(host.current, range.start, range.end);
  }
  function update(next, range = saved.current, meta = { key: "typing" }) {
    try {
      const normalized = normalizeStampRuns(next);
      pendingSelection.current = range;
      if (onChange(normalized, meta) === false) revertDOM(range);
      setError("");
    } catch (e) {
      setError(e.message);
      revertDOM(range);
    }
  }
  const format = (patch) => {
    try {
      const field = Object.keys(patch)[0];
      update(
        formatStampRuns(runs, saved.current.start, saved.current.end, patch),
        saved.current,
        {
          key: ["fontSize", "color"].includes(field)
            ? `${field}-${saved.current.start}:${saved.current.end}`
            : null,
        },
      );
    } catch (e) {
      setError(e.message);
      revertDOM(saved.current);
    }
  };
  const insert = (value) => {
    const range = capture();
    const caret = range.start + value.length;
    try {
      update(
        replaceStampText(runs, range.start, range.end, value, active),
        { start: caret, end: caret },
        { key: value === "\n" ? "typing" : null },
      );
    } catch (e) {
      setError(e.message);
      revertDOM(range);
    }
  };
  function input() {
    if (composing.current) return;
    const range = capture();
    const result = [];
    const walk = (node, style = active) => {
      if (node.nodeType === Node.TEXT_NODE) {
        result.push({ ...style, text: node.textContent });
        return;
      }
      if (node.nodeType !== Node.ELEMENT_NODE) return;
      const runIndex = Number(node.dataset?.run);
      if (node.hasAttribute?.("data-run") && runs[runIndex])
        style = runs[runIndex];
      if (node.tagName === "BR") {
        result.push({ ...style, text: "\n" });
        return;
      }
      if (
        ["DIV", "P"].includes(node.tagName) &&
        result.length &&
        !result.at(-1).text.endsWith("\n")
      )
        result.push({ ...style, text: "\n" });
      for (const child of node.childNodes) walk(child, style);
    };
    for (const child of host.current.childNodes) walk(child);
    // Always rebuild from safe span records after an input event. The selection
    // is restored by character offset, including multiline, mixed-style text.
    rendered.current = "";
    update(result, range);
  }
  return (
    <section className="stamp-rich-text" aria-label="Rich stamp text editor">
      <div className="stamp-rich-toolbar" onPointerDown={capture}>
        <select
          aria-label="Stamp text font"
          disabled={disabled}
          value={active.fontFamily}
          onChange={(e) => format({ fontFamily: e.target.value })}
        >
          {STAMP_FONTS.map((font) => (
            <option key={font}>{font}</option>
          ))}
        </select>
        <input
          type="number"
          aria-label="Stamp text size"
          min="8"
          max="160"
          disabled={disabled}
          value={active.fontSize}
          onChange={(e) => format({ fontSize: Number(e.target.value) })}
        />
        <button
          type="button"
          title="Semibold selected text"
          aria-label="Bold stamp text"
          disabled={disabled}
          aria-pressed={active.fontWeight === 600}
          onPointerDown={(e) => {
            capture();
            e.preventDefault();
          }}
          onClick={() =>
            format({ fontWeight: active.fontWeight === 600 ? 400 : 600 })
          }
        >
          <Bold size={14} />
        </button>
      </div>
      <p className="stamp-text-selection">
        {selection.end > selection.start
          ? `${selection.end - selection.start} characters selected · format this range`
          : "Select a range to mix fonts and sizes · no selection formats all"}
      </p>
      <div
        ref={host}
        role="textbox"
        aria-label="Stamp rich text"
        aria-multiline="true"
        contentEditable={!disabled}
        suppressContentEditableWarning
        spellCheck={false}
        onInput={input}
        onCompositionStart={() => {
          composing.current = true;
        }}
        onCompositionEnd={() => {
          composing.current = false;
          input();
        }}
        onDragOver={(e) => e.preventDefault()}
        onDrop={(e) => {
          e.preventDefault();
          if (!disabled) insert(e.dataTransfer.getData("text/plain"));
        }}
        onMouseUp={capture}
        onKeyUp={capture}
        onBeforeInput={(e) => {
          if (
            ["insertParagraph", "insertLineBreak"].includes(
              e.nativeEvent.inputType,
            )
          ) {
            e.preventDefault();
            insert("\n");
          }
        }}
        onKeyDown={(e) => {
          if (e.key === "Enter" && !composing.current) {
            e.preventDefault();
            insert("\n");
          }
          if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "b") {
            e.preventDefault();
            capture();
            format({ fontWeight: active.fontWeight === 600 ? 400 : 600 });
          }
        }}
        onPaste={(e) => {
          e.preventDefault();
          insert(e.clipboardData.getData("text/plain"));
        }}
      />
      <ColourPicker
        ariaLabel="Stamp text colour"
        value={active.color}
        disabled={disabled}
        onChange={(color) => format({ color })}
      />
      {error && (
        <p className="stamp-error" role="alert">
          {error}
        </p>
      )}
    </section>
  );
}
