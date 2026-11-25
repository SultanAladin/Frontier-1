// Experimental/LoftedVehicleSdfWorkbench/LoftedVehicleSdfValidation.mjs
// 🧩 Automated 7-check verification suite for the Lofted Bi-Directional Surface-Curve SDF vs. 3D Voxel SDF Workbench:
//      1. FCT1 Binary Decoding & 25,068-Triangle ControlVehicle Bounds / BVH Integrity
//      2. Monotonic Lofted Curve SDF Convergence (10x16 -> 18x24 -> 32x36 -> 48x64) + Wheel-Arch CSG Accuracy
//      3. O(M*K) vs. O(N^3) VRAM Compression & Bake-Time Verification against 16^3, 32^3, 64^3 Voxel SDFs
//      4. Showroom Crease-Split Geometry, Cutaway Plane & 3D RibxRail Spline Cage Buffer Integrity
//      5. WGSL & GLSL Shader Static Derivative-Uniformity & Resource-Slot Audit
//      6. HTML <-> JS DOM Control ID Synchronization
//      7. Frontier Governance Scan (BannedWordsVocabulary.md + Boolean Prefixes + SKILL-Emoji.md Allowlist)

import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";

import {
    LoftResolutionPresets,
    QuadPatchResolutionPresets,
    EncodeFloat16Bits,
    ParseFct1VehicleBinary,
    AssembleCompleteControlVehicle,
    BuildTriangleBvhAccelerator,
    CompileLoftedVehicleSdf,
    EvaluateLoftedVehicleSdf,
    CompileQuadPatchVehicleSdf,
    EvalBicubicQuadChartSurface,
    EvaluateQuadPatchVehicleSdf,
    CompileUniformVoxelVehicleSdf,
    EvaluateVoxelVehicleSdf,
    BenchmarkLoftVsVoxelSdf,
    BenchmarkQuadPatchVsVoxelSdf
} from "./LoftedVehicleSdfCompiler.js";

import {
    ShowroomVertexFloats,
    CageLineVertexFloats,
    BuildShowroomSceneBuffers,
    BuildCutawayPlaneBuffers,
    BuildLoftCurveCageBuffers,
    BuildQuadPatchCageBuffers,
    BuildVoxelCageBuffers
} from "./VehicleShowroomBuilder.js";

import {
    ShowroomSurfaceWgsl,
    RayMarchIsoSurfaceWgsl,
    CageOverlayWgsl,
    ShowroomSurfaceGlslVert,
    ShowroomSurfaceGlslFrag,
    RayMarchIsoSurfaceGlslVert,
    RayMarchIsoSurfaceGlslFrag,
    CageOverlayGlslVert,
    CageOverlayGlslFrag
} from "./LoftedVehicleShaders.js";

const DirHere  = path.dirname(fileURLToPath(import.meta.url));
const RepoRoot = path.resolve(DirHere, "../..");

function DecodeFloat16Bits(H)
{
    const Sign = (H & 0x8000) ? -1.0 : 1.0;
    const Exp  = (H >>> 10) & 0x1F;
    const Frac = H & 0x03FF;
    if (Exp === 0) return Sign * Math.pow(2, -14) * (Frac / 1024.0);
    if (Exp === 31) return Frac ? NaN : Sign * Infinity;
    return Sign * Math.pow(2, Exp - 15) * (1.0 + Frac / 1024.0);
}

let ChecksPassed = 0;
const TotalChecks = 8;

function AssertCondition(Cond, Msg)
{
    if (!Cond)
    {
        console.error(`🔴 FAILED: ${Msg}`);
        process.exit(1);
    }
}

console.log("🔍 Running Lofted Bi-Directional Vehicle SDF Workbench Verification Suite...\n");

// ============================================================================
// Check 1: FCT1 Binary Decoding & 25,068-Triangle ControlVehicle Integrity
// ============================================================================
function ReadRawArrayBuffer(FilePath)
{
    const Buf = fs.readFileSync(FilePath);
    const Copy = new Uint8Array(Buf.byteLength);
    Copy.set(Buf);
    return Copy.buffer;
}

