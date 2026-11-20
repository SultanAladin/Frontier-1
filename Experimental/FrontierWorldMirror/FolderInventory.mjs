// Indexed, iterative traversal: large collections do not recurse or rescan the scene per child.
export const CollectionTypes = {
  group: "Folders",
  geometry: "Geometry",
  camera: "Cameras",
  light: "Lights",
  sun: "Sun",
  atmosphere: "Atmospheres",
  moon: "Moons",
  stars: "Stars",
  wind: "Wind",
  clouds: "Clouds",
  "local-cloud": "Local clouds",
  "height-fog": "Height fog",
  "aerial-fog": "Aerial fog",
  "local-fog": "Local fog",
  precipitation: "Precipitation",
  rainbow: "Rainbows",
  flare: "Lens flares",
  post: "Post processing",
};
export function FolderInventory(Rows, Root, Hidden = {}) {
  const ById = new Map(Rows.map((Row) => [Row.Id, Row])),
    Children = new Map();
  for (const Row of Rows) {
    if (!Children.has(Row.Parent)) Children.set(Row.Parent, []);
    Children.get(Row.Parent).push(Row);
  }
  const Trail = [],
    Ancestors = new Set();
  let Owner = ById.get(Root),
    AncestorHidden = false;
  while (Owner && !Ancestors.has(Owner.Id)) {
    Ancestors.add(Owner.Id);
    Trail.unshift(Owner);
    AncestorHidden ||= !!Hidden[Owner.Id];
    Owner = ById.get(Owner.Parent);
  }
  const Entries = [],
    Seen = new Set([Root]),
    Stack = [...(Children.get(Root) || [])]
      .reverse()
      .map((Row) => ({ Row, Depth: 1, Inherited: AncestorHidden }));
  while (Stack.length) {
    const Item = Stack.pop();
    if (Seen.has(Item.Row.Id)) continue;
    Seen.add(Item.Row.Id);
    const Visible =
      Item.Row.Id === "camera" || !(Item.Inherited || Hidden[Item.Row.Id]);
    Entries.push({ ...Item, Visible });
    const Nested = Children.get(Item.Row.Id) || [];
    for (let I = Nested.length - 1; I >= 0; I--)
      Stack.push({
        Row: Nested[I],
        Depth: Item.Depth + 1,
        Inherited: !Visible,
      });
  }
  const Types = new Map();
  for (const { Row } of Entries)
    Types.set(Row.Panel, (Types.get(Row.Panel) || 0) + 1);
  return {
    Entries,
    Trail,
    ById,
    Children,
    Types: [...Types].sort((A, B) => B[1] - A[1] || A[0].localeCompare(B[0])),
    Direct: Entries.filter((Item) => Item.Depth === 1).length,
    Folders: Entries.filter((Item) => Item.Row.Panel === "group").length,
    Visible: Entries.filter((Item) => Item.Visible).length,
    Depth: Entries.reduce((Max, Item) => Math.max(Max, Item.Depth), 0),
    Constructed: Entries.filter((Item) => Item.Row.Preview).length,
  };
}
