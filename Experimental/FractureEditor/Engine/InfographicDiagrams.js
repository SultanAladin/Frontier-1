/*==========================================================================================================================================
                                                     INFOGRAPHICDIAGRAMS.JS
==========================================================================================================================================*/
// 🧩 High-DPI interactive Canvas2D infographic diagrams styled after the four SourceImages/General UI references.

import {
    EvaluateDirectionalToughness,
    EvaluatePieceSdfSlice
} from "./ConvexPartitioning.js";
import {
    RayleighDensityAtKm,
    MieDensityAtKm,
    OzoneDensityAtKm,
    EvaluateTransmittanceToTop,
    EvaluateSkyRayRadiance,
    ToneMapReinhardSrgb,
    SampleGlobalCloudDensity,
    SampleLocalCloudDensity
} from "./CelestialAtmosphereModel.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                 CANVAS DPI PROVISIONING
//------------------------------------------------------------------------------------------------------------------------

function PrepareCanvas2D(Canvas, CssWidth, CssHeight)
{
    if (!Canvas) return null;
    const dpr = Math.min((typeof window !== "undefined" && window.devicePixelRatio) || 1, 2);
    const w = Math.max(120, Math.round(CssWidth || Canvas.clientWidth || 340));
    const h = Math.max(60, Math.round(CssHeight || Canvas.clientHeight || 130));
    if (Canvas.width !== w * dpr || Canvas.height !== h * dpr)
    {
        Canvas.width = w * dpr;
        Canvas.height = h * dpr;
    }
    const ctx = Canvas.getContext("2d");
    if (!ctx) return null;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, w, h);
    return { ctx, w, h };
}

//------------------------------------------------------------------------------------------------------------------------
//                                  1. GRIFFITH STRESS & WEIBULL TELEMETRY WAVEFORM
//------------------------------------------------------------------------------------------------------------------------

// 📝 Direct homage to "Operational Efficiency" waveform in 0bab27674eea6c04ccaf172f194ce6ef.jpg:
//    dashed target line, right-side % labels, white glowing super-critical peaks with vertical shaded columns, amber arrest dips.
export function DrawGriffithWaveformDiagram(Canvas, PartitionResult, Spec, AccentHex = "#EFE838")
{
    const Prep = PrepareCanvas2D(Canvas, Canvas?.clientWidth || 356, 134);
    if (!Prep) return;
    const { ctx, w, h } = Prep;

    const padL = 8, padR = 38, padT = 18, padB = 22;
    const plotW = w - padL - padR;
    const plotH = h - padT - padB;

    // Subtle horizontal gridlines + right-hand % scale
    ctx.font = "500 9.5px 'Inter', system-ui, sans-serif";
    ctx.textAlign = "right";
    const Ticks = [
        { frac: 0.0, label: "100%" },
        { frac: 0.33, label: "75%" },
        { frac: 0.66, label: "50%" },
        { frac: 1.0, label: "25%" }
    ];
    for (const t of Ticks)
    {
        const y = padT + t.frac * plotH;
        ctx.strokeStyle = "rgba(255,255,255,0.045)";
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(padL, y);
        ctx.lineTo(padL + plotW, y);
        ctx.stroke();
        ctx.fillStyle = "rgba(255,255,255,0.34)";
        ctx.fillText(t.label, w - 4, y + 3);
    }

    // Dashed Critical Griffith Branching Threshold (G = 2 Gc)
    const threshY = padT + 0.36 * plotH;
    ctx.save();
    ctx.setLineDash([5, 5]);
    ctx.strokeStyle = "rgba(255,255,255,0.28)";
    ctx.beginPath();
    ctx.moveTo(padL, threshY);
    ctx.lineTo(padL + plotW, threshY);
    ctx.stroke();
    ctx.restore();

    ctx.fillStyle = "rgba(255,255,255,0.44)";
    ctx.textAlign = "left";
    ctx.fillText("Branching > 2·Gc", padL + 2, threshY - 5);

    // Construct telemetry curve across 64 samples from actual fragments
    const Frags = PartitionResult?.Fragments || [];
    const SampleCount = 64;
    const Points = [];
    for (let i = 0; i < SampleCount; ++i)
    {
        const u = i / (SampleCount - 1);
        const fIdx = Math.min(Frags.length - 1, Math.floor(u * Math.max(1, Frags.length)));
        const frag = Frags[fIdx];
        const baseProximity = frag ? frag.Proximity : Math.exp(-u * 2.5);
        const ripple = 0.09 * Math.sin(i * 1.15 + (Spec.Seed % 17)) + 0.05 * Math.cos(i * 2.7);
        const val = Math.max(0.08, Math.min(0.97, 0.24 + baseProximity * 0.64 + ripple));
        const x = padL + u * plotW;
        const y = padT + (1.0 - val) * plotH;
        Points.push({ x, y, val });
    }

    // Shaded vertical bands under super-critical peaks (just like 0bab2767)
    for (let i = 0; i < Points.length - 1; ++i)
    {
        if (Points[i].y < threshY)
        {
            ctx.fillStyle = "rgba(255,255,255,0.055)";
            ctx.fillRect(Points[i].x, Points[i].y, Points[i + 1].x - Points[i].x + 0.5, padT + plotH - Points[i].y);
        }
    }

    // Base muted polyline
    ctx.lineWidth = 1.4;
    ctx.strokeStyle = "rgba(255,255,255,0.28)";
    ctx.beginPath();
    Points.forEach((p, idx) => (idx === 0 ? ctx.moveTo(p.x, p.y) : ctx.lineTo(p.x, p.y)));
    ctx.stroke();

    // Bright white peaks above threshold + warm amber arrest dips below 0.30
    for (let i = 0; i < Points.length - 1; ++i)
    {
        const p0 = Points[i], p1 = Points[i + 1];
        if (p0.y <= threshY || p1.y <= threshY)
        {
            ctx.strokeStyle = "#FFFFFF";
            ctx.lineWidth = 2.0;
            ctx.beginPath();
            ctx.moveTo(p0.x, p0.y);
            ctx.lineTo(p1.x, p1.y);
            ctx.stroke();
        }
        else if (p0.val < 0.31 && p1.val < 0.31)
        {
            ctx.strokeStyle = "#F59E0B";
            ctx.lineWidth = 1.8;
            ctx.beginPath();
            ctx.moveTo(p0.x, p0.y);
            ctx.lineTo(p1.x, p1.y);
            ctx.stroke();
        }
    }

    // Vertex markers on peak crossings and minimum arrest dip
    let PeakVertex = Points[0], DipVertex = Points[Points.length - 1];
    for (const p of Points)
    {
        if (p.y < PeakVertex.y) PeakVertex = p;
        if (p.y > DipVertex.y) DipVertex = p;
    }
    const DrawDot = (pt, fill) =>
    {
        ctx.fillStyle = fill;
        ctx.beginPath();
        ctx.arc(pt.x, pt.y, 3.1, 0, Math.PI * 2);
        ctx.fill();
    };
    DrawDot(PeakVertex, "#FFFFFF");
    DrawDot(DipVertex, "#F59E0B");
    ctx.fillStyle = AccentHex;
    ctx.beginPath();
    ctx.arc(Points[Math.floor(Points.length * 0.25)].x, Points[Math.floor(Points.length * 0.25)].y, 2.6, 0, Math.PI * 2);
    ctx.fill();

    // Bottom X-axis labels
    const XLabels = ["Impact", "0.25 R", "0.50 R", "0.75 R", "Far Edge"];
    ctx.fillStyle = "rgba(255,255,255,0.34)";
    ctx.textAlign = "center";
    XLabels.forEach((lbl, idx) =>
    {
        const lx = padL + (idx / (XLabels.length - 1)) * plotW;
        ctx.fillText(lbl, lx, h - 5);
    });
}