const BodyBin = ParseFct1VehicleBinary(ReadRawArrayBuffer(path.join(DirHere, "Content/ControlVehicleBody.bin")));
const RimBin  = ParseFct1VehicleBinary(ReadRawArrayBuffer(path.join(DirHere, "Content/ControlVehicleRim.bin")));
const TyreBin = ParseFct1VehicleBinary(ReadRawArrayBuffer(path.join(DirHere, "Content/ControlVehicleTyre.bin")));

const Car = AssembleCompleteControlVehicle(BodyBin, RimBin, TyreBin);
AssertCondition(Car.TotalVerts === 12572, `Expected 12,572 vertices, got ${Car.TotalVerts}`);
AssertCondition(Car.TotalFaces === 25068, `Expected 25,068 triangles, got ${Car.TotalFaces}`);
AssertCondition(Math.abs(Car.BoundsMin[1]) < 0.005, `Expected tyre contact at Y=0.0, got ${Car.BoundsMin[1]}`);
AssertCondition(Math.abs(Car.BoundsMax[2] - 3.110) < 0.02, `Unexpected nose Z bound ${Car.BoundsMax[2]}`);

const ExactBvh = BuildTriangleBvhAccelerator(Car.Positions, Car.Indices, Car.TotalFaces);
AssertCondition(ExactBvh.BoxCount > 10000, `Unexpected BVH box count ${ExactBvh.BoxCount}`);
++ChecksPassed;
console.log(`✔️ [1/${TotalChecks}] FCT1 ControlVehicle assembled (${Car.TotalVerts} verts, ${Car.TotalFaces} tris, ${ExactBvh.BoxCount} BVH boxes)`);

// ============================================================================
// Check 2: Monotonic Lofted Curve SDF Convergence & rgba16float Precision
// ============================================================================
const CompiledLofts = [];
for (const P of LoftResolutionPresets)
{
    const L = CompileLoftedVehicleSdf(Car, P.RibsM, P.RailsK, P.WheelSamples);
    CompiledLofts.push(L);

    let MaxHalfErr = 0.0;
    for (let I = 0; I < L.HermiteKnotsF32.length; ++I)
    {
        const Orig = L.HermiteKnotsF32[I];
        const Dec  = DecodeFloat16Bits(L.HermiteKnotsRgba16[I]);
        MaxHalfErr = Math.max(MaxHalfErr, Math.abs(Orig - Dec));
    }
    AssertCondition(MaxHalfErr < 1.5e-3, `Half-float knot quantization error too high (${MaxHalfErr})`);
}

// Verify interior cabin negative sign & exterior positive sign + wheel-well cavity clearance
const RefLoft = CompiledLofts[2]; // 32x36
AssertCondition(EvaluateLoftedVehicleSdf(RefLoft, [0.0, 0.85, 0.0]) < -0.35, "Cabin center must be strictly negative inside Loft SDF");
AssertCondition(EvaluateLoftedVehicleSdf(RefLoft, [0.0, 2.50, 0.0]) > 0.65, "Point above roof must be strictly positive outside Loft SDF");
// Inside carved wheel-arch air gap between tyre top (Y=1.017) and arch lip (Y=1.068):
const WheelGapD = EvaluateLoftedVehicleSdf(CompiledLofts[3], [1.05, 1.042, 1.7274]);
AssertCondition(Math.abs(WheelGapD) < 0.06, `Wheel-well arch gap should be carved cleanly, got d=${WheelGapD}`);

