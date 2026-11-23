// TIER DESCENT — PROTOTYPE 01
// 🧩 Builds real cluster tiers for a ControlVehicle part and drives the Tier Descent infographic card.
//
// 📝 Mechanism: every tier is a complete, watertight cut of the cluster DAG (all clusters whose level equals the tier).
//    The deviation budget elects the coarsest tier whose group deviation stays inside the budget, which is the same
//    rule the run-time cut applies per group. Clicking a column overrides the election until Auto is pressed.

import { readFacet } from '../../../Shared/GeometryCompile/Engine/ImportFormats.js';
import { buildClusterHierarchy } from '../../../Shared/GeometryCompile/Engine/ClusterHierarchy.js';
import { VEHICLE_PALETTE } from '../../../Shared/Studio/Render.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const PartCatalogue = [
    { Token: 'Body', Label: 'Body', File: 'ControlVehicleBody.facet' },
    { Token: 'Tyre', Label: 'Tyre', File: 'ControlVehicleTyre.facet' },
    { Token: 'Rim',  Label: 'Rim',  File: 'ControlVehicleRim.facet'  },
];

const SeamOptions = {
    off:   { preserveMaterials: false, pinSeams: false, Label: 'free seams' },
    slide: { preserveMaterials: true,  pinSeams: false, Label: 'seams slide' },
    pin:   { preserveMaterials: true,  pinSeams: true,  Label: 'seams pinned' },
};

const ClusterTones = ['#5d6b7a', '#7a6a5a', '#56705f', '#6f5b74', '#7b7353', '#4f6878', '#76605f', '#5f7461', '#68637e', '#80705c'];
const ModelRoot    = new URL('../../../Shared/Models/', import.meta.url);
const RingLength   = 2 * Math.PI * 34;

//------------------------------------------------------------------------------------------------------------------------
//                                                    DOCUMENT LOOKUP
//------------------------------------------------------------------------------------------------------------------------

const Card = document.getElementById('TierCard');
const Find = (Token) => Card.querySelector(`[data-id="${Token}"]`);

