//==========================================================================================================================================
//                                                    LEVELDIALOGMODAL.JSX
//==========================================================================================================================================
// Construct-style Create Level modal with rich 16:9 visual stage cards, category rail, search, and instant level provisioning.

import React, { useEffect, useRef, useState } from "react";
import { Glyph } from "./Inspectors.jsx";
import { LEVEL_TEMPLATES } from "./LevelSystem.js";

const LEVEL_GROUPS = ["All", "Architectural", "Automotive", "Look-Dev", "Blank"];

function StagePreviewArtwork({ TemplateId }) {
  if (TemplateId === "gallery") {
    return (
      <svg viewBox="0 0 240 124" className="level-stage-svg" aria-hidden="true">
        <defs>
          <linearGradient id="lg-gal-bg" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stopColor="#23252a" />
            <stop offset="100%" stopColor="#141518" />
          </linearGradient>
          <radialGradient id="lg-gal-soft" cx="50%" cy="14%" r="60%">
            <stop offset="0%" stopColor="rgba(255,236,204,0.42)" />
            <stop offset="100%" stopColor="rgba(255,236,204,0)" />
          </radialGradient>
        </defs>
        <rect width="240" height="124" fill="url(#lg-gal-bg)" />
        <polygon points="0,0 54,26 54,96 0,124" fill="#6b2329" opacity="0.72" />
        <polygon points="240,0 186,26 186,96 240,124" fill="#1e5c45" opacity="0.72" />
        <rect x="54" y="26" width="132" height="70" fill="#2a2b2f" stroke="rgba(255,255,255,0.08)" />
        <polygon points="54,96 186,96 240,124 0,124" fill="#202125" />
        <rect x="86" y="10" width="68" height="7" rx="2" fill="#fff4df" />
        <rect width="240" height="124" fill="url(#lg-gal-soft)" />
        <rect x="101" y="76" width="38" height="20" rx="2" fill="#b79146" stroke="rgba(255,238,190,0.35)" />
        <circle cx="120" cy="60" r="16" fill="#d9dfe8" />
        <circle cx="114" cy="54" r="5" fill="#ffffff" opacity="0.82" />
        <circle cx="78" cy="83" r="12" fill="#cfc9bf" opacity="0.9" />
        <circle cx="164" cy="84" r="11" fill="rgba(190,226,255,0.36)" stroke="rgba(225,244,255,0.75)" strokeWidth="1.2" />
      </svg>
    );
  }

  if (TemplateId === "lobby") {
    return (
      <svg viewBox="0 0 240 124" className="level-stage-svg" aria-hidden="true">
        <defs>
          <linearGradient id="lg-lobby-wall" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stopColor="#33291f" />
            <stop offset="100%" stopColor="#1f1a15" />
          </linearGradient>
          <radialGradient id="lg-lobby-glow" cx="50%" cy="16%" r="58%">
            <stop offset="0%" stopColor="rgba(255,228,178,0.4)" />
            <stop offset="100%" stopColor="rgba(255,228,178,0)" />
          </radialGradient>
        </defs>
        <rect width="240" height="124" fill="url(#lg-lobby-wall)" />
        <rect x="0" y="92" width="240" height="32" fill="#171310" />
        <rect x="0" y="88" width="240" height="4" fill="#3d332a" />
        {/* Timber accent wall (left) */}
        <rect x="0" y="20" width="46" height="72" fill="#241811" />
        <path d="M8 24V88M18 24V88M28 24V88M38 24V88" stroke="#3a2a1e" strokeWidth="3" />
        {/* Ceiling wash + pendant glow */}
        <rect x="86" y="8" width="68" height="5" rx="2.5" fill="#fff2dc" opacity="0.92" />
        <rect width="240" height="124" fill="url(#lg-lobby-glow)" />
        {/* Light trainer aircraft silhouette (hanging display) */}
        <g transform="translate(166,34) scale(0.92)" stroke="#cfd6de" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" fill="#2b3138">
          <path d="M-26 0H26" />
          <path d="M0 -8V10" />
          <path d="M-9 -8L0 -12L9 -8" fill="none" />
          <path d="M-6 10L0 13L6 10" fill="none" />
        </g>
        {/* ControlVehicle silhouette */}
        <g transform="translate(72,72)">
          <path d="M-32 10C-30 2 -18 -2 -6 -2C6 -2 16 -4 24 2C29 6 30 8 30 10Z" fill="#d7dde6" stroke="#8f97a3" strokeWidth="0.8" />
          <path d="M-14 -1C-10 -6 2 -6.4 8 -2Z" fill="#141b24" />
          <circle cx="-18" cy="11" r="5" fill="#0d0f12" stroke="#5b6470" strokeWidth="1.1" />
          <circle cx="14" cy="11" r="5" fill="#0d0f12" stroke="#5b6470" strokeWidth="1.1" />
        </g>
      </svg>
    );
  }

  if (TemplateId === "lookdev") {
    return (
      <svg viewBox="0 0 240 124" className="level-stage-svg" aria-hidden="true">
        <defs>
          <radialGradient id="lg-ld-glow" cx="50%" cy="38%" r="62%">
            <stop offset="0%" stopColor="#2e3542" />
            <stop offset="100%" stopColor="#15171c" />
          </radialGradient>
        </defs>
        <rect width="240" height="124" fill="url(#lg-ld-glow)" />
        <path d="M0 88Q120 74 240 88V124H0Z" fill="#1c1f26" />
        <ellipse cx="120" cy="95" rx="34" ry="7" fill="#6b7280" opacity="0.55" />
        <circle cx="120" cy="66" r="20" fill="#2a67d4" />
        <circle cx="113" cy="58" r="6" fill="#a8cbff" opacity="0.78" />
        <ellipse cx="68" cy="84" rx="16" ry="9" fill="none" stroke="#cf4568" strokeWidth="5" />
        <circle cx="172" cy="80" r="13" fill="#32b683" opacity="0.9" />
      </svg>
    );
  }

  if (TemplateId === "duplicate") {
    return (
      <svg viewBox="0 0 240 124" className="level-stage-svg" aria-hidden="true">
        <rect width="240" height="124" fill="#17181b" />
        <rect x="44" y="22" width="112" height="66" rx="7" fill="#222429" stroke="rgba(255,255,255,0.16)" strokeWidth="1.2" />
        <rect x="82" y="38" width="112" height="66" rx="7" fill="#292c33" stroke="#dededb" strokeWidth="1.3" />
        <circle cx="124" cy="71" r="13" fill="rgba(222,222,219,0.2)" stroke="#dededb" strokeWidth="1.2" />
        <path d="M154 63V79M146 71H162" stroke="#dededb" strokeWidth="1.6" strokeLinecap="round" />
      </svg>
    );
  }

  // Empty Proving Ground
  return (
    <svg viewBox="0 0 240 124" className="level-stage-svg" aria-hidden="true">
      <rect width="240" height="124" fill="#16181c" />
      <line x1="0" y1="68" x2="240" y2="68" stroke="rgba(255,255,255,0.14)" strokeWidth="1" />
      <path d="M0 84H240M0 102H240M40 68L0 124M80 68L48 124M120 68V124M160 68L192 124M200 68L240 124" stroke="rgba(255,255,255,0.08)" strokeWidth="1" />
      <circle cx="186" cy="30" r="10" fill="rgba(255,214,150,0.22)" stroke="rgba(255,214,150,0.6)" strokeWidth="1.2" />
      <polygon points="120,52 138,62 138,82 120,92 102,82 102,62" fill="#2d3036" stroke="#d4d4d0" strokeWidth="1.2" />
      <path d="M102,62L120,72L138,62M120,72V92" stroke="rgba(255,255,255,0.35)" strokeWidth="1" />
    </svg>
  );
}