const Vox32 = CompileUniformVoxelVehicleSdf(Car, ExactBvh, 32, RefLoft);
const BenchResults = CompiledLofts.map((L) => BenchmarkLoftVsVoxelSdf(Car, ExactBvh, L, Vox32));
for (let I = 1; I < BenchResults.length; ++I)
{
    AssertCondition(
        BenchResults[I].LoftMeanErrMm < BenchResults[I - 1].LoftMeanErrMm,
        `Loft mean error failed to decrease monotonically (${BenchResults[I - 1].LoftMeanErrMm} -> ${BenchResults[I].LoftMeanErrMm})`
    );
}
AssertCondition(BenchResults[3].LoftMedianMm < 1.5, `48x64 Loft median error > 1.5 mm (${BenchResults[3].LoftMedianMm})`);
AssertCondition(BenchResults[3].LoftP90Mm < 12.0, `48x64 Loft P90 error > 12.0 mm (${BenchResults[3].LoftP90Mm})`);
++ChecksPassed;
console.log(
    `✔️ [2/${TotalChecks}] Monotonic Loft convergence verified: ` +
    `10x16 Med=${BenchResults[0].LoftMedianMm.toFixed(2)}mm -> ` +
    `18x24 Med=${BenchResults[1].LoftMedianMm.toFixed(2)}mm -> ` +
    `32x36 Med=${BenchResults[2].LoftMedianMm.toFixed(2)}mm -> ` +
    `48x64 Med=${BenchResults[3].LoftMedianMm.toFixed(2)}mm (P90=${BenchResults[3].LoftP90Mm.toFixed(2)}mm)`
);

// ============================================================================
// Check 3: Closed 6-Chart 4-Corner C² Quad-Patch Convergence, 0 Poles & Seam Watertightness
// ============================================================================
const BodyBvh = BuildTriangleBvhAccelerator(Car.BodyPart.Positions, Car.BodyPart.Indices, Car.BodyPart.FaceCount);
const CompiledQuads = [];
for (const P of QuadPatchResolutionPresets)
{
    const Q = CompileQuadPatchVehicleSdf(Car, P.GridN, P.WheelSamples, BodyBvh);
    CompiledQuads.push(Q);

    let MaxHalfErr = 0.0;
    for (let I = 0; I < Q.HermiteKnotsF32.length; ++I)
    {
        const Orig = Q.HermiteKnotsF32[I];
        const Dec  = DecodeFloat16Bits(Q.HermiteKnotsRgba16[I]);
        MaxHalfErr = Math.max(MaxHalfErr, Math.abs(Orig - Dec));
    }
    AssertCondition(MaxHalfErr < 1.5e-3, `Quad-Patch half-float knot quantization error too high (${MaxHalfErr})`);
}

const RefQuad = CompiledQuads[2]; // 6 x 16^2 = 1,536 4-corner patches
AssertCondition(EvaluateQuadPatchVehicleSdf(RefQuad, [0.0, 0.85, 0.0]) < -0.35, "Cabin center must be strictly negative inside Quad-Patch SDF");
AssertCondition(EvaluateQuadPatchVehicleSdf(RefQuad, [0.0, 2.50, 0.0]) > 0.65, "Point above roof must be strictly positive outside Quad-Patch SDF");
const QuadWheelGapD = EvaluateQuadPatchVehicleSdf(CompiledQuads[3], [1.05, 1.042, 1.7274]);
AssertCondition(Math.abs(QuadWheelGapD) < 0.06, `Quad-Patch wheel-well arch gap should be carved cleanly, got d=${QuadWheelGapD}`);

// Verify ZERO polar triangle-fan singularity at exact Front Grille (Chart 4) & Rear Bumper (Chart 5) centers:
for (const EndChart of [4, 5])
{
    const EndSurf = EvalBicubicQuadChartSurface(RefQuad, EndChart, 0.0, 0.0);
    const Jx = EndSurf.Sxi[1] * EndSurf.Seta[2] - EndSurf.Sxi[2] * EndSurf.Seta[1];
    const Jy = EndSurf.Sxi[2] * EndSurf.Seta[0] - EndSurf.Sxi[0] * EndSurf.Seta[2];
    const Jz = EndSurf.Sxi[0] * EndSurf.Seta[1] - EndSurf.Sxi[1] * EndSurf.Seta[0];
    const JacArea = Math.hypot(Jx, Jy, Jz);
    AssertCondition(JacArea > 0.15, `Degenerate polar Jacobian (${JacArea}) on end-cap Chart ${EndChart}`);
}

