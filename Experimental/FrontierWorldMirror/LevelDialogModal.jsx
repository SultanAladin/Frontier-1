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

  if (TemplateId === "slalom") {
    return (
      <svg viewBox="0 0 240 124" className="level-stage-svg" aria-hidden="true">
        <defs>
          <linearGradient id="lg-auto-sky" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stopColor="#1b2432" />
            <stop offset="52%" stopColor="#262b34" />
            <stop offset="53%" stopColor="#17191d" />
            <stop offset="100%" stopColor="#111216" />
          </linearGradient>
        </defs>
        <rect width="240" height="124" fill="url(#lg-auto-sky)" />
        <path d="M24 124L96 64H144L216 124Z" fill="#1e2127" />
        <path d="M120 66V122" stroke="#e2b95b" strokeWidth="1.6" strokeDasharray="6 5" opacity="0.65" />
        <path d="M40 124L100 64M200 124L140 64" stroke="rgba(255,255,255,0.18)" strokeWidth="1.2" />
        {/* Traffic Cone (cone-like cone) */}
        <g transform="translate(68,68)">
          <polygon points="2,34 10,30 22,30 30,34 25,38 7,38" fill="#252830" />
          <polygon points="8,33 14,6 18,6 24,33" fill="#ff5414" />
          <polygon points="10.8,20 12.2,13.5 19.8,13.5 21.2,20" fill="#f5f7fc" />
        </g>
        {/* Center Traffic Cone */}
        <g transform="translate(116,54) scale(0.78)">
          <polygon points="2,34 10,30 22,30 30,34 25,38 7,38" fill="#252830" />
          <polygon points="8,33 14,6 18,6 24,33" fill="#ff5414" />
          <polygon points="10.8,20 12.2,13.5 19.8,13.5 21.2,20" fill="#f5f7fc" />
        </g>
        {/* Cylinder Cone (cylinder-like cone) */}
        <g transform="translate(154,62)">
          <ellipse cx="16" cy="39" rx="12" ry="3.4" fill="#23262e" />
          <polygon points="11,38 12.4,5 19.6,5 21,38" fill="#f03e18" />
          <polygon points="11.9,16 12.1,11 19.9,11 20.1,16" fill="#f5f7fc" />
          <polygon points="11.4,26 11.7,21 20.3,21 20.6,26" fill="#f5f7fc" />
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
