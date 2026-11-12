#!/usr/bin/env python3
"""Builds Documentation/Plans/MarkdownStudioViewer.html — Frontier Editor-styled .md Studio
with Session Notepad (live workspace save + Push to GitHub), closable/reopenable Documents,
Notepad & Inspector panes, hidden scrollbars, smooth scroll animations, zero shadows, and #3B82F6 Blue Accent."""

from __future__ import annotations

import json
import pathlib

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
OUTPUT_HTML = REPO_ROOT / "Documentation" / "Plans" / "MarkdownStudioViewer.html"

EXCLUDED_DIRS = {
    ".git",
    "ExternalPackages",
    "build",
    ".cache",
    "node_modules",
    "__pycache__",
}


def collect_markdown_documents(include_content: bool = True) -> list[dict[str, object]]:
    found: list[pathlib.Path] = []

    plans_dir = REPO_ROOT / "Documentation" / "Plans"
    if plans_dir.exists():
        preferred = plans_dir / "DocumentSpacesAndRevisionPlan.md"
        if preferred.exists():
            found.append(preferred)
        for p in sorted(plans_dir.rglob("*.md")):
            if p not in found:
                found.append(p)

    for p in sorted(REPO_ROOT.rglob("*.md")):
        rel_parts = p.relative_to(REPO_ROOT).parts
        if any(part in EXCLUDED_DIRS for part in rel_parts):
            continue
        if p not in found:
            found.append(p)

    docs: list[dict[str, object]] = []
    for path in found:
        rel = path.relative_to(REPO_ROOT).as_posix()
        parts = rel.split("/")
        if rel.startswith("Documentation/Plans/"):
            sub_dir = "/".join(parts[2:-1])
            group = f"Plans/{sub_dir}" if sub_dir else "Plans"
        elif rel.startswith("Documentation/AgenticInstructions/"):
            group = "AgenticInstructions"
        elif rel.startswith("Documentation/"):
            group = "Documentation"
        elif len(parts) > 1:
            group = "/".join(parts[:-1])
        else:
            group = "Repository Root"

        stat = path.stat()
        entry: dict[str, object] = {
            "path": rel,
            "name": parts[-1],
            "group": group,
            "size": stat.st_size,
            "mtime": round(stat.st_mtime, 3),
        }
        if include_content:
            entry["content"] = path.read_text(encoding="utf-8")
        docs.append(entry)
    return docs