// Verify knot-aligned cube-box seam watertightness (< 1e-6 mm!) between Front (+Z, Chart 4, Eta=+1) & Top (+Y, Chart 2, Eta=-1):
let MaxSeamGapMm = 0.0;
for (let S = 0; S <= 32; ++S)
{
    const Xi = -1.0 + (2.0 * S) / 32.0;
    const FrontSeam = EvalBicubicQuadChartSurface(RefQuad, 4, Xi, 1.0).Pos;
    const TopSeam   = EvalBicubicQuadChartSurface(RefQuad, 2, Xi, -1.0).Pos;
    const GapMm = Math.hypot(FrontSeam[0] - TopSeam[0], FrontSeam[1] - TopSeam[1], FrontSeam[2] - TopSeam[2]) * 1000.0;
    if (GapMm > MaxSeamGapMm) MaxSeamGapMm = GapMm;
}
AssertCondition(MaxSeamGapMm < 1e-6, `Cube-box chart seam gap too large (${MaxSeamGapMm} mm)`);

const QuadBenchResults = CompiledQuads.map((Q) => BenchmarkQuadPatchVsVoxelSdf(Car, ExactBvh, Q, Vox32));
for (let I = 1; I < QuadBenchResults.length; ++I)
{
    AssertCondition(
        QuadBenchResults[I].LoftMeanErrMm < QuadBenchResults[I - 1].LoftMeanErrMm,
        `Quad-Patch mean error failed to decrease monotonically (${QuadBenchResults[I - 1].LoftMeanErrMm} -> ${QuadBenchResults[I].LoftMeanErrMm})`
    );
    AssertCondition(
        QuadBenchResults[I].LoftP90Mm < QuadBenchResults[I - 1].LoftP90Mm,
        `Quad-Patch P90 error failed to decrease monotonically (${QuadBenchResults[I - 1].LoftP90Mm} -> ${QuadBenchResults[I].LoftP90Mm})`
    );
}
AssertCondition(QuadBenchResults[3].LoftMedianMm < 2.0, `6x24^2 Quad-Patch median error > 2.0 mm (${QuadBenchResults[3].LoftMedianMm})`);
AssertCondition(QuadBenchResults[3].LoftP90Mm < 10.0, `6x24^2 Quad-Patch P90 error > 10.0 mm (${QuadBenchResults[3].LoftP90Mm})`);
++ChecksPassed;
console.log(
    `✔️ [3/${TotalChecks}] Closed 4-Corner C² Quad-Patch verified (0 poles, seam gap=${MaxSeamGapMm.toExponential(2)}mm): ` +
    `6x6² Med=${QuadBenchResults[0].LoftMedianMm.toFixed(2)}mm -> ` +
    `6x10² Med=${QuadBenchResults[1].LoftMedianMm.toFixed(2)}mm -> ` +
    `6x16² Med=${QuadBenchResults[2].LoftMedianMm.toFixed(2)}mm -> ` +
    `6x24² Med=${QuadBenchResults[3].LoftMedianMm.toFixed(2)}mm (P90=${QuadBenchResults[3].LoftP90Mm.toFixed(2)}mm)`
);

// ============================================================================
// Check 4: O(M*K) vs. O(N^3) VRAM Compression & Voxel SDF Consistency
// ============================================================================
const Vox16 = CompileUniformVoxelVehicleSdf(Car, ExactBvh, 16, RefQuad);
const Vox64 = CompileUniformVoxelVehicleSdf(Car, ExactBvh, 64, RefQuad);
AssertCondition(EvaluateVoxelVehicleSdf(Vox32, [0.0, 0.85, 0.0]) < -0.25, "Voxel 32^3 cabin center should be negative");
AssertCondition(EvaluateVoxelVehicleSdf(Vox64, [0.0, 2.50, 0.0]) > 0.65, "Voxel 64^3 point above roof should be positive");

