/*============================================================================================================================================
                                                          GARMENTPRESETS.JS
============================================================================================================================================*/
// 6 Parametric Couture & Apparel Pattern Blocks (SI Millimetres / Metres, Live Mannequin-Scaled).
// Includes Core 4 tailoring silhouettes (Couture Dress, Gathered Skirt, Tailored Tee, 45 deg Bias Silk Slip)
// plus 2 multi-layer & self-collision showcases (Layered Jacket + Tank, Draped Sheath + Knotted Wrap Collar).

export const GARMENT_PRESET_CATALOG = {
    coutureDress: {
        key: 'coutureDress',
        name: 'Atelier Fitted Bodice & Flared A-Line Dress',
        shortLabel: 'Couture Dress',
        category: 'Woven Tailoring',
        defaultTextile: 'cottonPoplin',
        defaultSwatch: '#c4513d',
        description: '4-panel couture dress with shaped necklines, tailored armholes, waist seam, and flared A-line skirt.',
    },
    gatheredSkirt: {
        key: 'gatheredSkirt',
        name: 'Gathered Pleated Midi Skirt & Waistband',
        shortLabel: 'Gathered Skirt',
        category: 'M:N Fullness',
        defaultTextile: 'duchesseSatin',
        defaultSwatch: '#2f5d8c',
        description: '4-panel high-waist midi skirt with true 1.38:1 M:N gather sewing into a negative-ease waistband.',
    },
    tailoredTee: {
        key: 'tailoredTee',
        name: 'Tailored Crew-Neck T-Shirt (Set-In Sleeves)',
        shortLabel: 'Tailored Tee',
        category: 'Knit Block',
        defaultTextile: 'cottonJersey',
        defaultSwatch: '#3b7a62',
        description: '4-panel knit crew-neck tee with front/back blocks and two cylindrical set-in sleeves sewn around armholes.',
    },
    biasSilkTunic: {
        key: 'biasSilkTunic',
        name: '45° Bias-Cut Mulberry Silk Slip Dress',
        shortLabel: '45° Bias Silk Slip',
        category: 'Bias Couture',
        defaultTextile: 'mulberrySilk',
        defaultSwatch: '#a23b5c',
        description: '2-panel cowl-drape slip dress cut at 45° bias grainline highlighting anisotropic shear trellising.',
    },
    layeredOvershirt: {
        key: 'layeredOvershirt',
        name: 'Layered Utility Jacket over Fitted Tank',
        shortLabel: 'Layered Jacket + Tank',
        category: 'Multi-Layer Contact',
        defaultTextile: 'rawDenim',
        defaultSwatch: '#2f5d8c',
        description: '6-panel two-layer ensemble: snug fitted under-tank (layer 0) beneath a structured tailored jacket & collar (layer 1).',
    },
    knottedWrapScarf: {
        key: 'knottedWrapScarf',
        name: 'Draped Tunic & Cross-Over Wrap Collar',
        shortLabel: 'Wrap Collar + Dress',
        category: 'FPB Wrap & Fold',
        defaultTextile: 'merinoWool',
        defaultSwatch: '#b58d63',
        description: '4-panel draped tunic topped with a crossed wrap-collar stole colliding over bust and shoulders (FPB showcase).',
    },
};

function lerpEdge(ax, ay, bx, by, samples = 4)
{
    const pts = [];
    for (let i = 0; i <= samples; ++i)
    {
        const t = i / samples;
        pts.push([ax + (bx - ax) * t, ay + (by - ay) * t]);
    }
    return pts;
}

function quadBezierEdge(ax, ay, cx, cy, bx, by, samples = 10)
{
    const pts = [];
    for (let i = 0; i <= samples; ++i)
    {
        const t = i / samples;
        const omt = 1.0 - t;
        pts.push([
            omt * omt * ax + 2.0 * omt * t * cx + t * t * bx,
            omt * omt * ay + 2.0 * omt * t * cy + t * t * by,
        ]);
    }
    return pts;
}

function polyLen(pts)
{
    let acc = 0.0;
    for (let i = 0; i < pts.length - 1; ++i)
        acc += Math.hypot(pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1]);
    return Math.max(1e-8, acc);
}

function makeTorsoCylinderArrange(centerY, zAxis, minRadius, sideSign, maxHalfWidth = 0.24)
{
    const radius = Math.max(minRadius, maxHalfWidth / 1.28);
    return (_u, _v, lx, ly) =>
    {
        const theta = lx / radius;
        const zApex = sideSign > 0 ? (zAxis + minRadius) : (zAxis - minRadius);
        if (sideSign > 0)
            return [Math.sin(theta) * radius, centerY + ly, zApex - (1.0 - Math.cos(theta)) * radius];
        return [-Math.sin(theta) * radius, centerY + ly, zApex + (1.0 - Math.cos(theta)) * radius];
    };
}