HTML_TEMPLATE = r"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Frontier — Markdown Studio</title>
<style>
  @font-face {
    font-family: "FrontierDMSans";
    src: url("fonts/DMSans/DMSans-Regular.ttf") format("truetype");
    font-weight: 400;
    font-style: normal;
  }
  @font-face {
    font-family: "FrontierDMSans";
    src: url("fonts/DMSans/DMSans-Medium.ttf") format("truetype");
    font-weight: 500;
    font-style: normal;
  }
  @font-face {
    font-family: "FrontierDMSans";
    src: url("fonts/DMSans/DMSans-SemiBold.ttf") format("truetype");
    font-weight: 600;
    font-style: normal;
  }
  @font-face {
    font-family: "FrontierDMSans";
    src: url("fonts/DMSans/DMSans-Bold.ttf") format("truetype");
    font-weight: 700;
    font-style: normal;
  }
  @font-face {
    font-family: "FrontierMono";
    src: url("fonts/JetBrainsMono/JetBrainsMono-Regular.ttf") format("truetype");
    font-weight: 400;
    font-style: normal;
  }
  @font-face {
    font-family: "FrontierMono";
    src: url("fonts/JetBrainsMono/JetBrainsMono-SemiBold.ttf") format("truetype");
    font-weight: 600;
    font-style: normal;
  }

  :root {
    --fr-root-gap: #262626;
    --fr-tabbar: #0a0a0a;
    --fr-dock: #121212;
    --fr-canvas: #0e0f12;
    --fr-footer: #151515;
    --fr-well: #191919;
    --fr-well-border: #2c2c2c;
    --fr-recess: #090909;
    --fr-deep-input: #111111;
    --fr-section: #202020;
    --fr-tile: #1a1a1a;
    --fr-row-hover: #1e1e1e;
    --fr-row-selected: #262626;
    --fr-row-selected-border: #3d4f6b;
    --fr-hairline: rgba(255, 255, 255, 0.065);

    /* Frontier Blue Accent (#3B82F6) */
    --fr-blue: #3b82f6;
    --fr-blue-hover: #2563eb;
    --fr-blue-light: #60a5fa;
    --fr-blue-pale: #93c5fd;
    --fr-blue-wash: rgba(59, 130, 246, 0.14);
    --fr-blue-stroke: rgba(59, 130, 246, 0.45);
    --fr-green: #34d399;

    --fr-ink: #ebebeb;
    --fr-ink-bright: #f7f8fa;
    --fr-ink-secondary: #b0b0b0;
    --fr-ink-muted: #8a8a8a;
    --fr-ink-faint: #5c5c5c;

    --syn-h: #60a5fa;
    --syn-bold: #f3f4f6;
    --syn-code: #93c5fd;
    --syn-quote: #34d399;
    --syn-table: #38bdf8;
    --syn-link: #60a5fa;

    --doc-scale: 1;
    --left-w: 308px;
    --right-w: 392px;
    --font-sans: "FrontierDMSans", "DM Sans", "Inter", -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    --font-mono: "FrontierMono", "JetBrains Mono", "Cascadia Code", Consolas, monospace;
  }

  /* Hide ALL scrollbar sliders globally + zero drop shadows */
  * {
    box-sizing: border-box;
    margin: 0;
    padding: 0;
    box-shadow: none !important;
    scrollbar-width: none !important;
    -ms-overflow-style: none !important;
  }

  *::-webkit-scrollbar {
    display: none !important;
    width: 0 !important;
    height: 0 !important;
  }

  html, body {
    width: 100%;
    height: 100%;
    overflow: hidden;
    background: var(--fr-root-gap);
    color: var(--fr-ink);
    font-family: var(--font-sans);
    font-size: 13px;
    -webkit-font-smoothing: antialiased;
  }

  /* 3-Pane Docked Workspace Grid with smooth animated open/close transitions */
  .workspace-grid {
    display: grid;
    grid-template-columns: var(--left-w) minmax(0, 1fr) var(--right-w);
    grid-template-rows: minmax(0, 1fr);
    gap: 2px;
    width: 100vw;
    height: 100vh;
    max-height: 100vh;
    background: var(--fr-root-gap);
    transition: grid-template-columns 0.24s cubic-bezier(0.22, 1, 0.36, 1);
  }

  .workspace-grid.left-closed {
    --left-w: 0px;
  }

  .workspace-grid.right-closed {
    --right-w: 0px;
  }

  .workspace-grid.right-wide:not(.right-closed) {
    --right-w: 520px;
  }

  .workspace-grid.left-closed.right-closed {
    gap: 0px;
  }

  .dock-pane {
    background: var(--fr-dock);
    display: flex;
    flex-direction: column;
    height: 100%;
    max-height: 100vh;
    min-width: 0;
    min-height: 0;
    overflow: hidden;
  }

  /* Trapezoidal Tab Strip (#0A0A0A, borderless panel-matched #121212 tabs, NO top notch) */
  .tab-strip {
    height: 30px;
    background: var(--fr-tabbar);
    display: flex;
    align-items: flex-end;
    padding: 0 6px;
    gap: 2px;
    position: relative;
    user-select: none;
    flex-shrink: 0;
    overflow-x: auto;
  }

  .trap-tab {
    height: 27px;
    padding: 0 14px 0 17px;
    background: #0e0e0e;
    color: var(--fr-ink-muted);
    font-size: 12px;
    font-weight: 500;
    display: inline-flex;
    align-items: center;
    gap: 8px;
    cursor: pointer;
    border: none;
    outline: none;
    clip-path: polygon(9px 0, calc(100% - 9px) 0, 100% 100%, 0 100%);
    white-space: nowrap;
    flex-shrink: 0;
    transition: background 0.12s ease, color 0.12s ease;
  }

  .trap-tab:hover {
    background: #161616;
    color: var(--fr-ink);
  }

  .trap-tab.active {
    background: var(--fr-dock);
    color: var(--fr-ink-bright);
  }

  .trap-tab .tab-dot {
    width: 6px;
    height: 6px;
    border-radius: 50%;
    background: var(--fr-blue);
    flex-shrink: 0;
  }

  .trap-tab .tab-x {
    width: 15px;
    height: 15px;
    border-radius: 50%;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    font-size: 11px;
    line-height: 1;
    color: var(--fr-ink-muted);
    transition: background 0.12s ease, color 0.12s ease;
  }

  .trap-tab .tab-x:hover {
    background: rgba(255, 255, 255, 0.12);
    color: #ffffff;
  }

  .tab-add-disc {
    width: 21px;
    height: 21px;
    margin: 0 0 3px 3px;
    border-radius: 50%;
    border: none;
    background: #171717;
    color: var(--fr-ink-secondary);
    font-size: 13px;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    cursor: pointer;
    flex-shrink: 0;
    transition: background 0.12s ease, color 0.12s ease;
  }

  .tab-add-disc:hover {
    background: var(--fr-blue);
    color: #ffffff;
  }

  /* =========================================================================
     LEFT PANE — DOCUMENTS BROWSER
     ========================================================================= */
  .pane-header {
    padding: 12px 14px 10px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 8px;
    flex-shrink: 0;
  }

  .pane-title-group {
    display: flex;
    align-items: baseline;
    gap: 8px;
  }

  .pane-title {
    font-size: 15.5px;
    font-weight: 600;
    color: var(--fr-ink-bright);
    letter-spacing: -0.01em;
  }

  .pane-count {
    font-size: 11.5px;
    color: var(--fr-ink-muted);
  }

  .round-disc-group {
    display: flex;
    align-items: center;
    gap: 6px;
  }

  .round-disc {
    width: 28px;
    height: 28px;
    border-radius: 50%;
    border: 1px solid var(--fr-well-border);
    background: var(--fr-well);
    color: var(--fr-ink-secondary);
    display: inline-flex;
    align-items: center;
    justify-content: center;
    cursor: pointer;
    font-size: 13px;
    transition: background 0.12s ease, border-color 0.12s ease, color 0.12s ease;
  }

  .round-disc:hover {
    background: #252528;
    color: #ffffff;
    border-color: #3c3c42;
  }

  .round-disc.active {
    background: var(--fr-blue-wash);
    border-color: var(--fr-blue-stroke);
    color: var(--fr-blue-light);
  }

  .stat-wells {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 8px;
    padding: 0 14px 10px;
    flex-shrink: 0;
  }

  .stat-well {
    background: var(--fr-well);
    border: 1px solid var(--fr-well-border);
    border-radius: 16px;
    padding: 10px 12px;
    display: flex;
    flex-direction: column;
    justify-content: space-between;
    gap: 10px;
    cursor: pointer;
    user-select: none;
    transition: border-color 0.14s ease, background 0.14s ease;
  }

  .stat-well:hover {
    background: #1f1f22;
  }

  .stat-well.selected {
    border-color: var(--fr-blue);
    background: rgba(59, 130, 246, 0.08);
  }

  .stat-well-top {
    display: flex;
    align-items: center;
    justify-content: space-between;
  }

  .token-disc-blue {
    width: 22px;
    height: 22px;
    border-radius: 50%;
    background: var(--fr-blue);
    color: #ffffff;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    font-size: 11px;
    font-weight: 700;
  }

  .token-disc-muted {
    width: 22px;
    height: 22px;
    border-radius: 50%;
    background: #252528;
    border: 1px solid #38383d;
    color: var(--fr-ink-secondary);
    display: inline-flex;
    align-items: center;
    justify-content: center;
    font-size: 11px;
  }

  .stat-well-bottom {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
  }

  .stat-caption {
    font-size: 11.5px;
    color: var(--fr-ink-muted);
  }

  .stat-value {
    font-size: 19px;
    font-weight: 600;
    color: var(--fr-ink-bright);
    line-height: 1;
  }

  .filter-strip {
    display: grid;
    grid-template-columns: 1fr auto;
    gap: 8px;
    padding: 0 14px 12px;
    border-bottom: 1px solid var(--fr-hairline);
    flex-shrink: 0;
  }

  .search-capsule {
    height: 34px;
    background: var(--fr-well);
    border: 1px solid var(--fr-well-border);
    border-radius: 17px;
    padding: 0 12px;
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .search-capsule:focus-within {
    border-color: var(--fr-blue);
    background: var(--fr-deep-input);
  }

  .search-capsule input {
    width: 100%;
    border: none;
    outline: none;
    background: transparent;
    color: var(--fr-ink-bright);
    font-family: var(--font-sans);
    font-size: 12.5px;
  }

  .search-capsule input::placeholder {
    color: var(--fr-ink-faint);
  }

  .studio-dropdown-wrap {
    position: relative;
  }

  .studio-dropdown-btn {
    height: 34px;
    padding: 0 12px;
    border-radius: 17px;
    background: var(--fr-well);
    border: 1px solid var(--fr-well-border);
    color: var(--fr-ink);
    font-size: 12px;
    font-weight: 500;
    display: inline-flex;
    align-items: center;
    gap: 8px;
    cursor: pointer;
    white-space: nowrap;
  }

  .studio-dropdown-btn:hover {
    background: #222226;
    border-color: #3a3a42;
  }

  .studio-dropdown-sheet {
    position: absolute;
    top: 38px;
    right: 0;
    min-width: 176px;
    background: #0c0c0e;
    border: 1px solid var(--fr-well-border);
    border-radius: 12px;
    padding: 5px;
    z-index: 40;
  }

  .studio-dropdown-option {
    padding: 7px 10px;
    border-radius: 8px;
    font-size: 12px;
    color: var(--fr-ink-secondary);
    cursor: pointer;
    display: flex;
    align-items: center;
    justify-content: space-between;
  }

  .studio-dropdown-option:hover {
    background: rgba(255, 255, 255, 0.06);
    color: #ffffff;
  }

  .studio-dropdown-option.active {
    background: var(--fr-blue-wash);
    color: var(--fr-blue-light);
    font-weight: 600;
  }

  .tree-scroll {
    flex: 1 1 0;
    min-height: 0;
    overflow-y: auto;
    overscroll-behavior: contain;
    padding: 10px 10px 16px;
    display: flex;
    flex-direction: column;
    gap: 8px;
  }

  .folder-group {
    display: flex;
    flex-direction: column;
    gap: 2px;
  }

  .folder-row {
    height: 30px;
    padding: 0 10px;
    background: var(--fr-section);
    border: 1px solid rgba(255, 255, 255, 0.04);
    border-radius: 8px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    cursor: pointer;
    user-select: none;
  }

  .folder-row:hover {
    background: #262629;
  }

  .folder-row-left {
    display: flex;
    align-items: center;
    gap: 7px;
    font-size: 12px;
    font-weight: 600;
    color: var(--fr-ink-bright);
    min-width: 0;
  }

  .folder-icon {
    width: 14px;
    height: 11px;
    border-radius: 2px;
    background: var(--fr-blue);
    display: inline-block;
    flex-shrink: 0;
  }

  .folder-badge {
    font-family: var(--font-mono);
    font-size: 10.5px;
    color: var(--fr-ink-muted);
  }

  .folder-entries {
    display: flex;
    flex-direction: column;
    gap: 2px;
    padding-left: 8px;
    margin-top: 2px;
  }

  .folder-entries.collapsed {
    display: none;
  }

  /* Square 0px corner radius on ALL Outliner entry rows */
  .entry-row {
    min-height: 32px;
    padding: 5px 10px 5px 11px;
    border-radius: 0px;
    background: #161618;
    border: 1px solid rgba(255, 255, 255, 0.035);
    border-left: 3px solid transparent;
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 8px;
    cursor: pointer;
    user-select: none;
    transition: background 0.12s ease, border-color 0.12s ease;
  }

  .entry-row:hover {
    background: var(--fr-row-hover);
    border-color: #2e2e34;
  }

  .entry-row.active {
    background: var(--fr-row-selected);
    border-color: var(--fr-row-selected-border);
    border-left: 3px solid var(--fr-blue);
  }

  .entry-main {
    display: flex;
    align-items: center;
    gap: 8px;
    min-width: 0;
    overflow: hidden;
  }

  .entry-square-icon {
    width: 18px;
    height: 18px;
    border-radius: 3px;
    background: var(--fr-blue-wash);
    border: 1px solid var(--fr-blue-stroke);
    color: var(--fr-blue-light);
    font-family: var(--font-mono);
    font-size: 9px;
    font-weight: 700;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    flex-shrink: 0;
  }

  .entry-name {
    font-size: 12.5px;
    color: var(--fr-ink);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }

  .entry-row.active .entry-name {
    color: #ffffff;
    font-weight: 500;
  }

  .entry-size {
    font-family: var(--font-mono);
    font-size: 10.5px;
    color: var(--fr-ink-faint);
    flex-shrink: 0;
  }

  /* =========================================================================
     CENTER PANE — VIEWPORT READER & SOURCE
     ========================================================================= */
  .viewport-top-strip {
    padding: 10px 18px;
    border-bottom: 1px solid var(--fr-hairline);
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 14px;
    flex-shrink: 0;
  }

  .doc-identity {
    display: flex;
    align-items: center;
    gap: 11px;
    min-width: 0;
  }

  .doc-glyph {
    width: 30px;
    height: 30px;
    border-radius: 8px;
    background: var(--fr-blue-wash);
    border: 1px solid var(--fr-blue-stroke);
    color: var(--fr-blue-light);
    font-family: var(--font-mono);
    font-size: 11px;
    font-weight: 700;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    flex-shrink: 0;
  }

  .doc-titles {
    display: flex;
    flex-direction: column;
    gap: 1px;
    min-width: 0;
  }

  .doc-title-row {
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .doc-heading {
    font-size: 14px;
    font-weight: 600;
    color: var(--fr-ink-bright);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }

  .mode-tag {
    padding: 2px 7px;
    border-radius: 5px;
    background: var(--fr-blue-wash);
    border: 1px solid var(--fr-blue-stroke);
    color: var(--fr-blue-light);
    font-family: var(--font-mono);
    font-size: 10px;
    font-weight: 600;
    letter-spacing: 0.05em;
    text-transform: uppercase;
    flex-shrink: 0;
  }

  .doc-subpath {
    font-size: 11.5px;
    color: var(--fr-ink-muted);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }

  .mode-recess {
    background: var(--fr-recess);
    border: 1px solid var(--fr-well-border);
    border-radius: 20px;
    padding: 3px;
    display: inline-flex;
    align-items: center;
    gap: 3px;
    flex-shrink: 0;
  }

  .mode-pill {
    appearance: none;
    border: none;
    background: transparent;
    color: var(--fr-ink-secondary);
    font-family: var(--font-sans);
    font-size: 12px;
    font-weight: 500;
    height: 26px;
    padding: 0 13px;
    border-radius: 14px;
    cursor: pointer;
    display: inline-flex;
    align-items: center;
    gap: 6px;
    transition: background 0.14s ease, color 0.14s ease;
  }

  .mode-pill:hover {
    color: #ffffff;
    background: rgba(255, 255, 255, 0.05);
  }

  .mode-pill.active {
    background: var(--fr-blue);
    color: #ffffff;
    font-weight: 600;
  }

  .viewport-subbar {
    padding: 8px 18px;
    border-bottom: 1px solid var(--fr-hairline);
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    flex-shrink: 0;
    flex-wrap: wrap;
  }

  .subbar-left, .subbar-right {
    display: flex;
    align-items: center;
    gap: 8px;
    flex-wrap: wrap;
  }

  .capsule-primary {
    height: 30px;
    padding: 0 13px;
    border-radius: 15px;
    border: 1px solid var(--fr-blue);
    background: var(--fr-blue);
    color: #ffffff;
    font-family: var(--font-sans);
    font-size: 12px;
    font-weight: 600;
    display: inline-flex;
    align-items: center;
    gap: 6px;
    cursor: pointer;
    transition: background 0.12s ease, border-color 0.12s ease;
  }

  .capsule-primary:hover {
    background: var(--fr-blue-hover);
    border-color: var(--fr-blue-hover);
  }

  .capsule-secondary {
    height: 30px;
    padding: 0 12px;
    border-radius: 15px;
    border: 1px solid var(--fr-well-border);
    background: var(--fr-well);
    color: var(--fr-ink);
    font-family: var(--font-sans);
    font-size: 12px;
    font-weight: 500;
    display: inline-flex;
    align-items: center;
    gap: 6px;
    cursor: pointer;
    transition: background 0.12s ease, border-color 0.12s ease, color 0.12s ease;
  }

  .capsule-secondary:hover {
    background: #232328;
    border-color: var(--fr-blue-stroke);
    color: #ffffff;
  }

  /* Reopen Pills shown whenever Documents / Notepad / Inspector is closed via X */
  .reopen-pill {
    height: 28px;
    padding: 0 11px;
    border-radius: 14px;
    border: 1px solid var(--fr-blue-stroke);
    background: var(--fr-blue-wash);
    color: var(--fr-blue-light);
    font-family: var(--font-sans);
    font-size: 11.5px;
    font-weight: 600;
    display: inline-flex;
    align-items: center;
    gap: 5px;
    cursor: pointer;
    transition: background 0.12s ease, color 0.12s ease;
  }

  .reopen-pill:hover {
    background: var(--fr-blue);
    color: #ffffff;
  }

  .zoom-capsule {
    height: 30px;
    border-radius: 15px;
    border: 1px solid var(--fr-well-border);
    background: var(--fr-well);
    display: inline-flex;
    align-items: center;
    overflow: hidden;
  }

  .zoom-step {
    appearance: none;
    border: none;
    background: transparent;
    color: var(--fr-ink-secondary);
    height: 100%;
    padding: 0 10px;
    font-size: 12px;
    cursor: pointer;
  }

  .zoom-step:hover {
    background: rgba(255, 255, 255, 0.06);
    color: #ffffff;
  }

  .zoom-readout {
    font-family: var(--font-mono);
    font-size: 11.5px;
    color: var(--fr-ink-bright);
    padding: 0 8px;
    border-left: 1px solid var(--fr-well-border);
    border-right: 1px solid var(--fr-well-border);
    cursor: pointer;
  }

  .document-stage {
    flex: 1 1 0;
    min-height: 0;
    height: 100%;
    display: grid;
    grid-template-columns: minmax(0, 1fr);
    grid-template-rows: minmax(0, 1fr);
    background: var(--fr-canvas);
    overflow: hidden;
  }

  .document-stage.mode-split {
    grid-template-columns: minmax(0, 1fr) minmax(0, 1fr);
    grid-template-rows: minmax(0, 1fr);
  }

  .raw-editor-pane {
    height: 100%;
    min-height: 0;
    overflow-y: auto;
    overflow-x: auto;
    overscroll-behavior: contain;
    background: #0b0c0f;
    border-right: 1px solid var(--fr-well-border);
    font-family: var(--font-mono);
    font-size: calc(12.5px * var(--doc-scale));
    line-height: 1.62;
    padding: 14px 0 48px;
  }

  .source-line {
    display: grid;
    grid-template-columns: 54px 1fr;
    padding-right: 18px;
  }

  .source-line:hover {
    background: rgba(59, 130, 246, 0.06);
  }

  .line-num {
    text-align: right;
    padding-right: 16px;
    color: #525866;
    user-select: none;
    font-size: 0.9em;
  }

  .line-code {
    white-space: pre-wrap;
    word-break: break-word;
    color: #d6dae3;
  }

  .rendered-pane {
    height: 100%;
    min-height: 0;
    overflow-y: auto;
    overflow-x: hidden;
    overscroll-behavior: contain;
    background: var(--fr-canvas);
    padding: 28px 34px 72px;
  }

  @keyframes docReveal {
    from {
      opacity: 0;
      transform: translateY(8px);
    }
    to {
      opacity: 1;
      transform: translateY(0);
    }
  }

  .md-sheet {
    max-width: 920px;
    margin: 0 auto;
    background: var(--fr-dock);
    border: 1px solid var(--fr-well-border);
    border-radius: 20px;
    padding: 32px 36px 40px;
    font-size: calc(14px * var(--doc-scale));
    line-height: 1.66;
    color: #dadde5;
    animation: docReveal 0.22s cubic-bezier(0.22, 1, 0.36, 1);
  }

  .md-sheet h1 {
    font-size: 1.68em;
    font-weight: 700;
    color: #ffffff;
    padding-bottom: 12px;
    margin: 0 0 16px;
    border-bottom: 1px solid var(--fr-well-border);
    letter-spacing: -0.015em;
  }

  .md-sheet h2 {
    font-size: 1.26em;
    font-weight: 600;
    color: #f4f6fa;
    padding-bottom: 7px;
    margin: 28px 0 12px;
    border-bottom: 1px solid var(--fr-hairline);
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .md-sheet h2::before {
    content: "";
    width: 4px;
    height: 15px;
    border-radius: 2px;
    background: var(--fr-blue);
    display: inline-block;
    flex-shrink: 0;
  }

  .md-sheet h3 {
    font-size: 1.06em;
    font-weight: 600;
    color: var(--fr-blue-pale);
    margin: 20px 0 8px;
  }

  .md-sheet p {
    margin: 10px 0;
  }

  .md-sheet ul, .md-sheet ol {
    margin: 8px 0 14px 22px;
  }

  .md-sheet li {
    margin: 5px 0;
  }

  .md-sheet strong {
    color: #ffffff;
    font-weight: 600;
  }

  .md-sheet hr {
    border: none;
    border-top: 1px solid var(--fr-well-border);
    margin: 24px 0;
  }

  .md-sheet blockquote {
    background: var(--fr-well);
    border: 1px solid var(--fr-well-border);
    border-left: 3px solid var(--fr-blue);
    border-radius: 12px;
    padding: 12px 16px;
    margin: 14px 0;
    color: var(--fr-ink-secondary);
  }

  .md-sheet code {
    font-family: var(--font-mono);
    font-size: 0.87em;
    background: #171b24;
    border: 1px solid rgba(59, 130, 246, 0.28);
    color: var(--fr-blue-pale);
    padding: 1.5px 6px;
    border-radius: 6px;
  }

  .md-sheet pre {
    background: #0b0d11;
    border: 1px solid var(--fr-well-border);
    border-radius: 14px;
    padding: 16px 18px;
    margin: 14px 0;
    overflow-x: auto;
    line-height: 1.52;
  }

  .md-sheet pre code {
    background: none;
    border: none;
    padding: 0;
    color: #d8dee9;
    font-size: 0.85em;
  }

  .md-sheet .table-scroll {
    overflow-x: auto;
    margin: 14px 0;
    border: 1px solid var(--fr-well-border);
    border-radius: 14px;
    background: #15161a;
  }

  .md-sheet table {
    width: 100%;
    border-collapse: collapse;
    font-size: 0.92em;
  }

  .md-sheet th {
    background: #1b1d24;
    color: #f1f5f9;
    font-size: 0.84em;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.04em;
    padding: 10px 13px;
    border-bottom: 1px solid var(--fr-well-border);
    border-right: 1px solid rgba(255, 255, 255, 0.04);
    text-align: left;
  }

  .md-sheet th:last-child, .md-sheet td:last-child {
    border-right: none;
  }

  .md-sheet td {
    padding: 9px 13px;
    border-bottom: 1px solid rgba(255, 255, 255, 0.05);
    border-right: 1px solid rgba(255, 255, 255, 0.04);
    vertical-align: top;
    color: #c9ced8;
  }

  .md-sheet tr:last-child td {
    border-bottom: none;
  }

  .md-sheet tr:hover td {
    background: rgba(59, 130, 246, 0.05);
  }

  .md-sheet a {
    color: var(--fr-blue-light);
    text-decoration: none;
  }

  .md-sheet a:hover {
    text-decoration: underline;
  }

  .md-sheet mark {
    background: rgba(59, 130, 246, 0.35);
    color: #ffffff;
    border-radius: 3px;
    padding: 0 3px;
  }

  @keyframes sectionFlash {
    0% { background: rgba(59, 130, 246, 0.24); }
    100% { background: transparent; }
  }

  .section-highlight {
    border-radius: 6px;
    animation: sectionFlash 1.1s ease-out;
  }

  /* =========================================================================
     RIGHT PANE — SESSION NOTEPAD + INSPECTOR
     ========================================================================= */
  .notepad-wrap {
    flex: 1;
    min-height: 0;
    display: flex;
    flex-direction: column;
  }

  .notepad-toolbar {
    padding: 10px 12px;
    border-bottom: 1px solid var(--fr-hairline);
    display: flex;
    flex-direction: column;
    gap: 8px;
    flex-shrink: 0;
  }

  .notepad-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 6px;
  }

  .note-filename-capsule {
    flex: 1;
    height: 30px;
    background: var(--fr-deep-input);
    border: 1px solid var(--fr-well-border);
    border-radius: 15px;
    padding: 0 11px;
    display: flex;
    align-items: center;
    gap: 6px;
    min-width: 0;
  }

  .note-filename-capsule:focus-within {
    border-color: var(--fr-blue);
  }

  .note-filename-input {
    width: 100%;
    border: none;
    outline: none;
    background: transparent;
    color: var(--fr-ink-bright);
    font-family: var(--font-mono);
    font-size: 11.5px;
  }

  .notepad-editor-surface {
    flex: 1;
    min-height: 0;
    margin: 10px 12px;
    background: #0b0d11;
    border: 1px solid var(--fr-well-border);
    border-radius: 16px;
    display: flex;
    flex-direction: column;
    overflow: hidden;
    transition: border-color 0.14s ease;
  }

  .notepad-editor-surface:focus-within {
    border-color: var(--fr-blue);
  }

  .notepad-textarea {
    flex: 1 1 0;
    width: 100%;
    height: 100%;
    min-height: 0;
    overflow-y: auto;
    overscroll-behavior: contain;
    border: none;
    outline: none;
    resize: none;
    background: transparent;
    color: var(--fr-ink-bright);
    font-family: var(--font-sans);
    font-size: 13.5px;
    line-height: 1.62;
    padding: 14px 16px 28px;
  }

  .notepad-textarea.mono-mode {
    font-family: var(--font-mono);
    font-size: 12.5px;
  }

  .notepad-textarea::placeholder {
    color: var(--fr-ink-faint);
  }

  .notepad-hint-strip {
    padding: 7px 14px;
    background: #13151b;
    border-top: 1px solid var(--fr-hairline);
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 8px;
    font-family: var(--font-mono);
    font-size: 10.5px;
    color: var(--fr-ink-muted);
    flex-shrink: 0;
  }

  .status-saved {
    color: var(--fr-blue-light);
    font-weight: 600;
  }

  .status-pushed {
    color: var(--fr-green);
    font-weight: 600;
  }

  .inspector-scroll {
    flex: 1 1 0;
    min-height: 0;
    overflow-y: auto;
    overscroll-behavior: contain;
    padding: 14px;
    display: flex;
    flex-direction: column;
    gap: 12px;
  }

  .inspector-eyebrow {
    font-size: 11px;
    font-weight: 600;
    letter-spacing: 0.06em;
    text-transform: uppercase;
    color: var(--fr-ink-muted);
  }

  .inspector-card {
    background: var(--fr-tile);
    border: 1px solid var(--fr-well-border);
    border-radius: 16px;
    padding: 12px;
    display: flex;
    flex-direction: column;
    gap: 8px;
  }

  .inspector-card-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding-bottom: 6px;
    border-bottom: 1px solid var(--fr-hairline);
  }

  .inspector-card-title {
    font-size: 12.5px;
    font-weight: 600;
    color: var(--fr-ink-bright);
  }

  .inspector-pill {
    padding: 2px 8px;
    border-radius: 10px;
    background: var(--fr-blue-wash);
    border: 1px solid var(--fr-blue-stroke);
    color: var(--fr-blue-light);
    font-family: var(--font-mono);
    font-size: 10px;
    font-weight: 600;
  }

  .kv-grid {
    display: grid;
    grid-template-columns: 74px 1fr;
    gap: 6px 8px;
    font-size: 12px;
  }

  .kv-key {
    color: var(--fr-ink-muted);
  }

  .kv-val {
    color: var(--fr-ink-bright);
    font-family: var(--font-mono);
    font-size: 11.2px;
    word-break: break-all;
  }

  .outline-items {
    display: flex;
    flex-direction: column;
    gap: 3px;
  }

  .outline-row {
    padding: 6px 9px;
    border-radius: 8px;
    font-size: 12px;
    color: var(--fr-ink-secondary);
    cursor: pointer;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    border-left: 2px solid transparent;
    transition: background 0.12s ease, color 0.12s ease, border-color 0.12s ease;
  }

  .outline-row:hover {
    background: rgba(59, 130, 246, 0.10);
    border-left-color: var(--fr-blue);
    color: #ffffff;
  }

  .outline-row.lvl-1 {
    font-weight: 600;
    color: var(--fr-ink-bright);
  }

  .outline-row.lvl-2 {
    padding-left: 16px;
  }

  .outline-row.lvl-3 {
    padding-left: 26px;
    font-size: 11.5px;
    color: var(--fr-ink-muted);
  }

  .pane-footer {
    height: 28px;
    background: var(--fr-footer);
    border-top: 1px solid var(--fr-hairline);
    padding: 0 14px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    font-family: var(--font-mono);
    font-size: 10.5px;
    color: var(--fr-ink-muted);
    user-select: none;
    flex-shrink: 0;
  }

  .footer-blue {
    color: var(--fr-blue-light);
    font-weight: 600;
  }

  .hidden {
    display: none !important;
  }

  .tok-h { color: var(--syn-h); font-weight: 600; }
  .tok-q { color: var(--syn-quote); }
  .tok-t { color: var(--syn-table); }
  .tok-c { color: var(--syn-code); }
  .tok-b { color: var(--syn-bold); font-weight: 600; }
  .tok-l { color: var(--syn-link); }
</style>
</head>
<body>

<input type="file" id="localFileInput" accept=".md,.markdown,.txt" multiple class="hidden">

<div class="workspace-grid" id="workspaceGrid">

  <!-- =======================================================================
       LEFT PANE: DOCUMENTS (WITH X TO CLOSE & REOPEN)
       ======================================================================= -->
  <aside class="dock-pane" id="leftPane">
    <div class="tab-strip">
      <div class="trap-tab active">
        <span class="tab-dot"></span>
        <span>Documents</span>
        <span class="tab-x" id="btnCloseDocumentsTab" title="Close Documents Pane">×</span>
      </div>
      <button class="tab-add-disc" id="btnAddTabLeft" title="Open local .md file">+</button>
    </div>

    <div class="pane-header">
      <div class="pane-title-group">
        <span class="pane-title">Documents</span>
        <span class="pane-count" id="headerTotalLabel">0 files</span>
      </div>
      <div class="round-disc-group">
        <button class="round-disc" id="btnRefreshTree" title="Refresh live files from disk">↻</button>
        <button class="round-disc" id="btnOpenLocalIcon" title="Open local .md file">+</button>
      </div>
    </div>

    <div class="stat-wells">
      <div class="stat-well" id="wellPlans" onclick="setFolderScope('plans')">
        <div class="stat-well-top">
          <span class="token-disc-blue">✓</span>
        </div>
        <div class="stat-well-bottom">
          <span class="stat-caption">Plans</span>
          <span class="stat-value" id="statPlansCount">0</span>
        </div>
      </div>

      <div class="stat-well selected" id="wellAll" onclick="setFolderScope('all')">
        <div class="stat-well-top">
          <span class="token-disc-muted">≡</span>
        </div>
        <div class="stat-well-bottom">
          <span class="stat-caption">All Docs</span>
          <span class="stat-value" id="statAllCount">0</span>
        </div>
      </div>
    </div>

    <div class="filter-strip">
      <div class="search-capsule">
        <span style="color:var(--fr-ink-muted);font-size:12px;">⌕</span>
        <input type="text" id="searchInput" placeholder="Search .md files...">
      </div>

      <div class="studio-dropdown-wrap" id="dropdownWrap">
        <button class="studio-dropdown-btn" id="dropdownBtn">
          <span id="dropdownLabel">All Folders</span>
          <span style="color:var(--fr-ink-muted);font-size:10px;">▾</span>
        </button>
        <div class="studio-dropdown-sheet hidden" id="dropdownSheet">
          <div class="studio-dropdown-option active" data-scope="all">All Folders</div>
          <div class="studio-dropdown-option" data-scope="plans">Plans Only</div>
          <div class="studio-dropdown-option" data-scope="docs">Governance Docs</div>
          <div class="studio-dropdown-option" data-scope="skills">Agentic Skills</div>
        </div>
      </div>
    </div>

    <div class="tree-scroll smooth-scroll-surface" id="treeContainer"></div>

    <div class="pane-footer">
      <span class="footer-blue" id="serverModeFooter">● EMBEDDED</span>
      <span id="leftFooterSummary">0 VISIBLE</span>
    </div>
  </aside>

  <!-- =======================================================================
       CENTER PANE: MARKDOWN VIEWPORT (CLEAN TAB STRIP — NO TOP NOTCH)
       ======================================================================= -->
  <main class="dock-pane" id="centerPane">
    <div class="tab-strip" id="tabsStrip"></div>

    <div class="viewport-top-strip">
      <div class="doc-identity">
        <span class="doc-glyph">MD</span>
        <div class="doc-titles">
          <div class="doc-title-row">
            <span class="doc-heading" id="activeDocTitle">Select a document</span>
            <span class="mode-tag" id="activeGroupBadge">PLANS</span>
          </div>
          <span class="doc-subpath" id="activeDocPath">—</span>
        </div>
      </div>

      <div class="mode-recess">
        <button class="mode-pill active" data-mode="preview" onclick="setViewMode('preview', this)">Preview</button>
        <button class="mode-pill" data-mode="split" onclick="setViewMode('split', this)">Split</button>
        <button class="mode-pill" data-mode="source" onclick="setViewMode('source', this)">Raw .md</button>
      </div>
    </div>

    <div class="viewport-subbar">
      <div class="subbar-left">
        <!-- Reopen pills appear here whenever Documents / Notepad / Inspector is closed -->
        <button class="reopen-pill hidden" id="btnReopenDocuments" title="Reopen Documents Pane">+ Documents</button>

        <button class="capsule-primary" id="btnOpenLocalMain">
          <span>+</span>
          <span>Open .md</span>
        </button>

        <div class="zoom-capsule">
          <button class="zoom-step" onclick="adjustZoom(-0.08)" title="Decrease Font Scale">−</button>
          <span class="zoom-readout" id="zoomLabel" onclick="resetZoom()" title="Reset Scale">100%</span>
          <button class="zoom-step" onclick="adjustZoom(0.08)" title="Increase Font Scale">+</button>
        </div>

        <button class="round-disc" id="btnScrollTop" title="Smooth Scroll to Top">↑</button>
        <span id="matchInfo" style="font-family:var(--font-mono);font-size:11.5px;color:var(--fr-blue-light);"></span>
      </div>

      <div class="subbar-right">
        <button class="reopen-pill hidden" id="btnReopenNotepad" title="Reopen Session Notepad">+ Notepad</button>
        <button class="reopen-pill hidden" id="btnReopenInspector" title="Reopen Document Inspector">+ Inspector</button>
      </div>
    </div>

    <div class="document-stage" id="documentStage">
      <div class="raw-editor-pane smooth-scroll-surface hidden" id="sourcePane"></div>
      <div class="rendered-pane smooth-scroll-surface" id="previewPane">
        <article class="md-sheet" id="mdBody"></article>
      </div>
    </div>

    <div class="pane-footer">
      <span><span class="footer-blue">READY</span> &nbsp;—&nbsp; <span id="footerMetrics">LINES 0 · WORDS 0 · 0.0 KB</span></span>
      <span>UTF-8 · LF · SMOOTH SCROLL</span>
    </div>
  </main>

  <!-- =======================================================================
       RIGHT PANE: SESSION NOTEPAD + INSPECTOR (EACH WITH X TO CLOSE & REOPEN)
       ======================================================================= -->
  <aside class="dock-pane" id="rightPane">
    <div class="tab-strip" id="rightTabStrip"></div>

    <!-- NOTEPAD VIEW -->
    <div class="notepad-wrap" id="notepadView">
      <div class="notepad-toolbar">
        <div class="notepad-row">
          <div class="note-filename-capsule" title="Saved directly into Documentation/Plans/ inside the live workspace">
            <span style="color:var(--fr-blue-light);font-size:11px;">✎</span>
            <input type="text" class="note-filename-input" id="noteFilenameInput" value="SessionNotes.md" spellcheck="false">
          </div>
          <button class="round-disc" id="btnNewNote" title="Create new note in Documentation/Plans/">+</button>
          <button class="round-disc" id="btnToggleMonoNote" title="Toggle Monospace / Sans Font">M</button>
          <button class="round-disc" id="btnWideNotepad" title="Expand / Narrow Notepad Width">⤢</button>
        </div>

        <div class="notepad-row">
          <button class="capsule-primary" id="btnPushGithub" title="Save & Push note to GitHub (origin/arena/429d7dc7-frontier)">
            <span>↑</span>
            <span>Push to GitHub</span>
          </button>
          <div style="display:flex;align-items:center;gap:6px;">
            <button class="capsule-secondary" id="btnSaveSession" title="Save immediately to live session workspace (Ctrl+S)">Save</button>
            <button class="capsule-secondary" id="btnCopyNote" title="Copy note to clipboard">Copy</button>
            <button class="round-disc" id="btnDownloadNote" title="Download note as .md file">⇩</button>
          </div>
        </div>
      </div>

      <div class="notepad-editor-surface">
        <textarea
          id="notepadTextarea"
          class="notepad-textarea smooth-scroll-surface"
          placeholder="Write or paste notes, feedback, or snippets here while reviewing plans...&#10;&#10;• Auto-saves directly into Documentation/Plans/SessionNotes.md in this session&#10;• Click 'Push to GitHub' above to commit & push to GitHub anytime"
        ></textarea>
        <div class="notepad-hint-strip">
          <span id="noteSaveStatus" class="status-saved">● Ready</span>
          <span id="noteMetrics">0 words</span>
        </div>
      </div>
    </div>

    <!-- INSPECTOR VIEW -->
    <div class="inspector-scroll smooth-scroll-surface hidden" id="inspectorView">
      <div class="inspector-eyebrow">Markdown Inspector</div>

      <div class="inspector-card">
        <div class="inspector-card-header">
          <span class="inspector-card-title">Document Properties</span>
          <span class="inspector-pill">BLUE ACCENT</span>
        </div>
        <div class="kv-grid">
          <span class="kv-key">Folder</span>
          <span class="kv-val" id="metaGroup">—</span>
          <span class="kv-key">Lines</span>
          <span class="kv-val" id="metaLines">0</span>
          <span class="kv-key">Words</span>
          <span class="kv-val" id="metaWords">0</span>
          <span class="kv-key">Size</span>
          <span class="kv-val" id="metaSize">0.0 KB</span>
        </div>
      </div>

      <div class="inspector-card">
        <div class="inspector-card-header">
          <span class="inspector-card-title">Section Outline</span>
          <span class="inspector-pill" id="outlineCountPill">0</span>
        </div>
        <div class="outline-items" id="outlineList"></div>
      </div>
    </div>

    <div class="pane-footer">
      <span id="rightFooterLeft">Documentation/Plans/SessionNotes.md</span>
      <span>Frontier Studio</span>
    </div>
  </aside>

</div>

<script id="embeddedDocs" type="application/json">__DOCUMENTS_JSON__</script>
<script>
(function() {
  let DOCS = JSON.parse(document.getElementById('embeddedDocs').textContent);
  const docByPath = new Map(DOCS.map(d => [d.path, d]));

  let liveServerConnected = false;
  let folderScope = 'all';
  let openTabs = [];
  let activePath = '';
  let viewMode = 'preview';
  let zoomScale = 1.0;
  let searchQuery = '';
  const collapsedGroups = new Set(['Experimental']);

  // Pane visibility & right-dock tab state ('notepad' | 'inspector')
  let leftPaneOpen = true;
  let rightTabsOpen = ['notepad', 'inspector'];
  let activeRightTab = 'notepad';
  let rightWide = false;

  // Notepad state
  let currentNoteRel = 'Documentation/Plans/SessionNotes.md';
  let noteSaveTimer = null;
  let noteDirty = false;

  // Pre-open Plan docs in center tabs (excluding SessionNotes.md / Note-*.md so center starts on the active Plan)
  DOCS.filter(d => String(d.group).startsWith('Plans') && d.name !== 'SessionNotes.md' && !d.name.startsWith('Note-')).forEach(d => openTabs.push(d.path));
  if (openTabs.length === 0 && DOCS.length > 0) {
    openTabs.push(DOCS[0].path);
  }
  activePath = openTabs[0] || '';

  // =========================================================================
  // SMOOTH ANIMATED SCROLL ENGINE (eased section jumps + smooth wheel glide)
  // =========================================================================
  const wheelTargets = new WeakMap();
  const wheelFrames = new WeakMap();

  function animateScrollTo(el, targetTop, durationMs) {
    if (!el) return;
    const cancelPrev = wheelFrames.get(el);
    if (cancelPrev) cancelAnimationFrame(cancelPrev);
    wheelTargets.delete(el);

    const startTop = el.scrollTop;
    const maxTop = Math.max(0, el.scrollHeight - el.clientHeight);
    const clampedTarget = Math.min(maxTop, Math.max(0, targetTop));
    const delta = clampedTarget - startTop;
    if (Math.abs(delta) < 1) return;

    const startTime = performance.now();
    function step(now) {
      const t = Math.min(1, (now - startTime) / durationMs);
      const eased = 1 - Math.pow(1 - t, 3); // cubic ease-out
      el.scrollTop = startTop + delta * eased;
      if (t < 1) {
        wheelFrames.set(el, requestAnimationFrame(step));
      } else {
        wheelFrames.delete(el);
      }
    }
    wheelFrames.set(el, requestAnimationFrame(step));
  }

  function glideWheelDelta(el, rawDeltaY, deltaMode) {
    if (!el || !rawDeltaY) return false;
    const maxTop = Math.max(0, el.scrollHeight - el.clientHeight);
    if (maxTop <= 0) return false;

    let dy = rawDeltaY;
    if (deltaMode === 1) dy *= 36;
    else if (deltaMode === 2) dy *= el.clientHeight * 0.85;

    const base = wheelTargets.has(el) ? wheelTargets.get(el) : el.scrollTop;
    const next = Math.min(maxTop, Math.max(0, base + dy));
    wheelTargets.set(el, next);

    // Immediate first-step response + 60fps cubic glide
    const initialDiff = next - el.scrollTop;
    if (Math.abs(initialDiff) >= 0.5) {
      el.scrollTop += initialDiff * 0.26;
    }

    if (!wheelFrames.has(el)) {
      const tick = () => {
        const target = wheelTargets.get(el);
        if (target === undefined) {
          wheelFrames.delete(el);
          return;
        }
        const diff = target - el.scrollTop;
        if (Math.abs(diff) < 0.6) {
          el.scrollTop = target;
          wheelTargets.delete(el);
          wheelFrames.delete(el);
          return;
        }
        el.scrollTop += diff * 0.26;
        wheelFrames.set(el, requestAnimationFrame(tick));
      };
      wheelFrames.set(el, requestAnimationFrame(tick));
    }
    return true;
  }

  window.addEventListener('wheel', e => {
    if (e.ctrlKey || e.metaKey || !e.deltaY || Math.abs(e.deltaX) > Math.abs(e.deltaY)) return;

    let scrollEl = e.target.closest('.smooth-scroll-surface');
    if (!scrollEl) {
      if (e.target.closest('#centerPane')) {
        scrollEl = viewMode === 'source'
          ? document.getElementById('sourcePane')
          : document.getElementById('previewPane');
      } else if (e.target.closest('#leftPane')) {
        scrollEl = document.getElementById('treeContainer');
      } else if (e.target.closest('#rightPane')) {
        scrollEl = activeRightTab === 'notepad'
          ? document.getElementById('notepadTextarea')
          : document.getElementById('inspectorView');
      } else {
        scrollEl = viewMode === 'source'
          ? document.getElementById('sourcePane')
          : document.getElementById('previewPane');
      }
    }

    if (glideWheelDelta(scrollEl, e.deltaY, e.deltaMode)) {
      e.preventDefault();
    }
  }, { passive: false });

  window.addEventListener('keydown', e => {
    const tag = (e.target && e.target.tagName) ? e.target.tagName.toLowerCase() : '';
    if (tag === 'input' || tag === 'textarea') return;
    const scrollEl = viewMode === 'source'
      ? document.getElementById('sourcePane')
      : document.getElementById('previewPane');
    if (!scrollEl) return;
    if (e.key === 'ArrowDown') { e.preventDefault(); glideWheelDelta(scrollEl, 96, 0); }
    else if (e.key === 'ArrowUp') { e.preventDefault(); glideWheelDelta(scrollEl, -96, 0); }
    else if (e.key === 'PageDown') { e.preventDefault(); glideWheelDelta(scrollEl, scrollEl.clientHeight * 0.82, 0); }
    else if (e.key === 'PageUp') { e.preventDefault(); glideWheelDelta(scrollEl, -scrollEl.clientHeight * 0.82, 0); }
    else if (e.key === 'Home') { e.preventDefault(); animateScrollTo(scrollEl, 0, 320); }
    else if (e.key === 'End') { e.preventDefault(); animateScrollTo(scrollEl, scrollEl.scrollHeight, 320); }
  });

  // =========================================================================
  // PANE OPEN / CLOSE WITH X + REOPEN PILLS
  // =========================================================================
  function syncPaneLayout() {
    const grid = document.getElementById('workspaceGrid');
    const rightOpen = rightTabsOpen.length > 0 && Boolean(activeRightTab);

    grid.classList.toggle('left-closed', !leftPaneOpen);
    grid.classList.toggle('right-closed', !rightOpen);
    grid.classList.toggle('right-wide', rightOpen && activeRightTab === 'notepad' && rightWide);

    document.getElementById('btnReopenDocuments').classList.toggle('hidden', leftPaneOpen);
    document.getElementById('btnReopenNotepad').classList.toggle('hidden', rightTabsOpen.includes('notepad'));
    document.getElementById('btnReopenInspector').classList.toggle('hidden', rightTabsOpen.includes('inspector'));

    renderRightPaneTabs();

    const showNotepad = rightOpen && activeRightTab === 'notepad';
    const showInspector = rightOpen && activeRightTab === 'inspector';
    document.getElementById('notepadView').classList.toggle('hidden', !showNotepad);
    document.getElementById('inspectorView').classList.toggle('hidden', !showInspector);
    document.getElementById('rightFooterLeft').textContent = showNotepad
      ? currentNoteRel
      : (document.getElementById('outlineCountPill').textContent + ' Sections');
  }

  function renderRightPaneTabs() {
    const strip = document.getElementById('rightTabStrip');
    const labels = { notepad: 'Notepad', inspector: 'Inspector' };
    strip.innerHTML = rightTabsOpen.map(id => {
      const cls = id === activeRightTab ? 'trap-tab active' : 'trap-tab';
      return (
        '<div class="' + cls + '" data-rtab="' + id + '">' +
        (id === activeRightTab ? '<span class="tab-dot"></span>' : '') +
        '<span>' + labels[id] + '</span>' +
        '<span class="tab-x" data-rclose="' + id + '" title="Close ' + labels[id] + '">×</span>' +
        '</div>'
      );
    }).join('');
  }

  function closeRightTab(tabId) {
    rightTabsOpen = rightTabsOpen.filter(t => t !== tabId);
    if (activeRightTab === tabId) {
      activeRightTab = rightTabsOpen[0] || '';
    }
    syncPaneLayout();
  }

  function reopenRightTab(tabId) {
    if (!rightTabsOpen.includes(tabId)) {
      if (tabId === 'notepad') rightTabsOpen.unshift('notepad');
      else rightTabsOpen.push(tabId);
    }
    activeRightTab = tabId;
    syncPaneLayout();
  }

  // =========================================================================
  // NOTEPAD (LIVE SESSION AUTO-SAVE + PUSH TO GITHUB)
  // =========================================================================
  function updateNoteMetrics() {
    const val = document.getElementById('notepadTextarea').value;
    const words = val.trim() ? val.trim().split(/\s+/).filter(Boolean).length : 0;
    const lines = val.split('\n').length;
    document.getElementById('noteMetrics').textContent = lines + ' ln · ' + words + ' words';
  }

  function setNoteStatus(text, cls) {
    const el = document.getElementById('noteSaveStatus');
    el.textContent = text;
    el.className = cls || 'status-saved';
  }

  function normalizeNoteRelFromInput() {
    let raw = document.getElementById('noteFilenameInput').value.trim() || 'SessionNotes.md';
    raw = raw.replace(/^\/+/, '');
    if (!raw.toLowerCase().endsWith('.md')) raw += '.md';
    document.getElementById('noteFilenameInput').value = raw.replace(/^Documentation\/Plans\//, '');
    currentNoteRel = raw.startsWith('Documentation/Plans/')
      ? raw
      : ('Documentation/Plans/' + raw);
    document.getElementById('rightFooterLeft').textContent = currentNoteRel;
    return currentNoteRel;
  }

  async function loadNoteFromServer(relPath) {
    const embedded = docByPath.get(relPath);
    if (embedded && embedded.content !== undefined) {
      document.getElementById('notepadTextarea').value = embedded.content;
      updateNoteMetrics();
    }
    try {
      const resp = await fetch('api/notes?path=' + encodeURIComponent(relPath), { cache: 'no-store' });
      if (!resp.ok) return;
      const data = await resp.json();
      liveServerConnected = true;
      currentNoteRel = data.path;
      document.getElementById('noteFilenameInput').value = data.name;
      document.getElementById('notepadTextarea').value = data.content || '';
      localStorage.setItem('frontier_note_' + currentNoteRel, data.content || '');
      updateNoteMetrics();
      setNoteStatus('● Synced with Session', 'status-saved');
    } catch (_) {
      const cached = localStorage.getItem('frontier_note_' + relPath);
      if (cached !== null) {
        document.getElementById('notepadTextarea').value = cached;
        updateNoteMetrics();
      }
    }
  }

  async function saveNoteToSession(manual) {
    const relPath = normalizeNoteRelFromInput();
    const content = document.getElementById('notepadTextarea').value;
    localStorage.setItem('frontier_note_' + relPath, content);
    noteDirty = false;

    try {
      setNoteStatus('● Saving to Session...', 'status-saved');
      const resp = await fetch('api/notes/save', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ path: relPath, content })
      });
      if (!resp.ok) throw new Error('HTTP ' + resp.status);
      const res = await resp.json();
      if (!res.ok) throw new Error(res.error || 'Save error');
      setNoteStatus(manual ? '✓ Saved to Session' : '● Auto-saved to Session', 'status-saved');
      syncLiveTree(true);
    } catch (_) {
      setNoteStatus('● Saved in Browser', 'status-saved');
    }
  }

  async function pushNoteToGithub() {
    const btn = document.getElementById('btnPushGithub');
    const relPath = normalizeNoteRelFromInput();
    const content = document.getElementById('notepadTextarea').value;
    localStorage.setItem('frontier_note_' + relPath, content);

    btn.disabled = true;
    setNoteStatus('↑ Pushing to GitHub...', 'status-saved');
    try {
      const resp = await fetch('api/notes/push', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ path: relPath, content })
      });
      const res = await resp.json();
      if (!resp.ok || !res.ok) {
        throw new Error(res.error || ('HTTP ' + resp.status));
      }
      setNoteStatus('✓ Pushed ' + res.commit + ' → GitHub', 'status-pushed');
      syncLiveTree(true);
    } catch (err) {
      setNoteStatus('✕ Push failed: ' + err.message, '');
    } finally {
      btn.disabled = false;
    }
  }

  // =========================================================================
  // MARKDOWN PARSER & RENDERER
  // =========================================================================
  function escapeHtml(str) {
    return String(str)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }

  function formatSize(bytes) {
    const kb = Number(bytes || 0) / 1024;
    return kb < 10 ? kb.toFixed(1) + ' KB' : Math.round(kb) + ' KB';
  }

  function highlightQuery(htmlText) {
    if (!searchQuery || searchQuery.length < 2) return htmlText;
    const escapedQ = searchQuery.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    const re = new RegExp('(' + escapedQ + ')', 'gi');
    return htmlText.replace(/(>[^<]*<)/g, seg => {
      const inner = seg.slice(1, -1);
      return '>' + inner.replace(re, '<mark>$1</mark>') + '<';
    });
  }

  function resolveRelativePath(basePath, href) {
    if (/^(https?:|mailto:|#)/i.test(href)) return null;
    const cleanHref = href.split('#')[0];
    const baseParts = basePath.split('/').slice(0, -1);
    for (const part of cleanHref.split('/')) {
      if (!part || part === '.') continue;
      if (part === '..') baseParts.pop();
      else baseParts.push(part);
    }
    const candidate = baseParts.join('/');
    return docByPath.has(candidate) ? candidate : null;
  }

  function formatInline(raw, currentDocPath) {
    const tokens = [];
    let s = escapeHtml(raw);
    s = s.replace(/`([^`]+)`/g, (_, code) => {
      const idx = tokens.push('<code>' + code + '</code>') - 1;
      return '\x00TOK' + idx + '\x00';
    });

    s = s.replace(/\[([^\]]+)\]\(([^)]+)\)/g, (_, label, href) => {
      const resolved = resolveRelativePath(currentDocPath, href);
      if (resolved) {
        return '<a href="#" data-doclink="' + escapeHtml(resolved) + '">' + label + '</a>';
      }
      return '<a href="' + href + '" target="_blank" rel="noopener">' + label + '</a>';
    });

    s = s.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
    s = s.replace(/(^|[^*])\*([^*\n]+)\*/g, '$1<em>$2</em>');
    s = s.replace(/\x00TOK(\d+)\x00/g, (_, i) => tokens[Number(i)]);
    return s;
  }

  function parseMarkdown(md, currentDocPath) {
    const cleaned = String(md || '').replace(/<!--[\s\S]*?-->/g, '');
    const lines = cleaned.replace(/\r\n/g, '\n').split('\n');
    let html = '';
    const headings = [];
    let i = 0;

    while (i < lines.length) {
      const line = lines[i];

      if (line.trimStart().startsWith('```')) {
        i++;
        const buf = [];
        while (i < lines.length && !lines[i].trimStart().startsWith('```')) {
          buf.push(escapeHtml(lines[i]));
          i++;
        }
        i++;
        html += '<pre><code>' + buf.join('\n') + '</code></pre>';
        continue;
      }

      if (/^\s*([-*_])\s*\1\s*\1[\s\-*_]*$/.test(line)) {
        html += '<hr>';
        i++;
        continue;
      }

      const hMatch = line.match(/^(#{1,6})\s+(.*)$/);
      if (hMatch) {
        const level = hMatch[1].length;
        const text = hMatch[2].trim();
        const slug = 'sec-' + headings.length;
        if (level <= 3) {
          headings.push({ level, text: text.replace(/[`*]/g, ''), slug });
        }
        html += '<h' + level + ' id="' + slug + '">' + formatInline(text, currentDocPath) + '</h' + level + '>';
        i++;
        continue;
      }

      if (line.includes('|') && i + 1 < lines.length && /^\s*\|?[\s:-]+\|[\s|:-]*$/.test(lines[i + 1])) {
        const splitRow = r => {
          let t = r.trim();
          if (t.startsWith('|')) t = t.slice(1);
          if (t.endsWith('|')) t = t.slice(0, -1);
          return t.split('|').map(c => c.trim());
        };
        const headers = splitRow(line);
        const aligns = splitRow(lines[i + 1]).map(a => {
          if (a.startsWith(':') && a.endsWith(':')) return 'center';
          if (a.endsWith(':')) return 'right';
          return 'left';
        });
        i += 2;
        let tbl = '<div class="table-scroll"><table><thead><tr>';
        headers.forEach((h, idx) => {
          tbl += '<th style="text-align:' + (aligns[idx] || 'left') + '">' + formatInline(h, currentDocPath) + '</th>';
        });
        tbl += '</tr></thead><tbody>';
        while (i < lines.length && lines[i].includes('|') && lines[i].trim() !== '') {
          const cells = splitRow(lines[i]);
          tbl += '<tr>';
          headers.forEach((_, idx) => {
            tbl += '<td style="text-align:' + (aligns[idx] || 'left') + '">' + formatInline(cells[idx] || '', currentDocPath) + '</td>';
          });
          tbl += '</tr>';
          i++;
        }
        tbl += '</tbody></table></div>';
        html += tbl;
        continue;
      }

      if (line.trimStart().startsWith('>')) {
        const qLines = [];
        while (i < lines.length && lines[i].trimStart().startsWith('>')) {
          qLines.push(lines[i].replace(/^\s*>\s?/, ''));
          i++;
        }
        html += '<blockquote>' + qLines.map(ql => '<p>' + formatInline(ql, currentDocPath) + '</p>').join('') + '</blockquote>';
        continue;
      }

      if (/^\s*[-*+]\s+/.test(line)) {
        html += '<ul>';
        while (i < lines.length && /^\s*[-*+]\s+/.test(lines[i])) {
          const itemText = lines[i].replace(/^\s*[-*+]\s+/, '');
          html += '<li>' + formatInline(itemText, currentDocPath) + '</li>';
          i++;
        }
        html += '</ul>';
        continue;
      }

      if (/^\s*\d+\.\s+/.test(line)) {
        html += '<ol>';
        while (i < lines.length && /^\s*\d+\.\s+/.test(lines[i])) {
          const itemText = lines[i].replace(/^\s*\d+\.\s+/, '');
          html += '<li>' + formatInline(itemText, currentDocPath) + '</li>';
          i++;
        }
        html += '</ol>';
        continue;
      }

      if (line.trim() === '') {
        i++;
        continue;
      }

      const pLines = [];
      while (
        i < lines.length &&
        lines[i].trim() !== '' &&
        !lines[i].trimStart().startsWith('#') &&
        !lines[i].trimStart().startsWith('```') &&
        !lines[i].trimStart().startsWith('>') &&
        !/^\s*[-*+]\s+/.test(lines[i]) &&
        !/^\s*\d+\.\s+/.test(lines[i])
      ) {
        pLines.push(formatInline(lines[i], currentDocPath));
        i++;
      }
      html += '<p>' + pLines.join(' ') + '</p>';
    }

    return { html, headings };
  }

  function colorizeSourceLine(rawLine) {
    let s = escapeHtml(rawLine);
    if (/^#{1,6}\s/.test(rawLine)) return '<span class="tok-h">' + s + '</span>';
    if (/^\s*&gt;/.test(s)) return '<span class="tok-q">' + s + '</span>';
    if (rawLine.trimStart().startsWith('|')) return '<span class="tok-t">' + s + '</span>';
    s = s.replace(/`([^`]+)`/g, '<span class="tok-c">`$1`</span>');
    s = s.replace(/\*\*([^*]+)\*\*/g, '<span class="tok-b">**$1**</span>');
    s = s.replace(/\[([^\]]+)\]\(([^)]+)\)/g, '<span class="tok-l">[$1]($2)</span>');
    return s;
  }

  function groupAllowedByScope(grp) {
    if (grp === 'Local Opened Files') return true;
    if (folderScope === 'plans') return grp === 'Plans' || grp.startsWith('Plans/');
    if (folderScope === 'docs') return grp === 'Documentation' || grp === 'Repository Root';
    if (folderScope === 'skills') return grp === 'AgenticInstructions';
    return true;
  }

  function orderedGroups() {
    const seen = [];
    for (const d of DOCS) {
      if (!seen.includes(d.group)) seen.push(d.group);
    }
    return seen.sort((a, b) => {
      const rank = g => {
        if (g === 'Plans' || g.startsWith('Plans/')) return 0;
        if (g === 'Local Opened Files') return 1;
        if (g === 'Documentation') return 2;
        if (g === 'AgenticInstructions') return 3;
        if (g === 'Repository Root') return 4;
        return 5;
      };
      return rank(a) - rank(b) || a.localeCompare(b);
    });
  }

  function renderTree() {
    const container = document.getElementById('treeContainer');
    const q = searchQuery.toLowerCase();
    const groups = orderedGroups();

    const plansTotal = DOCS.filter(d => String(d.group).startsWith('Plans')).length;
    document.getElementById('statPlansCount').textContent = String(plansTotal);
    document.getElementById('statAllCount').textContent = String(DOCS.length);
    document.getElementById('headerTotalLabel').textContent = DOCS.length + ' files';

    let visibleTotal = 0;
    let out = '';

    for (const grp of groups) {
      if (!groupAllowedByScope(grp)) continue;
      const items = DOCS.filter(d => d.group === grp && (
        !q ||
        d.name.toLowerCase().includes(q) ||
        d.path.toLowerCase().includes(q) ||
        String(d.content || '').toLowerCase().includes(q)
      ));
      if (items.length === 0) continue;
      visibleTotal += items.length;
      const collapsed = collapsedGroups.has(grp);

      out += '<div class="folder-group">';
      out += '<div class="folder-row" data-group="' + escapeHtml(grp) + '">';
      out += '<div class="folder-row-left">';
      out += '<span style="color:var(--fr-ink-muted);font-size:10px;width:10px;">' + (collapsed ? '▸' : '▾') + '</span>';
      out += '<span class="folder-icon"></span>';
      out += '<span>' + escapeHtml(grp) + '</span>';
      out += '</div>';
      out += '<span class="folder-badge">' + items.length + '</span>';
      out += '</div>';

      out += '<div class="folder-entries' + (collapsed ? ' collapsed' : '') + '">';
      for (const doc of items) {
        const cls = doc.path === activePath ? 'entry-row active' : 'entry-row';
        const byteCount = doc.size || new Blob([String(doc.content || '')]).size;
        out += '<div class="' + cls + '" data-open="' + escapeHtml(doc.path) + '" title="' + escapeHtml(doc.path) + '">';
        out += '<div class="entry-main">';
        out += '<span class="entry-square-icon">MD</span>';
        out += '<span class="entry-name">' + escapeHtml(doc.name) + '</span>';
        out += '</div>';
        out += '<span class="entry-size">' + formatSize(byteCount) + '</span>';
        out += '</div>';
      }
      out += '</div></div>';
    }

    container.innerHTML = out || '<div style="padding:16px;color:var(--fr-ink-muted);font-size:12px;">No matching Markdown documents.</div>';
    document.getElementById('leftFooterSummary').textContent = visibleTotal + ' VISIBLE';
  }

  function renderTabs() {
    const strip = document.getElementById('tabsStrip');
    const tabsHtml = openTabs.map(p => {
      const doc = docByPath.get(p);
      if (!doc) return '';
      const cls = p === activePath ? 'trap-tab active' : 'trap-tab';
      return (
        '<div class="' + cls + '" data-tab="' + escapeHtml(p) + '" title="' + escapeHtml(doc.path) + '">' +
        (p === activePath ? '<span class="tab-dot"></span>' : '') +
        '<span>' + escapeHtml(doc.name) + '</span>' +
        '<span class="tab-x" data-close="' + escapeHtml(p) + '">×</span>' +
        '</div>'
      );
    }).join('');

    strip.innerHTML =
      tabsHtml +
      '<button class="tab-add-disc" id="btnTabBarAdd" title="Open local .md file">+</button>';

    const addBtn = document.getElementById('btnTabBarAdd');
    if (addBtn) {
      addBtn.addEventListener('click', () => document.getElementById('localFileInput').click());
    }
  }

  function renderActiveDocument(triggerReveal) {
    const doc = docByPath.get(activePath);
    const mdBody = document.getElementById('mdBody');
    const sourcePane = document.getElementById('sourcePane');
    const previewPane = document.getElementById('previewPane');
    const stage = document.getElementById('documentStage');
    const outlineList = document.getElementById('outlineList');

    stage.classList.toggle('mode-split', viewMode === 'split');
    sourcePane.classList.toggle('hidden', viewMode === 'preview');
    previewPane.classList.toggle('hidden', viewMode === 'source');

    if (triggerReveal) {
      mdBody.style.animation = 'none';
      void mdBody.offsetWidth;
      mdBody.style.animation = '';
    }

    if (!doc) {
      document.getElementById('activeDocTitle').textContent = 'No document selected';
      document.getElementById('activeDocPath').textContent = 'Choose a .md file from Documents';
      document.getElementById('activeGroupBadge').textContent = 'IDLE';
      mdBody.innerHTML = '<p style="color:var(--fr-ink-muted);">Select a Markdown document in Documents or click <strong>+ Open .md</strong>.</p>';
      sourcePane.innerHTML = '';
      outlineList.innerHTML = '';
      document.getElementById('outlineCountPill').textContent = '0';
      return;
    }

    const content = String(doc.content || '');
    document.getElementById('activeDocTitle').textContent = doc.name;
    document.getElementById('activeDocPath').textContent = doc.path;
    document.getElementById('activeGroupBadge').textContent = String(doc.group).toUpperCase();

    const rawLines = content.replace(/\r\n/g, '\n').split('\n');
    sourcePane.innerHTML = rawLines.map((ln, idx) =>
      '<div class="source-line"><span class="line-num">' + (idx + 1) + '</span><span class="line-code">' +
      highlightQuery(colorizeSourceLine(ln)) + '</span></div>'
    ).join('');

    const parsed = parseMarkdown(content, doc.path);
    mdBody.innerHTML = highlightQuery('<div>' + parsed.html + '</div>');

    outlineList.innerHTML = parsed.headings.map(h =>
      '<div class="outline-row lvl-' + h.level + '" data-jump="' + h.slug + '" title="' + escapeHtml(h.text) + '">' +
      escapeHtml(h.text) + '</div>'
    ).join('');
    document.getElementById('outlineCountPill').textContent = String(parsed.headings.length);

    const words = content.trim() ? content.trim().split(/\s+/).filter(Boolean).length : 0;
    const sizeText = formatSize(new Blob([content]).size);

    document.getElementById('footerMetrics').textContent =
      'LINES ' + rawLines.length + ' · WORDS ' + words + ' · ' + sizeText;
    document.getElementById('metaGroup').textContent = doc.group;
    document.getElementById('metaLines').textContent = String(rawLines.length);
    document.getElementById('metaWords').textContent = String(words);
    document.getElementById('metaSize').textContent = sizeText;
    if (activeRightTab === 'inspector') {
      document.getElementById('rightFooterLeft').textContent = parsed.headings.length + ' Sections';
    }

    if (searchQuery && searchQuery.length >= 2) {
      const re = new RegExp(searchQuery.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), 'gi');
      const count = (content.match(re) || []).length;
      document.getElementById('matchInfo').textContent = count + (count === 1 ? ' match' : ' matches');
    } else {
      document.getElementById('matchInfo').textContent = '';
    }
  }

  async function ensureFreshFileContent(path, preserveScroll) {
    const existing = docByPath.get(path);
    if (!liveServerConnected || !existing || existing.localOnly) return;
    try {
      const resp = await fetch('api/file?path=' + encodeURIComponent(path), { cache: 'no-store' });
      if (!resp.ok) return;
      const fresh = await resp.json();
      if (existing.content !== fresh.content || existing.mtime !== fresh.mtime) {
        existing.content = fresh.content;
        existing.mtime = fresh.mtime;
        existing.size = fresh.size;
        if (path === activePath) {
          const prevPreviewTop = document.getElementById('previewPane').scrollTop;
          const prevSourceTop = document.getElementById('sourcePane').scrollTop;
          renderActiveDocument(false);
          if (preserveScroll) {
            document.getElementById('previewPane').scrollTop = prevPreviewTop;
            document.getElementById('sourcePane').scrollTop = prevSourceTop;
          }
        }
      }
    } catch (_) {}
  }

  async function syncLiveTree(preserveScroll) {
    try {
      const resp = await fetch('api/tree', { cache: 'no-store' });
      if (!resp.ok) return;
      const payload = await resp.json();
      if (!payload || !Array.isArray(payload.files)) return;

      liveServerConnected = true;
      document.getElementById('serverModeFooter').textContent = '● LIVE DISK';

      const localDocs = DOCS.filter(d => d.localOnly);
      const merged = [];
      for (const item of payload.files) {
        const prev = docByPath.get(item.path);
        const entry = {
          path: item.path,
          name: item.name,
          group: item.group,
          size: item.size,
          mtime: item.mtime,
          content: item.content !== undefined ? item.content : (prev ? prev.content : '')
        };
        docByPath.set(entry.path, entry);
        merged.push(entry);
      }
      for (const loc of localDocs) {
        merged.push(loc);
      }
      DOCS = merged;
      renderTree();
      renderTabs();
      if (activePath) {
        await ensureFreshFileContent(activePath, preserveScroll);
      }
    } catch (_) {}
  }

  async function openDocument(path) {
    if (!docByPath.has(path)) return;
    if (!openTabs.includes(path)) openTabs.push(path);
    activePath = path;
    renderTree();
    renderTabs();
    await ensureFreshFileContent(path, false);
    renderActiveDocument(true);
    animateScrollTo(document.getElementById('previewPane'), 0, 260);
    animateScrollTo(document.getElementById('sourcePane'), 0, 260);
  }

  function closeTab(path) {
    openTabs = openTabs.filter(p => p !== path);
    if (activePath === path) {
      activePath = openTabs[openTabs.length - 1] || '';
    }
    renderTree();
    renderTabs();
    renderActiveDocument(true);
  }

  const scopeTitles = {
    all: 'All Folders',
    plans: 'Plans Only',
    docs: 'Governance Docs',
    skills: 'Agentic Skills'
  };

  window.setFolderScope = function(scope) {
    folderScope = scope;
    document.getElementById('wellPlans').classList.toggle('selected', scope === 'plans');
    document.getElementById('wellAll').classList.toggle('selected', scope === 'all');
    document.getElementById('dropdownLabel').textContent = scopeTitles[scope] || 'All Folders';
    document.querySelectorAll('.studio-dropdown-option').forEach(opt => {
      opt.classList.toggle('active', opt.getAttribute('data-scope') === scope);
    });
    renderTree();
  };

  window.setViewMode = function(mode, btn) {
    viewMode = mode;
    document.querySelectorAll('.mode-pill').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    renderActiveDocument(true);
  };

  window.adjustZoom = function(delta) {
    zoomScale = Math.min(1.6, Math.max(0.75, Math.round((zoomScale + delta) * 100) / 100));
    document.documentElement.style.setProperty('--doc-scale', String(zoomScale));
    document.getElementById('zoomLabel').textContent = Math.round(zoomScale * 100) + '%';
  };

  window.resetZoom = function() {
    zoomScale = 1.0;
    document.documentElement.style.setProperty('--doc-scale', '1');
    document.getElementById('zoomLabel').textContent = '100%';
  };

  // =========================================================================
  // EVENT WIRING
  // =========================================================================
  // Close & Reopen Documents / Notepad / Inspector
  document.getElementById('btnCloseDocumentsTab').addEventListener('click', e => {
    e.stopPropagation();
    leftPaneOpen = false;
    syncPaneLayout();
  });

  document.getElementById('btnReopenDocuments').addEventListener('click', () => {
    leftPaneOpen = true;
    syncPaneLayout();
  });

  document.getElementById('rightTabStrip').addEventListener('click', e => {
    const closeBtn = e.target.closest('[data-rclose]');
    if (closeBtn) {
      e.stopPropagation();
      closeRightTab(closeBtn.getAttribute('data-rclose'));
      return;
    }
    const tab = e.target.closest('[data-rtab]');
    if (tab) {
      activeRightTab = tab.getAttribute('data-rtab');
      syncPaneLayout();
    }
  });

  document.getElementById('btnReopenNotepad').addEventListener('click', () => reopenRightTab('notepad'));
  document.getElementById('btnReopenInspector').addEventListener('click', () => reopenRightTab('inspector'));

  // Notepad actions
  document.getElementById('notepadTextarea').addEventListener('input', () => {
    noteDirty = true;
    updateNoteMetrics();
    setNoteStatus('● Editing...', '');
    clearTimeout(noteSaveTimer);
    noteSaveTimer = setTimeout(() => saveNoteToSession(false), 450);
  });

  document.getElementById('notepadTextarea').addEventListener('keydown', e => {
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') {
      e.preventDefault();
      clearTimeout(noteSaveTimer);
      saveNoteToSession(true);
      return;
    }
    if (e.key === 'Tab') {
      e.preventDefault();
      const ta = e.target;
      const start = ta.selectionStart;
      const end = ta.selectionEnd;
      ta.value = ta.value.substring(0, start) + '  ' + ta.value.substring(end);
      ta.selectionStart = ta.selectionEnd = start + 2;
      ta.dispatchEvent(new Event('input'));
    }
  });

  document.getElementById('noteFilenameInput').addEventListener('change', () => {
    const rel = normalizeNoteRelFromInput();
    loadNoteFromServer(rel);
  });

  document.getElementById('btnNewNote').addEventListener('click', () => {
    const stamp = new Date().toISOString().slice(11, 16).replace(':', '');
    document.getElementById('noteFilenameInput').value = 'Note-' + stamp + '.md';
    normalizeNoteRelFromInput();
    document.getElementById('notepadTextarea').value = '# Note (' + stamp + ')\n\n';
    updateNoteMetrics();
    saveNoteToSession(true);
    document.getElementById('notepadTextarea').focus();
  });

  document.getElementById('btnToggleMonoNote').addEventListener('click', function() {
    const ta = document.getElementById('notepadTextarea');
    const mono = ta.classList.toggle('mono-mode');
    this.classList.toggle('active', mono);
  });

  document.getElementById('btnWideNotepad').addEventListener('click', function() {
    rightWide = !rightWide;
    this.classList.toggle('active', rightWide);
    syncPaneLayout();
  });

  document.getElementById('btnSaveSession').addEventListener('click', () => {
    clearTimeout(noteSaveTimer);
    saveNoteToSession(true);
  });

  document.getElementById('btnPushGithub').addEventListener('click', pushNoteToGithub);

  document.getElementById('btnCopyNote').addEventListener('click', async function() {
    const text = document.getElementById('notepadTextarea').value;
    try {
      await navigator.clipboard.writeText(text);
      const orig = this.textContent;
      this.textContent = 'Copied ✓';
      setTimeout(() => { this.textContent = orig; }, 1200);
    } catch (_) {}
  });

  document.getElementById('btnDownloadNote').addEventListener('click', () => {
    normalizeNoteRelFromInput();
    const leaf = document.getElementById('noteFilenameInput').value || 'SessionNotes.md';
    const blob = new Blob([document.getElementById('notepadTextarea').value], { type: 'text/markdown;charset=utf-8' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = leaf;
    a.click();
    URL.revokeObjectURL(a.href);
  });

  // Smooth scroll to top button
  document.getElementById('btnScrollTop').addEventListener('click', () => {
    animateScrollTo(document.getElementById('previewPane'), 0, 360);
    animateScrollTo(document.getElementById('sourcePane'), 0, 360);
  });

  // Dropdown filter
  document.getElementById('dropdownBtn').addEventListener('click', e => {
    e.stopPropagation();
    document.getElementById('dropdownSheet').classList.toggle('hidden');
  });

  document.getElementById('dropdownSheet').addEventListener('click', e => {
    const opt = e.target.closest('[data-scope]');
    if (!opt) return;
    setFolderScope(opt.getAttribute('data-scope'));
    document.getElementById('dropdownSheet').classList.add('hidden');
  });

  document.addEventListener('click', e => {
    if (!e.target.closest('#dropdownWrap')) {
      document.getElementById('dropdownSheet').classList.add('hidden');
    }
  });

  document.getElementById('treeContainer').addEventListener('click', e => {
    const folder = e.target.closest('[data-group]');
    if (folder) {
      const g = folder.getAttribute('data-group');
      if (collapsedGroups.has(g)) collapsedGroups.delete(g);
      else collapsedGroups.add(g);
      renderTree();
      return;
    }
    const item = e.target.closest('[data-open]');
    if (item) openDocument(item.getAttribute('data-open'));
  });

  document.getElementById('tabsStrip').addEventListener('click', e => {
    const closeBtn = e.target.closest('[data-close]');
    if (closeBtn) {
      e.stopPropagation();
      closeTab(closeBtn.getAttribute('data-close'));
      return;
    }
    const tab = e.target.closest('[data-tab]');
    if (tab) openDocument(tab.getAttribute('data-tab'));
  });

  // Smooth animated Outline section jump + highlight flash
  document.getElementById('outlineList').addEventListener('click', e => {
    const entry = e.target.closest('[data-jump]');
    if (!entry) return;
    const target = document.getElementById(entry.getAttribute('data-jump'));
    const container = document.getElementById('previewPane');
    if (target && container) {
      const containerRect = container.getBoundingClientRect();
      const targetRect = target.getBoundingClientRect();
      const desiredTop = container.scrollTop + (targetRect.top - containerRect.top) - 24;
      animateScrollTo(container, desiredTop, 380);
      target.classList.remove('section-highlight');
      void target.offsetWidth;
      target.classList.add('section-highlight');
    }
  });

  document.getElementById('mdBody').addEventListener('click', e => {
    const link = e.target.closest('[data-doclink]');
    if (link) {
      e.preventDefault();
      openDocument(link.getAttribute('data-doclink'));
    }
  });

  document.getElementById('searchInput').addEventListener('input', e => {
    searchQuery = e.target.value.trim();
    renderTree();
    renderActiveDocument(false);
  });

  document.getElementById('btnRefreshTree').addEventListener('click', () => syncLiveTree(true));

  for (const id of ['btnOpenLocalMain', 'btnOpenLocalIcon', 'btnAddTabLeft']) {
    document.getElementById(id).addEventListener('click', () => {
      document.getElementById('localFileInput').click();
    });
  }

  document.getElementById('localFileInput').addEventListener('change', async e => {
    const files = Array.from(e.target.files || []);
    for (const f of files) {
      const text = await f.text();
      const virtualPath = 'Local/' + f.name;
      const entry = {
        path: virtualPath,
        name: f.name,
        group: 'Local Opened Files',
        size: f.size,
        mtime: Date.now() / 1000,
        content: text,
        localOnly: true
      };
      docByPath.set(virtualPath, entry);
      if (!DOCS.some(d => d.path === virtualPath)) DOCS.unshift(entry);
      openDocument(virtualPath);
    }
    e.target.value = '';
  });

  // Initialize UI
  syncPaneLayout();
  renderTree();
  renderTabs();
  renderActiveDocument(false);
  loadNoteFromServer(currentNoteRel);

  syncLiveTree(true);
  setInterval(() => {
    if (liveServerConnected && !noteDirty) syncLiveTree(true);
  }, 3000);
})();
</script>
</body>
</html>
"""


def render_viewer_html() -> str:
    docs = collect_markdown_documents(include_content=True)
    payload = json.dumps(docs, ensure_ascii=False).replace("</", "<\\/")
    return HTML_TEMPLATE.replace("__DOCUMENTS_JSON__", payload)


def main() -> None:
    html_out = render_viewer_html()
    OUTPUT_HTML.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_HTML.write_text(html_out, encoding="utf-8")
    docs_count = len(collect_markdown_documents(include_content=False))
    print(f"[BuildMarkdownViewer] Embedded {docs_count} .md documents -> {OUTPUT_HTML.relative_to(REPO_ROOT)}")


if __name__ == "__main__":
    main()