const Ratio32 = Vox32.VramFp16Bytes / RefQuad.CompactKnotBytes;
const Ratio64 = Vox64.VramFp16Bytes / RefQuad.CompactKnotBytes;
AssertCondition(Ratio32 > 15.0, `Expected >15x VRAM compression vs 32^3, got ${Ratio32}`);
AssertCondition(Ratio64 > 120.0, `Expected >120x VRAM compression vs 64^3, got ${Ratio64}`);
++ChecksPassed;
console.log(
    `✔️ [4/${TotalChecks}] VRAM scaling verified: Quad 6x16² = ${(RefQuad.CompactKnotBytes / 1024).toFixed(2)} KB compact ` +
    `(${Ratio32.toFixed(1)}x smaller than Voxel 32^3, ${Ratio64.toFixed(1)}x smaller than Voxel 64^3)`
);

// ============================================================================
// Check 5: Showroom Geometry, Cutaway Plane & 4-Corner Quad-Patch / Loft Cage Buffers
// ============================================================================
const ShowroomBuf = BuildShowroomSceneBuffers(Car);
AssertCondition(ShowroomBuf.VehicleIndexCount === Car.TotalFaces * 3, "Vehicle index count mismatch");
for (let I = 0; I < ShowroomBuf.Vertices.length; ++I)
{
    AssertCondition(Number.isFinite(ShowroomBuf.Vertices[I]), `Non-finite float in ShowroomBuf at ${I}`);
}
for (let V = 0; V < ShowroomBuf.Vertices.length; V += ShowroomVertexFloats)
{
    const NLen = Math.hypot(ShowroomBuf.Vertices[V + 3], ShowroomBuf.Vertices[V + 4], ShowroomBuf.Vertices[V + 5]);
    AssertCondition(Math.abs(NLen - 1.0) < 1e-2, `Non-unit normal (${NLen}) in ShowroomBuf`);
}

const CutZ = BuildCutawayPlaneBuffers(0, 0.35);
const CutX = BuildCutawayPlaneBuffers(1, 0.00);
AssertCondition(CutZ.IndexCount === 12 && CutX.IndexCount === 12, "Expected 12 double-sided indices on cutaway quad");

const QuadCage = BuildQuadPatchCageBuffers(RefQuad);
const LoftCage = BuildLoftCurveCageBuffers(RefLoft);
const VoxCage  = BuildVoxelCageBuffers(Vox32);
AssertCondition(QuadCage.VertexCount > 4000, `Too few Quad-Patch cage vertices (${QuadCage.VertexCount})`);
AssertCondition(LoftCage.VertexCount > 4000, `Too few Loft spline cage vertices (${LoftCage.VertexCount})`);
AssertCondition(VoxCage.VertexCount > 2000, `Too few Voxel cage vertices (${VoxCage.VertexCount})`);
for (let I = 0; I < QuadCage.Vertices.length; ++I)
{
    AssertCondition(Number.isFinite(QuadCage.Vertices[I]), `Non-finite float in QuadCage at ${I}`);
}
AssertCondition(QuadCage.Vertices.length === QuadCage.VertexCount * CageLineVertexFloats, "QuadCage stride mismatch");
++ChecksPassed;
console.log(`✔️ [5/${TotalChecks}] Showroom & 4-Corner Quad-Patch cage buffers verified (${QuadCage.VertexCount} quad cage verts, ${LoftCage.VertexCount} cyl cage verts)`);

