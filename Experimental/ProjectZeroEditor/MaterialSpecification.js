// Channel order follows MaterialInspector.cpp. These are browser-authored drafts,
// not resolved native slabs. Reflectance and refraction share the native IOR carrier.
export const MaterialChannels = [
  ["colour", "Base colour", "Colour", "#c7ada0", 0, 1, "Surface"],
  ["metallic", "Metallic", "Scalar", 0, 0, 1, "Surface"],
  ["roughness", "Roughness", "Scalar", 0.32, 0, 1, "Surface"],
  ["reflectance", "Reflectance / IOR", "Scalar", 1.5, 1, 2.5, "Surface"],
  ["normal", "Orientation", "Colour", "#8080ff", 0, 1, "Surface"],
  ["occlusion", "Occlusion", "Scalar", 1, 0, 1, "Surface"],
  ["emission", "Emission", "Scalar", 0, 0, 20, "Surface", "nit"],
  ["opacity", "Opacity", "Scalar", 1, 0, 1, "Surface"],
  ["anisotropy", "Anisotropy", "Scalar", 0, -1, 1, "Specular"],
  [
    "direction",
    "Anisotropy direction",
    "Scalar",
    0,
    0,
    6.2832,
    "Specular",
    "rad",
  ],
  ["coat", "Clear coat", "Scalar", 0, 0, 1, "Coating"],
  ["coatRoughness", "Coat roughness", "Scalar", 0.1, 0, 1, "Coating"],
  ["coatNormal", "Coat orientation", "Colour", "#8080ff", 0, 1, "Coating"],
  ["sheen", "Sheen colour", "Colour", "#000000", 0, 1, "Fabric"],
  ["sheenRoughness", "Sheen roughness", "Scalar", 0.5, 0, 1, "Fabric"],
  ["subsurface", "Subsurface colour", "Colour", "#cccccc", 0, 1, "Volume"],
  ["thickness", "Subsurface thickness", "Scalar", 1, 0, 2, "Volume", "m"],
  ["transmission", "Transmission", "Scalar", 0, 0, 1, "Volume"],
  ["ior", "IOR (refraction)", "Scalar", 1.5, 1, 2.5, "Volume"],
  ["displacement", "Displacement", "Scalar", 0, -0.1, 0.1, "Geometry", "m"],
].map(([Id, Label, Kind, Default, Minimum, Maximum, Group, Unit = ""]) => ({
  Id,
  Label,
  Kind,
  Default,
  Minimum,
  Maximum,
  Group,
  Unit,
}));
export const MaterialTypes = [
  "Standard",
  "SSR",
  "Glass",
  "Fabric",
  "Metal",
  "Clear coat",
  "Subsurface",
  "Emissive",
  "Unlit",
];
export const SourceNames = {
  Fill: "Fill",
  Gradient: "Gradient",
  Texture: "Texture",
  Code: "Code link",
};
export function ResolveMaterial(Values = {}) {
  const Saved = Values.Material || {};
  const Channels = Object.fromEntries(
    MaterialChannels.map((Field) => {
      const Legacy = {
        colour: Values.Albedo,
        roughness: Values.Roughness,
        emission: Values.Emission,
      }[Field.Id];
      const Fill = Legacy ?? Field.Default;
      return [
        Field.Id,
        {
          Source: "Fill",
          Fill,
          Stops: [
            {
              Id: "start",
              Position: 0,
              Value: Field.Kind === "Colour" ? "#282b43" : Field.Minimum,
            },
            {
              Id: "end",
              Position: 1,
              Value: Field.Kind === "Colour" ? "#d9b594" : Field.Maximum,
            },
          ],
          Axis: "U",
          Texture: null,
          Component: Field.Kind === "Colour" ? "RGB" : "R",
          Repeat: 1,
          Code: { Language: "C++", Symbol: "", Text: "" },
          ...Saved.Channels?.[Field.Id],
        },
      ];
    }),
  );
  for (const Field of MaterialChannels) {
    const Channel = Channels[Field.Id];
    const Sanitize = (Value) =>
      Field.Kind === "Colour"
        ? /^#[0-9a-f]{6}$/i.test(Value)
          ? Value
          : Field.Default
        : Clamp(Value, Field.Minimum, Field.Maximum);
    Channel.Source = Object.hasOwn(SourceNames, Channel.Source)
      ? Channel.Source
      : "Fill";
    Channel.Fill = Sanitize(Channel.Fill);
    Channel.Axis = Channel.Axis === "V" ? "V" : "U";
    Channel.Repeat = Clamp(Channel.Repeat, 0.1, 16);
    Channel.Component = (Field.Kind === "Colour"
      ? ["RGB", "R", "G", "B", "A"]
      : ["R", "G", "B", "A"]
    ).includes(Channel.Component)
      ? Channel.Component
      : Field.Kind === "Colour"
        ? "RGB"
        : "R";
    Channel.Stops =
      Array.isArray(Channel.Stops) && Channel.Stops.length >= 2
        ? Channel.Stops.slice(0, 8).map((Stop, Index) => ({
            Id: String(Stop?.Id || Index),
            Position: Clamp(Stop?.Position),
            Value: Sanitize(Stop?.Value),
          }))
        : [
            { Id: "start", Position: 0, Value: Channel.Fill },
            { Id: "end", Position: 1, Value: Field.Default },
          ];
    if (
      new Set(Channel.Stops.map((Stop) => Stop.Id)).size !==
      Channel.Stops.length
    )
      Channel.Stops = Channel.Stops.map((Stop, Index) => ({
        ...Stop,
        Id: String(Index),
      }));
    const Code = Channel.Code || {};
    Channel.Code = {
      Language: ["C++", "Slang / HLSL", "GLSL"].includes(Code.Language)
        ? Code.Language
        : "C++",
      Symbol: String(Code.Symbol || "").slice(0, 256),
      Text: String(Code.Text || "").slice(0, 12000),
    };
    if (
      !/^data:image\/(png|jpeg|webp);base64,[A-Za-z0-9+/=]+$/.test(
        Channel.Texture?.Data,
      ) ||
      Channel.Texture.Data.length > 300000
    )
      Channel.Texture = null;
  }
  Channels.reflectance = Channels.ior = Saved.Channels?.ior
    ? Channels.ior
    : Saved.Channels?.reflectance
      ? Channels.reflectance
      : Channels.ior;
  return {
    ...Saved,
    Name:
      typeof Saved.Name === "string"
        ? Saved.Name.slice(0, 80)
        : "Surface material",
    Type: MaterialTypes.includes(Saved.Type) ? Saved.Type : "Standard",
    Channels,
  };
}
export function ChangeChannel(Material, Id, Patch) {
  const Channel = { ...Material.Channels[Id], ...Patch };
  return {
    ...Material,
    Channels: {
      ...Material.Channels,
      [Id]: Channel,
      ...(Id === "ior" || Id === "reflectance"
        ? { ior: Channel, reflectance: Channel }
        : {}),
    },
  };
}
export function ChooseMaterialType(Material, Type) {
  const Presets = {
    Standard: {
      metallic: 0,
      roughness: 0.4,
      transmission: 0,
      coat: 0,
      emission: 0,
    },
    SSR: { metallic: 0.7, roughness: 0.12, transmission: 0 },
    Glass: { metallic: 0, roughness: 0.06, transmission: 1, ior: 1.5 },
    Fabric: {
      metallic: 0,
      roughness: 0.85,
      transmission: 0,
      sheen: "#c8a6aa",
      sheenRoughness: 0.7,
    },
    Metal: { metallic: 1, roughness: 0.22, transmission: 0 },
    "Clear coat": {
      metallic: 0.15,
      roughness: 0.35,
      coat: 1,
      coatRoughness: 0.08,
    },
    Subsurface: {
      metallic: 0,
      roughness: 0.5,
      subsurface: "#cb7655",
      thickness: 0.25,
    },
    Emissive: { emission: 3, metallic: 0 },
    Unlit: {},
  };
  let Next = { ...Material, Type };
  for (const [Id, Fill] of Object.entries({
    metallic: 0,
    roughness: 0.4,
    transmission: 0,
    coat: 0,
    emission: 0,
    opacity: 1,
    sheen: "#000000",
    ior: 1.5,
    ...(Presets[Type] || {}),
  }))
    Next = ChangeChannel(Next, Id, { Fill });
  return Next;
}
export const Clamp = (Value, Minimum = 0, Maximum = 1) =>
  Math.min(
    Maximum,
    Math.max(Minimum, Number.isFinite(+Value) ? +Value : Minimum),
  );
