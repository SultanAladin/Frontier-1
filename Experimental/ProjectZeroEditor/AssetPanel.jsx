import React, { useEffect, useMemo, useRef, useState } from "react";
import DrawerPanel from "./DrawerPanel.jsx";
import ShaderballPanel from "./ShaderballPanel.jsx";
import { ResolveMaterial } from "./MaterialSpecification.js";
import {
  AssetFile,
  AssetKinds,
  DownloadAsset,
  DownloadColourCodexToml,
  EngineAssets,
  ImportAsset,
  ImportColourCodexToml,
} from "./AssetDepot.js";
import {
  ChooseCardInk,
  ColourFilterCategories,
  ColourSortModes,
  MatchesColourFilter,
  NormalizeHex,
  PackColourDragData,
  ResolveColourEntry,
  SortColourAssets,
  SplitCardHeading,
} from "./ColourCodex.js";

function FilePreview({ Asset }) {
  const [Source, Show] = useState(Asset.Data || ""),
    [Status, Report] = useState("Loading original…"),
    [Family, Font] = useState("");
  useEffect(() => {
    let Cancelled = false,
      Url,
      Face;
    (async () => {
      try {
        const File = Asset.Data
          ? await (await fetch(Asset.Data)).blob()
          : await AssetFile(Asset.Id);
        if (!File)
          throw Error("Original bytes unavailable. Reimport this file.");
        if (Cancelled) return;
        if (Asset.Kind === "Fonts") {
          Face = new FontFace(
            "Asset" + Asset.Id.replace(/[^a-z0-9]/gi, ""),
            await File.arrayBuffer(),
          );
          await Face.load();
          if (Cancelled) return;
          document.fonts.add(Face);
          Font(Face.family);
          Report("Font loaded · preview only");
        } else if (Asset.Kind === "Images" || Asset.Kind === "Icons") {
          Url = URL.createObjectURL(File);
          Show(Url);
          Report(
            Asset.Width
              ? `${Asset.Width} × ${Asset.Height}`
              : "Embedded engine preview",
          );
        } else Report("File stored · native decoding/execution not connected");
      } catch (Error) {
        if (!Cancelled) Report(Error.message);
      }
    })();
    return () => {
      Cancelled = true;
      if (Url) URL.revokeObjectURL(Url);
      if (Face) document.fonts.delete(Face);
    };
  }, [Asset.Id]);
  return (
    <div className="asset-file-preview">
      {Asset.Kind === "Fonts" ? (
        <div
          className="font-specimen"
          style={{ fontFamily: Family || "inherit" }}
        >
          <strong>Aa</strong>
          <p>
            The quick brown fox
            <br />
            jumps over the lazy dog.
          </p>
          <small>0123456789 &@?!</small>
        </div>
      ) : Source ? (
        <img src={Source} alt={Asset.Name} />
      ) : (
        <div className="asset-file-symbol">
          {Asset.Kind === "Models" ? "◇" : Asset.Kind === "Code" ? "{ }" : "▤"}
        </div>
      )}
      <p>{Status}</p>
    </div>
  );
}

const BrowserThemes = {
  OLED: { Panel: "#080809", Section: "#121214", Inset: "#0d0d10", Raised: "#1e1e22", Selected: "#1b2434", Border: "rgba(255,255,255,0.08)", Text: "#f2f2f5", Muted: "#8d9099" },
  Dark: { Panel: "#121214", Section: "#1b1b1f", Inset: "#151518", Raised: "#27272c", Selected: "#232d40", Border: "rgba(255,255,255,0.09)", Text: "#ededf0", Muted: "#9497a1" },
  Light: { Panel: "#f4f5f7", Section: "#ffffff", Inset: "#eaecf0", Raised: "#e2e5eb", Selected: "#dbe7ff", Border: "rgba(18,22,30,0.12)", Text: "#15171c", Muted: "#5e6470" },
  Sepia: { Panel: "#f2eadb", Section: "#fbf6ec", Inset: "#e7decb", Raised: "#dfd3bc", Selected: "#e5d5b5", Border: "rgba(56,42,20,0.14)", Text: "#2b2216", Muted: "#726149" },
  Dracula: { Panel: "#1e1f29", Section: "#282a36", Inset: "#191a23", Raised: "#343746", Selected: "#383c56", Border: "rgba(255,255,255,0.10)", Text: "#f8f8f2", Muted: "#9ea4c2" },
  Nord: { Panel: "#242933", Section: "#2e3440", Inset: "#1f242d", Raised: "#3b4252", Selected: "#3b4b5e", Border: "rgba(216,222,233,0.11)", Text: "#eceff4", Muted: "#9aa5b8" },
  GitHub: { Panel: "#0d1117", Section: "#161b22", Inset: "#090d12", Raised: "#21262d", Selected: "#1f2d42", Border: "rgba(240,246,252,0.10)", Text: "#e6edf3", Muted: "#8b949e" },
};

