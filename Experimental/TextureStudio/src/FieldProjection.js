//============================================================================================================================================
//                                                             FIELDPROJECTION.JS                                                             
//============================================================================================================================================
// 🧩 Projects reusable field instances into scoped PBR channel overrides without mixing designer controls into the layer stack.

import * as THREE from "three";
import { PbrChannels, ChannelAddress, ReadMaterialParameter, ResolveAddress } from "./MaterialSpecification.js";
import { SampleGenerator, CamouflagePalette } from "./FieldSpecification.js";
const Prepared = new Map();
const Completed = new Map();
const Clamp = Quantity => Math.min(1, Math.max(0, Quantity));
export function EnumerateMaterialFields(Parameters)
{
    const Document = Parameters.Authoring;
    if (!Document) return [];
    return [...Document.Layers].reverse().filter(Layer => Layer.Enabled).flatMap(Layer => Layer.Channels.flatMap(Channel =>
    {
        const Address = ChannelAddress(Layer, Channel), Root = ResolveAddress(Parameters, Address), Source = Document.Sources[Root] || { Mode: "value" };
        if (Source.Mode === "value" && Layer.Model !== "detail" && !(["normal", "microNormal"].includes(Channel) || (Channel === "coverage" && Layer.Model === "surface"))) return [];
        const Reference = Document.Links[Address], Variable = Reference && Document.Variables.find(Entry => Entry.Identity === Reference);
        const Generator = Source.Generator && Document.Generators.find(Entry => Entry.Identity === Source.Generator);
        const Inputs = Generator ? Object.fromEntries(Generator.Inputs.map(Input => [Input.split(":").at(-1), ReadMaterialParameter(Parameters, Input)])) : null;
        return [{ Channel, LayerIdentity: Layer.Identity, Coverage: ReadMaterialParameter(Parameters, ChannelAddress(Layer, "coverage")) ?? 1, Model: Layer.Model, Address: Root, Source, Specification: PbrChannels[Channel], Domain: Variable || PbrChannels[Channel], Constant: ReadMaterialParameter(Parameters, Root), Procedure: Generator?.Procedure, GeneratorEnabled: Generator?.Enabled, Inputs }];
    }));
}
function FieldStamp(Parameters, Rows)
{
    return JSON.stringify([Rows, Rows.some(Row => Row.Source.Mode === "pattern") ? Parameters.pattern : null]);
}
async function ReadRaster(Row, Parameters)
{
    let Canvas;
    if (Row.Source.Mode === "pattern")
    {
        if (!Parameters.pattern) return null;
        const { RasterPatternChannel } = await import("./PatternSurfaceChannelControls.jsx");
        const Channel = ({ colour: "Color", roughness: "Roughness", metallic: "Metalness", normal: "Normal", microNormal: "Normal", coverage: "Opacity" })[Row.Channel];
        if (!Channel) return null;
        Canvas = await RasterPatternChannel(Parameters.pattern, Channel);
    }
    else if (Row.Source.Texture)
    {
        const Image = new globalThis.Image(); Image.src = Row.Source.Texture.dataUrl; await Image.decode();
        if (Image.width > 256 || Image.height > 256) throw Error("Channel image exceeds 256px.");
        Canvas = document.createElement("canvas"); Canvas.width = Image.width; Canvas.height = Image.height;
        Canvas.getContext("2d").drawImage(Image, 0, 0);
    }
    else return null;
    return { Width: Canvas.width, Height: Canvas.height, Pixels: Canvas.getContext("2d").getImageData(0, 0, Canvas.width, Canvas.height).data };
}
export function SamplePbrField(Row, Horizontal, Vertical, Raster)
{
    const { Source, Specification } = Row;
    let Components, Alpha = 1;
    if (["value", "solid"].includes(Source.Mode))
    {
        const Constant = Row.Constant ?? (Specification.Type === "vector" ? [0, 0, 1] : Specification.Type === "colour" ? "#ffffff" : Specification.Default ?? Specification.Minimum);
        Components = Specification.Type === "vector" ? Constant : Specification.Type === "colour" ? new THREE.Color(Constant).toArray() : [Constant, Constant, Constant];
        return [...Components, 1];
    }
    if (["texture", "pattern"].includes(Source.Mode))
    {
        if (!Raster) return [0, 0, 0, 0];
        const Offset = (Math.min(Raster.Height - 1, Math.floor(Vertical * Raster.Height)) * Raster.Width + Math.min(Raster.Width - 1, Math.floor(Horizontal * Raster.Width))) * 4;
        Components = [0, 1, 2].map(Index => Raster.Pixels[Offset + Index] / 255); Alpha = Raster.Pixels[Offset + 3] / 255;
        if (Specification.Type === "colour") Components = new THREE.Color().setRGB(...Components, THREE.SRGBColorSpace).toArray();
    }
    else
    {
        let Position;
        if (Source.Mode === "field")
        {
            if (!Row.Procedure || !Row.GeneratorEnabled) return [0, 0, 0, 0];
            const θ = (Row.Inputs.angle || 0) * Math.PI / 180, HorizontalRotated = (Horizontal - .5) * Math.cos(θ) - (Vertical - .5) * Math.sin(θ), VerticalRotated = (Horizontal - .5) * Math.sin(θ) + (Vertical - .5) * Math.cos(θ);
            Position = SampleGenerator(Row.Procedure, { ...Row.Inputs, colourCount: Object.keys(Row.Inputs).filter(Key => /^colour\d+$/.test(Key)).length || 4 }, HorizontalRotated, VerticalRotated);
            Position = .5 + (Position - .5) * Row.Inputs.amount;
        }
        else if (Source.Mode === "radial") Position = Clamp(Math.hypot(Horizontal - .5, Vertical - .5) * 2);
        else if (Source.Mode === "conic") Position = (Math.atan2(Vertical - .5, Horizontal - .5) / (2 * Math.PI) + 1) % 1;
        else Position = Horizontal;
        const Stops = Source.Stops || ["#000000", "#ffffff"], Segment = Clamp(Position) * (Stops.length - 1), First = Math.min(Stops.length - 2, Math.floor(Segment));
        if (Row.Procedure?.startsWith("camouflage") && Source.Mode === "field")
        {
            const Palette = Object.entries(Row.Inputs).filter(([Key]) => /^colour\d+$/.test(Key)).sort(([First], [Second]) => Number(First.slice(6)) - Number(Second.slice(6))).map(([, Colour]) => Colour);
            const Colours = Palette.length >= 2 ? Palette : CamouflagePalette, PositionInPalette = Clamp(Position) * (Colours.length - 1), Slot = Math.min(Colours.length - 2, Math.floor(PositionInPalette));
            Components = Row.Procedure === "camouflage-smooth" ? new THREE.Color(Colours[Slot]).lerp(new THREE.Color(Colours[Slot + 1]), PositionInPalette - Slot).toArray() : new THREE.Color(Colours[Math.round(PositionInPalette)]).toArray();
        }
        else Components = new THREE.Color(Stops[First]).lerp(new THREE.Color(Stops[First + 1]), Segment - First).toArray();
    }
    if (Specification.Type === "scalar")
    {
        const Maximum = Source.Direct !== true && Number.isFinite(Row.Constant) ? Math.max(Row.Domain.Minimum, Math.min(Row.Domain.Maximum, Row.Constant)) : Row.Domain.Maximum;
        const Quantity = Row.Domain.Minimum + (.2126 * Components[0] + .7152 * Components[1] + .0722 * Components[2]) * (Maximum - Row.Domain.Minimum);
        Components = [Quantity, Quantity, Quantity];
    }
    else if (Specification.Type === "vector")
    {
        if (["texture", "pattern"].includes(Source.Mode)) Components = Components.map(Component => Component * 2 - 1);
        else
        {
            const Scalar = { ...Row, Specification: { Type: "scalar" }, Domain: { Minimum: 0, Maximum: 1 } }, δ = 1 / 256;
            const HorizontalSlope = (SamplePbrField(Scalar, (Horizontal + δ) % 1, Vertical)[0] - SamplePbrField(Scalar, Horizontal, Vertical)[0]) / δ;
            const VerticalSlope = (SamplePbrField(Scalar, Horizontal, (Vertical + δ) % 1)[0] - SamplePbrField(Scalar, Horizontal, Vertical)[0]) / δ;
            Components = new THREE.Vector3(-HorizontalSlope * .03, VerticalSlope * .03, 1).normalize().toArray();
        }
    }
    if (Source.Direct !== true && Specification.Type === "colour" && typeof Row.Constant === "string") Components = new THREE.Color().fromArray(Components).multiply(new THREE.Color(Row.Constant)).toArray();
    if (Source.Direct !== true && Specification.Type === "vector" && Array.isArray(Row.Constant)) Components = new THREE.Vector3(Components[0] + Row.Constant[0], Components[1] + Row.Constant[1], Components[2] + Row.Constant[2] - 1).normalize().toArray();
    return [...Components, Alpha];
}
export async function RasterChannelPreview(Parameters, LayerIdentity, Channel, Edge = 48)
{
    const Row = EnumerateMaterialFields(Parameters).find(Entry => Entry.LayerIdentity === LayerIdentity && Entry.Channel === Channel);
    if (!Row) return null;
    const Raster = ["texture", "pattern"].includes(Row.Source.Mode) ? await ReadRaster(Row, Parameters) : null;
    const Canvas = document.createElement("canvas"); Canvas.width = Canvas.height = Edge;
    const Context = Canvas.getContext("2d"), Image = Context.createImageData(Edge, Edge);
    for (let Vertical = 0; Vertical < Edge; ++Vertical) for (let Horizontal = 0; Horizontal < Edge; ++Horizontal)
    {
        const Sample = SamplePbrField(Row, Horizontal / Edge, Vertical / Edge, Raster);
        let Colour;
        if (Row.Specification.Type === "colour") Colour = new THREE.Color().fromArray(Sample).convertLinearToSRGB().toArray();
        else if (Row.Specification.Type === "vector") Colour = Sample.slice(0, 3).map(Component => Component * .5 + .5);
        else Colour = [0, 1, 2].map(() => Clamp((Sample[0] - Row.Domain.Minimum) / (Row.Domain.Maximum - Row.Domain.Minimum)));
        Image.data.set([...Colour.map(Component => Math.round(Clamp(Component) * 255)), Math.round(Sample[3] * 255)], (Vertical * Edge + Horizontal) * 4);
    }
    Context.putImageData(Image, 0, 0);
    return Canvas.toDataURL("image/png");
}
export async function RasterPbrFields(Parameters, Edge = 128)
{
    const Rows = EnumerateMaterialFields(Parameters), Columns = Math.min(8, Math.max(1, Rows.length)), Width = Edge * Columns, Height = Edge * Math.max(1, Math.ceil(Rows.length / Columns)), Pixels = new Float32Array(Width * Height * 4);
    for (let Slot = 0; Slot < Rows.length; ++Slot)
    {
        const Row = Rows[Slot], Raster = ["texture", "pattern"].includes(Row.Source.Mode) ? await ReadRaster(Row, Parameters) : null;
        for (let Vertical = 0; Vertical < Edge; ++Vertical) for (let Horizontal = 0; Horizontal < Edge; ++Horizontal)
            Pixels.set(SamplePbrField(Row, Horizontal / Edge, Vertical / Edge, Raster), ((Math.floor(Slot / Columns) * Edge + Vertical) * Width + (Slot % Columns) * Edge + Horizontal) * 4);
    }
    return { Rows, Edge, Width, Height, Pixels };
}
export function AttachPbrFields(Material, Parameters)
{
    const Rows = EnumerateMaterialFields(Parameters);
    if (!Rows.length) return;
    if (Rows.some(Row => Row.Model === "coat" && Row.Channel === "coverage" && Row.Source.Mode !== "value")) Material.clearcoat = Math.max(Material.clearcoat, .000001);
    if (Rows.some(Row => Row.Model === "surface" && Row.Channel === "coverage" && (Row.Source.Mode !== "value" || (Row.Constant ?? 1) < 1))) Material.transparent = true;
    if (Rows.some(Row => ["thickness", "transmission"].includes(Row.Channel))) Material.transmission = Math.max(Material.transmission, .000001);
    if (Rows.some(Row => ["sheen", "sheenRoughness"].includes(Row.Channel))) Material.sheen = Math.max(Material.sheen, .000001);
    if (Rows.some(Row => Row.Channel === "anisotropy")) Material.anisotropy = Math.max(Material.anisotropy, .000001);
    const Edge = Rows.some(Row => Row.Procedure === "camouflage-smooth") ? 512 : 128, Columns = Math.min(8, Rows.length), Width = Edge * Columns, Height = Edge * Math.ceil(Rows.length / Columns), Atlas = new THREE.DataTexture(new Float32Array(Width * Height * 4), Width, Height, THREE.RGBAFormat, THREE.FloatType);
    Atlas.minFilter = Atlas.magFilter = THREE.NearestFilter; Atlas.needsUpdate = true;
    let Disposed = false;
    Material.addEventListener("dispose", () => { Disposed = true; Atlas.dispose(); });
    const Stamp = FieldStamp(Parameters, Rows);
    if (!Prepared.has(Stamp))
    {
        const Pending = RasterPbrFields(Parameters, Edge); Prepared.set(Stamp, Pending);
        Pending.catch(() => { if (Prepared.get(Stamp) === Pending) Prepared.delete(Stamp); });
        if (Prepared.size > 8) Prepared.delete(Prepared.keys().next().value);
    }
    Material.userData.ready = Promise.all([Material.userData.ready, Prepared.get(Stamp).then(Result => { Completed.set(Stamp, Result); if (Completed.size > 8) Completed.delete(Completed.keys().next().value); if (!Disposed) { Atlas.image.data = Result.Pixels; Atlas.needsUpdate = true; } })]);
    const Existing = Material.onBeforeCompile, ProgramKey = Material.customProgramCacheKey;
    Material.customProgramCacheKey = () => `${ProgramKey.call(Material)}-authoring-${Rows.map(Row => Row.Model + Row.Channel + (Row.Procedure === "camouflage-smooth" ? "-smooth" : "")).join("-")}`;
    Material.onBeforeCompile = Shader =>
    {
        Existing.call(Material, Shader);
        Shader.uniforms.uPbrFieldAtlas = { value: Atlas };
        Shader.uniforms.uPbrFieldSheenColour = { value: new THREE.Color(Parameters.sheenColor || "#ffffff") };
        Shader.vertexShader = `varying vec2 vPbrFieldUV;\n${Shader.vertexShader}`.replace("#include <begin_vertex>", "#include <begin_vertex>\nvPbrFieldUV=uv;");
        Shader.fragmentShader = `varying vec2 vPbrFieldUV; uniform sampler2D uPbrFieldAtlas; uniform vec3 uPbrFieldSheenColour;\n${Rows.map((Row, Index) => Row.Procedure === "camouflage-smooth" ? `
vec4 pbrTexel${Index}(vec2 q){return texture2D(uPbrFieldAtlas,(mod(mod(q,${Edge}.)+${Edge}.,${Edge}.)+vec2(.5)+vec2(${(Index % Columns) * Edge}.,${Math.floor(Index / Columns) * Edge}.))/vec2(${Width}.,${Height}.));}
vec4 pbrField${Index}(){vec2 q=fract(vPbrFieldUV)*${Edge}.;vec2 f=fract(q);q=floor(q);return mix(mix(pbrTexel${Index}(q),pbrTexel${Index}(q+vec2(1.,0.)),f.x),mix(pbrTexel${Index}(q+vec2(0.,1.)),pbrTexel${Index}(q+vec2(1.,1.)),f.x),f.y);}` : `vec4 pbrField${Index}(){return texture2D(uPbrFieldAtlas,vec2((fract(vPbrFieldUV.x)*${Edge - 1}.+.5+${(Index % Columns) * Edge}.)/${Width}.,(fract(vPbrFieldUV.y)*${Edge - 1}.+.5+${Math.floor(Index / Columns) * Edge}.)/${Height}.));}`).join("\n")}\n${Shader.fragmentShader}`;
        Shader.fragmentShader = Shader.fragmentShader.replace("#include <transmission_fragment>", THREE.ShaderChunk.transmission_fragment);
        const Before = (Marker, Code) => { Shader.fragmentShader = Shader.fragmentShader.replace(Marker, Code + "\n" + Marker); };
        const After = (Marker, Code) => { Shader.fragmentShader = Shader.fragmentShader.replace(Marker, Marker + "\n" + Code); };
        After("#include <color_fragment>", "vec3 pbrFieldBaseColour=diffuseColor.rgb;");
        Rows.forEach((Row, Index) =>
        {
            const Sample = `pbrField${Index}()`, CoverageSlot = Rows.findIndex(Other => Other.LayerIdentity === Row.LayerIdentity && Other.Channel === "coverage"), Scope = (Row.Model === "flakes" ? `*flakeMask` : "") + (Row.Model === "detail" && Row.Channel !== "coverage" ? CoverageSlot >= 0 ? `*pbrField${CoverageSlot}().r` : `*${Math.max(0, Math.min(1, Row.Coverage)).toFixed(6)}` : "");
            const Blend = (Target, Component = "r") => `${Target}=mix(${Target},${Sample}.${Component},${Sample}.a${Scope});`;
            if (Row.Channel === "colour") Row.Model === "surface" && !(Row.Source.Direct && Parameters.type !== 0) ? After("#include <color_fragment>", Blend("diffuseColor.rgb", "rgb")) : Before("#include <roughnessmap_fragment>", Blend("diffuseColor.rgb", "rgb"));
            if (Row.Channel === "roughness")
            {
                if (Row.Model === "coat") Before("#include <lights_fragment_begin>", `\n#ifdef USE_CLEARCOAT\n${Blend("material.clearcoatRoughness")}\n#endif\n`);
                else if (Row.Model === "surface" && !(Row.Source.Direct && Parameters.type !== 0)) After("#include <roughnessmap_fragment>", Blend("roughnessFactor"));
                else Before("#include <metalnessmap_fragment>", Blend("roughnessFactor"));
            }
            if (Row.Channel === "metallic") Row.Model === "surface" && !(Row.Source.Direct && Parameters.type !== 0) ? After("#include <metalnessmap_fragment>", Blend("metalnessFactor")) : Before("#include <normal_fragment_begin>", Blend("metalnessFactor"));
            if (Row.Channel === "ior") Before("#include <lights_fragment_begin>", Row.Model === "coat" ? `\n#ifdef USE_CLEARCOAT\nmaterial.clearcoatF0=mix(material.clearcoatF0,vec3(pow((${Sample}.r-1.)/(${Sample}.r+1.),2.)),${Sample}.a);\n#endif\n` : `${Blend("material.ior")}material.specularColor=mix(vec3(pow((material.ior-1.)/(material.ior+1.),2.))*specularIntensity,diffuseColor.rgb,metalnessFactor);`);
            if (Row.Channel === "specular") Before("#include <lights_fragment_begin>", `material.specularColor=mix(material.specularColor,mix(vec3(pow((material.ior-1.)/(material.ior+1.),2.))*${Sample}.r,diffuseColor.rgb,metalnessFactor),${Sample}.a);`);
            if (Row.Channel === "thickness") After("material.thickness = thickness;", `material.thickness=mix(material.thickness,${Sample}.r*2.,${Sample}.a);`);
            if (Row.Channel === "transmission") After("material.transmission = transmission;", Blend("material.transmission"));
            if (Row.Channel === "sheen") Before("#include <lights_fragment_begin>", `\n#ifdef USE_SHEEN\nmaterial.sheenColor=mix(material.sheenColor,uPbrFieldSheenColour*${Sample}.r,${Sample}.a);\n#endif\n`);
            if (Row.Channel === "sheenRoughness") Before("#include <lights_fragment_begin>", `\n#ifdef USE_SHEEN\n${Blend("material.sheenRoughness")}\n#endif\n`);
            if (Row.Channel === "anisotropy") Before("#include <lights_fragment_begin>", `\n#ifdef USE_ANISOTROPY\n${Blend("material.anisotropy")}material.alphaT=mix(pow2(material.roughness),1.,pow2(material.anisotropy));\n#endif\n`);
            if (Row.Channel === "coverage" && Row.Model === "surface") Before("#include <opaque_fragment>", `diffuseColor.a*=mix(1.,clamp(${Sample}.r,0.,1.),${Sample}.a);`);
            if (Row.Channel === "coverage" && Row.Model === "flakes") Before("#include <roughnessmap_fragment>", `diffuseColor.rgb=mix(pbrFieldBaseColour,diffuseColor.rgb,mix(1.,${Sample}.r,${Sample}.a));flakeMask*=mix(1.,${Sample}.r,${Sample}.a);`);
            if (Row.Channel === "coverage" && Row.Model === "coat") Before("#include <lights_fragment_begin>", `\n#ifdef USE_CLEARCOAT\n${Blend("material.clearcoat")}\n#endif\n`);
            if (["normal", "microNormal"].includes(Row.Channel) && Row.Model !== "coat") Before("#include <clearcoat_normal_fragment_maps>", `{
                vec3 Qx=dFdx(-vViewPosition),Qy=dFdy(-vViewPosition);vec2 Ux=dFdx(vPbrFieldUV),Uy=dFdy(vPbrFieldUV);float D=Ux.x*Uy.y-Ux.y*Uy.x;
                if(abs(D)>.00000001){vec3 T=normalize((Qx*Uy.y-Qy*Ux.y)/D);T=normalize(T-normal*dot(normal,T));vec3 B=normalize(cross(normal,T))*sign(D);vec3 N=${Sample}.rgb;N.y=-N.y;normal=normalize(mix(normal,mat3(T,B,normal)*normalize(N+vec3(0.,0.,.000001)),${Sample}.a${Scope}));}
            }`);
            if (Row.Channel === "normal" && Row.Model === "coat") After("#include <clearcoat_normal_fragment_maps>", `\n#ifdef USE_CLEARCOAT\n{vec3 T=normalize(dFdx(-vViewPosition));vec3 B=normalize(cross(clearcoatNormal,T));clearcoatNormal=normalize(mix(clearcoatNormal,mat3(T,B,clearcoatNormal)*normalize(${Sample}.rgb+vec3(0.,0.,.000001)),${Sample}.a));}\n#endif\n`);
        });
    };
}
export function ExportPbrAtlas(Parameters)
{
    const Rows = EnumerateMaterialFields(Parameters);
    if (!Rows.length) return null;
    const Result = Completed.get(FieldStamp(Parameters, Rows));
    if (!Result) throw Error("Field atlas is still preparing. Wait for the preview before shader export.");
    return { Rows: Result.Rows, Edge: Result.Edge, Width: Result.Width, Height: Result.Height, Pixels: Array.from(Result.Pixels) };
}