export const Rgb = (Hex) =>
  [1, 3, 5].map(
    (Index) => parseInt(String(Hex).slice(Index, Index + 2), 16) / 255 || 0,
  );
export function GradientValue(Channel, Position, Colour) {
  const Stops = [...Channel.Stops].sort((A, B) => A.Position - B.Position);
  const Right = Stops.findIndex((Stop) => Stop.Position >= Position);
  if (Right <= 0) return Right === 0 ? Stops[0].Value : Stops.at(-1).Value;
  const A = Stops[Right - 1],
    B = Stops[Right],
    T = Clamp(
      (Position - A.Position) / Math.max(0.00001, B.Position - A.Position),
    );
  if (!Colour) return +A.Value + (+B.Value - +A.Value) * T;
  const First = Rgb(A.Value),
    Last = Rgb(B.Value);
  return (
    "#" +
    First.map((Value, Index) =>
      Math.round((Value + (Last[Index] - Value) * T) * 255)
        .toString(16)
        .padStart(2, "0"),
    ).join("")
  );
}
export function GradientCss(Channel, Field) {
  return `linear-gradient(90deg,${[...Channel.Stops]
    .sort((A, B) => A.Position - B.Position)
    .map(
      (Stop) =>
        `${Field.Kind === "Colour" ? Stop.Value : `rgb(${Math.round(Clamp((Stop.Value - Field.Minimum) / (Field.Maximum - Field.Minimum)) * 255)} ${Math.round(Clamp((Stop.Value - Field.Minimum) / (Field.Maximum - Field.Minimum)) * 255)} ${Math.round(Clamp((Stop.Value - Field.Minimum) / (Field.Maximum - Field.Minimum)) * 255)})`} ${Stop.Position * 100}%`,
    )
    .join(",")})`;
}
