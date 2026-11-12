import Native from "./NativePanels.json";

export const HeightFogProperties = {
  Enabled: "enabled",
  Density: "density",
  "Falloff Height": "height",
  "Sun Scatter": "sunScatter",
  Colour: "color",
};

function Default(Name) {
  return Native.Panels["height-fog"].find((Field) => Field.Label === Name)?.Default;
}

export function HeightFogValue(Values, Name) {
  return Values[Name] !== undefined ? Values[Name] : Default(Name);
}

export function HeightFogReference(Values) {
  return Object.fromEntries(
    Object.entries(HeightFogProperties).map(([Name, Key]) => [
      Key,
      HeightFogValue(Values, Name),
    ]),
  );
}

export function HeightFogChanges(Values, Properties) {
  const Expected = HeightFogReference(Values),
    Changes = {};
  for (const [Name, Key] of Object.entries(HeightFogProperties))
    if (Properties?.[Key] !== undefined && Properties[Key] !== Expected[Key])
      Changes[Name] = Properties[Key];
  return Changes;
}

export function WithoutHeightFogDuplicates(Properties = {}) {
  const Result = { ...Properties };
  for (const Key of Object.values(HeightFogProperties)) delete Result[Key];
  return Result;
}

export function NormalizeHeightFogValues(Values = {}) {
  const Record = Values["height-fog"],
    Reference = Record?.ReferenceInspector;
  if (!Reference?.Properties) return Values;
  const Properties = WithoutHeightFogDuplicates(Reference.Properties);
  if (Object.keys(Properties).length === Object.keys(Reference.Properties).length)
    return Values;
  return {
    ...Values,
    "height-fog": {
      ...Record,
      ReferenceInspector: { ...Reference, Properties },
    },
  };
}
