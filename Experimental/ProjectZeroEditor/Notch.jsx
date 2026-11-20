import DrawerPanel from "./DrawerPanel.jsx";
import React, { useState, useRef, useEffect } from "react";
import { Glyph, Control, Card, Icon } from "./Inspectors.jsx";
const Qualities = ["Minimal", "Economy", "Standard", "Ultra", "Reference"];
const Channels = [
  "01 base colour",
  "02 metallic",
  "03 roughness",
  "04 reflectance/IOR",
  "05 orientation",
  "06 occlusion",
  "07 emission",
  "08 opacity",
  "09 anisotropy",
  "10 anisotropy direction",
  "11 clear coat",
  "12 coat roughness",
  "13 coat orientation",
  "14 sheen colour",
  "15 sheen roughness",
  "16 subsurface colour",
  "17 subsurface thickness",
  "18 transmission",
  "19 IOR (refraction)",
  "20 displacement",
];
export default function Notch({
  Open,
  Toggle,
  Settings,
  Assign,
  Name,
  Activate,
}) {
  const [Page, SelectPage] = useState("Dashboard"),
    [Tab, SelectTab] = useState("Fonts"),
    [Draft, Change] = useState({ ...Settings }),
    [Dirty, SetDirty] = useState(false),
    [Message, Say] = useState("");
  useEffect(() => {
    if (Open) {
      Change({ ...Settings });
      SetDirty(false);
    } else {
      const Timer = setTimeout(() => SelectPage("Dashboard"), 250);
      return () => clearTimeout(Timer);
    }
  }, [Open]);
  const Edit = (Key, Value) => {
    if (Page === "Render Settings") Assign(Key, Value);
    Change((Previous) => ({ ...Previous, [Key]: Value }));
    SetDirty(true);
  };
  const Field = (
    Label,
    ControlType = "Switch",
    Options = [],
    Default = true,
  ) => {
    const Key =
      Label === "Resolution" && Page === "Render Settings"
        ? "Shadow Resolution"
        : Label;
    const Field = {
      Label,
      Control: ControlType,
      Default,
      Options,
      Minimum: 0,
      Maximum: 100,
      Decimals: 0,
      Unit: "%",
    };
    return (
      <Control
        key={Label}
        Field={Field}
        Value={Draft[Key] ?? Default}
        Change={(Value) => Edit(Key, Value)}
      />
    );
  };
  const Slider = (Label, Minimum, Maximum, Default, Unit = "") => (
    <Control
      Field={{
        Label,
        Control: "Slider",
        Minimum,
        Maximum,
        Default,
        Unit,
        Decimals: 2,
      }}
      Value={Draft[Label] ?? Default}
      Change={(Value) => Edit(Label, Value)}
    />
  );
  const MapSide = Draft["Shadow Resolution"]
    ? [0, 256, 512, 1024, 2048][Draft["Shadow Resolution"]]
    : [256, 512, 1024, 2048, 2048][Settings.Quality];
  const Cycle = (Key, Count) => Assign(Key, (Settings[Key] + 1) % Count);
  const Titles = {
    "Render Settings": "Render Settings",
    Appearance: "Display Settings",
    Input: "Keybindings Setup",
    Notifications: "Telemetry & Notifications",
    Materials: "Materials",
  };
  const Subtitles = {
    "Render Settings": "Configure output rendering quality and passes.",
    Appearance: "Appearance & typography",
    Input: "Configure navigation style and keyboard shortcuts.",
    Notifications: "System resource overlay and alert preferences.",
    Materials: "Per-material selections, channels, and fold report.",
  };
  useEffect(() => {
    Change({
      ...Settings,
      "Global Illumination": Settings.GI,
      Reflections: Settings.Reflection,
      "FPS Overlay": Settings.FPS,
    });
    SetDirty(false);
  }, [Page]);
  const Save = () => {
    Object.entries(Draft).forEach(([Key, Value]) => Assign(Key, Value));
    SetDirty(false);
    Say("Applied to HTML preview only");
    setTimeout(() => Say(""), 3000);
  };
  return (
    <DrawerPanel
      Edge="top"
      Open={Open}
      Toggle={Toggle}
      Activate={Activate}
      Label={Name}
      Name="Project-Zero control center notch"
      ClassName="control-drawer"
      Escape={() =>
        Page !== "Dashboard" ? SelectPage("Dashboard") : Toggle(false)
      }
    >
      <div className="shade">
        <div className="drawer-backdrop" aria-hidden="true" />
        {Page === "Dashboard" ? (
          <div className="control-dashboard">
            <header>
              Control Center{" "}
              <span>
                <Glyph Name="WirelessSignal" Size={16} />
                <button
                  aria-label="Control Center settings"
                  onClick={() => SelectPage("Settings")}
                >
                  <Glyph Name="gear" Size={16} />
                </button>
              </span>
            </header>
            <div className="dashboard-grid">
              {[
                [
                  `GI: ${Settings.GI ? Settings.GI + " Bounces" : "Off"}`,
                  "sun",
                  !!Settings.GI,
                  () => Cycle("GI", 5),
                ],
                [
                  `Refl: ${["Off", "Sky", "Raytraced"][Settings.Reflection]}`,
                  "SparklesAntiAliasing",
                  !!Settings.Reflection,
                  () => Cycle("Reflection", 3),
                ],
                [
                  "Anti-Aliasing",
                  "SparklesAntiAliasing",
                  Settings.AA,
                  () => Assign("AA", !Settings.AA),
                ],
                [
                  "FPS Overlay",
                  "GaugeFrameRate",
                  Settings.FPS,
                  () => Assign("FPS", !Settings.FPS),
                ],
                [
                  "Notifications",
                  "NotificationsBell",
                  Settings.Notifications,
                  () => Assign("Notifications", !Settings.Notifications),
                ],
                [
                  Qualities[Settings.Quality],
                  "filter",
                  true,
                  () => Cycle("Quality", 5),
                ],
                [
                  `Patches: ${Settings.Patches ? "On" : "Off"}`,
                  "filter",
                  Settings.Patches,
                  () => Assign("Patches", !Settings.Patches),
                ],
                [
                  "Raytracing",
                  "RaytracingBeam",
                  Settings.Raytracing,
                  () => Assign("Raytracing", !Settings.Raytracing),
                ],
              ].map(([Label, Symbol, On, Action]) => (
                <button
                  key={Label}
                  className={On ? "active" : ""}
                  onClick={Action}
                >
                  <i>
                    <Glyph Name={Symbol} Size={24} />
                  </i>
                  <span>{Label}</span>
                </button>
              ))}
            </div>
            <div className="render-scale">
              <Glyph Name="VideoRenderScale" Size={24} />
              <input
                aria-label="Render scale"
                type="range"
                min="25"
                max="100"
                value={Settings.Scale}
                onChange={(Event) => Assign("Scale", +Event.target.value)}
              />
              <output>{Settings.Scale}%</output>
            </div>
          </div>
        ) : Page === "Settings" ? (
          <div className="settings-hub">
            <header>
              <button
                aria-label="Back to dashboard"
                onClick={() => SelectPage("Dashboard")}
              >
                <Glyph Name="back" />
              </button>
              <div>
                Settings<small>Project-Zero</small>
              </div>
              <button aria-label="Close settings" onClick={() => Toggle(false)}>
                <Glyph Name="close" />
              </button>
            </header>
            {[
              [
                "Render Settings",
                "Global illumination, anti-aliasing, quality",
                "filter",
              ],
              [
                "Appearance",
                "Theme, fonts, and system colors",
                "AppearancePalette",
              ],
              [
                "Input",
                "Shortcuts, mouse sensitivity, controllers",
                "ShieldInput",
              ],
              [
                "Notifications",
                "RAM usage, FPS, baking complete alerts",
                "NotificationsBell",
              ],
              [
                "Materials",
                "Selections, channels, and fold report",
                "LayersSlabs",
              ],
            ].map(([Label, Description, Mark]) => (
              <button
                className="hub-row"
                key={Label}
                onClick={() => SelectPage(Label)}
              >
                <Glyph Name={Mark} />
                <span>
                  {Label}
                  <small>{Description}</small>
                </span>
                <Glyph Name="arrow" />
              </button>
            ))}
          </div>
        ) : (
          <div
            className="settings-dialog"
            role="dialog"
            aria-label={Titles[Page]}
          >
            <header>
              <button
                aria-label="Back to settings"
                onClick={() => SelectPage("Settings")}
              >
                <Glyph Name="back" />
              </button>
              <div>
                <h2>{Titles[Page]}</h2>
                <p>{Subtitles[Page]}</p>
              </div>
              <button
                aria-label="Close control center"
                onClick={() => Toggle(false)}
              >
                <Glyph Name="close" />
              </button>
            </header>
            {Page === "Appearance" && (
              <nav className="appearance-tabs">
                {["Display", "Fonts", "Theme"].map((Label) => (
                  <button
                    key={Label}
                    className={Tab === Label ? "active" : ""}
                    onClick={() => SelectTab(Label)}
                  >
                    {Label}
                  </button>
                ))}
              </nav>
            )}
            <div className="settings-body" data-drawer-scroll>
              {Page === "Render Settings" ? (
                <>
                  <Card Title="Shadows">
                    <p>
                      Filter follows the quality tier; resolution can override
                      it
                    </p>
                    <div className="readout">
                      <span>Technique</span>
                      <span>
                        {Settings.Quality === 0
                          ? "Hard shadow map"
                          : Settings.Quality === 1
                            ? "Wide PCF"
                            : "PCSS (soft, contact-hardening)"}
                      </span>
                    </div>
                    {Field(
                      "Resolution",
                      "Select",
                      [
                        "Auto (tier)",
                        "256 × 256",
                        "512 × 512",
                        "1024 × 1024",
                        "2048 × 2048",
                      ],
                      0,
                    )}
                    <div className="readout">
                      <span>Map</span>
                      <span>
                        {MapSide} × {MapSide} ·{" "}
                        {((MapSide * MapSide * 4) / 1048576).toFixed(1)} MB per
                        light tap
                      </span>
                    </div>
                  </Card>
                  <Card Title="Ray Tracing & Bounces">
                    <p>
                      Multi-bounce GI, specular reflections and physical sky
                      ambient fill
                    </p>
                    {Field(
                      "Reflections",
                      "Select",
                      ["Off", "Sky", "Raytraced"],
                      2,
                    )}
                    {Field(
                      "Global Illumination",
                      "Select",
                      [
                        "Off",
                        "1 Bounce",
                        "2 Bounces",
                        "3 Bounces",
                        "4 Bounces",
                      ],
                      2,
                    )}
                    {Field("Sky Ambient Fill")}
                    {Field("Sky Light Reuse")}
                  </Card>
                  <Card Title="Denoising">
                    <p>
                      Keep genuine detail (flakes, reflections, glow, edges)
                      through the filter
                    </p>
                    {Field(
                      "Detail Guide",
                      "Select",
                      [
                        "Standard",
                        "Flakes",
                        "Reflections",
                        "Emissive",
                        "Fresnel Rim",
                        "Edges",
                        "Smart",
                      ],
                      6,
                    )}
                  </Card>
                </>
              ) : Page === "Appearance" ? (
                Tab === "Display" ? (
                  <>
                    <Card Title="Resolution & Scaling">
                      <p>Render target size and interface scale</p>
                      {Field(
                        "Resolution",
                        "Select",
                        ["Native", "2560 × 1440", "1920 × 1080", "1280 × 720"],
                        0,
                      )}
                      {Slider("UI Scale", 50, 200, 100, "%")}
                      {Field("Match Quality")}
                    </Card>
                    <Card Title="Presentation">
                      {Field("V-Sync", "Select", ["Off", "On", "Adaptive"], 1)}
                      {Field(
                        "Frame Cap",
                        "Select",
                        ["Unlimited", "60 fps", "120 fps", "144 fps"],
                        0,
                      )}
                      {Field("Fullscreen")}
                    </Card>
                    <Card Title="Safe area padding">
                      {Slider("Safe area", 0, 64, 0, "px")}
                    </Card>
                    <Card Title="Overlay">
                      {Field("FPS Overlay")}
                      {Field(
                        "Corner",
                        "Select",
                        [
                          "Top left",
                          "Top right",
                          "Bottom left",
                          "Bottom right",
                        ],
                        0,
                      )}
                    </Card>
                  </>
                ) : Tab === "Fonts" ? (
                  <>
                    <div className="font-strip-title">
                      <div>
                        <h3>Typography</h3>
                        <p>Typeface, type scale, and font weights</p>
                      </div>
                      <button
                        aria-label="Previous typefaces"
                        onClick={(Event) =>
                          Event.currentTarget.parentElement.nextElementSibling.scrollBy(
                            { left: -300, behavior: "smooth" },
                          )
                        }
                      >
                        ‹
                      </button>
                      <button
                        aria-label="Next typefaces"
                        onClick={(Event) =>
                          Event.currentTarget.parentElement.nextElementSibling.scrollBy(
                            { left: 300, behavior: "smooth" },
                          )
                        }
                      >
                        ›
                      </button>
                    </div>
                    <div className="font-strip">
                      {[
                        "Inter",
                        "General Sans",
                        "Archivo",
                        "Space Grotesk",
                        "Clash Display",
                        "Montserrat",
                        "Poppins",
                        "JetBrains Mono",
                        "Lato",
                        "Fira Sans",
                      ].map((Family) => (
                        <button
                          key={Family}
                          className={
                            (Draft["Font family"] || "Inter") === Family
                              ? "selected"
                              : ""
                          }
                          onClick={() => Edit("Font family", Family)}
                        >
                          <b>Aa</b>
                          <span>{Family}</span>
                          <small>The quick brown fox jumps</small>
                        </button>
                      ))}
                    </div>
                    <Card Title="Typeface & Colors">
                      <div className="typeface-preview">
                        <div>
                          <strong>{Draft["Font family"] || "Inter"}</strong>
                          <small>(72px bold)</small>
                        </div>
                        <div>
                          <code>
                            ABCDEFGHIJKLMNOPQRSTUVWXYZ
                            <br />
                            abcdefghijklmnopqrstuvwxyz
                            <br />
                            0123456789 !@#$%^&amp;*()
                          </code>
                          <p style={{ color: Draft.Accent || "#3b82f6" }}>
                            Accent — The quick brown fox jumps over…
                            <br />
                            (rendered in {Draft.Accent || "#3b82f6"})
                          </p>
                          <div className="font-discs">
                            {[
                              [Draft.Accent || "#3b82f6", "Accent"],
                              ["#ffffff", "Primary"],
                              ["#000000", "Background"],
                            ].map(([Colour, Label]) => (
                              <span key={Label}>
                                <i style={{ background: Colour }} />
                                {Colour}
                                <small>{Label}</small>
                              </span>
                            ))}
                          </div>
                        </div>
                      </div>
                      <p>
                        Font catalogue UI · this standalone copy embeds the
                        native DM Sans faces only.
                      </p>
                    </Card>
                    <h3 className="type-scale-heading">Type Scale</h3>
                    {[
                      "Title",
                      "Header",
                      "Subheader",
                      "Body",
                      "Label",
                      "Caption",
                    ].map((Label, Index) => (
                      <Card key={Label} Class="type-role">
                        <div>
                          <h3>{Label}</h3>
                          {Slider(
                            Label + " size",
                            8,
                            72,
                            [48, 32, 24, 16, 14, 12][Index],
                            "px",
                          )}
                          <div className="weight-chips">
                            {[
                              "Bold",
                              "ExtraBold",
                              "ExtraLight",
                              "Light",
                              "Medium",
                              "Regular",
                              "SemiBold",
                              "Thin",
                              "Black",
                            ].map((Weight) => (
                              <button
                                key={Weight}
                                className={
                                  (Draft[Label + " weight"] || "Regular") ===
                                  Weight
                                    ? "selected"
                                    : ""
                                }
                                onClick={() => Edit(Label + " weight", Weight)}
                              >
                                {Weight}
                              </button>
                            ))}
                          </div>
                        </div>
                        <div
                          className="type-role-sample"
                          style={{
                            fontSize:
                              Draft[Label + " size"] ||
                              [48, 32, 24, 16, 14, 12][Index],
                            fontWeight:
                              Draft[Label + " weight"] === "Bold" ? 700 : 400,
                          }}
                        >
                          {
                            [
                              "Display Title",
                              "Section Header",
                              "Card Subheader",
                              "The quick brown fox jumps over the lazy dog.",
                              "Form Label",
                              "Small caption text",
                            ][Index]
                          }
                        </div>
                      </Card>
                    ))}
                    <Card Title="Font Rendering">
                      {Field("Font antialiasing")}
                      <p>Smooth the edges of text</p>
                      {Field("Ligatures")}
                      <p>Enable special character combinations</p>
                    </Card>
                  </>
                ) : (
                  <>
                    <Card Title="Color Scheme">
                      <div className="theme-swatches">
                        {[
                          ["Dark", "#121212"],
                          ["OLED", "#000"],
                          ["Light", "#eee"],
                          ["Sepia", "#dca85b"],
                          ["Dracula", "#282a36"],
                          ["Nord", "#2e3440"],
                          ["GitHub", "#0d1117"],
                        ].map(([Label, Colour]) => (
                          <button
                            key={Label}
                            className={Draft.Theme === Label ? "selected" : ""}
                            onClick={() => Edit("Theme", Label)}
                          >
                            <i style={{ background: Colour }} />
                            {Label}
                          </button>
                        ))}
                      </div>
                    </Card>
                    <Card Title="Corner Radius">
                      {Slider("Corner radius", 0, 32, 16, "px")}
                    </Card>
                    <Card Title="Accent Color">
                      {Field("Accent", "Colour", [], "#3b82f6")}
                    </Card>
                    <Card Title="Semantic Colors">
                      {["Warning", "Success", "Info", "Caution"].map(
                        (Label, Index) =>
                          Field(
                            Label,
                            "Colour",
                            [],
                            ["#f59e0b", "#34c759", "#3b82f6", "#a78bfa"][Index],
                          ),
                      )}
                    </Card>
                  </>
                )
              ) : Page === "Input" ? (
                <>
                  <Card Title="Navigation">
                    {Field(
                      "Preset profile",
                      "Select",
                      ["Blender (Default)", "Maya / Unity", "Unreal Engine"],
                      0,
                    )}
                    {Slider("Mouse Sensitivity", 0, 100, 50, "%")}
                  </Card>
                  <Card Title="Custom Shortcuts">
                    {Field("Custom Shortcuts", "Switch", [], false)}
                    {[
                      "Select Tool",
                      "Translate Tool",
                      "Rotate Tool",
                      "Frame Selected",
                    ].map((Label, Index) => (
                      <label className="key-field" key={Label}>
                        {Label}
                        <input
                          aria-label={Label}
                          disabled={!Draft["Custom Shortcuts"]}
                          value={Draft[Label] || ["Q", "G", "R", "F"][Index]}
                          onKeyDown={(Event) => {
                            Event.preventDefault();
                            Edit(Label, Event.key.toUpperCase());
                          }}
                          readOnly
                        />
                      </label>
                    ))}
                  </Card>
                  <Card Title="Advanced Controls">
                    <p>Enable axis inversion and modifiers</p>
                    {Field("Advanced Controls", "Switch", [], false)}
                    {Field("Invert Y-Axis", "Switch", [], false)}
                  </Card>
                </>
              ) : Page === "Notifications" ? (
                <>
                  <Card Title="System resource overlay">
                    {[
                      "Show FPS Overlay",
                      "Show RAM Usage",
                      "Scene Metadata",
                    ].map((Label) => Field(Label))}
                  </Card>
                  <Card Title="Alerts">
                    {[
                      "Baking Complete",
                      "Render Finished",
                      "Autosave Errors",
                      "Frame-rate Drops",
                    ].map((Label) => Field(Label))}
                  </Card>
                </>
              ) : (
                <>
                  <Card Title="Material selection">
                    {Field(
                      "Material",
                      "Select",
                      ["Standard PBR", "Glass", "Chrome", "Emissive"],
                      0,
                    )}
                    {Field(
                      "Shading model",
                      "Select",
                      [
                        "Standard",
                        "Anisotropic",
                        "ClearCoated",
                        "Cloth",
                        "Subsurface",
                        "Transmissive",
                        "EmissiveOnly",
                        "Unlit",
                      ],
                      0,
                    )}
                    <p>
                      1 authored → 1 resident · alpha: opaque · single-sided
                    </p>
                  </Card>
                  <Card Title="Surface channels">
                    <div className="material-channels">
                      <div className="channel-heading">
                        <span>Channel</span>
                        <span>Value</span>
                        <span>Source</span>
                      </div>
                      {Channels.map((Label, Index) => (
                        <div key={Label}>
                          <span>{Label}</span>
                          <input
                            aria-label={Label}
                            value={
                              Draft[Label] ??
                              (Index === 0
                                ? "(0.8, 0.8, 0.8)"
                                : Index === 2
                                  ? "0.4"
                                  : Index === 3 || Index === 18
                                    ? "1.5"
                                    : "0")
                            }
                            onChange={(Event) =>
                              Edit(Label, Event.target.value)
                            }
                          />
                          <small>
                            {Index === 4 ? "mesh frame" : "constant"}
                          </small>
                        </div>
                      ))}
                    </div>
                  </Card>
                  <Card Title="Fold report">
                    <p>
                      Simple · one resident slab · no engine material compiler
                      connected
                    </p>
                    <button disabled>Render preview</button>
                  </Card>
                </>
              )}
            </div>
            <footer>
              <small>
                {Message ||
                  (Dirty
                    ? "Unsaved HTML preview changes"
                    : "HTML preview · no engine connection")}
              </small>
              <button
                onClick={() => {
                  Change({ ...Settings });
                  SetDirty(false);
                }}
              >
                {Page === "Appearance" ? "Reset" : "Discard Changes"}
              </button>
              <button className="primary" onClick={Save}>
                {Page === "Render Settings"
                  ? "Apply Render Settings"
                  : Page === "Input"
                    ? "Save keybindings"
                    : Page === "Notifications"
                      ? "Save Preferences"
                      : "Apply"}
              </button>
            </footer>
          </div>
        )}
      </div>
    </DrawerPanel>
  );
}