// ============================================================================
// Check 6: WGSL & GLSL Shader Static Derivative-Uniformity Audit
// ============================================================================
for (const WgslSrc of [ShowroomSurfaceWgsl, RayMarchIsoSurfaceWgsl, CageOverlayWgsl])
{
    AssertCondition(!/\btextureSample\s*\(/.test(WgslSrc), "Forbidden implicit-derivative textureSample() found in WGSL");
    const StorageHits = WgslSrc.match(/var<\s*storage/g) || [];
    AssertCondition(StorageHits.length <= 4, `Exceeded 4 storage slots (${StorageHits.length})`);
}
AssertCondition(ShowroomSurfaceWgsl.includes("EvalQuadPatchVehicleSdf") && RayMarchIsoSurfaceWgsl.includes("EvalQuadPatchVehicleSdf"), "Missing EvalQuadPatchVehicleSdf in WGSL");
AssertCondition(ShowroomSurfaceGlslFrag.includes("EvalQuadPatchVehicleSdf") && RayMarchIsoSurfaceGlslFrag.includes("EvalQuadPatchVehicleSdf"), "Missing EvalQuadPatchVehicleSdf in GLSL");
for (const GlslSrc of [ShowroomSurfaceGlslVert, ShowroomSurfaceGlslFrag, RayMarchIsoSurfaceGlslVert, RayMarchIsoSurfaceGlslFrag, CageOverlayGlslVert, CageOverlayGlslFrag])
{
    AssertCondition(GlslSrc.startsWith("#version 300 es"), "GLSL shader must start with #version 300 es");
    AssertCondition(!/\btexture\s*\(/.test(GlslSrc), "Forbidden implicit-derivative texture() found in GLSL; use texelFetch/textureLod");
}
++ChecksPassed;
console.log(`✔️ [6/${TotalChecks}] WGSL & GLSL ES 3.00 shaders passed derivative-uniformity & Quad-Patch parity audit`);

// ============================================================================
// Check 7: HTML <-> JS DOM Control Synchronization
// ============================================================================
const HtmlSrc = fs.readFileSync(path.join(DirHere, "index.html"), "utf8");
const WbSrc   = fs.readFileSync(path.join(DirHere, "LoftedVehicleSdfWorkbench.js"), "utf8");
const IdRegex = /getElementById\(\s*"([^"]+)"\s*\)/g;
let Match;
let MatchedIds = 0;
while ((Match = IdRegex.exec(WbSrc)) !== null)
{
    AssertCondition(HtmlSrc.includes(`id="${Match[1]}"`), `Missing DOM id="${Match[1]}" in index.html`);
    ++MatchedIds;
}
++ChecksPassed;
console.log(`✔️ [7/${TotalChecks}] HTML <-> JS DOM synchronization verified (${MatchedIds} element IDs matched)`);

// ============================================================================
// Check 7: Frontier Governance Scan (Banned Words, Bool Prefixes, Emoji Allowlist)
// ============================================================================
const TargetFiles = [
    "Experimental/LoftedVehicleSdfWorkbench/LoftedVehicleSdfCompiler.js",
    "Experimental/LoftedVehicleSdfWorkbench/VehicleShowroomBuilder.js",
    "Experimental/LoftedVehicleSdfWorkbench/LoftedVehicleShaders.js",
    "Experimental/LoftedVehicleSdfWorkbench/LoftedVehicleSdfWorkbench.js",
    "Experimental/LoftedVehicleSdfWorkbench/index.html",
    "Documentation/Plans/LoftedVehicleSdfArchitecturePlan.md"
];

const BannedVocabulary = [
    "action","adjacency","affinity","ancestor","and","archive","arena","artifact","artwork","assembly",
    "asset","backend","badge","base","binding","bloom","bridge","cache","cadence","card","carousel",
    "catalog","child","children","classification","commit","common","complex","compose","concurrency",
    "config","context","contract","controller","coordinator","core","coverage","dag","data","descendant",
    "designation","digest","dispatcher","domain","draw","element","emblem","emission","emit","emitter",
    "entity","evaluator","facet","family","fence","flag","floating","flow","frame","glide","glyph",
    "graph","handler","harness","helper","hierarchy","hook","horizon","hub","hybrid","info","inscription",
    "interpolant","item","kernel","kind","law","lineage","listener","manager","manifest","map","matrix",
    "member","menu","mesh","model","module","morph","mutation","nexus","node","object","occupant","orphan",
    "palette","parent","pass","peer","pill","pipeline","pool","popover","popup","predicate","probe",
    "processor","proof","provenance","provider","recipe","record","representation","reservoir","role",
    "script","service","shell","sibling","snapshot","stage","state","store","strata","studio","submission",
    "subsystem","system","table","thing","topology","traversal","tree","trial","tween","utility","vacancy",
    "value","worker","atlas","slice","substrate"
];

const AllowedEmojis = new Set([
    "🧩","📝","💡","⚠️","🔴","🐞","🐛","🚧","🔍","🚩","✔️","🟢",
    "🏳️","🏴","🏁","🎌","🏆","🥇","🥈","🥉","🏅","🎖️","⭐","🏷️"
]);

for (const Rel of TargetFiles)
{
    const Raw = fs.readFileSync(path.join(RepoRoot, Rel), "utf8");

    const EmojiMatches = Raw.match(/\p{Extended_Pictographic}(?:\uFE0F)?/gu) || [];
    for (const Em of EmojiMatches)
    {
        const WithVs = Em + "\uFE0F";
        AssertCondition(AllowedEmojis.has(Em) || AllowedEmojis.has(WithVs), `Disallowed emoji "${Em}" in ${Rel}`);
    }

    const Cleaned = Raw
        .replace(/<\/?script\b[^>]*>/gi, "")
        .replace(/\b(?:font-family|align-items|last-child|-apple-system|BlinkMacSystemFont)\b/g, "")
        .replace(/\bvalue="[^"]*"/g, "")
        .replace(/\bdata-[a-z0-9-]+="[^"]*"/g, "")
        .replace(/@binding\s*\(\s*\d+\s*\)/g, "")
        .replace(/\.(?:getContext|createImageData|putImageData|data|info|getCompilationInfo|getShaderInfoLog|getProgramInfoLog|createShaderModule|createBindGroupLayout|createPipelineLayout|createRenderPipeline|createBindGroup|beginRenderPass|setPipeline|drawIndexed|draw|bufferData|drawElements|drawArrays|uniformMatrix4fv|getElementById|addEventListener|value|getAttribute|pixelStorei)\b/g, "")
        .replace(/\b(?:GPUShaderStage|ELEMENT_ARRAY_BUFFER|STATIC_DRAW|DYNAMIC_DRAW|COMPILE_STATUS|LINK_STATUS|Object\.(?:freeze|entries|is|create)|requestAnimationFrame|contextmenu|binding|module|clearValue|depthClearValue|storeOp|depthStoreOp|topology)\b/g, "")
        .replace(/"store"/g, "")
        .replace(/\band\b/g, "");

    for (const Word of BannedVocabulary)
    {
        const Cap = Word[0].toUpperCase() + Word.slice(1);
        const Re = Word === "and"
            ? /(?:[a-z0-9]|^)(?:And)(?:[A-Z0-9]|$)/g
            : new RegExp(`\\b${Word}s?\\b|(?:[a-z0-9]|\\b)(?:${Cap})(?:[A-Z0-9s]|\\b)`, "g");
        const Hit = Re.exec(Cleaned);
        AssertCondition(!Hit, `Banned vocabulary "${Word}" matched "${Hit ? Hit[0] : ""}" in ${Rel}`);
    }

    if (!Rel.endsWith(".md") && !Rel.endsWith(".html"))
    {
        const BoolHit = Cleaned.match(/\b(?:Is|Has|Was|Can|Should|On)[A-Z][A-Za-z0-9]*/g);
        AssertCondition(!BoolHit, `Banned boolean prefix "${BoolHit ? BoolHit[0] : ""}" in ${Rel}`);
    }
}
++ChecksPassed;
console.log(`✔️ [8/${TotalChecks}] Governance scan passed across all ${TargetFiles.length} files (0 banned words, 0 bad emojis)`);
console.log(`\n🟢 ALL ${ChecksPassed}/${TotalChecks} LOFTED VEHICLE SDF WORKBENCH CHECKS PASSED!`);
