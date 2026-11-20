//============================================================================================================================================
//                                                              LIGHTPANEL.JS
//============================================================================================================================================
// 📦 Type-specific browser light instruments with live emission and aperture illustrations.

import { LightIcons } from "./LightSpecification.js";
import { ProjectLight, ProjectResponse } from "./LightProjection.js";
import { MountLightTransform } from "./EmitterPanel.jsx";
import { el } from "./InspectorDepot/kit.js";
const Clamp = (NumberValue, Minimum, Maximum) =>
  Math.max(Minimum, Math.min(Maximum, NumberValue));
function Temperature(Kelvin) {
  const Heat = Kelvin / 100;
  return [
    Heat <= 66 ? 255 : 329.6987 * (Heat - 60) ** -0.1332,
    Heat <= 66
      ? 99.4708 * Math.log(Heat) - 161.1196
      : 288.1222 * (Heat - 60) ** -0.0755,
    Heat >= 66
      ? 255
      : Heat <= 19
        ? 0
        : 138.5177 * Math.log(Heat - 10) - 305.0448,
  ].map((Channel) => Clamp(Channel, 0, 255));
}
export function LightPanel(Subject, Context) {
  const Properties = Subject.props,
    Style = Subject.type,
    Spot = Style === "spotlight",
    Point = Style === "pointlight",
    Photometric = Style === "ieslight",
    Area = Style === "arealight",
    Diode = Style === "ledlight",
    Strip = Style === "ledstrip";
  const Host = el("div", "mpanel lighting-panel"),
    Bindings = [],
    Charts = [];
  Host.dataset.lightType = Style;
  const Read = (Key, Fallback = 0) =>
    Number.isFinite(Number(Properties[Key]))
      ? Number(Properties[Key])
      : Fallback;
  const Write = (Key, Next) => {
    if (Subject.locked) return;
    Context.setProp(Subject, Key, Next);
    Refresh();
  };
  const Card = (Name, Caption, ClassName) => {
    const Section = el("section", "pcard lp-card " + ClassName);
    const Header = el("header");
    Header.append(el("h3", "", Name), el("span", "", Caption));
    Section.append(Header);
    Host.append(Section);
    return Section;
  };
  const Field = (Section, Key, Name, Minimum, Maximum, Step, Unit) => {
    const Label = el("label", "field");
    Label.append(el("span", "", Name));
    const Pill = el("div", "slider-pill"),
      Split = el("div", "split-value"),
      NumberInput = el("input"),
      Slider = el("input");
    NumberInput.type = "number";
    Slider.type = "range";
    for (const Input of [NumberInput, Slider]) {
      Input.min = Minimum;
      Input.max = Maximum;
      Input.step = Step;
      Input.dataset.lightProperty = Key;
      Input.setAttribute(
        "aria-label",
        Name + (Input === NumberInput ? " value" : ""),
      );
      Input.onblur = Refresh;
      Input.oninput = () => {
        if (Input.value === "") return;
        const Numeric = Number(Input.value);
        if (Number.isFinite(Numeric))
          Write(Key, Clamp(Numeric, Minimum, Maximum));
      };
    }
    Split.append(NumberInput, el("small", "", Unit));
    Pill.append(Split, Slider);
    Label.append(Pill);
    Section.append(Label);
    Bindings.push(() => {
      for (const Input of [NumberInput, Slider]) {
        Input.value = Read(Key);
        Input.disabled = Subject.locked;
      }
      Slider.style.setProperty(
        "--fill",
        (100 * (Read(Key) - Minimum)) / (Maximum - Minimum) + "%",
      );
    });
  };
  const Toggle = (Section, Key, Name) => {
    const Row = el("div", "lp-toggle-row"),
      Button = el("button", "lp-switch");
    Row.append(el("span", "", Name), Button);
    Button.setAttribute("role", "switch");
    Button.setAttribute("aria-label", Name);
    Button.append(el("i"));
    Button.onclick = () => Write(Key, !Properties[Key]);
    Section.append(Row);
    Bindings.push(() => {
      Button.setAttribute("aria-checked", !!Properties[Key]);
      Button.disabled = Subject.locked;
    });
  };
  const Canvas = (Section, Painter, Height = 158) => {
    const CanvasElement = el("canvas", "lp-canvas");
    CanvasElement.setAttribute("role", "img");
    CanvasElement.setAttribute(
      "aria-label",
      Section.querySelector("h3").textContent + " illustration",
    );
    Section.append(CanvasElement);
    Charts.push(() => {
      const Width = CanvasElement.clientWidth || 280,
        Ratio = Math.min(devicePixelRatio || 1, 2);
      CanvasElement.width = Width * Ratio;
      CanvasElement.height = Height * Ratio;
      CanvasElement.style.height = Height + "px";
      const Brush = CanvasElement.getContext("2d");
      Brush.setTransform(Ratio, 0, 0, Ratio, 0, 0);
      Painter(Brush, Width, Height);
    });
    return CanvasElement;
  };
  function Colour() {
    const Hex = /^#[0-9a-f]{6}$/i.test(Properties.color)
        ? Properties.color
        : "#ffffff",
      Channels = [1, 3, 5].map((Index) =>
        parseInt(Hex.slice(Index, Index + 2), 16),
      ),
      Warm = Properties.temperature
        ? Temperature(Read("temperature", 5600))
        : [255, 255, 255];
    return Channels.map((Channel, Index) =>
      Math.round((Channel * Warm[Index]) / 255),
    );
  }
  const Tint = (Opacity = 1) => `rgba(${Colour().join(",")},${Opacity})`;
  const Flux = () =>
    Math.max(
      0,
      Diode
        ? Read("watts", 10) * Read("efficacy", 110) * Read("dimmer", 1)
        : Strip
          ? Read("lumensPerMetre", 1000) *
            Read("length", 2.4) *
            Read("dimmer", 1)
          : Read(Point || Spot ? "intensity" : "lumens"),
    );
  const Preview = Card(
    {
      pointlight: "Radial falloff",
      spotlight: "Beam envelope",
      ieslight: "Photometric distribution",
      arealight: "Luminous surface",
      tubelight: "Linear radiance",
      ledlight: "LED emitter",
      ledstrip: "Ribbon light",
    }[Style],
    "",
    "lp-preview",
  );
  const Glyph = el("span", "lp-source-icon");
  const SourceIcon = el("img");
  SourceIcon.src = __LIGHT_ICONS__[LightIcons[Style]];
  SourceIcon.alt = "";
  Glyph.append(SourceIcon);
  Preview.querySelector("header").prepend(Glyph);
  const Primary = el("div", "lp-primary"),
    NumberPart = el("span", "lp-integer"),
    DecimalPart = el("span", "lp-decimal"),
    Unit = el("small", "", Point || Spot ? "cd" : "lm");
  Primary.append(NumberPart, DecimalPart, Unit);
  Preview.append(Primary);
  const Caption = el(
    "p",
    "lp-primary-caption",
    Diode || Strip
      ? "Estimated luminous flux"
      : Point || Spot
        ? "Authored luminous intensity"
        : Photometric
          ? "Profile source flux"
          : "Authored luminous flux",
  );
  Preview.append(Caption);
  const Descriptor = el("div", "lp-descriptor");
  Preview.append(Descriptor);
  Bindings.push(() => {
    Descriptor.textContent = Photometric
      ? (Properties.profile || "Downlight") + " / synthetic polar study"
      : Area
        ? (Properties.aperture || "Rectangle") +
          " / " +
          Read("width", 2).toFixed(1) +
          " × " +
          Read("height", 1).toFixed(1) +
          " m aperture"
        : Strip
          ? "Horizontal / " + Read("length", 2.4).toFixed(1) + " m run"
          : Diode
            ? Read("diameter", 40) +
              " mm package / " +
              Read("temperature", 4000) +
              " K"
            : Spot
              ? Read("angle", 26) +
                "° cone / " +
                Math.round(Read("penumbra", 0.4) * 100) +
                "% soft edge"
              : Point
                ? "Omnidirectional / decay " + Read("decay", 2).toFixed(1)
                : Read("length", 1.5).toFixed(1) + " m linear emitter";
  });
  Bindings.push(() => {
    const Parts = Flux()
      .toLocaleString("en-US", {
        minimumFractionDigits: 1,
        maximumFractionDigits: 1,
      })
      .split(".");
    NumberPart.textContent = Parts[0];
    DecimalPart.textContent = "." + Parts[1];
  });
  Canvas(
    Preview,
    (Brush, Width, Height) =>
      ProjectLight(Brush, Width, Height, {
        Style,
        Read,
        Properties,
        Tint,
        Flux,
      }),
    260,
  );
  Preview.append(
    el(
      "p",
      "lp-study-note",
      Photometric
        ? "Illustrative preset · no measured IES data"
        : "Source study · HTML authoring",
    ),
  );
  const Rail = el("div", "mp-rail lp-readings");
  Host.append(Rail);
  const Metric = (Name, Readout) => {
    const Cell = el("div", "mp-pill"),
      Reading = el("b", "v");
    Cell.append(Reading, el("span", "k", Name));
    Rail.append(Cell);
    Bindings.push(() => (Reading.textContent = Readout()));
  };
  const Format = (Numeric, Digits = 0) =>
    Numeric.toLocaleString("en-US", { maximumFractionDigits: Digits });
  if (Diode) {
    Metric("Driver power", () => Format(Read("watts", 10), 1) + " W");
    Metric("Efficacy target", () => Format(Read("efficacy", 110)) + " lm/W");
  } else if (Strip) {
    Metric(
      "Connected load",
      () => Format(Read("length", 2.4) * Read("wattsPerMetre", 14.4), 1) + " W",
    );
    Metric(
      "Emitter count",
      () =>
        Format(Math.round(Read("length", 2.4) * Read("ledsPerMetre", 60))) +
        " LEDs",
    );
  } else if (Point) {
    Metric(
      "At 5 m · estimate",
      () => Format(Flux() / 5 ** Read("decay", 2), 2) + " lx",
    );
    Metric("Reach", () => Format(Read("distance")) + " m");
  } else if (Spot) {
    Metric("Full cone", () => Format(Read("angle"), 1) + "°");
    Metric("Soft edge", () => Format(Read("penumbra") * 100) + " %");
  } else if (Photometric) {
    Metric(
      "Scaled output",
      () => Format(Flux() * Read("multiplier", 1)) + " lm",
    );
    Metric("Field angle", () => Format(Read("cone"), 1) + "°");
  } else if (Area) {
    Metric(
      "Aperture",
      () =>
        Format(
          Read("width") *
            Read("height") *
            (Properties.aperture === "Disk" ? Math.PI / 4 : 1),
          2,
        ) + " m²",
    );
    Metric("Emission", () => (Properties.twoSided ? "Two-sided" : "One-sided"));
  } else {
    Metric(
      "Linear output",
      () => Format(Flux() / Math.max(0.1, Read("length", 1.5))) + " lm/m",
    );
    Metric("Tube radius", () => Format(Read("radius", 0.04) * 1000) + " mm");
  }
  const Output = Card(
    Diode ? "Driver & colour" : Strip ? "Output per metre" : "Source & colour",
    "OUTPUT",
    "lp-output",
  );
  if (Diode) {
    Field(Output, "watts", "Driver power", 0.1, 100, 0.1, "W");
    Field(Output, "efficacy", "Efficacy target", 10, 250, 1, "lm/W");
    Field(Output, "dimmer", "Dimmer", 0, 1, 0.01, "");
  } else if (Strip) {
    Field(Output, "lumensPerMetre", "Flux per metre", 10, 4000, 10, "lm/m");
    Field(Output, "wattsPerMetre", "Load per metre", 1, 50, 0.1, "W/m");
    Field(Output, "dimmer", "Dimmer", 0, 1, 0.01, "");
  } else
    Field(
      Output,
      Point || Spot ? "intensity" : "lumens",
      Point || Spot ? "Intensity" : "Luminous flux",
      0,
      Point ? 60 : Spot ? 200 : Photometric ? 8000 : Area ? 20000 : 12000,
      Point ? 0.1 : Spot ? 1 : 50,
      Point || Spot ? "cd" : "lm",
    );
  if (Photometric || Style === "tubelight" || Diode || Strip)
    Field(Output, "temperature", "Colour temperature", 1800, 12000, 100, "K");
  const ColourRow = el("label", "lp-colour"),
    ColourInput = el("input"),
    ColourCode = el("output");
  ColourInput.type = "color";
  ColourInput.setAttribute("aria-label", "Emission colour");
  ColourInput.oninput = () => Write("color", ColourInput.value);
  ColourRow.append(el("span", "", "Emission tint"), ColourInput, ColourCode);
  Output.append(ColourRow);
  Bindings.push(() => {
    ColourInput.value = Properties.color || "#ffffff";
    ColourInput.disabled = Subject.locked;
    ColourCode.textContent = ColourInput.value.toUpperCase();
  });
  const Shape = Card(
    Diode
      ? "Package & optic"
      : Strip
        ? "Layout & segments"
        : Photometric
          ? "Distribution profile"
          : Point
            ? "Attenuation"
            : Spot
              ? "Beam shaping"
              : "Emitter dimensions",
    "OPTICS",
    "lp-shape",
  );
  const Choice = (Section, Key, Names) => {
    const Options = el("div", "lp-profiles");
    Names.forEach((Name) => {
      const Button = el("button", "", Name);
      Button.onclick = () => Write(Key, Name);
      Options.append(Button);
      Bindings.push(() => {
        Button.disabled = Subject.locked;
        Button.setAttribute(
          "aria-pressed",
          (Properties[Key] || Names[0]) === Name,
        );
      });
    });
    Section.append(Options);
  };
  if (Diode) {
    Field(Shape, "diameter", "Package diameter", 5, 120, 1, "mm");
    Field(Shape, "angle", "Emission angle", 10, 180, 1, "°");
  } else if (Strip) {
    Field(Shape, "length", "Strip length", 0.1, 20, 0.1, "m");
    Field(Shape, "ledsPerMetre", "Emitter density", 10, 240, 1, "/m");
    Field(Shape, "voltage", "Supply voltage", 5, 48, 1, "V");
    Toggle(Shape, "diffuser", "Opal diffuser");
  } else if (Photometric) {
    const Profiles = el("div", "lp-profiles");
    for (const Profile of [
      "Downlight",
      "Wall wash",
      "Batwing",
      "ECE Low Beam",
      "SAE Low Beam",
      "High Beam",
      "Fog Lamp",
      "Parking Lamp",
    ]) {
      const Button = el("button", "", Profile);
      Button.onclick = () => Write("profile", Profile);
      Profiles.append(Button);
      Bindings.push(() => {
        Button.setAttribute("aria-pressed", Properties.profile === Profile);
        Button.disabled = Subject.locked;
      });
    }
    Shape.append(Profiles);
    Shape.append(
      el("p", "lp-note", "Preset illustration · IES file import pending."),
    );
    Field(Shape, "multiplier", "Profile multiplier", 0, 4, 0.05, "×");
    Field(Shape, "cone", "Field angle", 5, 100, 0.5, "°");
    Field(Shape, "cutoff", "Cut-off pitch", -5, 5, 0.1, "°");
    Field(Shape, "range", "Photometric range", 1, 250, 1, "m");
  } else if (Point) {
    Field(Shape, "distance", "Reach", 1, 120, 1, "m");
    Field(Shape, "decay", "Decay exponent", 0, 4, 0.05, "");
  } else if (Spot) {
    Field(Shape, "angle", "Full cone angle", 2, 80, 0.5, "°");
    Field(Shape, "penumbra", "Penumbra", 0, 1, 0.01, "");
  } else if (Area) {
    Choice(Shape, "aperture", ["Rectangle", "Disk"]);
    Field(Shape, "width", "Width", 0.1, 20, 0.1, "m");
    Field(Shape, "height", "Height", 0.1, 20, 0.1, "m");
    Field(Shape, "spread", "Beam spread", 1, 180, 1, "°");
    Toggle(Shape, "twoSided", "Two-sided emission");
  } else {
    Field(Shape, "length", "Length", 0.1, 20, 0.1, "m");
    Field(Shape, "radius", "Tube radius", 0.01, 1, 0.01, "m");
    Field(Shape, "distance", "Reach", 1, 120, 1, "m");
  }
  const Response = Card(
    Photometric
      ? "Angular response"
      : Area
        ? "Aperture balance"
        : Strip
          ? "Electrical budget"
          : Diode
            ? "Conversion budget"
            : Spot
              ? "Beam section"
              : Point
                ? "Distance response"
                : "Linear output",
    "ANALYTICAL",
    "lp-response",
  );
  const ResponseReading = el("div", "lp-response-reading"),
    ResponseCaption = el("p", "lp-note");
  Response.append(ResponseReading, ResponseCaption);
  Bindings.push(() => {
    ResponseReading.textContent = Photometric
      ? Read("cone", 60) + "°"
      : Area
        ? Format(
            Flux() /
              Math.max(
                0.01,
                Read("width", 2) *
                  Read("height", 1) *
                  (Properties.aperture === "Disk" ? Math.PI / 4 : 1),
              ),
            0,
          ) + " lm/m²"
        : Strip
          ? Format(
              (Read("length", 2.4) * Read("wattsPerMetre", 14.4)) /
                Math.max(1, Read("voltage", 24)),
              2,
            ) + " A"
          : Diode
            ? Format(Read("dimmer", 1) * 100) + " %"
            : Spot
              ? Format(
                  2 * 5 * Math.tan((Read("angle", 26) * Math.PI) / 360),
                  2,
                ) + " m"
              : Point
                ? Format(Flux() / 5 ** Read("decay", 2), 2) + " lx"
                : Format(Flux() / Math.max(0.1, Read("length", 1.5))) + " lm/m";
    ResponseCaption.textContent = Photometric
      ? "Normalized preset sections · not measured candela"
      : Area
        ? "Flux per aperture area · not surface luminance"
        : Strip
          ? "Connected watts ÷ supply volts · ideal full-load current"
          : Diode
            ? "Authored dimmer · estimated output ignores thermal losses"
            : Spot
              ? "Beam diameter on a perpendicular plane at 5 m"
              : Point
                ? "Authored decay at 5 m · free-space estimate"
                : "Authored source flux per metre";
  });
  Canvas(
    Response,
    (Brush, Width, Height) =>
      ProjectResponse(Brush, Width, Height, {
        Style,
        Read,
        Properties,
        Tint,
        Flux,
      }),
    Point ? 180 : 100,
  );
  if (Point) {
    const Samples = el("table", "lp-response-samples");
    Samples.setAttribute("aria-label", "Distance response samples");
    Samples.innerHTML =
      "<thead><tr><th>Distance</th><th>Estimate</th><th>Relative</th></tr></thead><tbody></tbody>";
    Bindings.push(() => {
      Samples.tBodies[0].innerHTML = [1, 2, 5, 10]
        .map((Distance) => {
          const Ratio = 1 / Distance ** Read("decay", 2);
          return `<tr><td>${Distance} m</td><td>${(Flux() * Ratio).toLocaleString("en-US", { minimumFractionDigits: 2, maximumFractionDigits: 2 })} lx</td><td>${(Ratio * 100).toFixed(1)}%</td></tr>`;
        })
        .join("");
    });
    Response.append(Samples);
  }
  // Keep the response with the source that drives it, rather than in a separate card.
  Response.classList.remove("pcard", "lp-card");
  Output.append(Response);
  const Placement = el("div", "lp-card lp-transform");
  Host.append(Placement);
  const Transform = MountLightTransform(Placement, Subject, Context);
  Bindings.push(Transform.Refresh);
  const Visibility = Card("Scene participation", "FLAGS", "lp-participation");
  Toggle(Visibility, "shadows", "Cast shadows");
  Toggle(
    Visibility,
    Photometric
      ? "showDistribution"
      : Spot
        ? "showCone"
        : Point
          ? "gizmoGlow"
          : "showShape",
    Photometric
      ? "Draw distribution"
      : Spot
        ? "Draw cone"
        : Point
          ? "Show glow"
          : "Draw emitter",
  );
  const SourceColumn = el("div", "lp-source-column"),
    ControlColumn = el("div", "lp-control-column");
  const SummaryColumn = el("div", "lp-summary-column"),
    StatusColumn = el("div", "lp-status-column");
  SummaryColumn.append(Preview);
  StatusColumn.append(Rail, Visibility);
  SourceColumn.append(Output);
  ControlColumn.append(Shape, Placement);
  Host.replaceChildren(
    SummaryColumn,
    StatusColumn,
    SourceColumn,
    ControlColumn,
  );
  function Refresh() {
    Bindings.forEach((Sync) => Sync());
    Charts.forEach((Render) => Render());
  }
  Context.register(Refresh);
  const Observer = new ResizeObserver(Refresh);
  Observer.observe(Host);
  Host._dispose = () => {
    Observer.disconnect();
    Transform.Dispose();
  };
  Refresh();
  return Host;
}
