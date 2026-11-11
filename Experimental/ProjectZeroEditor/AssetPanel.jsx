import React, { useEffect, useMemo, useRef, useState } from "react";
import DrawerPanel from "./DrawerPanel.jsx";
import ShaderballPanel from "./ShaderballPanel.jsx";
import { ResolveMaterial } from "./MaterialSpecification.js";
import {
  AssetFile,
  AssetKinds,
  DownloadAsset,
  EngineAssets,
  ImportAsset,
} from "./AssetDepot.js";

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
}) {
  const BuiltIn = useMemo(() => EngineAssets(), []),
    Library = [...Assets, ...BuiltIn];
  const [Origin, ChooseOrigin] = useState("All"),
    [Kind, ChooseKind] = useState("Materials"),
    [Query, Search] = useState(""),
    [View, Layout] = useState("grid"),
    [Selection, Select] = useState("engine-material-0"),
    [Message, Report] = useState(""),
    [Busy, Work] = useState(false),
    [Rename, RenameDraft] = useState("");
  const Input = useRef(null),
    Locked = useRef(false);
  const Visible = Library.filter(
    (Asset) =>
      (Origin === "All" || Asset.Origin === Origin) &&
      (Kind === "All" || Asset.Kind === Kind) &&
      Asset.Name.toLowerCase().includes(Query.toLowerCase()),
  );
  const Current = Library.find((Asset) => Asset.Id === Selection);
  const Bound = Subject?.Panel === "geometry";
  const [TargetId, ChooseTarget] = useState(Bound ? Subject.Id : "");
  useEffect(() => {
    if (Subject?.Panel === "geometry") ChooseTarget(Subject.Id);
  }, [Subject?.Id]);
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
        Added.push(await ImportAsset(File));
      } catch (Error) {
        Errors.push(Error.message);
      }
    }
    if (Added.length) {
      Store((Previous) => [...Previous, ...Added]);
      ChooseOrigin("Imported");
      ChooseKind("All");
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
  return (
    <DrawerPanel
      Edge="bottom"
      Open={Open}
      Toggle={Toggle}
      Activate={Activate}
      Label="Asset Browser"
      Name="Asset Browser notch"
      ClassName="asset-drawer"
    >
      <div className="asset-browser">
        <header className="asset-browser-header drawer-drag-zone">
          <div>
            <span className="asset-eyebrow">CONTENT / LIBRARY</span>
            <h1>
              Asset Browser <small>{Library.length} assets</small>
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
              aria-label="Close Asset Browser"
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
          <input
            type="search"
            aria-label="Search assets"
            placeholder="Search this library…"
            value={Query}
            onChange={(Event) => Search(Event.target.value)}
          />
          <div className="asset-view-switch">
            {["grid", "list"].map((Name) => (
              <button
                key={Name}
                aria-label={Name + " asset view"}
                aria-pressed={View === Name}
                onClick={() => Layout(Name)}
              >
                {Name === "grid" ? "▦" : "☷"}
              </button>
            ))}
          </div>
        </div>
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
              Project and imported assets stay in this browser.
            </p>
          </nav>
          <section className="asset-results">
            <header className="drawer-drag-zone">
              <h2>{Kind === "All" ? "All assets" : Kind}</h2>
              <span>{Visible.length} results</span>
            </header>
            <div className={"asset-catalogue " + View} data-drawer-scroll>
              {Visible.map((Asset) => (
                <button
                  className={
                    "asset-cell " + (Asset.Id === Selection ? "selected" : "")
                  }
                  key={Asset.Id}
                  data-asset-id={Asset.Id}
                  aria-label={"Select asset " + Asset.Name}
                  aria-pressed={Asset.Id === Selection}
                  onClick={() => {
                    Select(Asset.Id);
                    RenameDraft("");
                  }}
                  onDoubleClick={() => {
                    Select(Asset.Id);
                    if (Asset.Kind === "Materials" && Asset.Origin !== "Engine")
                      EditMaterial(Asset.Id);
                  }}
                >
                  <span className="asset-thumbnail">
                    {Asset.Kind === "Materials" ? (
                      <span
                        className="asset-material-orb"
                        style={{ "--orb": Asset.Material.Channels.colour.Fill }}
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
                      {Asset.Kind} · {Asset.Origin}
                    </small>
                  </span>
                </button>
              ))}
              {!Visible.length && (
                <div className="asset-empty">
                  <h3>No assets found</h3>
                  <p>Change the filters, import files or create a material.</p>
                  <button
                    onClick={() => {
                      Search("");
                      ChooseKind("All");
                      ChooseOrigin("All");
                    }}
                  >
                    Clear filters
                  </button>
                </div>
              )}
            </div>
          </section>
          <aside className="asset-preview" data-drawer-scroll>
            {Current ? (
              <>
                <div className="asset-preview-heading">
                  <span className="asset-eyebrow">ASSET DETAILS</span>
                  <span className="asset-origin-label">{Current.Origin}</span>
                </div>
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
                        onChange={(Event) => ChooseTarget(Event.target.value)}
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
                        Apply(structuredClone(Current.Material), Target.Id);
                        Report(
                          "Applied an independent material copy to " +
                            Target.Name,
                        );
                      }}
                    >
                      Apply copy to {Target ? Target.Name : "selected object"}
                    </button>
                    <button
                      disabled={
                        Busy ||
                        (Current.Origin === "Engine" && Assets.length >= 256)
                      }
                      onClick={() => {
                        if (Current.Origin === "Engine") {
                          const Copy = Create(Current.Material, Current.Name);
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
                <h3>Select an asset</h3>
                <p>Inspect its source and preview here.</p>
              </div>
            )}
          </aside>
        </div>
        <footer className="asset-browser-footer">
          <span role="status">
            {Message ||
              "Drag the header or page grip to close · scroll the catalogue independently"}
          </span>
          <span>HTML LIBRARY · NO NATIVE ASSET PIPELINE</span>
        </footer>
      </div>
    </DrawerPanel>
  );
}
