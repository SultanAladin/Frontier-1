/*============================================================================================================================================
                                                          PATTERNCOLOURSOURCES.JS                                                           
============================================================================================================================================*/
// 🧩 Validated pattern paint sources and SVG paint-server coordinates.

// Pattern paint sources are authoring data; finish maps keep their numeric tints.
const SourceIdentities = new WeakMap();
const HexColour = /^#[\da-f]{6}$/i;
const RasterImage = /^data:image\/(?:png|jpeg|webp);base64,[A-Za-z0-9+/]+=*$/;
export function NormalizePatternColourSource(Input)
{
  if (!Input || !["linear", "radial", "image"].includes(Input.category)) return undefined;
  if (Input.category === "image")
  {
    if (typeof Input.image !== "string" || Input.image.length > 6000000 || !RasterImage.test(Input.image)) return undefined;
    return { category: "image", image: Input.image, fit: ["cover", "contain", "stretch"].includes(Input.fit) ? Input.fit : "cover" };
  }
  const Stops = Array.isArray(Input.stops) ? Input.stops.slice(0, 12).filter(Stop => HexColour.test(Stop?.color) && Number.isFinite(Stop.offset)).map(Stop => ({ color: Stop.color, offset: Math.max(0, Math.min(1, Stop.offset)) })).sort((Left, Right) => Left.offset - Right.offset) : [];
  if (Stops.length < 2) return undefined;
  return { category: Input.category, angle: Number.isFinite(Input.angle) ? Math.max(-180, Math.min(180, Input.angle)) : 0, stops: Stops };
}
function PatternSourceIdentity(Layer, Channel, Source)
{
  let Identity = SourceIdentities.get(Source);
  if (!Identity)
  {
    let Hash = 2166136261;
    const Text = JSON.stringify(Source);
    for (let Index = 0; Index < Text.length; Index++) Hash = Math.imul(Hash ^ Text.charCodeAt(Index), 16777619);
    Identity = (Hash >>> 0).toString(36);
    SourceIdentities.set(Source, Identity);
  }
  return `pattern-paint-${String(Layer.id || "ground").replace(/[^\w-]/g, "-")}-${Channel}-${Layer.paint ? "sized" : "unit"}-${Layer.width || 100}-${Layer.height || 100}-${Layer.strokeWidth || 0}-${Identity}`;
}
export function PatternColourReference(Layer, Channel, Solid)
{
  const Source = Layer[`${Channel}Source`];
  return Source ? `url(#${PatternSourceIdentity(Layer, Channel, Source)})` : Solid;
}
export function PatternColourDefinitions(Layer)
{
  return ["fill", "stroke"].map(Channel =>
  {
    const Source = Layer[`${Channel}Source`];
    if (!Source) return "";
    const Identity = PatternSourceIdentity(Layer, Channel, Source);
    const Width = Layer.paint ? Math.max(Layer.width, Channel === "stroke" ? Layer.strokeWidth || 1 : 1) : 100;
    const Height = Layer.paint ? Math.max(Layer.height, Channel === "stroke" ? Layer.strokeWidth || 1 : 1) : 100;
    const OriginX = Layer.paint ? -Width / 2 : 0;
    const OriginY = Layer.paint ? -Height / 2 : 0;
    if (Source.category === "image")
    {
      const Aspect = Source.fit === "stretch" ? "none" : `xMidYMid ${Source.fit === "contain" ? "meet" : "slice"}`;
      return `<pattern id="${Identity}" ${Channel === "stroke" ? `patternUnits="userSpaceOnUse" x="${OriginX}" y="${OriginY}" width="${Width}" height="${Height}"` : 'width="1" height="1"'} viewBox="0 0 ${Width} ${Height}" preserveAspectRatio="none"><image width="${Width}" height="${Height}" preserveAspectRatio="${Aspect}" href="${Source.image}"/></pattern>`;
    }
    const Stops = Source.stops.map(Stop => `<stop offset="${Stop.offset}" stop-color="${Stop.color}"/>`).join("");
    if (Source.category === "radial") return `<radialGradient id="${Identity}" ${Channel === "stroke" ? `gradientUnits="userSpaceOnUse" cx="${OriginX + Width / 2}" cy="${OriginY + Height / 2}" r="${Math.max(Width, Height) * .7}"` : 'cx=".5" cy=".5" r=".7"'}>${Stops}</radialGradient>`;
    const Angle = Source.angle * Math.PI / 180;
    const X = Math.cos(Angle) / 2, Y = Math.sin(Angle) / 2;
    return `<linearGradient id="${Identity}" ${Channel === "stroke" ? `gradientUnits="userSpaceOnUse" x1="${OriginX + (.5 - X) * Width}" y1="${OriginY + (.5 - Y) * Height}" x2="${OriginX + (.5 + X) * Width}" y2="${OriginY + (.5 + Y) * Height}"` : `x1="${.5 - X}" y1="${.5 - Y}" x2="${.5 + X}" y2="${.5 + Y}"`}>${Stops}</linearGradient>`;
  }).join("");
}
export function PatternGroundLayer(Document)
{
  return { id: "ground", paint: true, width: 512, height: 512, fillSource: Document.backgroundSource };
}
export function PatternGroundRectangles(Document, Width, Height)
{
  if (!Document.backgroundSource) return `<rect width="${Width}" height="${Height}" fill="${Document.background}" opacity="${Document.backgroundOpacity}"/>`;
  const Fill = PatternColourReference(PatternGroundLayer(Document), "fill", Document.background);
  let Shapes = "";
  for (let Column = 0; Column < Width / 512; Column++)
  {
    const Offset = Document.repeat === "half-drop" && Column % 2 ? 256 : 0;
    for (let Row = Offset ? -1 : 0; Row < Height / 512; Row++)
    {
      const FlipX = Document.repeat === "mirror" && Column % 2 ? -1 : 1;
      const FlipY = Document.repeat === "mirror" && Math.abs(Row % 2) ? -1 : 1;
      Shapes += `<rect width="512" height="512" fill="${Fill}" transform="translate(${Column * 512 + (FlipX < 0 ? 512 : 0)} ${Row * 512 + Offset + (FlipY < 0 ? 512 : 0)}) scale(${FlipX} ${FlipY})"/>`;
    }
  }
  return `<g opacity="${Document.backgroundOpacity}">${Shapes}</g>`;
}
export function PatternColourPreviewCss(Source, Solid)
{
  if (!Source) return Solid;
  if (Source.category === "image") return `url("${Source.image}") center / cover`;
  const Stops = Source.stops.map(Stop => `${Stop.color} ${Stop.offset * 100}%`).join(",");
  return Source.category === "radial" ? `radial-gradient(circle,${Stops})` : `linear-gradient(${Source.angle + 90}deg,${Stops})`;
}
