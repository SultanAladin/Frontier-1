/*============================================================================================================================================
                                                              ARTWORKMASK.JS
============================================================================================================================================*/
// 🧩 Bounded, non-destructive mask sources and retained paint geometry shared by canvas, history and portable SVG export.

export function NormalizeArtworkMask(Input)
{
    if (Input === undefined || Input === null) return undefined;
    if (!Input || !["rect", "ellipse", "image", "paint"].includes(Input.category)) throw new Error("Choose a filled, rectangle, ellipse or embedded image mask.");
    const Scalar = (NumberInput, Fallback, Minimum, Maximum) => Number.isFinite(Number(NumberInput)) ? Math.max(Minimum, Math.min(Maximum, Number(NumberInput))) : Fallback;
    if (Input.category === "image" && (typeof Input.image !== "string" || Input.image.length > 6000000 || !/^data:image\/(png|jpeg|webp);base64,[a-z\d+/=]+$/i.test(Input.image))) throw new Error("Mask images must be bounded embedded PNG, JPEG or WebP.");
    if (Input.paint !== undefined && (!Array.isArray(Input.paint) || Input.paint.length > 256)) throw new Error("Masks support up to 256 retained paint operations.");
    let Population = 0;
    const Paint = (Input.paint || []).map(Operation =>
    {
        if (!Operation || !["stroke", "rect", "ellipse", "path", "gradient"].includes(Operation.category) || !Array.isArray(Operation.points) || Operation.points.length < 2 || Operation.points.length > 2048) throw new Error("Mask paint geometry is invalid or exceeds its point limit.");
        Population += Operation.points.length;
        if (Population > 16384) throw new Error("Mask paint is limited to 16,384 retained points.");
        return {
            category: Operation.category,
            points: Operation.points.map(Point =>
            {
                if (!Array.isArray(Point) || Point.length !== 2 || Point.some(Coordinate => !Number.isFinite(Coordinate))) throw new Error("Mask paint coordinates must be finite pairs.");
                return Point.map(Coordinate => Math.round(Math.max(0, Math.min(1, Coordinate)) * 1000000) / 1000000);
            }),
            ink: Scalar(Operation.ink, 1, 0, 1), opacity: Scalar(Operation.opacity, 1, 0, 1),
            width: Scalar(Operation.width, .04, .001, 1), softness: Scalar(Operation.softness, 0, 0, 1), filled: Operation.filled !== false,
        };
    });
    return {
        category: Input.category, enabled: Input.enabled !== false, invert: Input.invert === true,
        mode: Input.mode === "luminance" ? "luminance" : "alpha",
        x: Scalar(Input.x, 50, -100, 200), y: Scalar(Input.y, 50, -100, 200),
        width: Scalar(Input.width, 100, 1, 400), height: Scalar(Input.height, 100, 1, 400),
        opacity: Scalar(Input.opacity, 1, 0, 1),
        ...(Input.category === "paint" ? { base: Scalar(Input.base, 1, 0, 1) } : {}),
        ...(Input.category === "image" ? { image: Input.image } : {}),
        ...(Paint.length ? { paint: Paint } : {}),
    };
}
export function ArtworkMaskPaintSVG(Operation, Token)
{
    const [Start, End] = [Operation.points[0], Operation.points.at(-1)];
    const X = Math.min(Start[0], End[0]), Y = Math.min(Start[1], End[1]), Width = Math.abs(End[0] - Start[0]), Height = Math.abs(End[1] - Start[1]);
    const Gray = Math.round(Operation.ink * 255), Colour = `rgb(${Gray},${Gray},${Gray})`;
    const Filter = Operation.softness > 0 ? `<defs><filter id="${Token}-soft" x="-100%" y="-100%" width="300%" height="300%" color-interpolation-filters="sRGB"><feGaussianBlur stdDeviation="${Operation.width * Operation.softness * .5}"/></filter></defs>` : "";
    const Attributes = `opacity="${Operation.opacity}" fill="${Operation.filled && Operation.category !== "stroke" ? Colour : "none"}" stroke="${Colour}" stroke-width="${Operation.category === "stroke" || !Operation.filled ? Operation.width : 0}" stroke-linecap="round" stroke-linejoin="round"${Filter ? ` filter="url(#${Token}-soft)"` : ""}`;
    let Geometry;
    if (Operation.category === "rect") Geometry = `<rect x="${X}" y="${Y}" width="${Width}" height="${Height}" ${Attributes}/>`;
    else if (Operation.category === "ellipse") Geometry = `<ellipse cx="${X + Width / 2}" cy="${Y + Height / 2}" rx="${Width / 2}" ry="${Height / 2}" ${Attributes}/>`;
    else if (Operation.category === "gradient") Geometry = `<defs><linearGradient id="${Token}-gradient" gradientUnits="userSpaceOnUse" x1="${Start[0]}" y1="${Start[1]}" x2="${End[0]}" y2="${End[1]}"><stop stop-color="${Colour}"/><stop offset="1" stop-color="${Operation.ink < .5 ? "white" : "black"}"/></linearGradient></defs><rect width="1" height="1" fill="url(#${Token}-gradient)" opacity="${Operation.opacity}"/>`;
    else Geometry = `<${Operation.category === "path" ? "polygon" : "polyline"} points="${Operation.points.map(Point => Point.join(",")).join(" ")}" ${Attributes}/>`;
    return Filter + Geometry;
}
export function ApplyArtworkMask(Artwork, Content)
{
    const Mask = Artwork.mask;
    if (!Mask || !Mask.enabled) return Content;
    let Hash = 2166136261;
    const Identity = JSON.stringify(Mask);
    for (let Index = 0; Index < Identity.length; ++Index) Hash = Math.imul(Hash ^ Identity.charCodeAt(Index), 16777619);
    const Token = `artwork-mask-${String(Artwork.id || "shape").replace(/[^\w-]/g, "")}-${(Hash >>> 0).toString(36)}`;
    const X = (Mask.x - Mask.width / 2) / 100, Y = (Mask.y - Mask.height / 2) / 100;
    const Width = Mask.width / 100, Height = Mask.height / 100;
    const Source = Mask.category === "paint" ? `<rect width="1" height="1" fill="rgb(${Math.round(Mask.base * 255)},${Math.round(Mask.base * 255)},${Math.round(Mask.base * 255)})"/>` : Mask.category === "image" ? `<image x="${X}" y="${Y}" width="${Width}" height="${Height}" preserveAspectRatio="none" href="${Mask.image}"/>` : Mask.category === "ellipse" ? `<ellipse cx="${Mask.x / 100}" cy="${Mask.y / 100}" rx="${Width / 2}" ry="${Height / 2}" fill="white"/>` : `<rect x="${X}" y="${Y}" width="${Width}" height="${Height}" fill="white"/>`;
    const Coefficients = Mask.mode === "luminance" ? ".2126 .7152 .0722 0 0" : "0 0 0 0 1";
    const Paint = (Mask.paint || []).map((Operation, Index) => ArtworkMaskPaintSVG(Operation, `${Token}-${Index}`)).join("");
    const Definitions = `<filter id="${Token}-tone" x="0" y="0" width="100%" height="100%" color-interpolation-filters="sRGB"><feColorMatrix type="matrix" values="${Coefficients} ${Coefficients} ${Coefficients} 0 0 0 1 0"/></filter><filter id="${Token}-invert" x="0" y="0" width="100%" height="100%" color-interpolation-filters="sRGB"><feComponentTransfer><feFuncR type="table" tableValues="1 0"/><feFuncG type="table" tableValues="1 0"/><feFuncB type="table" tableValues="1 0"/></feComponentTransfer></filter><mask id="${Token}" maskUnits="objectBoundingBox" maskContentUnits="objectBoundingBox" x="0" y="0" width="1" height="1" style="mask-type:luminance"><rect width="1" height="1" fill="black"/><g opacity="${Mask.opacity}"><g${Mask.invert ? ` filter="url(#${Token}-invert)"` : ""}><rect width="1" height="1" fill="black"/>${Mask.category === "paint" ? Source : `<g filter="url(#${Token}-tone)">${Source}</g>`}${Paint}</g></g></mask>`;
    // 📝 SVG object bounding boxes exclude stroke and collapse on flat paths. Include a non-rendering
    // geometric enclosure so a reveal-all mask keeps the original stroke, including horizontal/vertical lines.
    const Sized = Artwork.paint && !["image", "svg"].includes(Artwork.kind);
    const Padding = (Artwork.strokeWidth || 0) * (Artwork.strokeLinejoin === "miter" ? 2 : Artwork.strokeLinecap === "square" ? Math.SQRT1_2 : .5);
    const EnclosureWidth = Sized ? Artwork.width : 100, EnclosureHeight = Sized ? Artwork.height : 100;
    const Enclosure = (Sized || Padding > 0) && Number.isFinite(EnclosureWidth) && Number.isFinite(EnclosureHeight)
        ? `<rect x="${(Sized ? -EnclosureWidth / 2 : 0) - Padding}" y="${(Sized ? -EnclosureHeight / 2 : 0) - Padding}" width="${EnclosureWidth + Padding * 2}" height="${EnclosureHeight + Padding * 2}" fill="white" stroke="none" opacity="0" pointer-events="none"/>` : "";
    return `<defs>${Definitions}</defs><g mask="url(#${Token})">${Enclosure}${Content}</g>`;
}
export function ArtworkMaskPreviewSVG(Mask)
{
    return `<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 1 1"><rect width="1" height="1" fill="black"/>${ApplyArtworkMask({ id: "preview", mask: { ...Mask, enabled: true } }, '<rect width="1" height="1" fill="white"/>')}</svg>`;
}