//------------------------------------------------------------------------------------------------------------------------
//                                  2. POLAR ANISOTROPY & HERTZIAN CONE RADAR
//------------------------------------------------------------------------------------------------------------------------

// 📝 Inspired by nested concentric radar rings in 13edfbc6a1110a5abd8ec9099429cf07.jpg.
export function DrawPolarAnisotropyRadar(Canvas, MaterialSpec, GrainAngleDeg = 0, AccentHex = "#EFE838")
{
    const Prep = PrepareCanvas2D(Canvas, Canvas?.clientWidth || 150, 128);
    if (!Prep) return;
    const { ctx, w, h } = Prep;

    const cx = w * 0.5, cy = h * 0.52;
    const maxR = Math.min(w, h) * 0.41;

    // Nested concentric & slightly eccentric technical rings
    for (let rFrac of [0.33, 0.66, 1.0])
    {
        ctx.strokeStyle = "rgba(255,255,255,0.09)";
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.arc(cx, cy, maxR * rFrac, 0, Math.PI * 2);
        ctx.stroke();
    }

    // Crosshairs
    ctx.strokeStyle = "rgba(255,255,255,0.07)";
    ctx.beginPath();
    ctx.moveTo(cx - maxR - 6, cy); ctx.lineTo(cx + maxR + 6, cy);
    ctx.moveTo(cx, cy - maxR - 6); ctx.lineTo(cx, cy + maxR + 6);
    ctx.stroke();

    // Hertzian cone wedge if present
    if (MaterialSpec.HertzConeRad > 0.05)
    {
        ctx.fillStyle = "rgba(239, 232, 56, 0.09)";
        ctx.beginPath();
        ctx.moveTo(cx, cy);
        ctx.arc(cx, cy, maxR * 0.92, -Math.PI * 0.5 - MaterialSpec.HertzConeRad * 0.5, -Math.PI * 0.5 + MaterialSpec.HertzConeRad * 0.5);
        ctx.closePath();
        ctx.fill();
    }

    // Directional Toughness Rose Gc(θ)
    const GrainRad = (GrainAngleDeg * Math.PI) / 180.0;
    const Samples = 72;
    const RawVals = [];
    let MaxG = 1e-3;
    for (let i = 0; i <= Samples; ++i)
    {
        const th = (i / Samples) * Math.PI * 2.0;
        const n = [Math.cos(th), 0, Math.sin(th)];
        const g = EvaluateDirectionalToughness(MaterialSpec, n, GrainRad, null);
        RawVals.push({ th, g });
        if (g > MaxG) MaxG = g;
    }

    ctx.beginPath();
    RawVals.forEach(({ th, g }, idx) =>
    {
        const r = (0.28 + 0.70 * (g / MaxG)) * maxR;
        const px = cx + Math.cos(th) * r;
        const py = cy + Math.sin(th) * r;
        if (idx === 0) ctx.moveTo(px, py);
        else ctx.lineTo(px, py);
    });
    ctx.closePath();
    ctx.fillStyle = "rgba(56, 178, 139, 0.16)";
    ctx.fill();
    ctx.strokeStyle = "#38B28B";
    ctx.lineWidth = 1.65;
    ctx.stroke();

    // Grain / Bedding Axis Indicator Line
    const gx = Math.cos(GrainRad) * maxR * 0.96;
    const gy = Math.sin(GrainRad) * maxR * 0.96;
    ctx.strokeStyle = AccentHex;
    ctx.lineWidth = 1.6;
    ctx.beginPath();
    ctx.moveTo(cx - gx, cy - gy);
    ctx.lineTo(cx + gx, cy + gy);
    ctx.stroke();

    ctx.fillStyle = AccentHex;
    ctx.beginPath();
    ctx.arc(cx + gx, cy + gy, 3.2, 0, Math.PI * 2);
    ctx.fill();
}

//------------------------------------------------------------------------------------------------------------------------
//                                  3. PIECE SDF ISO-CONTOUR SLICE & RESIDENCY MATRIX
//------------------------------------------------------------------------------------------------------------------------