export default function LevelDialogModal({
  Open,
  LevelCount = 1,
  ActiveLevelName = "Cornell-Studio Gallery",
  OnCancel,
  OnCreate,
}) {
  const [LevelName, SetLevelName] = useState("");
  const [TemplateId, SetTemplateId] = useState("gallery");
  const [Group, SetGroup] = useState("All");
  const [Query, SetQuery] = useState("");
  const [Notes, SetNotes] = useState("");
  const NameInputRef = useRef(null);
  const DialogRef = useRef(null);

  useEffect(() => {
    if (!Open) return;
    SetLevelName(`Stage ${String(LevelCount + 1).padStart(2, "0")}`);
    SetTemplateId("gallery");
    SetGroup("All");
    SetQuery("");
    SetNotes("");
    const Timer = window.setTimeout(() => {
      NameInputRef.current?.focus();
      NameInputRef.current?.select();
    }, 24);
    return () => window.clearTimeout(Timer);
  }, [Open, LevelCount]);

  if (!Open) return null;

  const FilteredTemplates = LEVEL_TEMPLATES.filter(
    (Item) =>
      (Group === "All" || Item.Category === Group) &&
      (Item.Title.toLowerCase().includes(Query.trim().toLowerCase()) ||
        Item.Subtitle.toLowerCase().includes(Query.trim().toLowerCase())),
  );

  const PickedTemplate =
    LEVEL_TEMPLATES.find((Item) => Item.Id === TemplateId) || LEVEL_TEMPLATES[0];

  const Submit = (OverrideTemplateId) => {
    const ChosenId = typeof OverrideTemplateId === "string" ? OverrideTemplateId : TemplateId;
    const ChosenMeta = LEVEL_TEMPLATES.find((T) => T.Id === ChosenId);
    const FinalName =
      LevelName.trim() ||
      ChosenMeta?.Title ||
      `Stage ${String(LevelCount + 1).padStart(2, "0")}`;
    OnCreate?.({
      Name: FinalName,
      TemplateId: ChosenId,
      Notes: Notes.trim(),
    });
  };

  const HandleKeyDown = (Ev) => {
    Ev.stopPropagation();
    if (Ev.key === "Escape") {
      Ev.preventDefault();
      OnCancel?.();
    } else if (Ev.key === "Enter" && Ev.target?.tagName !== "TEXTAREA") {
      Ev.preventDefault();
      Submit();
    }
  };

  return (
    <div
      className="modal-scrim construct-scrim"
      role="presentation"
      onClick={(Ev) => {
        if (Ev.target === Ev.currentTarget) OnCancel?.();
      }}
    >
      <section
        className="construct-dialog level-construct-dialog"
        role="dialog"
        aria-modal="true"
        aria-label="Create New Level"
        ref={DialogRef}
        onKeyDown={HandleKeyDown}
      >
        <header className="construct-titlebar">
          <h2>Create Level</h2>
          <button type="button" aria-label="Close level dialog" onClick={OnCancel}>
            <Glyph Name="close" Size={14} />
          </button>
        </header>

        <div className="construct-path">
          <strong>LEVELS / FRONTIER</strong>
          <span className="active">01 Stage Template</span>
          <Glyph Name="arrow" Size={12} />
          <span className="active">02 World Details</span>
          <kbd>{LevelCount} Loaded</kbd>
        </div>

        <label className="construct-search">
          <Glyph Name="search" Size={16} />
          <input
            placeholder="Search stage templates (Cornell, Cone Slalom, Look-Dev, Empty)…"
            aria-label="Search level templates"
            value={Query}
            onChange={(Ev) => SetQuery(Ev.target.value)}
          />
          {Query && (
            <button
              type="button"
              aria-label="Clear template search"
              onClick={() => SetQuery("")}
            >
              <Glyph Name="close" Size={13} />
            </button>
          )}
        </label>

        <div className="construct-catalogue level-construct-body">
          <nav className="construct-categories" aria-label="Level template categories">
            {LEVEL_GROUPS.map((Label) => (
              <button
                key={Label}
                type="button"
                className={Group === Label ? "active" : ""}
                aria-pressed={Group === Label}
                onClick={() => SetGroup(Label)}
              >
                {Label}
              </button>
            ))}
          </nav>

          <div className="construct-results level-construct-results">
            <div className="level-stage-grid" role="radiogroup" aria-label="Starting Stage Template">
              {FilteredTemplates.map((Item) => {
                const Active = TemplateId === Item.Id;
                const Sub =
                  Item.Id === "duplicate"
                    ? `Clone "${ActiveLevelName}" with all entities, forces & OpenPBR materials`
                    : Item.Subtitle;
                return (
                  <button
                    key={Item.Id}
                    type="button"
                    role="radio"
                    aria-checked={Active}
                    className={"level-stage-card" + (Active ? " active" : "")}
                    onClick={() => SetTemplateId(Item.Id)}
                    onDoubleClick={() => Submit(Item.Id)}
                  >
                    <div className="level-stage-artwork">
                      <StagePreviewArtwork TemplateId={Item.Id} />
                      <span className="level-stage-badge">{Item.Badge || Item.Category}</span>
                      {Active && (
                        <span className="level-stage-selected-dot" aria-hidden="true">
                          ✓
                        </span>
                      )}
                    </div>
                    <div className="level-stage-meta">
                      <div className="level-stage-heading">
                        <strong>{Item.Title}</strong>
                      </div>
                      <p>{Sub}</p>
                      <span className="level-stage-spec">{Item.Spec}</span>
                    </div>
                  </button>
                );
              })}
            </div>

            {!FilteredTemplates.length && (
              <p className="construct-empty" role="status">
                No matching stage templates.
              </p>
            )}

            <div className="level-construct-form">
              <label className="level-construct-field">
                <span>Level Name</span>
                <input
                  ref={NameInputRef}
                  type="text"
                  value={LevelName}
                  maxLength={64}
                  placeholder="e.g. Exterior Courtyard, Cone Chicane…"
                  onChange={(Ev) => SetLevelName(Ev.target.value)}
                />
              </label>
              <label className="level-construct-field">
                <span>World Notes</span>
                <input
                  type="text"
                  value={Notes}
                  maxLength={140}
                  placeholder={`Optional notes · Template: ${PickedTemplate.Title}`}
                  onChange={(Ev) => SetNotes(Ev.target.value)}
                />
              </label>
            </div>
          </div>
        </div>

        <footer>
          <small>
            Template: <strong style={{ color: "#d8d8d5", fontWeight: 500 }}>{PickedTemplate.Title}</strong> · Double-click any card to create immediately
          </small>
          <button type="button" onClick={OnCancel}>
            Cancel
          </button>
          <button
            type="button"
            className="construct-add"
            onClick={() => Submit()}
          >
            Create level
            <Glyph Name="plus" Size={14} />
          </button>
        </footer>
      </section>
    </div>
  );
}