function BrowserSvgIcon({ Name }) {
  return (
    <svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      {Name === "search" && <><circle cx="11" cy="11" r="7" /><path d="m21 21-4.35-4.35" /></>}
      {Name === "grid" && <><rect x="3" y="3" width="7" height="7" rx="1.4" /><rect x="14" y="3" width="7" height="7" rx="1.4" /><rect x="3" y="14" width="7" height="7" rx="1.4" /><rect x="14" y="14" width="7" height="7" rx="1.4" /></>}
      {Name === "list" && <><path d="M4 6h.01M9 6h11M4 12h.01M9 12h11M4 18h.01M9 18h11" /></>}
      {Name === "vertical" && <><rect x="3" y="4" width="18" height="16" rx="2" /><path d="M9.5 4v16M14.5 4v16" /></>}
      {Name === "horizontal" && <><rect x="3" y="4" width="18" height="16" rx="2" /><path d="M3 9.5h18M3 14.5h18" /></>}
    </svg>
  );
}

export default function AssetPanel({
  Open,
  Toggle,
  Activate,
  Assets,
  Store,
  Subject,
  Values,
  Apply,
  EditMaterial,
  Targets = [],
  ApplyColour,
  ActiveColourTargetLabel,
  StageChange,
  OnColourDragChange,
  Appearance = {},
}) {
  const BuiltIn = useMemo(() => EngineAssets(), []),
    Library = [...Assets, ...BuiltIn];
  const AllColours = useMemo(
    () => Library.filter((Item) => Item.Kind === "Colours"),
    [Library],
  );
  const [Origin, ChooseOrigin] = useState("All"),
    [Kind, ChooseKind] = useState("Materials"),
    [Query, Search] = useState(""),
    [View, Layout] = useState("grid"),
    [ColourView, SetColourView] = useState("grid"),
    [ColourFilter, SetColourFilter] = useState("All"),
    [ColourSort, SetColourSort] = useState("spectrum"),
    [Selection, Select] = useState("engine-material-0"),
    [Message, Report] = useState(""),
    [Busy, Work] = useState(false),
    [Rename, RenameDraft] = useState(""),
    [HexDraft, EditHexDraft] = useState("#3B1F3A"),
    [DrawerStage, SetDrawerStage] = useState("closed"),
    [RequestedRest, RequestRest] = useState(null);
  const Input = useRef(null),
    Locked = useRef(false),
    RestToken = useRef(0);

  const SnapDrawerTo = (Value) => {
    RestToken.current += 1;
    RequestRest({ Value, Token: RestToken.current });
  };

  useEffect(() => {
    const OnColourApplied = (Event) => {
      const Detail = Event.detail;
      if (Detail?.Colour && Detail?.TargetLabel) {
        Report(
          `Applied ${Detail.Colour.Name} (${Detail.Colour.Hex}) → ${Detail.TargetLabel}`,
        );
      }
    };
    window.addEventListener("frontier:colour-applied", OnColourApplied);
    return () =>
      window.removeEventListener("frontier:colour-applied", OnColourApplied);
  }, []);

  const ColoursInOriginAndSearch = useMemo(
    () =>
      AllColours.filter(
        (Asset) =>
          (Origin === "All" || Asset.Origin === Origin) &&
          (
            Asset.Name +
            " " +
            (Asset.Colour?.Hex || "") +
            " " +
            (Asset.Colour?.Family || "")
          )
            .toLowerCase()
            .includes(Query.toLowerCase()),
      ),
    [AllColours, Origin, Query],
  );

  const ColourFilterCounts = useMemo(
    () =>
      Object.fromEntries(
        ColourFilterCategories.map((Category) => [
          Category,
          ColoursInOriginAndSearch.filter((Asset) =>
            MatchesColourFilter(Asset.Colour, Category),
          ).length,
        ]),
      ),
    [ColoursInOriginAndSearch],
  );

  const Visible = useMemo(() => {
    if (Kind === "Colours") {
      const FilteredColours = ColoursInOriginAndSearch.filter((Asset) =>
        MatchesColourFilter(Asset.Colour, ColourFilter),
      );
      return SortColourAssets(FilteredColours, ColourSort);
    }
    return Library.filter(
      (Asset) =>
        (Origin === "All" || Asset.Origin === Origin) &&
        (Kind === "All" || Asset.Kind === Kind) &&
        (Asset.Name + " " + (Asset.Colour?.Hex || ""))
          .toLowerCase()
          .includes(Query.toLowerCase()),
    );
  }, [
    Kind,
    ColoursInOriginAndSearch,
    ColourFilter,
    ColourSort,
    Library,
    Origin,
    Query,
  ]);

  const Current =
    Library.find((Asset) => Asset.Id === Selection) || Visible[0] || null;
  const Bound = Subject?.Panel === "geometry";
  const [TargetId, ChooseTarget] = useState(Bound ? Subject.Id : "");
  useEffect(() => {
    if (Subject?.Panel === "geometry") ChooseTarget(Subject.Id);
  }, [Subject?.Id]);
  useEffect(() => {
    if (Current?.Kind === "Colours" && Current?.Colour?.Hex) {
      EditHexDraft(Current.Colour.Hex);
    }
  }, [Current?.Id, Current?.Colour?.Hex]);
  const Target = Targets.find((Item) => Item.Id === TargetId);

  const Create = (
    Material = ResolveMaterial({}),
    Name = "Untitled material",
  ) => {
    if (Assets.length >= 256) {
      Report(
        "Library limit reached (256 project/imported assets). Remove an asset first.",
      );
      return null;
    }
    const Names = new Set(Assets.map((Asset) => Asset.Name));
    let Unique = Name,
      Index = 2;
    while (Names.has(Unique)) Unique = Name + " " + Index++;
    const Asset = {
      Id: "asset-" + crypto.randomUUID(),
      Name: Unique,
      Kind: "Materials",
      Origin: "Project",
      Material: { ...structuredClone(Material), Name: Unique },
      Detail:
        "Browser-authored material. Applying makes an independent object copy.",
    };
    Store((Previous) => [...Previous, Asset]);
    ChooseOrigin("Project");
    ChooseKind("Materials");
    Search("");
    Select(Asset.Id);
    Report("Created " + Unique);
    return Asset;
  };

  const CreateColour = (InitialHex = null, BaseName = "Untitled colour") => {
    if (Assets.length >= 256) {
      Report(
        "Library limit reached (256 project/imported assets). Remove an asset first.",
      );
      return null;
    }
    const Presets = ["#3B1F3A", "#F6B800", "#2454FF", "#E63956", "#10B981"];
    const Names = new Set(Library.map((Asset) => Asset.Name));
    let Unique = BaseName,
      Index = 2;
    while (Names.has(Unique)) Unique = BaseName + " " + Index++;
    const ColourCount = Assets.filter((Item) => Item.Kind === "Colours").length;
    const Colour = ResolveColourEntry({
      Name: Unique,
      Hex: InitialHex || Presets[ColourCount % Presets.length],
      Family: "Project",
    });
    const Asset = {
      Id: "asset-" + crypto.randomUUID(),
      Name: Unique,
      Kind: "Colours",
      Origin: "Project",
      Colour,
      Detail:
        "Browser-authored ColourCodex alias · drag onto any colour slot or double-click to apply.",
    };
    Store((Previous) => [...Previous, Asset]);
    ChooseOrigin("Project");
    ChooseKind("Colours");
    SetColourFilter("All");
    Search("");
    Select(Asset.Id);
    Report(`Created ${Unique} (${Colour.Hex})`);
    return Asset;
  };

  const BeginColourDrag = (Event, ColourEntry) => {
    PackColourDragData(Event, ColourEntry);
    document.body.classList.add("dragging-codex-colour");
    OnColourDragChange?.(true);
    if (DrawerStage === "full") {
      SnapDrawerTo(0.46);
    }
  };

  const EndColourDrag = () => {
    document.body.classList.remove("dragging-codex-colour");
    OnColourDragChange?.(false);
  };

  const TriggerDoubleClickColour = (Asset) => {
    if (!Asset?.Colour) return;
    const AppliedLabel = ApplyColour?.(Asset.Colour);
    if (AppliedLabel) {
      Report(
        `Applied ${Asset.Colour.Name} (${Asset.Colour.Hex}) → ${AppliedLabel}`,
      );
    } else {
      Report("Select an object or focus a colour slot first to apply a colour.");
    }
  };

  const UpdateSelectedColourHex = (RawHex) => {
    if (!Current || Current.Origin === "Engine" || Current.Kind !== "Colours")
      return;
    const CleanHex = NormalizeHex(RawHex, Current.Colour.Hex);
    const NextColour = ResolveColourEntry({
      ...Current.Colour,
      Name: Current.Name,
      Hex: CleanHex,
    });
    Store((Previous) =>
      Previous.map((Asset) =>
        Asset.Id === Current.Id ? { ...Asset, Colour: NextColour } : Asset,
      ),
    );
    EditHexDraft(CleanHex);
  };

  const Import = async (Event) => {
    if (Locked.current) return;
    const Files = [...Event.target.files];
    Event.target.value = "";
    if (!Files.length) return;
    Locked.current = true;
    Work(true);
    const Added = [],
      Errors = [];
    for (const File of Files.slice(0, Math.min(32, 256 - Assets.length))) {
      try {
        if (File.name.toLowerCase().endsWith(".toml")) {
          const ParsedColours = await ImportColourCodexToml(File);
          Added.push(
            ...ParsedColours.slice(
              0,
              Math.max(0, 256 - Assets.length - Added.length),
            ),
          );
        } else {
          Added.push(await ImportAsset(File));
        }
      } catch (Error) {
        Errors.push(Error.message);
      }
    }
    if (Added.length) {
      Store((Previous) => [...Previous, ...Added]);
      ChooseOrigin("Imported");
      ChooseKind(
        Added.every((Item) => Item.Kind === Added[0].Kind)
          ? Added[0].Kind
          : "All",
      );
      SetColourFilter("All");
      Search("");
      Select(Added[0].Id);
    }
    Report(
      `${Added.length} imported${Errors.length ? " · " + Errors.join(" · ") : " · originals saved in this browser"}${Files.length > Math.min(32, 256 - Assets.length) ? " · batch or library limit reached; remaining files were not imported" : ""}`,
    );
    Work(false);
    Locked.current = false;
  };

  const Remove = async () => {
    if (!Current || Current.Origin === "Engine") return;
    try {
      await AssetFile(Current.Id, undefined, true);
      Store((Previous) => Previous.filter((Asset) => Asset.Id !== Current.Id));
      Select((Previous) => (Previous === Current.Id ? null : Previous));
      Report(
        "Removed from this browser library; object material copies are unchanged.",
      );
    } catch (Error) {
      Report(Error.message);
    }
  };

  const CardInk =
    Current?.Kind === "Colours" && Current?.Colour
      ? ChooseCardInk(Current.Colour.Hex)
      : null;

  const ActiveCatalogueMode = Kind === "Colours" ? ColourView : View;
  const FirstStripInk =
    Kind === "Colours" && Visible[0]?.Colour
      ? ChooseCardInk(Visible[0].Colour.Hex)
      : null;

  const ThemeSpec = BrowserThemes[Appearance?.Theme] || BrowserThemes.Dark;
  const BrowserThemeVars = {
    "--cb-panel": ThemeSpec.Panel,
    "--cb-section": ThemeSpec.Section,
    "--cb-inset": ThemeSpec.Inset,
    "--cb-raised": ThemeSpec.Raised,
    "--cb-selected": ThemeSpec.Selected,
    "--cb-border": ThemeSpec.Border,
    "--cb-text": ThemeSpec.Text,
    "--cb-muted": ThemeSpec.Muted,
    "--cb-accent": Appearance?.Accent || "#3b82f6",
    "--cb-radius": (Appearance?.["Corner radius"] ?? 16) + "px",
  };

  return (
    <DrawerPanel
      Edge="bottom"
      Open={Open}
      Toggle={Toggle}
      Activate={Activate}
      Label="ContentBrowser"
      Name="ContentBrowser notch"
      ClassName="asset-drawer"
      RequestedRest={RequestedRest}
      StageChange={(NextStage) => {
        SetDrawerStage(NextStage);
        StageChange?.(NextStage);
      }}
    >
      <div className="asset-browser" aria-label="ContentBrowser" style={BrowserThemeVars}>
        <header className="asset-browser-header drawer-drag-zone">
          <div>
            <span className="asset-eyebrow">CONTENT / LIBRARY</span>
            <h1>
              ContentBrowser <small>{Library.length} items</small>
            </h1>
          </div>
          <div className="asset-header-actions">
            <button
              disabled={Busy || Assets.length >= 256}
              onClick={() => Create()}
            >
              + Material
            </button>
            <button
              disabled={Busy || Assets.length >= 256}
              onClick={() => CreateColour()}
            >
              + Colour
            </button>
            <button
              disabled={!Bound || Busy || Assets.length >= 256}
              title={
                Bound
                  ? "Save a copy of the selected object's material"
                  : "Select a geometry object first"
              }
              onClick={() =>
                Create(ResolveMaterial(Values), Subject.Name + " material")
              }
            >
              Save selected material
            </button>
            {Kind === "Colours" && (
              <button
                onClick={() => {
                  DownloadColourCodexToml(AllColours, "ColourCodex.toml");
                  Report(
                    `Exported ${AllColours.length} colours to ColourCodex.toml`,
                  );
                }}
              >
                Export .toml
              </button>
            )}
            <button
              className="primary"
              disabled={Busy || Assets.length >= 256}
              onClick={() => Input.current.click()}
            >
              {Busy ? "Importing…" : "Import files"}
            </button>
            <input
              ref={Input}
              type="file"
              multiple
              hidden
              aria-label="Import assets"
              onChange={Import}
            />
            <button
              className="asset-split-toggle"
              aria-label="Toggle split content shelf"
              aria-pressed={DrawerStage === "half"}
              title="Dock ContentBrowser to bottom half so scene colour slots stay visible above"
              onClick={() => SnapDrawerTo(DrawerStage === "half" ? 1 : 0.46)}
            >
              {DrawerStage === "half" ? "⬜ Full" : "⬒ Split"}
            </button>
            <button
              aria-label="Close ContentBrowser"
              onClick={() => Toggle(false)}
            >
              ×
            </button>
          </div>
        </header>
        <div className="asset-browser-tools">
          <div
            className="asset-origin-tabs"
            role="group"
            aria-label="Asset origin"
          >
            {["All", "Project", "Imported", "Engine"].map((Name) => (
              <button
                key={Name}
                aria-pressed={Name === Origin}
                onClick={() => ChooseOrigin(Name)}
              >
                {Name}
              </button>
            ))}
          </div>
          <div className="asset-search-wrap">
            <BrowserSvgIcon Name="search" />
            <input
              type="search"
              aria-label="Search assets"
              placeholder="Search ContentBrowser…"
              value={Query}
              onChange={(Event) => Search(Event.target.value)}
            />
            {Query && (
              <button
                type="button"
                className="asset-search-clear"
                aria-label="Clear search"
                onClick={() => Search("")}
              >
                ×
              </button>
            )}
          </div>
          {Kind === "Colours" ? (
            <div
              className="asset-view-switch colour-view-switch"
              role="group"
              aria-label="Colour layout"
            >
              <button
                aria-label="grid asset view"
                title="Grid view (circle swatches)"
                aria-pressed={ColourView === "grid"}
                onClick={() => SetColourView("grid")}
              >
                <BrowserSvgIcon Name="grid" />
                <span>Grid</span>
              </button>
              <button
                aria-label="vertical list colour view"
                title="List view · Vertical colour strips"
                aria-pressed={ColourView === "list-vertical"}
                onClick={() => SetColourView("list-vertical")}
              >
                <BrowserSvgIcon Name="vertical" />
                <span>Vertical</span>
              </button>
              <button
                aria-label="horizontal list colour view"
                title="List view · Horizontal colour strips"
                aria-pressed={ColourView === "list-horizontal"}
                onClick={() => SetColourView("list-horizontal")}
              >
                <BrowserSvgIcon Name="horizontal" />
                <span>Horizontal</span>
              </button>
            </div>
          ) : (
            <div
              className="asset-view-switch"
              role="group"
              aria-label="Content layout"
            >
              {["grid", "list"].map((Name) => (
                <button
                  key={Name}
                  aria-label={Name + " asset view"}
                  aria-pressed={View === Name}
                  onClick={() => Layout(Name)}
                >
                  <BrowserSvgIcon Name={Name} />
                  <span>{Name === "grid" ? "Grid" : "List"}</span>
                </button>
              ))}
            </div>
          )}
        </div>
        {Kind === "Colours" && (
          <div
            className="colour-filters-bar"
            role="region"
            aria-label="Colour Filters"
          >
            <span className="colour-filters-title">Colour Filters</span>
            <div
              className="colour-filter-chips"
              role="group"
              aria-label="Colour filter family"
            >
              {ColourFilterCategories.map((Category) => (
                <button
                  key={Category}
                  aria-pressed={ColourFilter === Category}
                  onClick={() => SetColourFilter(Category)}
                >
                  <span>{Category}</span>
                  <small>{ColourFilterCounts[Category]}</small>
                </button>
              ))}
            </div>
            <label className="colour-sort-select">
              Sort
              <select
                aria-label="Sort colours"
                value={ColourSort}
                onChange={(Event) => SetColourSort(Event.target.value)}
              >
                {ColourSortModes.map((Mode) => (
                  <option key={Mode.Id} value={Mode.Id}>
                    {Mode.Label}
                  </option>
                ))}
              </select>
            </label>
          </div>
        )}
        <div className="asset-browser-body">
          <nav
            className="asset-category-rail"
            aria-label="Asset categories"
            data-drawer-scroll
          >
            {AssetKinds.map((Name) => (
              <button
                key={Name}
                aria-pressed={Name === Kind}
                onClick={() => ChooseKind(Name)}
              >
                <span>{Name}</span>
                <small>
                  {
                    Library.filter(
                      (Asset) =>
                        (Name === "All" || Asset.Kind === Name) &&
                        (Origin === "All" || Asset.Origin === Origin),
                    ).length
                  }
                </small>
              </button>
            ))}
            <p>
              Engine files are read-only.
              <br />
              Project and imported items stay in this browser.
            </p>
          </nav>
          <section className="asset-results">
            <header className="drawer-drag-zone">
              <h2>
                {Kind === "All"
                  ? "All content"
                  : Kind === "Colours" && ColourFilter !== "All"
                    ? `Colours · ${ColourFilter}`
                    : Kind}
              </h2>
              <span>{Visible.length} results</span>
            </header>
            {Kind === "Colours" &&
            (ActiveCatalogueMode === "list-vertical" ||
              ActiveCatalogueMode === "list-horizontal") ? (
              <div
                className={
                  "asset-catalogue colour-strip-deck " + ActiveCatalogueMode
                }
                data-drawer-scroll
                aria-label={
                  ActiveCatalogueMode === "list-vertical"
                    ? "Vertical colour strip list"
                    : "Horizontal colour strip list"
                }
              >
                {ActiveCatalogueMode === "list-vertical" &&
                  Visible.length > 0 &&
                  FirstStripInk && (
                    <span
                      className="colour-strip-banner"
                      style={{ color: FirstStripInk.text }}
                    >
                      {ColourFilter === "All"
                        ? "ColourCodex Spectrum"
                        : ColourFilter}
                    </span>
                  )}
                {Visible.map((Asset) => {
                  const StripInk = ChooseCardInk(Asset.Colour.Hex);
                  const IsSelected = Current?.Id === Asset.Id;
                  return (
                    <button
                      key={Asset.Id}
                      data-asset-id={Asset.Id}
                      className={
                        "colour-strip-band " + (IsSelected ? "selected" : "")
                      }
                      aria-label={"Select asset " + Asset.Name}
                      aria-pressed={IsSelected}
                      draggable
                      onDragStart={(Event) =>
                        BeginColourDrag(Event, Asset.Colour)
                      }
                      onDragEnd={EndColourDrag}
                      onClick={() => {
                        Select(Asset.Id);
                        RenameDraft("");
                      }}
                      onDoubleClick={() => {
                        Select(Asset.Id);
                        TriggerDoubleClickColour(Asset);
                      }}
                      style={{
                        backgroundColor: Asset.Colour.Hex,
                        color: StripInk.text,
                      }}
                      title={`${Asset.Name} · ${Asset.Colour.Hex} (Drag to colour slot or double-click)`}
                    >
                      {ActiveCatalogueMode === "list-vertical" ? (
                        <span className="colour-strip-vertical-hex">
                          {Asset.Colour.CompactHex}
                        </span>
                      ) : (
                        <>
                          <strong className="colour-strip-horizontal-name">
                            {Asset.Name}
                          </strong>
                          <span className="colour-strip-horizontal-meta">
                            <b>{Asset.Colour.CompactHex}</b>
                            <small style={{ color: StripInk.muted }}>
                              RGB {Asset.Colour.RGB.join(", ")}
                            </small>
                          </span>
                        </>
                      )}
                    </button>
                  );
                })}
                {!Visible.length && (
                  <div className="asset-empty">
                    <h3>No colours match filter</h3>
                    <p>Choose another Colour Filter or clear search.</p>
                    <button
                      onClick={() => {
                        Search("");
                        SetColourFilter("All");
                        ChooseOrigin("All");
                      }}
                    >
                      Clear filters
                    </button>
                  </div>
                )}
              </div>
            ) : (
              <div
                className={"asset-catalogue " + ActiveCatalogueMode}
                data-drawer-scroll
              >
                {Visible.map((Asset) => (
                  <button
                    className={
                      "asset-cell " +
                      (Current?.Id === Asset.Id ? "selected " : "") +
                      (Asset.Kind === "Colours" ? "colour-asset-cell" : "")
                    }
                    key={Asset.Id}
                    data-asset-id={Asset.Id}
                    aria-label={"Select asset " + Asset.Name}
                    aria-pressed={Current?.Id === Asset.Id}
                    draggable={Asset.Kind === "Colours"}
                    onDragStart={
                      Asset.Kind === "Colours"
                        ? (Event) => BeginColourDrag(Event, Asset.Colour)
                        : undefined
                    }
                    onDragEnd={
                      Asset.Kind === "Colours" ? EndColourDrag : undefined
                    }
                    onClick={() => {
                      Select(Asset.Id);
                      RenameDraft("");
                    }}
                    onDoubleClick={() => {
                      Select(Asset.Id);
                      if (
                        Asset.Kind === "Materials" &&
                        Asset.Origin !== "Engine"
                      )
                        EditMaterial(Asset.Id);
                      else if (Asset.Kind === "Colours")
                        TriggerDoubleClickColour(Asset);
                    }}
                  >
                    <span className="asset-thumbnail">
                      {Asset.Kind === "Materials" ? (
                        <span
                          className="asset-material-orb"
                          style={{
                            "--orb": Asset.Material.Channels.colour.Fill,
                          }}
                        />
                      ) : Asset.Kind === "Colours" ? (
                        <span
                          className="asset-colour-circle"
                          style={{ backgroundColor: Asset.Colour.Hex }}
                        />
                      ) : Asset.Kind === "Fonts" ? (
                        <span className="asset-font-icon">Aa</span>
                      ) : (Asset.Thumbnail || Asset.Data) &&
                        ["Images", "Icons"].includes(Asset.Kind) ? (
                        <img
                          src={Asset.Thumbnail || Asset.Data}
                          alt=""
                          loading="lazy"
                        />
                      ) : (
                        <span className="asset-kind-icon">
                          {Asset.Kind === "Models"
                            ? "◇"
                            : Asset.Kind === "Code"
                              ? "{ }"
                              : "▤"}
                        </span>
                      )}
                    </span>
                    <span className="asset-cell-name">
                      {Asset.Name}
                      <small>
                        {Asset.Kind === "Colours"
                          ? `${Asset.Colour.Hex} · ${Asset.Origin}`
                          : `${Asset.Kind} · ${Asset.Origin}`}
                      </small>
                    </span>
                  </button>
                ))}
                {!Visible.length && (
                  <div className="asset-empty">
                    <h3>No content found</h3>
                    <p>
                      Change the filters, import files or create a material or
                      colour.
                    </p>
                    <button
                      onClick={() => {
                        Search("");
                        SetColourFilter("All");
                        ChooseKind("All");
                        ChooseOrigin("All");
                      }}
                    >
                      Clear filters
                    </button>
                  </div>
                )}
              </div>
            )}
          </section>
          <aside className="asset-preview" data-drawer-scroll>
            {Current ? (
              <>
                <div className="asset-preview-heading">
                  <span className="asset-eyebrow">CONTENT DETAILS</span>
                  <span className="asset-origin-label">{Current.Origin}</span>
                </div>
                {Current.Kind === "Colours" && Current.Colour && CardInk ? (
                  <>
                    <div
                      className="codex-swatch-card"
                      data-contrast={CardInk.contrastLabel}
                      draggable
                      onDragStart={(Event) =>
                        BeginColourDrag(Event, Current.Colour)
                      }
                      onDragEnd={EndColourDrag}
                      onDoubleClick={() => TriggerDoubleClickColour(Current)}
                      style={{
                        backgroundColor: Current.Colour.Hex,
                        color: CardInk.text,
                        borderColor: CardInk.border,
                      }}
                      title="Drag onto any colour slot or double-click to apply"
                    >
                      <div className="codex-card-title">
                        {SplitCardHeading(Current.Name).map((Line, Index) => (
                          <span key={Index}>{Line}</span>
                        ))}
                      </div>
                      <div
                        className="codex-card-specs"
                        style={{ color: CardInk.muted }}
                      >
                        <div>
                          <b style={{ color: CardInk.text }}>HEX:</b>{" "}
                          {Current.Colour.Hex}
                        </div>
                        <div>
                          <b style={{ color: CardInk.text }}>RGB:</b>{" "}
                          {Current.Colour.RGB.join(", ")}
                        </div>
                        <div>
                          <b style={{ color: CardInk.text }}>CMYK:</b>{" "}
                          {Current.Colour.CMYK.join(", ")}
                        </div>
                      </div>
                    </div>
                    <p>{Current.Detail}</p>
                    <dl>
                      <dt>Type</dt>
                      <dd>ColourCodex</dd>
                      <dt>Filter</dt>
                      <dd>{Current.Colour.FilterGroup}</dd>
                      <dt>Target slot</dt>
                      <dd>{ActiveColourTargetLabel || "Selected item"}</dd>
                    </dl>
                    {Current.Origin !== "Engine" && (
                      <div className="codex-hex-edit">
                        <label>
                          Edit colour
                          <div className="codex-hex-controls">
                            <input
                              type="color"
                              aria-label="Edit codex colour picker"
                              value={Current.Colour.Hex.toLowerCase()}
                              onChange={(Event) =>
                                UpdateSelectedColourHex(Event.target.value)
                              }
                            />
                            <input
                              aria-label="Edit codex colour hex"
                              value={HexDraft}
                              maxLength={7}
                              onChange={(Event) =>
                                EditHexDraft(Event.target.value)
                              }
                              onBlur={() => UpdateSelectedColourHex(HexDraft)}
                              onKeyDown={(Event) => {
                                if (Event.key === "Enter")
                                  Event.currentTarget.blur();
                              }}
                            />
                          </div>
                        </label>
                      </div>
                    )}
                    <div className="asset-material-actions">
                      <button
                        className="primary"
                        aria-label="Apply colour to active slot"
                        onClick={() => TriggerDoubleClickColour(Current)}
                      >
                        Apply to {ActiveColourTargetLabel || "colour slot"}
                      </button>
                      <button
                        onClick={() => {
                          DownloadColourCodexToml(
                            AllColours,
                            "ColourCodex.toml",
                          );
                          Report(
                            `Exported ${AllColours.length} colours to ColourCodex.toml`,
                          );
                        }}
                      >
                        Export ColourCodex.toml
                      </button>
                    </div>
                    <div className="asset-file-actions">
                      <button
                        onClick={async () => {
                          try {
                            await DownloadAsset(Current);
                            Report("Downloaded " + Current.Name + ".toml");
                          } catch (Error) {
                            Report(Error.message);
                          }
                        }}
                      >
                        Download .toml
                      </button>
                      {Current.Origin === "Engine" ? (
                        <button
                          disabled={Busy || Assets.length >= 256}
                          onClick={() =>
                            CreateColour(
                              Current.Colour.Hex,
                              Current.Name + " copy",
                            )
                          }
                        >
                          Copy to Project
                        </button>
                      ) : (
                        <button onClick={Remove}>Remove asset</button>
                      )}
                    </div>
                  </>
                ) : (
                  <>
                    <h2>{Current.Name}</h2>
                    {Current.Kind === "Materials" ? (
                      <ShaderballPanel
                        key={Current.Id}
                        Material={Current.Material}
                        Compact
                      />
                    ) : Current.Id === "engine-shaderball" ? (
                      <ShaderballPanel Material={ResolveMaterial({})} Compact />
                    ) : (
                      <FilePreview key={Current.Id} Asset={Current} />
                    )}
                    <p>{Current.Detail}</p>
                    <dl>
                      <dt>Type</dt>
                      <dd>{Current.Kind}</dd>
                      <dt>Owner</dt>
                      <dd>
                        {Current.Origin === "Engine"
                          ? "Engine library"
                          : "Browser project"}
                      </dd>
                      {Current.Size !== undefined && (
                        <>
                          <dt>File size</dt>
                          <dd>{(Current.Size / 1024).toFixed(1)} KiB</dd>
                        </>
                      )}
                    </dl>
                    {Current.Kind === "Materials" && (
                      <div className="asset-material-actions">
                        <label className="asset-assign-target">
                          Apply to
                          <select
                            aria-label="Material assignment target"
                            value={Target?.Id || ""}
                            onChange={(Event) =>
                              ChooseTarget(Event.target.value)
                            }
                          >
                            <option value="" disabled>
                              Select an object
                            </option>
                            {Targets.map((Item) => (
                              <option key={Item.Id} value={Item.Id}>
                                {Item.Name}
                              </option>
                            ))}
                          </select>
                        </label>
                        <button
                          className="primary"
                          disabled={!Target}
                          onClick={() => {
                            Apply(
                              structuredClone(Current.Material),
                              Target.Id,
                            );
                            Report(
                              "Applied an independent material copy to " +
                                Target.Name,
                            );
                          }}
                        >
                          Apply copy to{" "}
                          {Target ? Target.Name : "selected object"}
                        </button>
                        <button
                          disabled={
                            Busy ||
                            (Current.Origin === "Engine" &&
                              Assets.length >= 256)
                          }
                          onClick={() => {
                            if (Current.Origin === "Engine") {
                              const Copy = Create(
                                Current.Material,
                                Current.Name,
                              );
                              if (Copy) EditMaterial(Copy.Id);
                            } else EditMaterial(Current.Id);
                          }}
                        >
                          {Current.Origin === "Engine"
                            ? "Copy & edit in ShaderEditor"
                            : "Edit in ShaderEditor"}
                        </button>
                      </div>
                    )}
                    <div className="asset-file-actions">
                      <button
                        onClick={async () => {
                          try {
                            await DownloadAsset(Current);
                            Report("Downloaded " + Current.Name);
                          } catch (Error) {
                            Report(Error.message);
                          }
                        }}
                      >
                        Download
                        {Current.Kind === "Materials" ? " material" : " file"}
                      </button>
                      {Current.Origin !== "Engine" && (
                        <button onClick={Remove}>Remove asset</button>
                      )}
                    </div>
                  </>
                )}
                {Current.Origin !== "Engine" && (
                  <form
                    className="asset-rename"
                    onSubmit={(Event) => {
                      Event.preventDefault();
                      const Name = Rename.trim();
                      if (!Name) return;
                      Store((Previous) =>
                        Previous.map((Asset) =>
                          Asset.Id === Current.Id
                            ? {
                                ...Asset,
                                Name,
                                ...(Asset.Material
                                  ? { Material: { ...Asset.Material, Name } }
                                  : {}),
                                ...(Asset.Colour
                                  ? {
                                      Colour: ResolveColourEntry({
                                        ...Asset.Colour,
                                        Name,
                                      }),
                                    }
                                  : {}),
                              }
                            : Asset,
                        ),
                      );
                      Report("Asset renamed");
                      RenameDraft("");
                    }}
                  >
                    <input
                      aria-label="Asset name"
                      placeholder={Current.Name}
                      value={Rename}
                      maxLength={100}
                      onChange={(Event) => RenameDraft(Event.target.value)}
                    />
                    <button disabled={!Rename.trim()}>Rename</button>
                  </form>
                )}
              </>
            ) : (
              <div className="asset-empty">
                <h3>Select content</h3>
                <p>Inspect its source and preview here.</p>
              </div>
            )}
          </aside>
        </div>
        <footer className="asset-browser-footer">
          <span role="status">
            {Message ||
              (Kind === "Colours"
                ? "Drag any colour swatch/strip onto a colour slot or double-click to apply"
                : "Drag the header or page grip to close · scroll the catalogue independently")}
          </span>
          <span>CONTENTBROWSER · HTML PREVIEW</span>
        </footer>
      </div>
    </DrawerPanel>
  );
}