function buildBodicePiece({
    id, name, role, widthBust, widthWaist, shoulderSpan, neckWidth, neckDrop,
    shoulderSlope, armholeDrop, bodiceHeight, grainlineAngleDeg = 0, layerOrder = 0,
    sheetOffset = [0, 0], arrange3D, spacingScale = 1.0,
})
{
    const halfBust = 0.5 * widthBust;
    const halfWaist = 0.5 * widthWaist;
    const halfNeck = 0.5 * neckWidth;
    const shX = Math.min(halfBust - 0.012, 0.5 * shoulderSpan);

    const yTop = 0.5 * bodiceHeight;
    const ySh = yTop - shoulderSlope;
    const yArm = yTop - armholeDrop;
    const yBot = -0.5 * bodiceHeight;

    const segShL = lerpEdge(-shX, ySh, -halfNeck, yTop, 5);
    const segNeck = quadBezierEdge(-halfNeck, yTop, 0.0, yTop - neckDrop * 1.5, halfNeck, yTop, 10);
    const segShR = lerpEdge(halfNeck, yTop, shX, ySh, 5);
    const topCurve = [...segShL, ...segNeck.slice(1), ...segShR.slice(1)];

    const lenShL = polyLen(segShL);
    const lenNeck = polyLen(segNeck);
    const lenShR = polyLen(segShR);
    const lenTop = lenShL + lenNeck + lenShR;

    const segSideL = lerpEdge(-halfWaist, yBot, -halfBust, yArm, 8);
    const segArmL = quadBezierEdge(-halfBust, yArm, -shX + 0.018, 0.5 * (yArm + ySh) - 0.012, -shX, ySh, 8);
    const leftCurve = [...segSideL, ...segArmL.slice(1)];
    const tSideL = polyLen(segSideL) / (polyLen(segSideL) + polyLen(segArmL));

    const segSideR = lerpEdge(halfWaist, yBot, halfBust, yArm, 8);
    const segArmR = quadBezierEdge(halfBust, yArm, shX - 0.018, 0.5 * (yArm + ySh) - 0.012, shX, ySh, 8);
    const rightCurve = [...segSideR, ...segArmR.slice(1)];
    const tSideR = polyLen(segSideR) / (polyLen(segSideR) + polyLen(segArmR));

    const bottomCurve = lerpEdge(-halfWaist, yBot, halfWaist, yBot, 14);

    return {
        id,
        name,
        role,
        layerOrder,
        grainlineAngleDeg,
        sheetOffset,
        spacingScale,
        seamAllowanceMm: 12,
        bottomCurve,
        topCurve,
        leftCurve,
        rightCurve,
        arrange3D,
        subEdges: {
            shoulderNegX: { side: 'top', t0: 0.0, t1: lenShL / lenTop },
            neckline:     { side: 'top', t0: lenShL / lenTop, t1: (lenShL + lenNeck) / lenTop },
            shoulderPosX: { side: 'top', t0: (lenShL + lenNeck) / lenTop, t1: 1.0 },
            sideNegX:     { side: 'left',  t0: 0.0,    t1: tSideL },
            armholeNegX:  { side: 'left',  t0: tSideL, t1: 1.0 },
            sidePosX:     { side: 'right', t0: 0.0,    t1: tSideR },
            armholePosX:  { side: 'right', t0: tSideR, t1: 1.0 },
        },
        notches: [
            { side: 'top', t: 0.5, label: 'CF/CB' },
            { side: 'left', t: tSideL, label: 'Underarm' },
            { side: 'right', t: tSideR, label: 'Underarm' },
        ],
    };
}

function buildSkirtPiece({
    id, name, role, topWidth, bottomWidth, length, waistDip = 0.008, hemArch = 0.022,
    grainlineAngleDeg = 0, layerOrder = 0, sheetOffset = [0, 0], arrange3D,
})
{
    const wt = 0.5 * topWidth;
    const wb = 0.5 * bottomWidth;
    const yTop = 0.5 * length;
    const yBot = -0.5 * length;

    return {
        id,
        name,
        role,
        layerOrder,
        grainlineAngleDeg,
        sheetOffset,
        seamAllowanceMm: 15,
        topCurve: quadBezierEdge(-wt, yTop, 0.0, yTop - waistDip, wt, yTop, 16),
        bottomCurve: quadBezierEdge(-wb, yBot, 0.0, yBot - hemArch, wb, yBot, 16),
        leftCurve: lerpEdge(-wb, yBot, -wt, yTop, 12),
        rightCurve: lerpEdge(wb, yBot, wt, yTop, 12),
        arrange3D,
        notches: [{ side: 'top', t: 0.5, label: 'Waist Center' }],
    };
}

function makeSleevePiece(id, name, armSide, mannequin, { capWidth, cuffWidth, length, grainlineAngleDeg, sheetOffset, layerOrder = 1, radius = 0.068 })
{
    const arm = armSide > 0 ? mannequin.armAxes.right : mannequin.armAxes.left;
    let ax = arm.elbow[0] - arm.shoulder[0];
    let ay = arm.elbow[1] - arm.shoulder[1];
    let az = arm.elbow[2] - arm.shoulder[2];
    const aLen = Math.hypot(ax, ay, az) || 1.0;
    ax /= aLen; ay /= aLen; az /= aLen;
    let ux = -ax * ay, uy = 1.0 - ay * ay, uz = -az * ay;
    const uLen = Math.hypot(ux, uy, uz) || 1.0;
    ux /= uLen; uy /= uLen; uz /= uLen;
    const zx = ay * uz - az * uy;
    const zy = az * ux - ax * uz;
    const zz = ax * uy - ay * ux;

    const midX = arm.shoulder[0] + ax * (0.095 + length * 0.14);
    const midY = arm.shoulder[1] + ay * (0.095 + length * 0.14);
    const midZ = arm.shoulder[2] + az * (0.095 + length * 0.14);

    const wc = 0.5 * capWidth;
    const wf = 0.5 * cuffWidth;
    const yt = 0.5 * length;
    const yb = -0.5 * length;

    return {
        id,
        name,
        role: 'Set-In Sleeve',
        layerOrder,
        grainlineAngleDeg,
        sheetOffset,
        seamAllowanceMm: 10,
        outwardAxis: { p0: arm.shoulder, p1: arm.elbow },
        topCurve: quadBezierEdge(-wc, yt - 0.020, 0.0, yt + 0.028, wc, yt - 0.020, 12),
        bottomCurve: lerpEdge(-wf, yb, wf, yb, 12),
        leftCurve: lerpEdge(-wf, yb, -wc, yt - 0.020, 7),
        rightCurve: lerpEdge(wf, yb, wc, yt - 0.020, 7),
        subEdges: {
            capHalfA: { side: 'top', t0: 0.0, t1: 0.5 },
            capHalfB: { side: 'top', t0: 0.5, t1: 1.0 },
        },
        arrange3D: (_u, _v, lx, ly) =>
        {
            const along = -ly;
            const phi = lx / radius;
            const c = Math.cos(phi) * radius;
            const s = Math.sin(phi) * radius * armSide;
            return [
                midX + ax * along + ux * c + zx * s,
                midY + ay * along + uy * c + zy * s,
                midZ + az * along + uz * c + zz * s,
            ];
        },
    };
}