export function DrawPieceSdfSliceCanvas(Canvas, Fragments, SliceY = 0.0, SdfResolution = 64, Enabled = true)
{
    const Prep = PrepareCanvas2D(Canvas, Canvas?.clientWidth || 160, 112);
    if (!Prep) return { OccupancyRatio: 0 };
    const { ctx, w, h } = Prep;

    const SampleRes = Math.min(56, SdfResolution);
    const Slice = EvaluatePieceSdfSlice(Fragments, SliceY, SampleRes, 1.2);
    const Offscreen = document.createElement("canvas");
    Offscreen.width = SampleRes;
    Offscreen.height = SampleRes;
    const octx = Offscreen.getContext("2d");
    if (octx)
    {
        const img = octx.createImageData(SampleRes, SampleRes);
        for (let i = 0; i < Slice.Field.length; ++i)
        {
            const sd = Slice.Field[i];
            const wave = 0.5 + 0.5 * Math.cos(sd * 48.0);
            const idx = i * 4;
            if (!Enabled)
            {
                const dim = sd <= 0 ? 48 : 18;
                img.data[idx] = dim; img.data[idx + 1] = dim + 4; img.data[idx + 2] = dim + 2; img.data[idx + 3] = 255;
            }
            else if (sd <= 0)
            {
                const core = Math.min(1, -sd * 4.2);
                img.data[idx] = Math.round(40 + 195 * core + 20 * wave);
                img.data[idx + 1] = Math.round(176 + 55 * core + 18 * wave);
                img.data[idx + 2] = Math.round(136 - 75 * core);
                img.data[idx + 3] = 255;
            }
            else
            {
                const ext = Math.exp(-sd * 3.2);
                img.data[idx] = Math.round(13 + 22 * wave * ext);
                img.data[idx + 1] = Math.round(18 + 58 * wave * ext);
                img.data[idx + 2] = Math.round(18 + 48 * wave * ext);
                img.data[idx + 3] = 255;
            }
        }
        octx.putImageData(img, 0, 0);
        ctx.imageSmoothingEnabled = true;
        ctx.drawImage(Offscreen, 0, 0, w, h);
    }

    // Subtle crosshair & resolution tag
    ctx.strokeStyle = "rgba(255,255,255,0.12)";
    ctx.lineWidth = 1;
    ctx.strokeRect(0.5, 0.5, w - 1, h - 1);
    ctx.fillStyle = "rgba(10,12,11,0.72)";
    ctx.fillRect(6, 6, 68, 17);
    ctx.fillStyle = Enabled ? "#4EF2BE" : "rgba(255,255,255,0.45)";
    ctx.font = "600 9.5px 'Inter', monospace";
    ctx.fillText(Enabled ? `${SdfResolution}³ R16F` : "SDF Idle", 11, 18);

    return Slice;
}

//------------------------------------------------------------------------------------------------------------------------
//                                  4. 24-HOUR SOLAR DIURNAL ARC & TWILIGHT CLOCK
//------------------------------------------------------------------------------------------------------------------------