const Layout = {
    HeadingNote:  Find('HeadingNote'),  PartChips:    Find('PartChips'),
    ShedWhole:    Find('ShedWhole'),    ShedFraction: Find('ShedFraction'),
    TierBadge:    Find('TierBadge'),    ElectionNote: Find('ElectionNote'),
    SourceCount:  Find('SourceCount'),  SourceBar:    Find('SourceBar'),
    TierLabel:    Find('TierLabel'),    TierCount:    Find('TierCount'),
    TierBar:      Find('TierBar'),      DeltaPill:    Find('DeltaPill'),
    SeamMode:     Find('SeamMode'),     RingArc:      Find('RingArc'),
    DriftWhole:   Find('DriftWhole'),   DriftFraction:Find('DriftFraction'),
    DriftStatus:  Find('DriftStatus'),  Shading:      Find('Shading'),
    Preview:      Find('Preview'),      PreviewNote:  Find('PreviewNote'),
    Pin:          Find('Pin'),          PinTitle:     Find('PinTitle'),
    PinNote:      Find('PinNote'),      ClusterCount: Find('ClusterCount'),
    BuildTime:    Find('BuildTime'),    TierTotal:    Find('TierTotal'),
    Stair:        Find('Stair'),        BudgetHandle: Find('BudgetHandle'),
    BudgetLabel:  Find('BudgetLabel'),  AutoElect:    Find('AutoElect'),
    TierRail:     Find('TierRail'),
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    CARD CONDITION
//------------------------------------------------------------------------------------------------------------------------

const Condition = {
    PartToken:     'Body',
    SeamMode:      'slide',
    Shading:       'clusters',
    Budget:        0,          // [model units] - deviation budget
    ForcedTier:    -1,         // [idx] - -1 while the budget elects
    Yaw:           -0.65,      // [rad]
    Pitch:         0.38,       // [rad]
    HoverCluster:  -1,         // [idx]
    Facets:        new Map(),  // PartToken → decoded facet
    Build:         null,       // current cluster DAG + per-tier triangle lists
    DrawnFacets:   [],         // screen-space triangles in painter order (hit testing)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    DAG CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

async function DecodePart(Token)
{
    if (Condition.Facets.has(Token)) return Condition.Facets.get(Token);
    const Entry    = PartCatalogue.find((Part) => Part.Token === Token);
    const Bytes    = await (await fetch(new URL(Entry.File, ModelRoot))).arrayBuffer();
    const Facet    = readFacet(Bytes);
    Condition.Facets.set(Token, Facet);
    return Facet;
}

function MeasureBounds(Positions)
{
    const Low  = [Infinity, Infinity, Infinity];
    const High = [-Infinity, -Infinity, -Infinity];
    for (let Index = 0; Index < Positions.length; Index += 3)
    {
        for (let Axis = 0; Axis < 3; Axis++)
        {
            Low[Axis]  = Math.min(Low[Axis], Positions[Index + Axis]);
            High[Axis] = Math.max(High[Axis], Positions[Index + Axis]);
        }
    }
    const Centre = Low.map((Value, Axis) => (Value + High[Axis]) / 2);
    const Radius = Math.hypot(High[0] - Low[0], High[1] - Low[1], High[2] - Low[2]) / 2;
    return { Centre, Radius };
}

async function ConstructTiers()
{
    const Facet   = await DecodePart(Condition.PartToken);
    const Seams   = SeamOptions[Condition.SeamMode];
    const Dag     = buildClusterHierarchy(Facet.positions, Facet.indices, { families: Facet.families, clusterTriangles: 64, ...Seams });
    const Tiers   = Dag.levels.map((Level) => ({ ...Level, Facets: [] }));

    // 📝 Gather each tier's triangles from its clusters; corner stream and triangleFamily share one ordering.
    for (let Cluster = 0; Cluster < Dag.clusterCount; Cluster++)
    {
        const Tier  = Tiers[Dag.clusterLevel[Cluster]];
        const First = Dag.clusterFirst[Cluster];
        const Count = Dag.clusterSize[Cluster];
        for (let Corner = First; Corner < First + Count; Corner += 3)
        {
            Tier.Facets.push([Dag.cornerVertex[Corner], Dag.cornerVertex[Corner + 1], Dag.cornerVertex[Corner + 2], Cluster, Dag.triangleFamily[Corner / 3]]);
        }
    }

    const PeakDeviation = Math.max(1e-6, ...Tiers.map((Tier) => Tier.error));
    Condition.Build     = { Facet, Dag, Tiers, PeakDeviation, Bounds: MeasureBounds(Facet.positions) };
    Condition.Budget    = PeakDeviation * 0.42;
    Condition.ForcedTier   = -1;
    Condition.HoverCluster = -1;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ELECTION
//------------------------------------------------------------------------------------------------------------------------

function ElectTier()
{
    const Tiers = Condition.Build.Tiers;
    if (Condition.ForcedTier >= 0) return Condition.ForcedTier;
    let Elected = 0;
    Tiers.forEach((Tier, Index) => { if (Tier.error <= Condition.Budget + 1e-9) Elected = Index; });
    return Elected;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    READOUTS
//------------------------------------------------------------------------------------------------------------------------

const ReadoutTargets = new Map();

// 📝 Tween a numeric readout from its last shown figure; split into whole and dimmed fraction like the references.
function TweenReadout(WholeNode, FractionNode, Target, Decimals)
{
    const Start    = ReadoutTargets.get(WholeNode) ?? 0;
    const Begin    = performance.now();
    ReadoutTargets.set(WholeNode, Target);
    const Advance  = (Now) =>
    {
        const Ratio  = Math.min(1, (Now - Begin) / 420);
        const Eased  = 1 - Math.pow(1 - Ratio, 3);
        const Shown  = (Start + (Target - Start) * Eased).toFixed(Decimals);
        const [Whole, Fraction] = Shown.split('.');
        WholeNode.textContent    = Whole;
        FractionNode.textContent = Fraction ? `.${Fraction}` : '';
        if (Ratio < 1) requestAnimationFrame(Advance);
    };
    requestAnimationFrame(Advance);
}

const Thousands = (Count) => Count.toLocaleString('en-US');

function RefreshReadouts()
{
    const { Tiers, Dag } = Condition.Build;
    const Elected  = ElectTier();
    const Tier     = Tiers[Elected];
    const Source   = Tiers[0].triangles;
    const Shed     = 100 * (1 - Tier.triangles / Source);
    const Drift    = 100 * (Tier.materialError || 0);
    const OverBudget = Tier.error > Condition.Budget + 1e-9;

    TweenReadout(Layout.ShedWhole, Layout.ShedFraction, Shed, 1);
    TweenReadout(Layout.DriftWhole, Layout.DriftFraction, Drift, 2);

    Layout.TierBadge.textContent    = `Tier ${Elected} of ${Tiers.length - 1}`;
    Layout.ElectionNote.textContent = Condition.ForcedTier >= 0 ? (OverBudget ? 'forced · over budget' : 'forced · within budget') : 'elected by budget';
    Layout.ElectionNote.style.color = OverBudget ? 'var(--Amber)' : '';
    Layout.SourceCount.textContent  = Thousands(Source);
    Layout.TierLabel.textContent    = `Tier ${Elected}`;
    Layout.TierCount.textContent    = Thousands(Tier.triangles);
    Layout.SourceBar.style.height   = '120px';
    Layout.TierBar.style.height     = `${Math.max(34, 120 * Tier.triangles / Source)}px`;
    Layout.DeltaPill.textContent    = Elected === 0 ? '±0%' : `−${Shed.toFixed(0)}%`;

    // 📝 Ring fills with drift; 0–1% faithful, 1–6% drifting, above 6% broken (thresholds are prototype choices).
    const Status = Drift <= 1 ? ['Faithful', 'var(--Mint)'] : Drift <= 6 ? ['Drifting', 'var(--Amber)'] : ['Broken', 'var(--Coral)'];
    const Filled = Math.min(1, Drift / 20) * RingLength;
    Layout.RingArc.style.strokeDasharray = `${Math.max(0.001, Filled)} ${RingLength}`;
    Layout.RingArc.style.stroke          = Status[1];
    Layout.DriftStatus.lastChild.textContent = Status[0];
    Layout.DriftStatus.firstChild.style.background = Status[1];
    if (Condition.Build.Facet.families && new Set(Condition.Build.Facet.families).size < 2)
    {
        Layout.DriftStatus.lastChild.textContent = 'Single material';
    }

    const Entry = PartCatalogue.find((Part) => Part.Token === Condition.PartToken);
    Layout.HeadingNote.textContent  = `Cluster tiers · ControlVehicle ${Entry.Label} · ${SeamOptions[Condition.SeamMode].Label}`;
    Layout.ClusterCount.textContent = Thousands(Tier.clusters);
    Layout.BuildTime.textContent    = Dag.buildMs.toFixed(0);
    Layout.TierTotal.textContent    = Tiers.length;
    Layout.AutoElect.classList.toggle('On', Condition.ForcedTier < 0);

    RefreshStair(Elected);
    RefreshRail(Elected);
    RasterizePreview();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TIER STAIRCASE
//------------------------------------------------------------------------------------------------------------------------

const SvgSpace = 'http://www.w3.org/2000/svg';
const StairInset = { Top: 14, Bottom: 22, Left: 4, Right: 54 };

function SvgShape(Tag, Attributes)
{
    const Shape = document.createElementNS(SvgSpace, Tag);
    for (const [Key, Text] of Object.entries(Attributes)) Shape.setAttribute(Key, Text);
    return Shape;
}

function StairExtent()
{
    const Box = Layout.Stair.getBoundingClientRect();
    return { Width: Box.width, Height: Box.height, PlotHeight: Box.height - StairInset.Top - StairInset.Bottom };
}

const DeviationToY = (Deviation, Extent, Peak) => StairInset.Top + Extent.PlotHeight * (1 - Deviation / (Peak * 1.1));

function RefreshStair(Elected)
{
    const { Tiers, PeakDeviation } = Condition.Build;
    const Extent  = StairExtent();
    const Stair   = Layout.Stair;
    const Source  = Tiers[0].triangles;
    const Span    = (Extent.Width - StairInset.Left - StairInset.Right) / Tiers.length;
    Stair.setAttribute('viewBox', `0 0 ${Extent.Width} ${Extent.Height}`);
    Stair.replaceChildren();

    Tiers.forEach((Tier, Index) =>
    {
        const Height  = Math.max(4, Extent.PlotHeight * Tier.triangles / Source);
        const Column  = SvgShape('rect', { x: StairInset.Left + Index * Span + Span * 0.16, y: StairInset.Top + Extent.PlotHeight - Height, width: Span * 0.68, height: Height, rx: Math.min(8, Span * 0.3) });
        const Eligible = Tier.error <= Condition.Budget + 1e-9;
        Column.setAttribute('class', `Column${Eligible ? ' Eligible' : ''}${Index === Elected ? ' Elected' : ''}`);
        Column.addEventListener('click', () => { Condition.ForcedTier = Index; RefreshReadouts(); });
        Column.append(SvgShape('title', {}));
        Column.firstChild.textContent = `Tier ${Index}: ${Thousands(Tier.triangles)} triangles · deviation ${Tier.error.toFixed(4)}`;
        Stair.append(Column);
        const Caption = SvgShape('text', { x: StairInset.Left + Index * Span + Span / 2, y: Extent.Height - 6, 'text-anchor': 'middle', class: 'AxisText' });
        Caption.textContent = Index;
        Stair.append(Caption);
    });

    const Points = Tiers.map((Tier, Index) => [StairInset.Left + Index * Span + Span / 2, DeviationToY(Tier.error, Extent, PeakDeviation)]);
    Stair.append(SvgShape('path', { class: 'Trace', d: Points.map(([X, Y], Index) => `${Index ? 'L' : 'M'}${X},${Y}`).join(' ') }));
    Points.forEach(([X, Y], Index) => Stair.append(SvgShape('circle', { cx: X, cy: Y, r: 3.2, class: `TraceDot${Tiers[Index].error > Condition.Budget + 1e-9 ? ' Over' : ''}` })));

    const BudgetY = DeviationToY(Condition.Budget, Extent, PeakDeviation);
    Stair.append(SvgShape('line', { x1: 0, x2: Extent.Width - StairInset.Right + 6, y1: BudgetY, y2: BudgetY, class: 'Budget' }));
    Layout.BudgetHandle.style.top   = `${BudgetY}px`;
    Layout.BudgetLabel.textContent  = `budget ${Condition.Budget.toFixed(3)}`;
}

function RefreshRail(Elected)
{
    const { Tiers } = Condition.Build;
    const Source = Tiers[0].triangles;
    Layout.TierRail.replaceChildren(...Tiers.map((Tier, Index) =>
    {
        const Row = document.createElement('li');
        Row.className = Index === Elected ? 'Elected' : '';
        Row.innerHTML = `<span>Tier ${Index}</span><span class="Meter"><i style="width:${(100 * Tier.triangles / Source).toFixed(1)}%"></i></span>`
                      + `<span>${Thousands(Tier.triangles)}</span><span class="Dim">${Tier.error.toFixed(3)}</span>`;
        Row.addEventListener('click', () => { Condition.ForcedTier = Index; RefreshReadouts(); });
        return Row;
    }));
}

function BindBudgetDrag()
{
    const Handle = Layout.BudgetHandle;
    Handle.addEventListener('pointerdown', (Press) =>
    {
        Handle.setPointerCapture(Press.pointerId);
        const Move = (Motion) =>
        {
            const Extent = StairExtent();
            const Box    = Layout.Stair.getBoundingClientRect();
            const Ratio  = 1 - (Motion.clientY - Box.top - StairInset.Top) / Extent.PlotHeight;
            Condition.Budget     = Math.max(0, Math.min(1, Ratio)) * Condition.Build.PeakDeviation * 1.1;
            Condition.ForcedTier = -1;
            RefreshReadouts();
        };
        const Release = () => { Handle.removeEventListener('pointermove', Move); Handle.removeEventListener('pointerup', Release); };
        Handle.addEventListener('pointermove', Move);
        Handle.addEventListener('pointerup', Release);
    });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PREVIEW RASTERIZATION
//------------------------------------------------------------------------------------------------------------------------

function FamilyTone(Family)
{
    const Swatch = VEHICLE_PALETTE[Family];
    if (!Swatch) return [150, 150, 156];
    const Lift = Swatch.emission ? 1 : 0;
    return Swatch.albedo.map((Channel) => Math.round(255 * Math.min(1, Math.pow(Channel, 1 / 2.2) * 0.9 + 0.12 + Lift * 0.2)));
}

function ParseHex(Hex) { return [1, 3, 5].map((At) => parseInt(Hex.slice(At, At + 2), 16)); }

function RasterizePreview()
{
    const Canvas  = Layout.Preview;
    const Scale   = window.devicePixelRatio || 1;
    const Width   = Canvas.clientWidth, Height = Canvas.clientHeight;
    if (Canvas.width !== Math.round(Width * Scale) || Canvas.height !== Math.round(Height * Scale))
    {
        Canvas.width  = Math.round(Width * Scale);
        Canvas.height = Math.round(Height * Scale);
    }
    const Pen = Canvas.getContext('2d');
    Pen.setTransform(Scale, 0, 0, Scale, 0, 0);
    Pen.clearRect(0, 0, Width, Height);
    if (!Condition.Build) return;

    const { Facet, Tiers, Bounds } = Condition.Build;
    const Tier     = Tiers[ElectTier()];
    const Positions = Facet.positions;
    const CosYaw   = Math.cos(Condition.Yaw),   SinYaw   = Math.sin(Condition.Yaw);
    const CosPitch = Math.cos(Condition.Pitch), SinPitch = Math.sin(Condition.Pitch);
    const Zoom     = Math.min(Width, Height) * 0.44 / Bounds.Radius;
    const MidX     = Width / 2, MidY = Height / 2 + 6;

    // 📝 Authored frame is +X forward, +Y left, +Z up. Screen right = rotated X, depth grows away from the viewer.
    const Project = (Vertex) =>
    {
        const X = Positions[Vertex * 3] - Bounds.Centre[0];
        const Y = Positions[Vertex * 3 + 1] - Bounds.Centre[1];
        const Z = Positions[Vertex * 3 + 2] - Bounds.Centre[2];
        const Rx = X * CosYaw - Y * SinYaw;
        const Ry = X * SinYaw + Y * CosYaw;
        const Up    = Z * CosPitch + Ry * SinPitch;
        const Depth = Ry * CosPitch - Z * SinPitch;
        return [MidX + Rx * Zoom, MidY - Up * Zoom, Depth, Rx, Up];
    };

    const Projected = new Map();
    const Cached    = (Vertex) => { let P = Projected.get(Vertex); if (!P) { P = Project(Vertex); Projected.set(Vertex, P); } return P; };
    const Drawn     = [];
    const Light     = [-0.35, 0.55, 0.76];

    for (const [A, B, C, Cluster, Family] of Tier.Facets)
    {
        const Pa = Cached(A), Pb = Cached(B), Pc = Cached(C);
        // View-space normal from (right, up, toward viewer = −depth).
        const U = [Pb[3] - Pa[3], Pb[4] - Pa[4], -(Pb[2] - Pa[2])];
        const V = [Pc[3] - Pa[3], Pc[4] - Pa[4], -(Pc[2] - Pa[2])];
        let Nx = U[1] * V[2] - U[2] * V[1], Ny = U[2] * V[0] - U[0] * V[2], Nz = U[0] * V[1] - U[1] * V[0];
        const Length = Math.hypot(Nx, Ny, Nz) || 1;
        Nx /= Length; Ny /= Length; Nz /= Length;
        if (Nz < -0.02) continue;
        const Lambert = Math.max(0, Nx * Light[0] + Ny * Light[1] + Nz * Light[2]);
        Drawn.push({ Pa, Pb, Pc, Cluster, Family, Depth: (Pa[2] + Pb[2] + Pc[2]) / 3, Brightness: 0.32 + 0.68 * Lambert });
    }
    Drawn.sort((First, Second) => Second.Depth - First.Depth);

    Pen.lineJoin = 'round';
    for (const Facet3 of Drawn)
    {
        let Tone;
        if (Condition.Shading === 'materials') Tone = FamilyTone(Facet3.Family);
        else if (Condition.Shading === 'clay') Tone = [196, 196, 200];
        else Tone = ParseHex(ClusterTones[Facet3.Cluster % ClusterTones.length]);
        if (Facet3.Cluster === Condition.HoverCluster) Tone = [215, 243, 106];
        const Shade = Tone.map((Channel) => Math.round(Channel * Facet3.Brightness));
        Pen.beginPath();
        Pen.moveTo(Facet3.Pa[0], Facet3.Pa[1]);
        Pen.lineTo(Facet3.Pb[0], Facet3.Pb[1]);
        Pen.lineTo(Facet3.Pc[0], Facet3.Pc[1]);
        Pen.closePath();
        Pen.fillStyle   = `rgb(${Shade})`;
        Pen.strokeStyle = Condition.Shading === 'clusters' ? 'rgba(10,10,12,0.55)' : `rgb(${Shade})`;
        Pen.lineWidth   = Condition.Shading === 'clusters' ? 0.6 : 0.5;
        Pen.fill();
        Pen.stroke();
    }
    Condition.DrawnFacets = Drawn;

    // Ground shadow ellipse under the part.
    const Floor = Pen.createRadialGradient(MidX, MidY + Bounds.Radius * Zoom * 0.62, 0, MidX, MidY + Bounds.Radius * Zoom * 0.62, Bounds.Radius * Zoom);
    Floor.addColorStop(0, 'rgba(215,243,106,0.10)');
    Floor.addColorStop(1, 'rgba(215,243,106,0)');
    Pen.globalCompositeOperation = 'destination-over';
    Pen.fillStyle = Floor;
    Pen.fillRect(0, 0, Width, Height);
    Pen.globalCompositeOperation = 'source-over';

    PlacePin(Tier);
}

function PlacePin(Tier)
{
    const Dag     = Condition.Build.Dag;
    const Target  = Condition.HoverCluster;
    const Members = Condition.DrawnFacets.filter((Facet3) => Facet3.Cluster === Target);
    if (Target < 0 || !Members.length) { Layout.Pin.hidden = true; return; }
    let X = 0, Y = 0;
    for (const Facet3 of Members) { X += (Facet3.Pa[0] + Facet3.Pb[0] + Facet3.Pc[0]) / 3; Y += (Facet3.Pa[1] + Facet3.Pb[1] + Facet3.Pc[1]) / 3; }
    const Canvas = Layout.Preview;
    Layout.Pin.hidden        = false;
    Layout.Pin.style.left    = `${Canvas.offsetLeft + X / Members.length}px`;
    Layout.Pin.style.top     = `${Canvas.offsetTop + Y / Members.length}px`;
    const OwnGroup           = Dag.clusterGroupSelf[Target];
    const Family             = Dag.clusterFamily[Target];
    const FamilyName         = Family >= 0 && VEHICLE_PALETTE[Family] ? VEHICLE_PALETTE[Family].name : 'mixed materials';
    Layout.PinTitle.textContent = `Cluster ${Target}`;
    Layout.PinNote.textContent  = `${Dag.clusterSize[Target] / 3} tris · ${FamilyName} · ${OwnGroup < 0 ? 'source' : `group ${OwnGroup}`}`;
}

function InsideFacet(X, Y, Facet3)
{
    const Sign = (P, Q, R) => (P[0] - R[0]) * (Q[1] - R[1]) - (Q[0] - R[0]) * (P[1] - R[1]);
    const Point = [X, Y];
    const D1 = Sign(Point, Facet3.Pa, Facet3.Pb), D2 = Sign(Point, Facet3.Pb, Facet3.Pc), D3 = Sign(Point, Facet3.Pc, Facet3.Pa);
    return !((D1 < 0 || D2 < 0 || D3 < 0) && (D1 > 0 || D2 > 0 || D3 > 0));
}

function BindPreviewPointer()
{
    const Canvas = Layout.Preview;
    let DragOrigin = null;
    Canvas.addEventListener('pointerdown', (Press) => { DragOrigin = [Press.clientX, Press.clientY, Condition.Yaw, Condition.Pitch]; Canvas.setPointerCapture(Press.pointerId); });
    Canvas.addEventListener('pointerup', () => { DragOrigin = null; });
    Canvas.addEventListener('pointerleave', () => { if (!DragOrigin) { Condition.HoverCluster = -1; RasterizePreview(); } });
    Canvas.addEventListener('pointermove', (Motion) =>
    {
        if (DragOrigin)
        {
            Condition.Yaw   = DragOrigin[2] + (Motion.clientX - DragOrigin[0]) * 0.01;
            Condition.Pitch = Math.max(-0.2, Math.min(1.3, DragOrigin[3] + (Motion.clientY - DragOrigin[1]) * 0.008));
            RasterizePreview();
            return;
        }
        const Box = Canvas.getBoundingClientRect();
        const X = Motion.clientX - Box.left, Y = Motion.clientY - Box.top;
        let Hit = -1;
        for (let Index = Condition.DrawnFacets.length - 1; Index >= 0; Index--)
        {
            if (InsideFacet(X, Y, Condition.DrawnFacets[Index])) { Hit = Condition.DrawnFacets[Index].Cluster; break; }
        }
        if (Hit !== Condition.HoverCluster) { Condition.HoverCluster = Hit; RasterizePreview(); }
    });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONTROLS
//------------------------------------------------------------------------------------------------------------------------

function MarkSegment(Group, Attribute, Token)
{
    Group.querySelectorAll('button').forEach((Button) => Button.classList.toggle('On', Button.dataset[Attribute] === Token));
}

async function SelectPart(Token)
{
    Condition.PartToken = Token;
    Layout.PartChips.querySelectorAll('button').forEach((Button) => Button.classList.toggle('On', Button.dataset.part === Token));
    await ConstructTiers();
    RefreshReadouts();
}

async function BindControls()
{
    for (const Part of PartCatalogue)
    {
        const Facet  = await DecodePart(Part.Token);
        const Button = document.createElement('button');
        Button.dataset.part = Part.Token;
        Button.innerHTML    = `<small>${(Facet.indices.length / 3 / 1000).toFixed(1)}k</small><b>${Part.Label}</b>`;
        Button.addEventListener('click', () => SelectPart(Part.Token));
        Layout.PartChips.append(Button);
    }
    Layout.SeamMode.addEventListener('click', async (Press) =>
    {
        const Mode = Press.target.closest('button')?.dataset.mode;
        if (!Mode || Mode === Condition.SeamMode) return;
        Condition.SeamMode = Mode;
        MarkSegment(Layout.SeamMode, 'mode', Mode);
        await ConstructTiers();
        RefreshReadouts();
    });
    Layout.Shading.addEventListener('click', (Press) =>
    {
        const Mode = Press.target.closest('button')?.dataset.shading;
        if (!Mode) return;
        Condition.Shading = Mode;
        MarkSegment(Layout.Shading, 'shading', Mode);
        RasterizePreview();
    });
    Layout.AutoElect.addEventListener('click', () => { Condition.ForcedTier = -1; RefreshReadouts(); });
    BindBudgetDrag();
    BindPreviewPointer();
    new ResizeObserver(() => { if (Condition.Build) { RefreshStair(ElectTier()); RasterizePreview(); } }).observe(Card);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MAIN
//------------------------------------------------------------------------------------------------------------------------

await BindControls();
await SelectPart(Condition.PartToken);
window.TierDescentCondition = Condition; // 🔍 exposed for the capture script