export function buildGarmentPresetSpec(presetKey, mannequin, overrides = {})
{
    const m = mannequin.measurements;
    const h = mannequin.bounds.maxY - mannequin.bounds.minY;

    const yShoulderRidge = mannequin.bounds.minY + h * 0.838;
    const yWaist = mannequin.bounds.minY + h * 0.620;
    const zSpine = 0.042;

    const bustCirc = (m.bust || 84) * 0.01;
    const waistCirc = (m.waist || 69) * 0.01;
    const hipCirc = (m.hips || 96) * 0.01;
    const shoulderSpan = Math.min(0.350, (m.shoulderWidth || 41) * 0.01 * 0.83);

    const easeScale = overrides.easeScale ?? 1.0;
    const lengthScale = overrides.lengthScale ?? 1.0;
    const flareScale = overrides.flareScale ?? 1.0;
    const grainOffsetDeg = overrides.grainOffsetDeg ?? 0.0;

    if (presetKey === 'gatheredSkirt')
    {
        const bandWidth = (0.5 * waistCirc - 0.019) * easeScale;
        const bandHeight = 0.060;
        const gatherRatio = 1.38 * flareScale;
        const skirtTopWidth = bandWidth * gatherRatio;
        const skirtHemWidth = Math.min(0.90, Math.max(skirtTopWidth * 1.28, (0.5 * hipCirc + 0.24) * flareScale));
        const skirtLength = 0.55 * lengthScale;

        const zWaist = 0.052;
        const bandCenterY = yWaist + 0.032;
        const skirtCenterY = bandCenterY - 0.5 * bandHeight - 0.5 * skirtLength - 0.004;

        const bandFront = buildSkirtPiece({
            id: 'bandFront', name: 'Waistband Front', role: 'Fitted Waistband',
            topWidth: bandWidth, bottomWidth: bandWidth * 1.045, length: bandHeight,
            waistDip: -0.012, hemArch: -0.008, grainlineAngleDeg: 90 + grainOffsetDeg, layerOrder: 0,
            sheetOffset: [-0.38, 0.36],
            arrange3D: makeTorsoCylinderArrange(bandCenterY, zWaist, 0.118, +1, 0.5 * bandWidth),
        });
        bandFront.pinFilter = (u, v) => v > 0.90 && Math.abs(u - 0.5) < 0.08;

        const bandBack = buildSkirtPiece({
            id: 'bandBack', name: 'Waistband Back', role: 'Fitted Waistband',
            topWidth: bandWidth, bottomWidth: bandWidth * 1.045, length: bandHeight,
            waistDip: -0.006, hemArch: -0.004, grainlineAngleDeg: 90 + grainOffsetDeg, layerOrder: 0,
            sheetOffset: [0.38, 0.36],
            arrange3D: makeTorsoCylinderArrange(bandCenterY, zWaist, 0.118, -1, 0.5 * bandWidth),
        });
        bandBack.pinFilter = (u, v) => v > 0.90 && Math.abs(u - 0.5) < 0.08;

        const skirtFront = buildSkirtPiece({
            id: 'skirtFront', name: 'Gathered Skirt Front (1.38x)', role: 'Gathered Skirt Panel',
            topWidth: skirtTopWidth, bottomWidth: skirtHemWidth, length: skirtLength,
            waistDip: 0.008, hemArch: 0.026, grainlineAngleDeg: grainOffsetDeg, layerOrder: 1,
            sheetOffset: [-0.42, -0.06],
            arrange3D: makeTorsoCylinderArrange(skirtCenterY, zSpine, 0.192, +1, 0.5 * skirtHemWidth),
        });
        const skirtBack = buildSkirtPiece({
            id: 'skirtBack', name: 'Gathered Skirt Back (1.38x)', role: 'Gathered Skirt Panel',
            topWidth: skirtTopWidth, bottomWidth: skirtHemWidth, length: skirtLength,
            waistDip: 0.008, hemArch: 0.026, grainlineAngleDeg: grainOffsetDeg, layerOrder: 1,
            sheetOffset: [0.42, -0.06],
            arrange3D: makeTorsoCylinderArrange(skirtCenterY, zSpine, 0.192, -1, 0.5 * skirtHemWidth),
        });

        return {
            ...GARMENT_PRESET_CATALOG.gatheredSkirt,
            pieces: [bandFront, bandBack, skirtFront, skirtBack],
            seams: [
                { id: 'bandSidePosX',  name: 'Waistband Left Side',      pieceA: 'bandFront',  edgeA: 'right',  pieceB: 'bandBack',  edgeB: 'left' },
                { id: 'bandSideNegX',  name: 'Waistband Right Side',     pieceA: 'bandFront',  edgeA: 'left',   pieceB: 'bandBack',  edgeB: 'right' },
                { id: 'gatherFront',   name: 'Front Gather Waist (1.38:1)', pieceA: 'skirtFront', edgeA: 'top',  pieceB: 'bandFront', edgeB: 'bottom' },
                { id: 'gatherBack',    name: 'Back Gather Waist (1.38:1)',  pieceA: 'skirtBack',  edgeA: 'top',  pieceB: 'bandBack',  edgeB: 'bottom' },
                { id: 'skirtSidePosX', name: 'Skirt Left Side Seam',     pieceA: 'skirtFront', edgeA: 'right',  pieceB: 'skirtBack', edgeB: 'left' },
                { id: 'skirtSideNegX', name: 'Skirt Right Side Seam',    pieceA: 'skirtFront', edgeA: 'left',   pieceB: 'skirtBack', edgeB: 'right' },
            ],
        };
    }

    if (presetKey === 'tailoredTee')
    {
        const bodyWidth = (0.5 * bustCirc + 0.068) * easeScale;
        const hemWidth = (0.5 * hipCirc + 0.055) * easeScale;
        const teeLen = 0.50 * lengthScale;
        const topY = yShoulderRidge + 0.015;
        const centerY = topY - 0.5 * teeLen;

        const teeFront = buildBodicePiece({
            id: 'teeFront', name: 'Tee Front Block', role: 'Front Knit Block',
            widthBust: bodyWidth, widthWaist: hemWidth, shoulderSpan: shoulderSpan + 0.020,
            neckWidth: 0.148, neckDrop: 0.060, shoulderSlope: 0.025, armholeDrop: 0.215,
            bodiceHeight: teeLen, grainlineAngleDeg: grainOffsetDeg, layerOrder: 0,
            sheetOffset: [-0.26, 0.08],
            arrange3D: makeTorsoCylinderArrange(centerY, zSpine, 0.162, +1),
        });
        const teeBack = buildBodicePiece({
            id: 'teeBack', name: 'Tee Back Block', role: 'Back Knit Block',
            widthBust: bodyWidth, widthWaist: hemWidth, shoulderSpan: shoulderSpan + 0.020,
            neckWidth: 0.148, neckDrop: 0.022, shoulderSlope: 0.022, armholeDrop: 0.215,
            bodiceHeight: teeLen, grainlineAngleDeg: grainOffsetDeg, layerOrder: 0,
            sheetOffset: [0.26, 0.08],
            arrange3D: makeTorsoCylinderArrange(centerY, zSpine, 0.162, -1),
        });

        const sleevePosX = makeSleevePiece('sleevePosX', 'Left Set-In Sleeve (+X)', +1, mannequin, {
            capWidth: 0.34 * easeScale, cuffWidth: 0.27 * easeScale, length: 0.17 * lengthScale,
            grainlineAngleDeg: grainOffsetDeg, sheetOffset: [-0.26, -0.34],
        });
        const sleeveNegX = makeSleevePiece('sleeveNegX', 'Right Set-In Sleeve (-X)', -1, mannequin, {
            capWidth: 0.34 * easeScale, cuffWidth: 0.27 * easeScale, length: 0.17 * lengthScale,
            grainlineAngleDeg: grainOffsetDeg, sheetOffset: [0.26, -0.34],
        });

        return {
            ...GARMENT_PRESET_CATALOG.tailoredTee,
            pieces: [teeFront, teeBack, sleevePosX, sleeveNegX],
            seams: [
                { id: 'shPosX',    name: 'Left Shoulder Seam',   pieceA: 'teeFront',   edgeA: 'shoulderPosX', pieceB: 'teeBack',  edgeB: 'shoulderNegX', reverseB: true },
                { id: 'shNegX',    name: 'Right Shoulder Seam',  pieceA: 'teeFront',   edgeA: 'shoulderNegX', pieceB: 'teeBack',  edgeB: 'shoulderPosX', reverseB: true },
                { id: 'sidePosX',  name: 'Left Side Seam',       pieceA: 'teeFront',   edgeA: 'sidePosX',     pieceB: 'teeBack',  edgeB: 'sideNegX' },
                { id: 'sideNegX',  name: 'Right Side Seam',      pieceA: 'teeFront',   edgeA: 'sideNegX',     pieceB: 'teeBack',  edgeB: 'sidePosX' },
                { id: 'capPosXF',  name: 'Left Armhole Front',   pieceA: 'sleevePosX', edgeA: 'capHalfB',     pieceB: 'teeFront', edgeB: 'armholePosX', reverseB: true },
                { id: 'capPosXB',  name: 'Left Armhole Back',    pieceA: 'sleevePosX', edgeA: 'capHalfA',     pieceB: 'teeBack',  edgeB: 'armholeNegX' },
                { id: 'underPosX', name: 'Left Underarm Seam',   pieceA: 'sleevePosX', edgeA: 'left',         pieceB: 'sleevePosX', edgeB: 'right' },
                { id: 'capNegXF',  name: 'Right Armhole Front',  pieceA: 'sleeveNegX', edgeA: 'capHalfB',     pieceB: 'teeFront', edgeB: 'armholeNegX', reverseB: true },
                { id: 'capNegXB',  name: 'Right Armhole Back',   pieceA: 'sleeveNegX', edgeA: 'capHalfA',     pieceB: 'teeBack',  edgeB: 'armholePosX' },
                { id: 'underNegX', name: 'Right Underarm Seam',  pieceA: 'sleeveNegX', edgeA: 'left',         pieceB: 'sleeveNegX', edgeB: 'right' },
            ],
        };
    }

    if (presetKey === 'biasSilkTunic')
    {
        const bustW = (0.5 * bustCirc + 0.052) * easeScale;
        const hemW = Math.min(0.86, (0.5 * hipCirc + 0.22 * flareScale) * easeScale);
        const totalLen = 0.84 * lengthScale;
        const topY = yShoulderRidge + 0.016;
        const centerY = topY - 0.5 * totalLen;

        const slipFront = buildBodicePiece({
            id: 'slipFront', name: '45° Bias Silk Front', role: '45° Bias Dress Panel',
            widthBust: bustW, widthWaist: hemW, shoulderSpan,
            neckWidth: 0.152, neckDrop: 0.065, shoulderSlope: 0.024, armholeDrop: 0.218,
            bodiceHeight: totalLen, grainlineAngleDeg: 45.0 + grainOffsetDeg, layerOrder: 0,
            sheetOffset: [-0.28, 0.0],
            arrange3D: makeTorsoCylinderArrange(centerY, zSpine, 0.172, +1, 0.5 * hemW),
        });
        const slipBack = buildBodicePiece({
            id: 'slipBack', name: '45° Bias Silk Back', role: '45° Bias Dress Panel',
            widthBust: bustW, widthWaist: hemW, shoulderSpan,
            neckWidth: 0.152, neckDrop: 0.030, shoulderSlope: 0.022, armholeDrop: 0.218,
            bodiceHeight: totalLen, grainlineAngleDeg: 45.0 + grainOffsetDeg, layerOrder: 0,
            sheetOffset: [0.28, 0.0],
            arrange3D: makeTorsoCylinderArrange(centerY, zSpine, 0.172, -1, 0.5 * hemW),
        });

        return {
            ...GARMENT_PRESET_CATALOG.biasSilkTunic,
            pieces: [slipFront, slipBack],
            seams: [
                { id: 'shPosX',   name: 'Left Shoulder Seam',   pieceA: 'slipFront', edgeA: 'shoulderPosX', pieceB: 'slipBack', edgeB: 'shoulderNegX', reverseB: true },
                { id: 'shNegX',   name: 'Right Shoulder Seam',  pieceA: 'slipFront', edgeA: 'shoulderNegX', pieceB: 'slipBack', edgeB: 'shoulderPosX', reverseB: true },
                { id: 'sidePosX', name: 'Left Bias Side Seam',  pieceA: 'slipFront', edgeA: 'sidePosX',     pieceB: 'slipBack', edgeB: 'sideNegX' },
                { id: 'sideNegX', name: 'Right Bias Side Seam', pieceA: 'slipFront', edgeA: 'sideNegX',     pieceB: 'slipBack', edgeB: 'sidePosX' },
            ],
        };
    }

    if (presetKey === 'layeredOvershirt')
    {
        // Multi-layer cloth-on-cloth garment: Layer 0 snug under-tank + Layer 1 tailored open utility jacket + sleeves
        const innerBust = (0.5 * bustCirc + 0.036) * easeScale;
        const innerWaist = (0.5 * waistCirc + 0.044) * easeScale;
        const innerLen = 0.44 * lengthScale;
        const topY = yShoulderRidge + 0.014;
        const innerCenterY = topY - 0.5 * innerLen - 0.008;

        const tankFront = buildBodicePiece({
            id: 'tankFront', name: 'Under-Tank Front (L0)', role: 'Inner Base Layer',
            widthBust: innerBust, widthWaist: innerWaist, shoulderSpan: shoulderSpan - 0.02,
            neckWidth: 0.156, neckDrop: 0.072, shoulderSlope: 0.024, armholeDrop: 0.215,
            bodiceHeight: innerLen, grainlineAngleDeg: grainOffsetDeg, layerOrder: 0,
            sheetOffset: [-0.52, 0.24],
            arrange3D: makeTorsoCylinderArrange(innerCenterY, zSpine, 0.148, +1),
        });
        const tankBack = buildBodicePiece({
            id: 'tankBack', name: 'Under-Tank Back (L0)', role: 'Inner Base Layer',
            widthBust: innerBust, widthWaist: innerWaist, shoulderSpan: shoulderSpan - 0.02,
            neckWidth: 0.156, neckDrop: 0.034, shoulderSlope: 0.022, armholeDrop: 0.215,
            bodiceHeight: innerLen, grainlineAngleDeg: grainOffsetDeg, layerOrder: 0,
            sheetOffset: [0.52, 0.24],
            arrange3D: makeTorsoCylinderArrange(innerCenterY, zSpine, 0.148, -1),
        });

        const jacketBust = (0.5 * bustCirc + 0.105) * easeScale;
        const jacketHem = (0.5 * hipCirc + 0.095) * flareScale * easeScale;
        const jacketLen = 0.58 * lengthScale;
        const jacketTopY = yShoulderRidge + 0.026;
        const jacketCenterY = jacketTopY - 0.5 * jacketLen;

        const jacketBack = buildBodicePiece({
            id: 'jacketBack', name: 'Utility Jacket Back (L1)', role: 'Outer Shell Layer',
            widthBust: jacketBust, widthWaist: jacketHem, shoulderSpan: shoulderSpan + 0.045,
            neckWidth: 0.160, neckDrop: 0.026, shoulderSlope: 0.024, armholeDrop: 0.228,
            bodiceHeight: jacketLen, grainlineAngleDeg: grainOffsetDeg, layerOrder: 1,
            sheetOffset: [0.0, 0.22],
            arrange3D: makeTorsoCylinderArrange(jacketCenterY, zSpine, 0.192, -1, 0.5 * jacketHem),
        });

        const jacketFront = buildBodicePiece({
            id: 'jacketFront', name: 'Utility Jacket Front (L1)', role: 'Outer Shell Layer',
            widthBust: jacketBust, widthWaist: jacketHem, shoulderSpan: shoulderSpan + 0.045,
            neckWidth: 0.160, neckDrop: 0.088, shoulderSlope: 0.026, armholeDrop: 0.228,
            bodiceHeight: jacketLen, grainlineAngleDeg: grainOffsetDeg, layerOrder: 1,
            sheetOffset: [0.0, -0.36],
            arrange3D: makeTorsoCylinderArrange(jacketCenterY, zSpine, 0.192, +1, 0.5 * jacketHem),
        });

        const sleevePosX = makeSleevePiece('jSleevePosX', 'Jacket Sleeve Left (+X)', +1, mannequin, {
            capWidth: 0.37 * easeScale, cuffWidth: 0.28 * easeScale, length: 0.26 * lengthScale,
            grainlineAngleDeg: grainOffsetDeg, sheetOffset: [-0.52, -0.28], layerOrder: 1, radius: 0.078,
        });
        const sleeveNegX = makeSleevePiece('jSleeveNegX', 'Jacket Sleeve Right (-X)', -1, mannequin, {
            capWidth: 0.37 * easeScale, cuffWidth: 0.28 * easeScale, length: 0.26 * lengthScale,
            grainlineAngleDeg: grainOffsetDeg, sheetOffset: [0.52, -0.28], layerOrder: 1, radius: 0.078,
        });

        return {
            ...GARMENT_PRESET_CATALOG.layeredOvershirt,
            pieces: [tankFront, tankBack, jacketFront, jacketBack, sleevePosX, sleeveNegX],
            seams: [
                { id: 'tShPosX',   name: 'Tank Left Shoulder',   pieceA: 'tankFront',   edgeA: 'shoulderPosX', pieceB: 'tankBack',   edgeB: 'shoulderNegX', reverseB: true },
                { id: 'tShNegX',   name: 'Tank Right Shoulder',  pieceA: 'tankFront',   edgeA: 'shoulderNegX', pieceB: 'tankBack',   edgeB: 'shoulderPosX', reverseB: true },
                { id: 'tSidePosX', name: 'Tank Left Side',       pieceA: 'tankFront',   edgeA: 'sidePosX',     pieceB: 'tankBack',   edgeB: 'sideNegX' },
                { id: 'tSideNegX', name: 'Tank Right Side',      pieceA: 'tankFront',   edgeA: 'sideNegX',     pieceB: 'tankBack',   edgeB: 'sidePosX' },
                { id: 'jShPosX',   name: 'Jacket Left Shoulder', pieceA: 'jacketFront', edgeA: 'shoulderPosX', pieceB: 'jacketBack', edgeB: 'shoulderNegX', reverseB: true },
                { id: 'jShNegX',   name: 'Jacket Right Shoulder',pieceA: 'jacketFront', edgeA: 'shoulderNegX', pieceB: 'jacketBack', edgeB: 'shoulderPosX', reverseB: true },
                { id: 'jSidePosX', name: 'Jacket Left Side',     pieceA: 'jacketFront', edgeA: 'sidePosX',     pieceB: 'jacketBack', edgeB: 'sideNegX' },
                { id: 'jSideNegX', name: 'Jacket Right Side',    pieceA: 'jacketFront', edgeA: 'sideNegX',     pieceB: 'jacketBack', edgeB: 'sidePosX' },
                { id: 'jCapPosXF', name: 'Jacket Left Armhole F',pieceA: 'jSleevePosX', edgeA: 'capHalfB',     pieceB: 'jacketFront', edgeB: 'armholePosX', reverseB: true },
                { id: 'jCapPosXB', name: 'Jacket Left Armhole B',pieceA: 'jSleevePosX', edgeA: 'capHalfA',     pieceB: 'jacketBack',  edgeB: 'armholeNegX' },
                { id: 'jUndPosX',  name: 'Jacket Left Underarm', pieceA: 'jSleevePosX', edgeA: 'left',         pieceB: 'jSleevePosX', edgeB: 'right' },
                { id: 'jCapNegXF', name: 'Jacket Right Armhole F',pieceA:'jSleeveNegX', edgeA: 'capHalfB',     pieceB: 'jacketFront', edgeB: 'armholeNegX', reverseB: true },
                { id: 'jCapNegXB', name: 'Jacket Right Armhole B',pieceA:'jSleeveNegX', edgeA: 'capHalfA',     pieceB: 'jacketBack',  edgeB: 'armholePosX' },
                { id: 'jUndNegX',  name: 'Jacket Right Underarm', pieceA:'jSleeveNegX', edgeA: 'left',         pieceB: 'jSleeveNegX', edgeB: 'right' },
            ],
        };
    }

    if (presetKey === 'knottedWrapScarf')
    {
        // Draped shell dress + overlapping crossed wrap collar stole for FPB self-contact showcase
        const bustW = (0.5 * bustCirc + 0.056) * easeScale;
        const hemW = Math.min(0.84, (0.5 * hipCirc + 0.16 * flareScale) * easeScale);
        const totalLen = 0.78 * lengthScale;
        const topY = yShoulderRidge + 0.015;
        const centerY = topY - 0.5 * totalLen;

        const baseFront = buildBodicePiece({
            id: 'wrapBaseFront', name: 'Sheath Dress Front (L0)', role: 'Base Sheath',
            widthBust: bustW, widthWaist: hemW, shoulderSpan,
            neckWidth: 0.148, neckDrop: 0.056, shoulderSlope: 0.024, armholeDrop: 0.214,
            bodiceHeight: totalLen, grainlineAngleDeg: grainOffsetDeg, layerOrder: 0,
            sheetOffset: [-0.30, 0.05],
            arrange3D: makeTorsoCylinderArrange(centerY, zSpine, 0.162, +1, 0.5 * bustW),
        });
        const baseBack = buildBodicePiece({
            id: 'wrapBaseBack', name: 'Sheath Dress Back (L0)', role: 'Base Sheath',
            widthBust: bustW, widthWaist: hemW, shoulderSpan,
            neckWidth: 0.148, neckDrop: 0.026, shoulderSlope: 0.022, armholeDrop: 0.214,
            bodiceHeight: totalLen, grainlineAngleDeg: grainOffsetDeg, layerOrder: 0,
            sheetOffset: [0.30, 0.05],
            arrange3D: makeTorsoCylinderArrange(centerY, zSpine, 0.162, -1, 0.5 * bustW),
        });

        // Two contoured bias shoulder wrap sashes overlapping across upper chest & meeting seamlessly at nape
        const sashLen = 0.48;
        const sashW = 0.125;
        const makeSash = (id, name, sign, layerOrder, sheetY) => ({
            id,
            name,
            role: 'Crossed Wrap Stole',
            layerOrder,
            grainlineAngleDeg: 35 * sign + grainOffsetDeg,
            sheetOffset: [0.0, sheetY],
            spacingScale: 0.90,
            seamAllowanceMm: 10,
            topCurve: lerpEdge(-0.5 * sashLen, 0.5 * sashW, 0.5 * sashLen, 0.5 * sashW, 18),
            bottomCurve: lerpEdge(-0.5 * sashLen, -0.5 * sashW, 0.5 * sashLen, -0.5 * sashW, 18),
            leftCurve: lerpEdge(-0.5 * sashLen, -0.5 * sashW, -0.5 * sashLen, 0.5 * sashW, 6),
            rightCurve: lerpEdge(0.5 * sashLen, -0.5 * sashW, 0.5 * sashLen, 0.5 * sashW, 6),
            pinFilter: layerOrder === 1 ? ((u, v) => u < 0.04 && Math.abs(v - 0.5) < 0.08) : null,
            arrange3D: (uParam, vParam) =>
            {
                const radialBoost = layerOrder === 2 ? 0.016 : 0.008;
                const theta = uParam * Math.PI * 0.98 * sign;
                const rCollar = 0.138 + 0.046 * Math.sin(uParam * Math.PI * 0.85) + radialBoost;
                const y = (yShoulderRidge + 0.030) - 0.24 * Math.pow(uParam, 1.35) + (vParam - 0.5) * sashW * 0.85;
                const x = Math.sin(theta) * rCollar * 0.90;
                const z = zSpine - Math.cos(theta) * (0.138 + radialBoost) + (uParam > 0.55 ? 0.016 * (uParam - 0.55) : 0);
                return [x, y, z];
            },
        });

        const sashLeft = makeSash('sashLeft', 'Cross-Over Wrap Left (L1)', +1, 1, -0.44);
        const sashRight = makeSash('sashRight', 'Cross-Over Wrap Right (L2)', -1, 2, -0.64);

        return {
            ...GARMENT_PRESET_CATALOG.knottedWrapScarf,
            pieces: [baseFront, baseBack, sashLeft, sashRight],
            seams: [
                { id: 'wShPosX',    name: 'Left Shoulder Seam',   pieceA: 'wrapBaseFront', edgeA: 'shoulderPosX', pieceB: 'wrapBaseBack', edgeB: 'shoulderNegX', reverseB: true },
                { id: 'wShNegX',    name: 'Right Shoulder Seam',  pieceA: 'wrapBaseFront', edgeA: 'shoulderNegX', pieceB: 'wrapBaseBack', edgeB: 'shoulderPosX', reverseB: true },
                { id: 'wSidePosX',  name: 'Left Side Seam',       pieceA: 'wrapBaseFront', edgeA: 'sidePosX',     pieceB: 'wrapBaseBack', edgeB: 'sideNegX' },
                { id: 'wSideNegX',  name: 'Right Side Seam',      pieceA: 'wrapBaseFront', edgeA: 'sideNegX',     pieceB: 'wrapBaseBack', edgeB: 'sidePosX' },
                { id: 'napeJoin',   name: 'Collar Nape Seam',     pieceA: 'sashLeft',      edgeA: 'left',         pieceB: 'sashRight',    edgeB: 'left' },
                { id: 'frontCross', name: 'Chest Wrap Knot Seam', pieceA: 'sashLeft',      edgeA: 'right',        pieceB: 'sashRight',    edgeB: 'right' },
            ],
        };
    }

    // Default: Couture Fitted Bodice & Flared A-Line Dress
    const bodiceBustW = (0.5 * bustCirc + 0.048) * easeScale;
    const bodiceWaistW = (0.5 * waistCirc + 0.038) * easeScale;
    const bodiceTopY = yShoulderRidge + 0.016;
    const bodiceLen = Math.max(0.32, bodiceTopY - yWaist);
    const bodiceCenterY = bodiceTopY - 0.5 * bodiceLen;

    const skirtLen = 0.52 * lengthScale;
    const skirtTopW = bodiceWaistW * 1.04;
    const skirtHemW = Math.min(0.88, Math.max(skirtTopW * 1.52, (0.5 * hipCirc + 0.26) * flareScale));
    const skirtCenterY = yWaist - 0.5 * skirtLen - 0.006;

    const bodiceFront = buildBodicePiece({
        id: 'bodiceFront', name: 'Bodice Front', role: 'Fitted Bodice Block',
        widthBust: bodiceBustW, widthWaist: bodiceWaistW, shoulderSpan,
        neckWidth: 0.148, neckDrop: 0.058, shoulderSlope: 0.024, armholeDrop: 0.212,
        bodiceHeight: bodiceLen, grainlineAngleDeg: grainOffsetDeg, layerOrder: 0,
        sheetOffset: [-0.38, 0.28],
        arrange3D: makeTorsoCylinderArrange(bodiceCenterY, zSpine, 0.158, +1),
    });
    const bodiceBack = buildBodicePiece({
        id: 'bodiceBack', name: 'Bodice Back', role: 'Fitted Bodice Block',
        widthBust: bodiceBustW, widthWaist: bodiceWaistW, shoulderSpan,
        neckWidth: 0.148, neckDrop: 0.024, shoulderSlope: 0.022, armholeDrop: 0.212,
        bodiceHeight: bodiceLen, grainlineAngleDeg: grainOffsetDeg, layerOrder: 0,
        sheetOffset: [0.38, 0.28],
        arrange3D: makeTorsoCylinderArrange(bodiceCenterY, zSpine, 0.158, -1),
    });
    const dressSkirtFront = buildSkirtPiece({
        id: 'dressSkirtFront', name: 'A-Line Skirt Front', role: 'Flared Skirt Panel',
        topWidth: skirtTopW, bottomWidth: skirtHemW, length: skirtLen,
        waistDip: 0.010, hemArch: 0.026, grainlineAngleDeg: grainOffsetDeg, layerOrder: 1,
        sheetOffset: [-0.42, -0.24],
        arrange3D: makeTorsoCylinderArrange(skirtCenterY, zSpine, 0.185, +1, 0.5 * skirtHemW),
    });
    const dressSkirtBack = buildSkirtPiece({
        id: 'dressSkirtBack', name: 'A-Line Skirt Back', role: 'Flared Skirt Panel',
        topWidth: skirtTopW, bottomWidth: skirtHemW, length: skirtLen,
        waistDip: 0.010, hemArch: 0.026, grainlineAngleDeg: grainOffsetDeg, layerOrder: 1,
        sheetOffset: [0.42, -0.24],
        arrange3D: makeTorsoCylinderArrange(skirtCenterY, zSpine, 0.185, -1, 0.5 * skirtHemW),
    });

    return {
        ...GARMENT_PRESET_CATALOG.coutureDress,
        pieces: [bodiceFront, bodiceBack, dressSkirtFront, dressSkirtBack],
        seams: [
            { id: 'shPosX',      name: 'Left Shoulder Seam',  pieceA: 'bodiceFront',     edgeA: 'shoulderPosX', pieceB: 'bodiceBack',      edgeB: 'shoulderNegX', reverseB: true },
            { id: 'shNegX',      name: 'Right Shoulder Seam', pieceA: 'bodiceFront',     edgeA: 'shoulderNegX', pieceB: 'bodiceBack',      edgeB: 'shoulderPosX', reverseB: true },
            { id: 'bodSidePosX', name: 'Bodice Left Side',    pieceA: 'bodiceFront',     edgeA: 'sidePosX',     pieceB: 'bodiceBack',      edgeB: 'sideNegX' },
            { id: 'bodSideNegX', name: 'Bodice Right Side',   pieceA: 'bodiceFront',     edgeA: 'sideNegX',     pieceB: 'bodiceBack',      edgeB: 'sidePosX' },
            { id: 'waistF',      name: 'Front Waist Seam',    pieceA: 'bodiceFront',     edgeA: 'bottom',       pieceB: 'dressSkirtFront', edgeB: 'top' },
            { id: 'waistB',      name: 'Back Waist Seam',     pieceA: 'bodiceBack',      edgeA: 'bottom',       pieceB: 'dressSkirtBack',  edgeB: 'top' },
            { id: 'skSidePosX',  name: 'Skirt Left Side',     pieceA: 'dressSkirtFront', edgeA: 'right',        pieceB: 'dressSkirtBack',  edgeB: 'left' },
            { id: 'skSideNegX',  name: 'Skirt Right Side',    pieceA: 'dressSkirtFront', edgeA: 'left',         pieceB: 'dressSkirtBack',  edgeB: 'right' },
        ],
    };
}