export function DrawSolarDiurnalArcCanvas(Canvas, SunParams, SolarSolved, AccentHex = "#EFE838")
{
    const Prep = PrepareCanvas2D(Canvas, Canvas?.clientWidth || 356, 138);
    if (!Prep) return;
    const { ctx, w, h } = Prep;

    const padL = 10, padR = 34, padT = 16, padB = 22;
    const plotW = w - padL - padR;
    const plotH = h - padT - padB;
    const MapHourX = (hr) => padL + (hr / 24.0) * plotW;
    const MapElY = (el) => padT + ((90.0 - Math.max(-55, Math.min(90, el))) / 145.0) * plotH;

    const HorizonY = MapElY(0);
    const CivilY = MapElY(-6);

    // Civil twilight band
    ctx.fillStyle = "rgba(239, 232, 56, 0.045)";
    ctx.fillRect(padL, HorizonY, plotW, CivilY - HorizonY);

    // Horizon 0° solid hairline + Civil -6° dashed line
    ctx.strokeStyle = "rgba(255,255,255,0.20)";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(padL, HorizonY); ctx.lineTo(padL + plotW, HorizonY);
    ctx.stroke();

    ctx.save();
    ctx.setLineDash([4, 4]);
    ctx.strokeStyle = "rgba(255,255,255,0.10)";
    ctx.beginPath();
    ctx.moveTo(padL, CivilY); ctx.lineTo(padL + plotW, CivilY);
    ctx.stroke();
    ctx.restore();

    ctx.fillStyle = "rgba(255,255,255,0.36)";
    ctx.font = "500 9.5px 'Inter', sans-serif";
    ctx.textAlign = "right";
    ctx.fillText("+60°", w - 3, MapElY(60) + 3);
    ctx.fillText("0°", w - 3, HorizonY + 3);
    ctx.fillText("-30°", w - 3, MapElY(-30) + 3);

    // Fill area under daylight curve
    const Curve = SolarSolved.DiurnalCurve || [];
    if (Curve.length)
    {
        const grad = ctx.createLinearGradient(0, padT, 0, HorizonY);
        grad.addColorStop(0, "rgba(239, 232, 56, 0.22)");
        grad.addColorStop(1, "rgba(239, 232, 56, 0.0)");
        ctx.fillStyle = grad;
        ctx.beginPath();
        Curve.forEach((pt, idx) =>
        {
            const x = MapHourX(pt.Hour);
            const y = Math.min(HorizonY, MapElY(pt.ElevationDeg));
            if (idx === 0) ctx.moveTo(x, HorizonY);
            ctx.lineTo(x, y);
        });
        ctx.lineTo(MapHourX(24), HorizonY);
        ctx.closePath();
        ctx.fill();

        // Full 24h trajectory
        ctx.strokeStyle = "rgba(255,255,255,0.32)";
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        Curve.forEach((pt, idx) =>
        {
            const x = MapHourX(pt.Hour), y = MapElY(pt.ElevationDeg);
            if (idx === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
        });
        ctx.stroke();

        // Bright daylight arc segment
        ctx.strokeStyle = AccentHex;
        ctx.lineWidth = 2.1;
        ctx.beginPath();
        let penDown = false;
        for (const pt of Curve)
        {
            if (pt.ElevationDeg >= 0)
            {
                const x = MapHourX(pt.Hour), y = MapElY(pt.ElevationDeg);
                if (!penDown) { ctx.moveTo(x, y); penDown = true; }
                else ctx.lineTo(x, y);
            }
            else penDown = false;
        }
        ctx.stroke();
    }

    // Active LocalHours vertical cursor + Solar Orb
    const sunX = MapHourX(SunParams.LocalHours);
    const sunY = MapElY(SolarSolved.ElevationDeg);
    ctx.strokeStyle = "rgba(239, 232, 56, 0.45)";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(sunX, padT);
    ctx.lineTo(sunX, padT + plotH);
    ctx.stroke();

    const orbGlow = ctx.createRadialGradient(sunX, sunY, 1, sunX, sunY, 13);
    orbGlow.addColorStop(0, AccentHex);
    orbGlow.addColorStop(1, "rgba(239, 232, 56, 0)");
    ctx.fillStyle = orbGlow;
    ctx.beginPath();
    ctx.arc(sunX, sunY, 13, 0, Math.PI * 2);
    ctx.fill();

    ctx.fillStyle = "#FFFFFF";
    ctx.beginPath();
    ctx.arc(sunX, sunY, 4.0, 0, Math.PI * 2);
    ctx.fill();

    // Hour axis ticks
    ctx.fillStyle = "rgba(255,255,255,0.34)";
    ctx.textAlign = "center";
    for (const hr of [0, 6, 12, 18, 24])
    {
        ctx.fillText(`${String(hr).padStart(2, "0")}:00`, MapHourX(hr), h - 5);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                  5. OBSERVER WORLD DOT-MATRIX GRATICULE MAP
//------------------------------------------------------------------------------------------------------------------------

export function DrawObserverWorldMapCanvas(Canvas, LatitudeDeg, LongitudeDeg, AccentHex = "#EFE838")
{
    const Prep = PrepareCanvas2D(Canvas, Canvas?.clientWidth || 356, 102);
    if (!Prep) return;
    const { ctx, w, h } = Prep;

    ctx.fillStyle = "#0D0F0E";
    ctx.fillRect(0, 0, w, h);

    // Coarse continental silhouette approximations in dot-matrix coordinates
    const InContinent = (lon, lat) =>
    {
        // North America
        if (lon > -162 && lon < -55 && lat > 15 && lat < 70 && (lat - 15) * 0.9 > (-55 - lon) * -0.25) return lon < -75 || lat > 42;
        // South America
        if (lon > -82 && lon < -35 && lat > -54 && lat < 12 && Math.abs(lon + 58 + lat * 0.25) < 18 - Math.abs(lat + 15) * 0.25) return true;
        // Europe + Asia
        if (lon > -10 && lon < 145 && lat > 34 && lat < 72) return true;
        if (lon > 65 && lon < 122 && lat > 10 && lat <= 34) return true;
        // Africa
        if (lon > -17 && lon < 51 && lat > -35 && lat < 35 && Math.abs(lon - 18) < 28 - Math.max(0, -lat) * 0.45) return true;
        // Australia
        if (lon > 113 && lon < 153 && lat > -39 && lat < -12) return true;
        return false;
    };

    const cols = 58, rows = 22;
    for (let r = 0; r < rows; ++r)
    {
        const lat = 82 - (r / (rows - 1)) * 164;
        for (let c = 0; c < cols; ++c)
        {
            const lon = -175 + (c / (cols - 1)) * 350;
            const land = InContinent(lon, lat);
            const x = 8 + (c / (cols - 1)) * (w - 16);
            const y = 8 + (r / (rows - 1)) * (h - 16);
            ctx.fillStyle = land ? "rgba(255,255,255,0.28)" : "rgba(255,255,255,0.045)";
            ctx.beginPath();
            ctx.arc(x, y, land ? 1.45 : 0.9, 0, Math.PI * 2);
            ctx.fill();
        }
    }

    // Equator hairline
    ctx.strokeStyle = "rgba(255,255,255,0.08)";
    ctx.beginPath();
    ctx.moveTo(0, h * 0.5); ctx.lineTo(w, h * 0.5);
    ctx.stroke();

    // Observer Reticle
    const ox = ((LongitudeDeg + 180.0) / 360.0) * w;
    const oy = ((90.0 - LatitudeDeg) / 180.0) * h;

    ctx.strokeStyle = "rgba(239,232,56,0.35)";
    ctx.beginPath();
    ctx.moveTo(ox, 0); ctx.lineTo(ox, h);
    ctx.moveTo(0, oy); ctx.lineTo(w, oy);
    ctx.stroke();

    ctx.strokeStyle = AccentHex;
    ctx.lineWidth = 1.6;
    ctx.beginPath();
    ctx.arc(ox, oy, 6.5, 0, Math.PI * 2);
    ctx.stroke();

    ctx.fillStyle = AccentHex;
    ctx.beginPath();
    ctx.arc(ox, oy, 2.5, 0, Math.PI * 2);
    ctx.fill();
}

//------------------------------------------------------------------------------------------------------------------------
//                             6. ATMOSPHERE 256×512 LUT BAKE VIEWER & DENSITY CURVES
//------------------------------------------------------------------------------------------------------------------------

export function DrawAtmosphereLutCanvas(Canvas, AtmosParams, SunParams, SolarSolved)
{
    const Prep = PrepareCanvas2D(Canvas, Canvas?.clientWidth || 356, 110);
    if (!Prep) return;
    const { ctx, w, h } = Prep;

    const Mode = AtmosParams.PreviewMode || "altitude";
    const SunDir = AtmosParams.BakedSunDirection || SolarSolved.Direction;
    const bw = 120, bh = 56;
    const Off = document.createElement("canvas");
    Off.width = bw; Off.height = bh;
    const octx = Off.getContext("2d");

    if (octx)
    {
        const img = octx.createImageData(bw, bh);
        // Opposite-Sun azimuth for clean altitude profile per AtmosphereSkyNativePanel.md
        const oppLen = Math.hypot(SunDir[0], SunDir[2]) || 1;
        const oppX = -SunDir[0] / oppLen, oppZ = -SunDir[2] / oppLen;

        for (let py = 0; py < bh; ++py)
        {
            const v = py / (bh - 1); // 0 = top/zenith, 1 = bottom
            for (let px = 0; px < bw; ++px)
            {
                const u = px / (bw - 1);
                let rgb = [0, 0, 0];

                if (Mode === "altitude")
                {
                    const el = (1.0 - v) * (Math.PI * 0.5);
                    const dir = [oppX * Math.cos(el), Math.sin(el), oppZ * Math.cos(el)];
                    rgb = ToneMapReinhardSrgb(EvaluateSkyRayRadiance(AtmosParams, dir, SunDir, SunParams.TintRgb, SunParams.SkyGainMultiplier));
                }
                else if (Mode === "panorama")
                {
                    const az = u * Math.PI * 2.0;
                    const el = (1.0 - v) * (Math.PI * 0.5);
                    const dir = [Math.cos(el) * Math.sin(az), Math.sin(el), -Math.cos(el) * Math.cos(az)];
                    rgb = ToneMapReinhardSrgb(EvaluateSkyRayRadiance(AtmosParams, dir, SunDir, SunParams.TintRgb, SunParams.SkyGainMultiplier));
                }
                else if (Mode === "transmittance")
                {
                    const altKm = (1.0 - v) * AtmosParams.AtmosphereHeightKm;
                    const signed = u * 2.0 - 1.0;
                    const cosZen = Math.sign(signed) * Math.pow(Math.abs(signed), 1.0 / 0.35);
                    const T = EvaluateTransmittanceToTop(AtmosParams, altKm, cosZen, 10);
                    rgb = T.map((t) => Math.round(Math.pow(Math.max(0, Math.min(1, t)), 1 / 2.2) * 255));
                }
                else
                {
                    // Full sphere 360x180 equirectangular (black below horizon expected per spec)
                    const az = u * Math.PI * 2.0;
                    const el = (0.5 - v) * Math.PI;
                    if (el < 0)
                    {
                        rgb = [6, 7, 7];
                    }
                    else
                    {
                        const dir = [Math.cos(el) * Math.sin(az), Math.sin(el), -Math.cos(el) * Math.cos(az)];
                        rgb = ToneMapReinhardSrgb(EvaluateSkyRayRadiance(AtmosParams, dir, SunDir, SunParams.TintRgb, SunParams.SkyGainMultiplier));
                    }
                }

                const idx = (py * bw + px) * 4;
                img.data[idx] = rgb[0];
                img.data[idx + 1] = rgb[1];
                img.data[idx + 2] = rgb[2];
                img.data[idx + 3] = 255;
            }
        }
        octx.putImageData(img, 0, 0);
        ctx.imageSmoothingEnabled = true;
        ctx.drawImage(Off, 0, 0, w, h);
    }

    ctx.strokeStyle = "rgba(255,255,255,0.09)";
    ctx.strokeRect(0.5, 0.5, w - 1, h - 1);
}

export function DrawAtmosphereDensityCurvesCanvas(Canvas, AtmosParams, AccentHex = "#EFE838")
{
    const Prep = PrepareCanvas2D(Canvas, Canvas?.clientWidth || 356, 128);
    if (!Prep) return;
    const { ctx, w, h } = Prep;

    const padL = 32, padR = 92, padT = 14, padB = 20;
    const plotW = w - padL - padR;
    const plotH = h - padT - padB;
    const MaxAlt = AtmosParams.AtmosphereHeightKm;

    ctx.font = "500 9.5px 'Inter', sans-serif";
    ctx.fillStyle = "rgba(255,255,255,0.35)";
    ctx.textAlign = "right";
    for (const frac of [0, 0.5, 1.0])
    {
        const y = padT + (1.0 - frac) * plotH;
        ctx.strokeStyle = "rgba(255,255,255,0.05)";
        ctx.beginPath(); ctx.moveTo(padL, y); ctx.lineTo(padL + plotW, y); ctx.stroke();
        ctx.fillText(`${Math.round(frac * MaxAlt)}km`, padL - 5, y + 3);
    }

    const PlotProfile = (EvalFn, StrokeColor, FillRgba) =>
    {
        ctx.beginPath();
        for (let i = 0; i <= 48; ++i)
        {
            const frac = i / 48;
            const alt = frac * MaxAlt;
            const d = Math.min(1.25, EvalFn(alt));
            const x = padL + (d / 1.25) * plotW;
            const y = padT + (1.0 - frac) * plotH;
            if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
        }
        ctx.strokeStyle = StrokeColor;
        ctx.lineWidth = 1.9;
        ctx.stroke();

        ctx.lineTo(padL, padT);
        ctx.lineTo(padL, padT + plotH);
        ctx.closePath();
        ctx.fillStyle = FillRgba;
        ctx.fill();
    };

    PlotProfile((alt) => RayleighDensityAtKm(AtmosParams, alt) * Math.min(1.25, AtmosParams.RayleighMultiplier), "#60A5FA", "rgba(96,165,250,0.10)");
    PlotProfile((alt) => MieDensityAtKm(AtmosParams, alt) * Math.min(1.25, AtmosParams.MieMultiplier), "#F59E0B", "rgba(245,158,11,0.10)");
    PlotProfile((alt) => OzoneDensityAtKm(AtmosParams, alt) * Math.min(1.25, AtmosParams.OzoneMultiplier), "#38B28B", "rgba(56,178,139,0.12)");

    // Right inset: Henyey-Greenstein Phase Lobe Polar Diagram
    const pcx = w - 44, pcy = h * 0.50, pr = 31;
    ctx.strokeStyle = "rgba(255,255,255,0.09)";
    ctx.beginPath(); ctx.arc(pcx, pcy, pr, 0, Math.PI * 2); ctx.stroke();

    const g = Math.max(-0.85, Math.min(0.92, AtmosParams.MiePhaseG));
    const g2 = g * g;
    let maxP = 1e-4;
    const Lobe = [];
    for (let i = 0; i <= 60; ++i)
    {
        const th = (i / 60) * Math.PI * 2.0;
        const c = Math.cos(th);
        const p = (1.0 - g2) / Math.pow(1.0 + g2 - 2.0 * g * c, 1.25);
        Lobe.push({ th, p });
        if (p > maxP) maxP = p;
    }
    ctx.beginPath();
    Lobe.forEach(({ th, p }, idx) =>
    {
        const rr = (0.14 + 0.84 * Math.pow(p / maxP, 0.55)) * pr;
        const lx = pcx + Math.cos(th) * rr;
        const ly = pcy + Math.sin(th) * rr;
        if (idx === 0) ctx.moveTo(lx, ly); else ctx.lineTo(lx, ly);
    });
    ctx.closePath();
    ctx.fillStyle = "rgba(239, 232, 56, 0.14)";
    ctx.fill();
    ctx.strokeStyle = AccentHex;
    ctx.lineWidth = 1.5;
    ctx.stroke();

    ctx.fillStyle = "rgba(255,255,255,0.42)";
    ctx.textAlign = "center";
    ctx.fillText(`HG g=${AtmosParams.MiePhaseG.toFixed(2)}`, pcx, h - 5);
}

//------------------------------------------------------------------------------------------------------------------------
//                       7. CONTINUOUS CLOUD DENSITY MASK (1 - exp(-3·ρ)) & WORLD-Z SLAB
//------------------------------------------------------------------------------------------------------------------------

export function DrawCloudDensityMaskCanvas(Canvas, CloudsParams)
{
    const Prep = PrepareCanvas2D(Canvas, Canvas?.clientWidth || 356, 120);
    if (!Prep) return;
    const { ctx, w, h } = Prep;

    const Scope = CloudsParams.ActiveScope;
    const bw = 144, bh = 72;
    const Off = document.createElement("canvas");
    Off.width = bw; Off.height = bh;
    const octx = Off.getContext("2d");

    if (octx)
    {
        const img = octx.createImageData(bw, bh);
        const Aspect = bh / bw;
        for (let py = 0; py < bh; ++py)
        {
            const ny = (py / (bh - 1)) * 2.0 - 1.0;
            for (let px = 0; px < bw; ++px)
            {
                const nx = (px / (bw - 1)) * 2.0 - 1.0;
                let MeanRho = 0.0;

                if (Scope === "global")
                {
                    const G = CloudsParams.Global;
                    const Top = Math.min(G.CeilingHeightM, G.BaseHeightM + G.ThicknessM);
                    const WorldX = nx * 3000.0; // 6 km span per CloudsNativePanel.md
                    const WorldZ = ny * 3000.0 * Aspect;
                    if (G.Enabled && Top > G.BaseHeightM)
                    {
                        for (let s = 0; s < 8; ++s)
                        {
                            const alt = G.BaseHeightM + ((s + 0.5) / 8.0) * (Top - G.BaseHeightM);
                            MeanRho += SampleGlobalCloudDensity(G, WorldX, WorldZ, alt);
                        }
                        MeanRho /= 8.0;
                    }
                }
                else
                {
                    const L = CloudsParams.Local;
                    if (L.Enabled)
                    {
                        for (let s = 0; s < 8; ++s)
                        {
                            const vy = ((s + 0.5) / 8.0) * 2.0 - 1.0;
                            MeanRho += SampleLocalCloudDensity(L, nx, vy, ny);
                        }
                        MeanRho /= 8.0;
                    }
                }

                // Exact spec display mapping: 1 - exp(-3 * density) into blue-grey telemetry palette
                const Mapped = 1.0 - Math.exp(-3.0 * MeanRho);
                const idx = (py * bw + px) * 4;
                img.data[idx] = Math.round(14 + Mapped * 184);
                img.data[idx + 1] = Math.round(19 + Mapped * 204);
                img.data[idx + 2] = Math.round(23 + Mapped * 216);
                img.data[idx + 3] = 255;
            }
        }
        octx.putImageData(img, 0, 0);
        ctx.imageSmoothingEnabled = true;
        ctx.drawImage(Off, 0, 0, w, h);
    }

    // Technical corner framing & scale label
    ctx.strokeStyle = "rgba(255,255,255,0.10)";
    ctx.strokeRect(0.5, 0.5, w - 1, h - 1);
    ctx.fillStyle = "rgba(10,12,12,0.72)";
    ctx.fillRect(8, h - 22, Scope === "global" ? 118 : 138, 16);
    ctx.fillStyle = "rgba(255,255,255,0.72)";
    ctx.font = "500 9.5px 'Inter', monospace";
    ctx.fillText(
        Scope === "global" ? "6.0 km × 3.0 km · 8-Tap" : "Local Ellipsoid · 8-Tap",
        13,
        h - 11
    );
}

export function DrawCloudAltitudeSlabCanvas(Canvas, CloudsParams, AccentHex = "#EFE838")
{
    const Prep = PrepareCanvas2D(Canvas, Canvas?.clientWidth || 356, 124);
    if (!Prep) return;
    const { ctx, w, h } = Prep;

    const G = CloudsParams.Global;
    const MaxZ = 6000.0;
    const padL = 42, padR = 10, padT = 10, padB = 16;
    const plotW = w - padL - padR;
    const plotH = h - padT - padB;
    const MapZ = (z) => padT + (1.0 - Math.max(0, Math.min(MaxZ, z)) / MaxZ) * plotH;

    // Altitude gridlines (World Z)
    ctx.font = "500 9.5px 'Inter', sans-serif";
    ctx.textAlign = "right";
    for (const zVal of [0, 1500, 3000, 4500, 6000])
    {
        const y = MapZ(zVal);
        ctx.strokeStyle = "rgba(255,255,255,0.05)";
        ctx.beginPath(); ctx.moveTo(padL, y); ctx.lineTo(padL + plotW, y); ctx.stroke();
        ctx.fillStyle = "rgba(255,255,255,0.35)";
        ctx.fillText(`${(zVal / 1000).toFixed(1)}k`, padL - 6, y + 3);
    }

    const BaseY = MapZ(G.BaseHeightM);
    const AuthoredTopM = G.BaseHeightM + G.ThicknessM;
    const EffectiveTopM = Math.min(G.CeilingHeightM, AuthoredTopM);
    const TopY = MapZ(EffectiveTopM);
    const CeilingY = MapZ(G.CeilingHeightM);

    // X-Z Vertical Cross-Section Slice inside slab
    const SliceCols = 72, SliceRows = 24;
    if (EffectiveTopM > G.BaseHeightM && G.Enabled)
    {
        const cellW = plotW / SliceCols;
        const cellH = Math.max(1, (BaseY - TopY) / SliceRows);
        for (let r = 0; r < SliceRows; ++r)
        {
            const alt = G.BaseHeightM + ((r + 0.5) / SliceRows) * (EffectiveTopM - G.BaseHeightM);
            const py = BaseY - (r + 1) * cellH;
            for (let c = 0; c < SliceCols; ++c)
            {
                const wx = ((c / (SliceCols - 1)) * 2.0 - 1.0) * 3000.0;
                const rho = SampleGlobalCloudDensity(G, wx, 0.0, alt);
                const val = 1.0 - Math.exp(-3.0 * rho);
                if (val > 0.03)
                {
                    ctx.fillStyle = `rgba(186, 224, 238, ${(val * 0.85).toFixed(3)})`;
                    ctx.fillRect(padL + c * cellW, py, cellW + 0.6, cellH + 0.6);
                }
            }
        }
    }

    // Ceiling limit dashed line
    ctx.save();
    ctx.setLineDash([5, 4]);
    ctx.strokeStyle = "#EF4444";
    ctx.lineWidth = 1.2;
    ctx.beginPath();
    ctx.moveTo(padL, CeilingY);
    ctx.lineTo(padL + plotW, CeilingY);
    ctx.stroke();
    ctx.restore();

    // Cloud Base draggable bar
    ctx.strokeStyle = AccentHex;
    ctx.lineWidth = 2.0;
    ctx.beginPath();
    ctx.moveTo(padL, BaseY);
    ctx.lineTo(padL + plotW, BaseY);
    ctx.stroke();

    ctx.fillStyle = AccentHex;
    ctx.textAlign = "left";
    ctx.fillText(`Base ${Math.round(G.BaseHeightM)} m Z`, padL + 6, Math.min(h - 4, BaseY + 12));
    ctx.fillStyle = "#F87171";
    ctx.textAlign = "right";
    ctx.fillText(`Ceiling ${Math.round(G.CeilingHeightM)} m`, padL + plotW - 4, Math.max(11, CeilingY - 4));
}

//------------------------------------------------------------------------------------------------------------------------
//                          8. SUN DISC ANGULAR SUBTENSE & CHROMATIC CORONA DIAGRAM
//------------------------------------------------------------------------------------------------------------------------

export function DrawSunDiscPreviewCanvas(Canvas, SunParams)
{
    const Prep = PrepareCanvas2D(Canvas, Canvas?.clientWidth || 356, 84);
    if (!Prep) return;
    const { ctx, w, h } = Prep;

    const cx = w * 0.26, cy = h * 0.5;
    const RefRadius = 11.0; // Canonical 0.533 deg reference ring
    const LiveRadius = Math.max(3.0, Math.min(h * 0.42, (SunParams.AngularDiameterDeg / 0.533) * RefRadius));

    const [tr, tg, tb] = SunParams.TintRgb.map((v) => Math.max(0, Math.min(255, Math.round((v / 1.2) * 255))));
    const CoreRgb = `rgb(${Math.min(255, tr + 35)}, ${Math.min(255, tg + 30)}, ${Math.min(255, tb + 25)})`;

    const Glow = ctx.createRadialGradient(cx, cy, LiveRadius * 0.2, cx, cy, LiveRadius * 2.4);
    Glow.addColorStop(0, `rgba(${tr}, ${tg}, ${tb}, 0.55)`);
    Glow.addColorStop(1, `rgba(${tr}, ${tg}, ${tb}, 0.0)`);
    ctx.fillStyle = Glow;
    ctx.beginPath();
    ctx.arc(cx, cy, LiveRadius * 2.4, 0, Math.PI * 2);
    ctx.fill();

    ctx.fillStyle = CoreRgb;
    ctx.beginPath();
    ctx.arc(cx, cy, LiveRadius, 0, Math.PI * 2);
    ctx.fill();

    // Dashed 0.533° canonical solar disc ring
    ctx.save();
    ctx.setLineDash([3, 3]);
    ctx.strokeStyle = "rgba(255,255,255,0.45)";
    ctx.beginPath();
    ctx.arc(cx, cy, RefRadius, 0, Math.PI * 2);
    ctx.stroke();
    ctx.restore();

    // Right side chromatic spectral bars
    const bx = w * 0.52, bw = w * 0.42;
    const Channels = [
        { lbl: "R", val: SunParams.TintRgb[0], col: "#F87171", y: 22 },
        { lbl: "G", val: SunParams.TintRgb[1], col: "#4ADE80", y: 44 },
        { lbl: "B", val: SunParams.TintRgb[2], col: "#60A5FA", y: 66 }
    ];
    ctx.font = "500 9.5px 'Inter', sans-serif";
    for (const ch of Channels)
    {
        ctx.fillStyle = "rgba(255,255,255,0.45)";
        ctx.textAlign = "left";
        ctx.fillText(ch.lbl, bx, ch.y + 3);
        ctx.fillStyle = "rgba(255,255,255,0.07)";
        ctx.fillRect(bx + 14, ch.y - 3, bw - 18, 5);
        ctx.fillStyle = ch.col;
        ctx.fillRect(bx + 14, ch.y - 3, Math.min(1, ch.val / 1.4) * (bw - 18), 5);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                        9. LOCAL VOLUMETRIC CLOUD PROPORTIONAL BOUNDS & X-Z SLICE
//------------------------------------------------------------------------------------------------------------------------

export function DrawLocalCloudBoundsCanvas(Canvas, LocalSpec, AccentHex = "#38B28B")
{
    const Prep = PrepareCanvas2D(Canvas, Canvas?.clientWidth || 356, 114);
    if (!Prep) return;
    const { ctx, w, h } = Prep;

    // Left half: proportional isometric wireframe bounds; Right half: X-Y vertical density cross-section
    const IsoCx = w * 0.27, IsoCy = h * 0.54;
    const MaxExt = Math.max(0.5, LocalSpec.HalfExtentX, LocalSpec.HalfExtentY, LocalSpec.HalfExtentZ);
    const sx = (LocalSpec.HalfExtentX / MaxExt) * 34;
    const sy = (LocalSpec.HalfExtentY / MaxExt) * 28;
    const sz = (LocalSpec.HalfExtentZ / MaxExt) * 30;

    const ProjectIso = (x, y, z) => [
        IsoCx + (x - z) * 0.866,
        IsoCy - y + (x + z) * 0.45
    ];

    const Corners = [
        [-sx, -sy, -sz], [sx, -sy, -sz], [sx, sy, -sz], [-sx, sy, -sz],
        [-sx, -sy,  sz], [sx, -sy,  sz], [sx, sy,  sz], [-sx, sy,  sz]
    ].map(([x, y, z]) => ProjectIso(x, y, z));

    const Edges = [
        [0,1],[1,2],[2,3],[3,0],
        [4,5],[5,6],[6,7],[7,4],
        [0,4],[1,5],[2,6],[3,7]
    ];
    ctx.strokeStyle = AccentHex;
    ctx.lineWidth = 1.3;
    for (const [a, b] of Edges)
    {
        ctx.beginPath();
        ctx.moveTo(Corners[a][0], Corners[a][1]);
        ctx.lineTo(Corners[b][0], Corners[b][1]);
        ctx.stroke();
    }

    // Right half: actual X-Z vertical cross-section slice
    const rx = w * 0.53, ry = 12, rw = w * 0.43, rh = h - 28;
    const cols = 42, rows = 26;
    const cw = rw / cols, ch = rh / rows;
    for (let r = 0; r < rows; ++r)
    {
        const ny = 1.0 - (r / (rows - 1)) * 2.0;
        for (let c = 0; c < cols; ++c)
        {
            const nx = (c / (cols - 1)) * 2.0 - 1.0;
            const rho = SampleLocalCloudDensity(LocalSpec, nx, ny, 0.0);
            const val = 1.0 - Math.exp(-3.0 * rho);
            if (val > 0.02)
            {
                ctx.fillStyle = `rgba(78, 242, 190, ${(val * 0.88).toFixed(3)})`;
                ctx.fillRect(rx + c * cw, ry + r * ch, cw + 0.5, ch + 0.5);
            }
        }
    }
    ctx.strokeStyle = "rgba(255,255,255,0.12)";
    ctx.strokeRect(rx, ry, rw, rh);

    const BaseZ = LocalSpec.CentreY - LocalSpec.HalfExtentY;
    const TopZ = LocalSpec.CentreY + LocalSpec.HalfExtentY;
    ctx.fillStyle = "rgba(255,255,255,0.46)";
    ctx.font = "500 9.5px 'Inter', sans-serif";
    ctx.textAlign = "left";
    ctx.fillText(`Base ${BaseZ.toFixed(2)}m · Top ${TopZ.toFixed(2)}m Z`, 10, h - 5);
    ctx.textAlign = "right";
    ctx.fillText("X–Z Density Slice", rx + rw, h - 5);
}

//------------------------------------------------------------------------------------------------------------------------
//                  10. 3D NAVIER–CAUCHY PRINCIPAL STRESS & MOHR–COULOMB FAILURE SLICE
//------------------------------------------------------------------------------------------------------------------------

export function DrawEngineeringStressSliceCanvas(Canvas, EngReport, AccentHex = "#EFE838")
{
    const Prep = PrepareCanvas2D(Canvas, Canvas?.clientWidth || 356, 122);
    if (!Prep || !EngReport || !EngReport.StressSliceGrid) return;
    const { ctx, w, h } = Prep;

    const N = EngReport.LatticeGridN || 12;
    const Grid = EngReport.StressSliceGrid;

    // Left panel: 12x12 X-Y continuum Cauchy Von Mises / Principal Tension + Failure Mode map
    const mapSize = h - 24;
    const mx = 10, my = 8;
    const cellW = mapSize / N, cellH = mapSize / N;
    const maxVM = Math.max(1.0, EngReport.PeakVonMisesMPa);

    for (let iy = 0; iy < N; ++iy)
    {
        const drawRow = N - 1 - iy;
        for (let ix = 0; ix < N; ++ix)
        {
            const base = (iy * N + ix) * 3;
            const mode = Grid[base + 2];
            if (mode < 0)
            {
                ctx.fillStyle = "rgba(255,255,255,0.025)";
                ctx.fillRect(mx + ix * cellW, my + drawRow * cellH, cellW - 0.8, cellH - 0.8);
                continue;
            }
            const vmNorm = Math.min(1.0, Math.pow(Math.max(0, Grid[base + 1]) / maxVM, 0.55));
            if (mode > 1.5)
            {
                // Mode-II Mohr-Coulomb Compressive Shear zone (amber-crimson)
                const r = Math.round(90 + 165 * vmNorm);
                const g = Math.round(45 + 95 * vmNorm);
                const b = Math.round(28 + 20 * (1 - vmNorm));
                ctx.fillStyle = `rgb(${r},${g},${b})`;
            }
            else
            {
                // Mode-I Rankine / LEFM Tensile Opening zone (emerald-gold)
                const r = Math.round(24 + 215 * vmNorm);
                const g = Math.round(58 + 174 * vmNorm);
                const b = Math.round(62 * (1 - vmNorm * 0.6));
                ctx.fillStyle = `rgb(${r},${g},${b})`;
            }
            ctx.fillRect(mx + ix * cellW, my + drawRow * cellH, cellW - 0.8, cellH - 0.8);
        }
    }
    ctx.strokeStyle = "rgba(255,255,255,0.14)";
    ctx.strokeRect(mx, my, mapSize, mapSize);

    // Right panel: Mohr-Coulomb failure envelope + constitutive engineering metrics
    const rx = mx + mapSize + 14;
    const rw = w - rx - 10;
    const ey = my + 66, eh = 48;

    // Axes for (sigma_n, tau) Mohr-Coulomb envelope
    ctx.strokeStyle = "rgba(255,255,255,0.18)";
    ctx.beginPath();
    ctx.moveTo(rx, ey); ctx.lineTo(rx + rw, ey);
    ctx.moveTo(rx + rw * 0.28, ey + 6); ctx.lineTo(rx + rw * 0.28, ey - eh);
    ctx.stroke();

    // Mohr-Coulomb shear yield line tau = c + (-sigma_n) * tan(phi)
    const phiRad = (EngReport.InternalFrictionDeg * Math.PI) / 180.0;
    const slope = Math.min(1.15, Math.tan(phiRad) * 0.72);
    ctx.strokeStyle = AccentHex;
    ctx.lineWidth = 1.6;
    ctx.beginPath();
    ctx.moveTo(rx + rw * 0.12, ey - 8);
    ctx.lineTo(rx + rw * 0.94, Math.max(my + 6, ey - 12 - rw * 0.65 * slope));
    ctx.stroke();

    // Peak Mohr stress circle
    const cRad = Math.min(20, Math.max(8, eh * 0.36));
    ctx.strokeStyle = "rgba(248, 113, 113, 0.88)";
    ctx.lineWidth = 1.2;
    ctx.beginPath();
    ctx.arc(rx + rw * 0.58, ey, cRad, Math.PI, 0, false);
    ctx.stroke();

    ctx.fillStyle = "rgba(255,255,255,0.82)";
    ctx.font = "600 9.5px 'Inter', sans-serif";
    ctx.textAlign = "left";
    ctx.fillText(`KI / KIc: ${EngReport.StressIntensityKI.toFixed(1)} / ${EngReport.FractureToughnessKIc.toFixed(2)} MPa√m`, rx, my + 10);
    ctx.fillStyle = "rgba(255,255,255,0.56)";
    ctx.font = "500 9px 'Inter', sans-serif";
    ctx.fillText(`Hertz p0 ${EngReport.PeakContactPressureMPa.toFixed(0)} MPa · φ=${EngReport.InternalFrictionDeg}°`, rx, my + 23);
    ctx.fillText(`Hillerborg l_ch ${EngReport.HillerborgLengthMm.toFixed(1)} mm · Rq ${EngReport.RoughnessRmsMm.toFixed(1)} mm`, rx, my + 35);
    ctx.fillText(`Mode-I: ${EngReport.ModeITensileCuts} · Shear: ${EngReport.ModeIIShearCuts} · T-Junc: ${EngReport.WakeShieldedTJunctions}`, rx, my + 47);

    ctx.fillStyle = "rgba(255,255,255,0.44)";
    ctx.fillText("Cauchy σ_vM + Mode I/II", mx, h - 4);
    ctx.textAlign = "right";
    ctx.fillText("Mohr–Coulomb τ(σ_n)", w - 10, h - 4);
}
