/*==========================================================================================================================================
                                                       FORCEFIELDVISUALIZATION.JS
==========================================================================================================================================*/
// 🧩 Force-field slice visualization for the Force Field and Particle editors: turbo heatmap, arrows and tracers from 100 m to 100 km.
'use strict';

// 📝 One renderer, three projections of the same horizontal slice (y = SliceHeight):
//      Map      top-down, log zoom and pan, screen-constant sampling (the Force Field editor's main view)
//      Oblique  the slice tilted and turned (orthographic), with source columns standing on it
//      Camera   any external view-projection (column-major, WebGPU depth 0–1): the Particle editor's 3D viewport
//    The caller supplies Sample(Contribution, X, Y, Z, Time, CellSpan, Out) and the source outlines; this file never knows
//    which editor it is in. The look is the eos Force Field Debugger's vector field (colour stops, white arrows, 5 px
//    heads at ±0.55 rad, #050b12 ground), extended with level-of-detail sampling, tracers, a scale bar, a legend,
//    a minimap and a cascade overlay.
const ForceFieldVisualization = (() =>
{
    //------------------------------------------------------------------------------------------------------------------------
    //                                                     COLOUR MAP
    //------------------------------------------------------------------------------------------------------------------------

    const ColourStops = [[5, 18, 81], [0, 112, 241], [0, 224, 210], [235, 239, 39], [239, 60, 18]];
    const ColourLut   = new Uint8ClampedArray(256 * 3);
    for (let Index = 0; Index < 256; ++Index)
    {
        const Share = Index / 255 * (ColourStops.length - 1);
        const Lower = Math.min(ColourStops.length - 2, Math.floor(Share));
        const Blend = Share - Lower;
        for (let Channel = 0; Channel < 3; ++Channel)
            ColourLut[Index * 3 + Channel] = ColourStops[Lower][Channel] + (ColourStops[Lower + 1][Channel] - ColourStops[Lower][Channel]) * Blend;
    }

    function ResolveColour(Share)
    {
        const Index = Math.max(0, Math.min(255, Math.round(Share * 255))) * 3;
        return [ColourLut[Index], ColourLut[Index + 1], ColourLut[Index + 2]];
    }

    function ResolveColourText(Share, Alpha)
    {
        const [Red, Green, Blue] = ResolveColour(Share);
        return Alpha === undefined ? `rgb(${Red},${Green},${Blue})` : `rgba(${Red},${Green},${Blue},${Alpha})`;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                QUANTITIES AND FORMATTING
    //------------------------------------------------------------------------------------------------------------------------

    const Quantities = {
        Speed:      { Label: 'Horizontal speed', Short: '|v|', Signed: false, Derivative: false },
        AlongX:     { Label: 'East · X',     Short: 'vx',  Signed: true,  Derivative: false },
        AlongZ:     { Label: 'South · Z',    Short: 'vz',  Signed: true,  Derivative: false },
        Vertical:   { Label: 'Vertical · Y', Short: 'vy',  Signed: true,  Derivative: false },
        Vorticity:  { Label: 'Vorticity',    Short: 'ω',   Signed: true,  Derivative: true },
        Divergence: { Label: 'Divergence',   Short: '∇·v', Signed: true,  Derivative: true }
    };
    const ContributionUnits = { Flow: 'm/s', Accelerate: 'm/s²', Damp: '1/s' };

    function ResolveUnit(Contribution, Quantity)
    {
        if (Contribution === 'Damp') return '1/s';
        if (Quantities[Quantity] && Quantities[Quantity].Derivative) return Contribution === 'Flow' ? '1/s' : '1/s²';
        return ContributionUnits[Contribution] || 'm/s';
    }

    function FormatDistance(Metres)
    {
        const Magnitude = Math.abs(Metres);
        if (Magnitude < 1e-6) return '0 m';
        if (Magnitude >= 1000) return `${(Metres / 1000).toFixed(Magnitude >= 10000 ? 0 : Magnitude >= 2000 ? 1 : 2).replace(/\.0+$/, '')} km`;
        if (Magnitude >= 10) return `${Metres.toFixed(0)} m`;
        if (Magnitude >= 1) return `${Metres.toFixed(1).replace(/\.0$/, '')} m`;
        return `${(Metres * 100).toFixed(0)} cm`;
    }

    // 📝 The largest 1 · 2 · 5 × 10ᵏ that does not exceed the target (scale bars, ground grid, arrow spacing labels).
    function NiceLength(Target)
    {
        const Decade = Math.pow(10, Math.floor(Math.log10(Math.max(1e-9, Target))));
        for (const Step of [5, 2, 1]) if (Step * Decade <= Target) return Step * Decade;
        return Decade;
    }

    function FormatReading(Reading)
    {
        const Magnitude = Math.abs(Reading);
        if (Magnitude === 0) return '0';
        if (Magnitude >= 100) return Reading.toFixed(0);
        if (Magnitude >= 10) return Reading.toFixed(1);
        if (Magnitude >= 1) return Reading.toFixed(2);
        if (Magnitude >= 0.01) return Reading.toFixed(3);
        return Reading.toExponential(1);
    }

    const Smooth01 = Share => { const Clamped = Math.min(1, Math.max(0, Share)); return Clamped * Clamped * (3 - 2 * Clamped); };

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    PROJECTION
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Inverse of a column-major 4 × 4 (cofactor expansion); null when singular.
    function InvertProjection(Source)
    {
        const S = Source, Out = new Float64Array(16);
        Out[0]  =  S[5] * S[10] * S[15] - S[5] * S[11] * S[14] - S[9] * S[6] * S[15] + S[9] * S[7] * S[14] + S[13] * S[6] * S[11] - S[13] * S[7] * S[10];
        Out[4]  = -S[4] * S[10] * S[15] + S[4] * S[11] * S[14] + S[8] * S[6] * S[15] - S[8] * S[7] * S[14] - S[12] * S[6] * S[11] + S[12] * S[7] * S[10];
        Out[8]  =  S[4] * S[9] * S[15] - S[4] * S[11] * S[13] - S[8] * S[5] * S[15] + S[8] * S[7] * S[13] + S[12] * S[5] * S[11] - S[12] * S[7] * S[9];
        Out[12] = -S[4] * S[9] * S[14] + S[4] * S[10] * S[13] + S[8] * S[5] * S[14] - S[8] * S[6] * S[13] - S[12] * S[5] * S[10] + S[12] * S[6] * S[9];
        Out[1]  = -S[1] * S[10] * S[15] + S[1] * S[11] * S[14] + S[9] * S[2] * S[15] - S[9] * S[3] * S[14] - S[13] * S[2] * S[11] + S[13] * S[3] * S[10];
        Out[5]  =  S[0] * S[10] * S[15] - S[0] * S[11] * S[14] - S[8] * S[2] * S[15] + S[8] * S[3] * S[14] + S[12] * S[2] * S[11] - S[12] * S[3] * S[10];
        Out[9]  = -S[0] * S[9] * S[15] + S[0] * S[11] * S[13] + S[8] * S[1] * S[15] - S[8] * S[3] * S[13] - S[12] * S[1] * S[11] + S[12] * S[3] * S[9];
        Out[13] =  S[0] * S[9] * S[14] - S[0] * S[10] * S[13] - S[8] * S[1] * S[14] + S[8] * S[2] * S[13] + S[12] * S[1] * S[10] - S[12] * S[2] * S[9];
        Out[2]  =  S[1] * S[6] * S[15] - S[1] * S[7] * S[14] - S[5] * S[2] * S[15] + S[5] * S[3] * S[14] + S[13] * S[2] * S[7] - S[13] * S[3] * S[6];
        Out[6]  = -S[0] * S[6] * S[15] + S[0] * S[7] * S[14] + S[4] * S[2] * S[15] - S[4] * S[3] * S[14] - S[12] * S[2] * S[7] + S[12] * S[3] * S[6];
        Out[10] =  S[0] * S[5] * S[15] - S[0] * S[7] * S[13] - S[4] * S[1] * S[15] + S[4] * S[3] * S[13] + S[12] * S[1] * S[7] - S[12] * S[3] * S[5];
        Out[14] = -S[0] * S[5] * S[14] + S[0] * S[6] * S[13] + S[4] * S[1] * S[14] - S[4] * S[2] * S[13] - S[12] * S[1] * S[6] + S[12] * S[2] * S[5];
        Out[3]  = -S[1] * S[6] * S[11] + S[1] * S[7] * S[10] + S[5] * S[2] * S[11] - S[5] * S[3] * S[10] - S[9] * S[2] * S[7] + S[9] * S[3] * S[6];
        Out[7]  =  S[0] * S[6] * S[11] - S[0] * S[7] * S[10] - S[4] * S[2] * S[11] + S[4] * S[3] * S[10] + S[8] * S[2] * S[7] - S[8] * S[3] * S[6];
        Out[11] = -S[0] * S[5] * S[11] + S[0] * S[7] * S[9] + S[4] * S[1] * S[11] - S[4] * S[3] * S[9] - S[8] * S[1] * S[7] + S[8] * S[3] * S[5];
        Out[15] =  S[0] * S[5] * S[10] - S[0] * S[6] * S[9] - S[4] * S[1] * S[10] + S[4] * S[2] * S[9] + S[8] * S[1] * S[6] - S[8] * S[2] * S[5];
        const Determinant = S[0] * Out[0] + S[1] * Out[4] + S[2] * Out[8] + S[3] * Out[12];
        if (Math.abs(Determinant) < 1e-20) return null;
        for (let Index = 0; Index < 16; ++Index) Out[Index] /= Determinant;
        return Out;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                      INSTANCE
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Specification: { Sample(Contribution, X, Y, Z, Time, CellSpan, Out) → inside?, OnSourcePick(Id), OnSourceMove(Id, X, Z),
    //    OnHover(Probe | null), OnViewChange(), OnMinimapPick(X, Z), MinimumSpan, MaximumSpan }.
    function Create(Canvas, Specification)
    {
        const Context = Canvas.getContext('2d');
        const Options = {
            Quantity: 'Speed', Contribution: 'Flow', ScaleMode: 'Automatic', FixedScale: 20,
            Heatmap: true, HeatmapStyle: 'Smooth', Opacity: 1, Arrows: true, ArrowSpacing: 40, ArrowWeight: 1,
            FlowLines: false, FlowLineSpacing: 46, FlowLineLength: 22, FlowLinePulse: true, QuietCutoff: 0.025,
            Tracers: true, TracerCount: 360, SliceHeight: 10, CellPixels: 6, CameraCells: 48,
            ShowScaleBar: true, ShowLegend: true, ShowSources: true, ShowLabels: true, MinimalLabels: false, ShowCascades: false, ShowGroundGrid: true,
            AllowOblique: true,
            Cascades: [], CascadeFocus: [0, 0], Sources: [], Extent: null, ClipToExtent: false, ProbePoint: null,
            PlacementMode: false, PlacementPreview: null,
            Background: '#050b12', RefreshInterval: 66, Animated: true, Minimap: null, MinimapExtent: null, LegendTitle: ''
        };
        const View = { Mode: 'Map', CentreX: 0, CentreZ: 0, Span: 2000, Yaw: Math.PI / 4, Pitch: 0.62, ViewProjection: null };
        const Statistics = { Samples: 0, Milliseconds: 0, CellSpan: 1, Scale: 1, Peak: 0, Columns: 0, Rows: 0, ArrowSpan: 1, Streamlines: 0, Refreshes: 0 };
        const Probe = new Float64Array(3);
        const Lattice = { Columns: 0, Rows: 0, OriginX: 0, OriginZ: 0, CellSpan: 1, MedianVertical: 0, Vectors: new Float32Array(0), Quantity: new Float32Array(0),
                          Inside: new Uint8Array(0), Image: null, ImageCanvas: document.createElement('canvas'), Stamp: '', Time: -1, RefreshedAt: 0,
                          Unit: 'm/s' };
        const Offscreen = document.createElement('canvas');
        const Tracers = [];
        const Streamlines = [];
        let StreamlinesStamp = '';
        let Width = 1, Height = 1, Ratio = 1, LastRender = 0, Dirty = true, ScaleReading = 0, ElapsedDisplay = 0;
        let Inverse = null;
        let MinimapStamp = 0, MinimapTime = -1;
        const MinimapLattice = { Canvas: document.createElement('canvas'), Size: 72 };

        //--------------------------------------------------------------------------------------------------------------------
        //                                              PROJECTION FUNCTIONS
        //--------------------------------------------------------------------------------------------------------------------

        // 📝 Map and Oblique are affine on the slice plane: Screen = Linear · (X − CentreX, Z − CentreZ) + (W/2, H/2),
        //    with heights above the slice lifted by cos(pitch) in Oblique.
        function ResolveAffine()
        {
            const PixelsPerMetre = Width / Math.max(1e-6, View.Span);
            if (View.Mode === 'Map') return { A: PixelsPerMetre, B: 0, C: 0, D: PixelsPerMetre, Lift: 0, PixelsPerMetre };
            const Cosine = Math.cos(View.Yaw), Sine = Math.sin(View.Yaw), Tilt = Math.sin(View.Pitch);
            return { A: PixelsPerMetre * Cosine, B: -PixelsPerMetre * Sine, C: PixelsPerMetre * Sine * Tilt, D: PixelsPerMetre * Cosine * Tilt,
                     Lift: PixelsPerMetre * Math.cos(View.Pitch), PixelsPerMetre };
        }

        function Project(X, Y, Z)
        {
            if (View.Mode === 'Camera')
            {
                const M = View.ViewProjection;
                if (!M) return null;
                const ClipX = M[0] * X + M[4] * Y + M[8] * Z + M[12];
                const ClipY = M[1] * X + M[5] * Y + M[9] * Z + M[13];
                const ClipW = M[3] * X + M[7] * Y + M[11] * Z + M[15];
                if (ClipW <= 1e-4) return null;
                return [(ClipX / ClipW * 0.5 + 0.5) * Width, (0.5 - ClipY / ClipW * 0.5) * Height];
            }
            const Affine = ResolveAffine();
            const DeltaX = X - View.CentreX, DeltaZ = Z - View.CentreZ;
            return [Width / 2 + Affine.A * DeltaX + Affine.B * DeltaZ,
                    Height / 2 + Affine.C * DeltaX + Affine.D * DeltaZ - Affine.Lift * (Y - Options.SliceHeight)];
        }

        // 📝 Screen → slice plane. Camera mode unprojects the near and far points and intersects the ray with y = SliceHeight.
        function ScreenToPlane(ScreenX, ScreenY)
        {
            if (View.Mode === 'Camera')
            {
                if (!Inverse) return null;
                const NdcX = ScreenX / Width * 2 - 1, NdcY = 1 - ScreenY / Height * 2;
                const Unproject = Depth =>
                {
                    const M = Inverse;
                    const X = M[0] * NdcX + M[4] * NdcY + M[8] * Depth + M[12];
                    const Y = M[1] * NdcX + M[5] * NdcY + M[9] * Depth + M[13];
                    const Z = M[2] * NdcX + M[6] * NdcY + M[10] * Depth + M[14];
                    const W = M[3] * NdcX + M[7] * NdcY + M[11] * Depth + M[15];
                    return [X / W, Y / W, Z / W];
                };
                const Near = Unproject(0), Far = Unproject(1);
                const DirectionY = Far[1] - Near[1];
                if (Math.abs(DirectionY) < 1e-9) return null;
                const Parameter = (Options.SliceHeight - Near[1]) / DirectionY;
                if (Parameter < 0) return null;
                return [Near[0] + (Far[0] - Near[0]) * Parameter, Near[2] + (Far[2] - Near[2]) * Parameter];
            }
            const Affine = ResolveAffine();
            const Determinant = Affine.A * Affine.D - Affine.B * Affine.C;
            if (Math.abs(Determinant) < 1e-12) return null;
            const OffsetX = ScreenX - Width / 2, OffsetY = ScreenY - Height / 2;
            return [View.CentreX + (Affine.D * OffsetX - Affine.B * OffsetY) / Determinant,
                    View.CentreZ + (-Affine.C * OffsetX + Affine.A * OffsetY) / Determinant];
        }

        // 📝 The world rectangle the grid must cover: the view (Map), the inverse of the screen corners (Oblique, depth
        //    clamped to three spans) or the authored extent (Camera).
        function ResolveCoverage()
        {
            let MinimumX, MinimumZ, MaximumX, MaximumZ;
            if (View.Mode === 'Camera')
            {
                const Extent = Options.Extent || { MinimumX: -50, MinimumZ: -50, MaximumX: 50, MaximumZ: 50 };
                return { MinimumX: Extent.MinimumX, MinimumZ: Extent.MinimumZ, MaximumX: Extent.MaximumX, MaximumZ: Extent.MaximumZ };
            }
            const Corners = [[0, 0], [Width, 0], [0, Height], [Width, Height]].map(([X, Y]) => ScreenToPlane(X, Y)).filter(Boolean);
            MinimumX = Math.min(...Corners.map(Corner => Corner[0]));
            MaximumX = Math.max(...Corners.map(Corner => Corner[0]));
            MinimumZ = Math.min(...Corners.map(Corner => Corner[1]));
            MaximumZ = Math.max(...Corners.map(Corner => Corner[1]));
            // 📝 Oblique shows the slice as a plate (0.84 of a span a side) standing in space, so its edges and the columns read as 3D.
            const Limit = View.Mode === 'Oblique' ? View.Span * 0.42 : View.Span * 3;
            MinimumX = Math.max(MinimumX, View.CentreX - Limit); MaximumX = Math.min(MaximumX, View.CentreX + Limit);
            MinimumZ = Math.max(MinimumZ, View.CentreZ - Limit); MaximumZ = Math.min(MaximumZ, View.CentreZ + Limit);
            if (Options.ClipToExtent && Options.Extent)
            {
                MinimumX = Math.max(MinimumX, Options.Extent.MinimumX); MaximumX = Math.min(MaximumX, Options.Extent.MaximumX);
                MinimumZ = Math.max(MinimumZ, Options.Extent.MinimumZ); MaximumZ = Math.min(MaximumZ, Options.Extent.MaximumZ);
            }
            return { MinimumX, MinimumZ, MaximumX, MaximumZ };
        }

        //--------------------------------------------------------------------------------------------------------------------
        //                                                   SAMPLING
        //--------------------------------------------------------------------------------------------------------------------

        // 📝 Level of detail: the cell span is the power of two (in metres) nearest CellPixels on screen, so the grid snaps
        //    to the world and panning never shimmers; each halving of zoom is one level, like a clipmap cascade.
        function ResolveCellSpan()
        {
            if (View.Mode === 'Camera')
            {
                const Coverage = ResolveCoverage();
                return Math.max(Coverage.MaximumX - Coverage.MinimumX, Coverage.MaximumZ - Coverage.MinimumZ) / Options.CameraCells;
            }
            const Wanted = View.Span / Math.max(1, Width) * Options.CellPixels;
            return Math.pow(2, Math.round(Math.log2(Math.max(1e-4, Wanted))));
        }

        function ResolveStamp()
        {
            const M = View.ViewProjection;
            return [View.Mode, View.CentreX, View.CentreZ, View.Span, View.Yaw, View.Pitch, Width, Height, Options.Contribution, Options.SliceHeight,
                    Options.CellPixels, Options.CameraCells, Options.ClipToExtent, View.Mode === 'Camera' && M ? 'camera' : ''].join('|');
        }

        function SampleLattice(Time)
        {
            const Started = performance.now();
            const Coverage = ResolveCoverage();
            let CellSpan = ResolveCellSpan();
            let OriginX, OriginZ, Columns, Rows;
            for (;;)
            {
                if (View.Mode === 'Camera')
                {
                    OriginX = Coverage.MinimumX + CellSpan / 2;
                    OriginZ = Coverage.MinimumZ + CellSpan / 2;
                    Columns = Math.max(2, Math.round((Coverage.MaximumX - Coverage.MinimumX) / CellSpan));
                    Rows = Math.max(2, Math.round((Coverage.MaximumZ - Coverage.MinimumZ) / CellSpan));
                }
                else
                {
                    OriginX = Math.floor(Coverage.MinimumX / CellSpan) * CellSpan;
                    OriginZ = Math.floor(Coverage.MinimumZ / CellSpan) * CellSpan;
                    Columns = Math.max(2, Math.ceil((Coverage.MaximumX - OriginX) / CellSpan) + 2);
                    Rows = Math.max(2, Math.ceil((Coverage.MaximumZ - OriginZ) / CellSpan) + 2);
                }
                if (Columns * Rows <= 48000 || View.Mode === 'Camera') break;
                CellSpan *= 2;
            }
            const Count = Columns * Rows;
            if (Lattice.Vectors.length < Count * 3)
            {
                Lattice.Vectors = new Float32Array(Count * 3);
                Lattice.Quantity = new Float32Array(Count);
                Lattice.Inside = new Uint8Array(Count);
            }
            const Contribution = Options.Contribution, SliceY = Options.SliceHeight;
            const Vectors = Lattice.Vectors, Inside = Lattice.Inside;
            for (let Row = 0; Row < Rows; ++Row)
            {
                const Z = OriginZ + Row * CellSpan;
                for (let Column = 0; Column < Columns; ++Column)
                {
                    const X = OriginX + Column * CellSpan;
                    const Index = Row * Columns + Column;
                    Probe[0] = Probe[1] = Probe[2] = 0;
                    const Within = Specification.Sample(Contribution, X, SliceY, Z, Time, CellSpan, Probe) !== false;
                    Inside[Index] = Within ? 1 : 0;
                    Vectors[Index * 3] = Probe[0];
                    Vectors[Index * 3 + 1] = Probe[1];
                    Vectors[Index * 3 + 2] = Probe[2];
                }
            }
            Object.assign(Lattice, { Columns, Rows, OriginX, OriginZ, CellSpan, Time });
            DeriveQuantity();
            Statistics.Samples = Count;
            Statistics.Milliseconds = performance.now() - Started;
            Statistics.CellSpan = CellSpan;
            Statistics.Columns = Columns;
            Statistics.Rows = Rows;
            Statistics.Refreshes++;
        }

        function DeriveQuantity()
        {
            const { Columns, Rows, CellSpan, Vectors, Quantity, Inside } = Lattice;
            const Mode = Options.Contribution === 'Damp' ? 'Speed' : Options.Quantity;
            const Inverse2 = 1 / (2 * CellSpan);
            let Peak = 0;
            for (let Row = 0; Row < Rows; ++Row)
            {
                for (let Column = 0; Column < Columns; ++Column)
                {
                    const Index = Row * Columns + Column;
                    const VX = Vectors[Index * 3], VY = Vectors[Index * 3 + 1], VZ = Vectors[Index * 3 + 2];
                    let Reading;
                    if (Options.Contribution === 'Damp') Reading = VX;
                    else if (Mode === 'Speed') Reading = Math.sqrt(VX * VX + VZ * VZ);
                    else if (Mode === 'AlongX') Reading = VX;
                    else if (Mode === 'AlongZ') Reading = VZ;
                    else if (Mode === 'Vertical') Reading = VY;
                    else
                    {
                        const Left = Column > 0 ? Index - 1 : Index, Right = Column < Columns - 1 ? Index + 1 : Index;
                        const Up = Row > 0 ? Index - Columns : Index, Down = Row < Rows - 1 ? Index + Columns : Index;
                        const SpanX = (Right - Left) || 1, SpanZ = ((Down - Up) / Columns) || 1;
                        const ScaleX = 2 / SpanX * Inverse2, ScaleZ = 2 / SpanZ * Inverse2;
                        const DvxDz = (Vectors[Down * 3] - Vectors[Up * 3]) * ScaleZ;
                        const DvzDx = (Vectors[Right * 3 + 2] - Vectors[Left * 3 + 2]) * ScaleX;
                        if (Mode === 'Vorticity') Reading = DvxDz - DvzDx;
                        else Reading = (Vectors[Right * 3] - Vectors[Left * 3]) * ScaleX + (Vectors[Down * 3 + 2] - Vectors[Up * 3 + 2]) * ScaleZ;
                    }
                    Quantity[Index] = Reading;
                    if (Inside[Index]) Peak = Math.max(Peak, Math.abs(Reading));
                }
            }
            Statistics.Peak = Peak;
            Lattice.Unit = ResolveUnit(Options.Contribution, Mode);
            // 📝 The rising / sinking markers compare each cell with the view's median vertical reading, so a uniform pull
            //    (gravity everywhere) marks nothing while a lift column or a downdraft still stands out.
            const Strides = [];
            const Count = Columns * Rows, Stride = Math.max(1, Math.floor(Count / 400));
            for (let Index = 0; Index < Count; Index += Stride) if (Inside[Index]) Strides.push(Vectors[Index * 3 + 1]);
            Strides.sort((A, B) => A - B);
            Lattice.MedianVertical = Strides.length ? Strides[Strides.length >> 1] : 0;
            // 📝 Automatic scale: the 98th percentile of |reading| (a 256-bin histogram), eased between refreshes and snapped
            //    when the view jumps by more than ×4, so the legend does not flicker while the field animates.
            let Target = Options.FixedScale;
            if (Options.ScaleMode === 'Automatic')
            {
                const Bins = new Uint32Array(256);
                let Counted = 0;
                const Count = Columns * Rows;
                for (let Index = 0; Index < Count; ++Index)
                {
                    if (!Inside[Index]) continue;
                    Bins[Math.min(255, Math.floor(Math.abs(Quantity[Index]) / (Peak || 1) * 255))]++;
                    Counted++;
                }
                let Running = 0, Bin = 255;
                for (let Index = 0; Index < 256; ++Index) { Running += Bins[Index]; if (Running >= Counted * 0.98) { Bin = Index; break; } }
                Target = Math.max(Peak * (Bin + 1) / 256, Peak * 0.25, 1e-6);
                if (Peak <= 1e-9) Target = ScaleReading || 1;
            }
            if (!ScaleReading || Options.ScaleMode !== 'Automatic' || Target > ScaleReading * 4 || Target < ScaleReading / 4) ScaleReading = Target;
            else ScaleReading += (Target - ScaleReading) * 0.3;
            Statistics.Scale = ScaleReading;
        }

        // 📝 Bilinear read of the sampled lattice: arrows and tracers use the grid the heatmap shows, so the three agree.
        function Interpolate(X, Z, Out)
        {
            const { Columns, Rows, OriginX, OriginZ, CellSpan, Vectors, Inside } = Lattice;
            const U = (X - OriginX) / CellSpan, V = (Z - OriginZ) / CellSpan;
            if (U < 0 || V < 0 || U > Columns - 1 || V > Rows - 1) return false;
            const Column = Math.min(Columns - 2, Math.floor(U)), Row = Math.min(Rows - 2, Math.floor(V));
            const S = U - Column, T = V - Row;
            const I00 = Row * Columns + Column, I10 = I00 + 1, I01 = I00 + Columns, I11 = I01 + 1;
            if (!(Inside[I00] | Inside[I10] | Inside[I01] | Inside[I11])) return false;
            for (let Channel = 0; Channel < 3; ++Channel)
            {
                Out[Channel] = (Vectors[I00 * 3 + Channel] * (1 - S) + Vectors[I10 * 3 + Channel] * S) * (1 - T)
                             + (Vectors[I01 * 3 + Channel] * (1 - S) + Vectors[I11 * 3 + Channel] * S) * T;
            }
            return true;
        }

        //--------------------------------------------------------------------------------------------------------------------
        //                                                   HEATMAP
        //--------------------------------------------------------------------------------------------------------------------

        function ColourLattice()
        {
            const { Columns, Rows, Quantity, Inside } = Lattice;
            const Canvas2 = Lattice.ImageCanvas;
            if (Canvas2.width !== Columns || Canvas2.height !== Rows) { Canvas2.width = Columns; Canvas2.height = Rows; Lattice.Image = null; }
            const Pen = Canvas2.getContext('2d');
            if (!Lattice.Image) Lattice.Image = Pen.createImageData(Columns, Rows);
            const Pixels = Lattice.Image.data;
            const Signed = Options.Contribution !== 'Damp' && Quantities[Options.Quantity] && Quantities[Options.Quantity].Signed;
            const InverseScale = 1 / (ScaleReading || 1);
            for (let Index = 0, Count = Columns * Rows; Index < Count; ++Index)
            {
                const Share = Signed ? 0.5 + 0.5 * Quantity[Index] * InverseScale : Quantity[Index] * InverseScale;
                const Lut = Math.max(0, Math.min(255, Math.round(Share * 255))) * 3;
                Pixels[Index * 4] = ColourLut[Lut];
                Pixels[Index * 4 + 1] = ColourLut[Lut + 1];
                Pixels[Index * 4 + 2] = ColourLut[Lut + 2];
                Pixels[Index * 4 + 3] = Inside[Index] ? 255 : 0;
            }
            Pen.putImageData(Lattice.Image, 0, 0);
        }

        function RenderHeatmap()
        {
            if (!Options.Heatmap) return;
            const { Columns, Rows, OriginX, OriginZ, CellSpan } = Lattice;
            if (View.Mode !== 'Camera')
            {
                // 📝 Image pixel i is centred on lattice point i: world X = OriginX + (u − 0.5) · CellSpan.
                const Affine = ResolveAffine();
                const WorldX = OriginX - CellSpan / 2 - View.CentreX, WorldZ = OriginZ - CellSpan / 2 - View.CentreZ;
                Context.save();
                Context.globalAlpha = Options.Opacity;
                Context.imageSmoothingEnabled = Options.HeatmapStyle !== 'Cells';
                Context.imageSmoothingQuality = 'high';
                Context.setTransform(Ratio * Affine.A * CellSpan, Ratio * Affine.C * CellSpan, Ratio * Affine.B * CellSpan, Ratio * Affine.D * CellSpan,
                                     Ratio * (Width / 2 + Affine.A * WorldX + Affine.B * WorldZ), Ratio * (Height / 2 + Affine.C * WorldX + Affine.D * WorldZ));
                Context.drawImage(Lattice.ImageCanvas, 0, 0);
                Context.restore();
                return;
            }
            // 📝 Perspective: each lattice point fills its projected cell, opaque, on an offscreen layer (no seams where cells
            //    overlap), and the layer is laid over the 3D view at the chosen opacity.
            if (Offscreen.width !== Canvas.width || Offscreen.height !== Canvas.height) { Offscreen.width = Canvas.width; Offscreen.height = Canvas.height; }
            const Pen = Offscreen.getContext('2d');
            Pen.setTransform(1, 0, 0, 1, 0, 0);
            Pen.clearRect(0, 0, Offscreen.width, Offscreen.height);
            Pen.setTransform(Ratio, 0, 0, Ratio, 0, 0);
            const Corners = new Float32Array((Columns + 1) * (Rows + 1) * 2);
            const Visible = new Uint8Array((Columns + 1) * (Rows + 1));
            for (let Row = 0; Row <= Rows; ++Row)
            {
                for (let Column = 0; Column <= Columns; ++Column)
                {
                    const Point = Project(OriginX + (Column - 0.5) * CellSpan, Options.SliceHeight, OriginZ + (Row - 0.5) * CellSpan);
                    const Index = Row * (Columns + 1) + Column;
                    if (Point) { Corners[Index * 2] = Point[0]; Corners[Index * 2 + 1] = Point[1]; Visible[Index] = 1; }
                }
            }
            const Pixels = Lattice.Image.data;
            for (let Row = 0; Row < Rows; ++Row)
            {
                for (let Column = 0; Column < Columns; ++Column)
                {
                    const Index = Row * Columns + Column;
                    if (!Pixels[Index * 4 + 3]) continue;
                    const C0 = Row * (Columns + 1) + Column, C1 = C0 + 1, C2 = C0 + Columns + 2, C3 = C0 + Columns + 1;
                    if (!(Visible[C0] && Visible[C1] && Visible[C2] && Visible[C3])) continue;
                    const CentreX = (Corners[C0 * 2] + Corners[C1 * 2] + Corners[C2 * 2] + Corners[C3 * 2]) / 4;
                    const CentreY = (Corners[C0 * 2 + 1] + Corners[C1 * 2 + 1] + Corners[C2 * 2 + 1] + Corners[C3 * 2 + 1]) / 4;
                    const Grow = (Corner, Axis) => { const Reading = Corners[Corner * 2 + Axis], Centre = Axis ? CentreY : CentreX; return Reading + Math.sign(Reading - Centre) * 0.6; };
                    Pen.fillStyle = `rgb(${Pixels[Index * 4]},${Pixels[Index * 4 + 1]},${Pixels[Index * 4 + 2]})`;
                    Pen.beginPath();
                    Pen.moveTo(Grow(C0, 0), Grow(C0, 1));
                    Pen.lineTo(Grow(C1, 0), Grow(C1, 1));
                    Pen.lineTo(Grow(C2, 0), Grow(C2, 1));
                    Pen.lineTo(Grow(C3, 0), Grow(C3, 1));
                    Pen.fill();
                }
            }
            Context.save();
            Context.setTransform(1, 0, 0, 1, 0, 0);
            Context.globalAlpha = Options.Opacity;
            Context.drawImage(Offscreen, 0, 0);
            Context.restore();
        }

        //--------------------------------------------------------------------------------------------------------------------
        //                                                GROUND AND CASCADES
        //--------------------------------------------------------------------------------------------------------------------

        function StrokeWorldPolygon(Points, Close)
        {
            let Started = false;
            Context.beginPath();
            for (const [X, Y, Z] of Points)
            {
                const Point = Project(X, Y, Z);
                if (!Point) { Started = false; continue; }
                if (!Started) { Context.moveTo(Point[0], Point[1]); Started = true; }
                else Context.lineTo(Point[0], Point[1]);
            }
            if (Close) Context.closePath();
            Context.stroke();
        }

        function RenderGroundGrid()
        {
            if (!Options.ShowGroundGrid || View.Mode === 'Camera') return;
            const Coverage = ResolveCoverage();
            const Step = NiceLength(View.Span / 6);
            const SliceY = Options.SliceHeight;
            Context.save();
            Context.strokeStyle = 'rgba(255,255,255,0.05)';
            Context.lineWidth = 1;
            for (let X = Math.ceil(Coverage.MinimumX / Step) * Step; X <= Coverage.MaximumX; X += Step)
                StrokeWorldPolygon([[X, SliceY, Coverage.MinimumZ], [X, SliceY, Coverage.MaximumZ]], false);
            for (let Z = Math.ceil(Coverage.MinimumZ / Step) * Step; Z <= Coverage.MaximumZ; Z += Step)
                StrokeWorldPolygon([[Coverage.MinimumX, SliceY, Z], [Coverage.MaximumX, SliceY, Z]], false);
            if (Options.Extent)
            {
                const E = Options.Extent;
                Context.strokeStyle = 'rgba(159,180,214,0.35)';
                Context.setLineDash([6, 6]);
                StrokeWorldPolygon([[E.MinimumX, SliceY, E.MinimumZ], [E.MaximumX, SliceY, E.MinimumZ], [E.MaximumX, SliceY, E.MaximumZ], [E.MinimumX, SliceY, E.MaximumZ]], true);
            }
            if (View.Mode === 'Oblique')
            {
                // 📝 The plate's rim and a faint ground shadow below it, so the slice height reads.
                Context.setLineDash([]);
                Context.strokeStyle = 'rgba(223,231,243,0.55)';
                Context.lineWidth = 1.2;
                StrokeWorldPolygon([[Coverage.MinimumX, SliceY, Coverage.MinimumZ], [Coverage.MaximumX, SliceY, Coverage.MinimumZ],
                                    [Coverage.MaximumX, SliceY, Coverage.MaximumZ], [Coverage.MinimumX, SliceY, Coverage.MaximumZ]], true);
            }
            Context.restore();
        }

        function RenderCascades()
        {
            if (!Options.ShowCascades || !Options.Cascades.length || View.Mode === 'Camera') return;
            const [FocusX, FocusZ] = Options.CascadeFocus;
            const SliceY = Options.SliceHeight;
            Context.save();
            Options.Cascades.forEach((Cascade, Position) =>
            {
                const Half = Cascade.Extent / 2;
                const Strength = 0.75 - Position * 0.12;
                Context.strokeStyle = `rgba(223,231,243,${Math.max(0.25, Strength)})`;
                Context.lineWidth = 1.2;
                Context.setLineDash(Position === 0 ? [] : [7, 5]);
                StrokeWorldPolygon([[FocusX - Half, SliceY, FocusZ - Half], [FocusX + Half, SliceY, FocusZ - Half], [FocusX + Half, SliceY, FocusZ + Half], [FocusX - Half, SliceY, FocusZ + Half]], true);
                const Corner = Project(FocusX - Half, SliceY, FocusZ - Half);
                const Across = Cascade.Extent * (View.Mode === 'Camera' ? 0 : ResolveAffine().PixelsPerMetre);
                if (Corner && Across >= 90 && Corner[0] > -200 && Corner[1] > -40 && Corner[0] < Width && Corner[1] < Height)
                {
                    const Label = Cascade.Label || `C${Position} · ${FormatDistance(Cascade.Extent)} · ${FormatDistance(Cascade.Cell)} cells`;
                    const LabelX = Math.max(8, Corner[0] + 6), LabelY = Math.max(16, Corner[1] + 15);
                    Context.font = '500 10.5px "DM Sans", sans-serif';
                    const Measure = Context.measureText(Label).width;
                    Context.fillStyle = 'rgba(8,10,14,0.78)';
                    Context.fillRect(LabelX - 5, LabelY - 11, Measure + 10, 16);
                    Context.fillStyle = '#dfe7f3';
                    Context.fillText(Label, LabelX, LabelY);
                }
            });
            const Focus = Project(FocusX, SliceY, FocusZ);
            if (Focus)
            {
                Context.setLineDash([]);
                Context.strokeStyle = '#ffffff';
                Context.beginPath();
                Context.arc(Focus[0], Focus[1], 4, 0, Math.PI * 2);
                Context.stroke();
            }
            Context.restore();
        }

        //--------------------------------------------------------------------------------------------------------------------
        //                                                    ARROWS
        //--------------------------------------------------------------------------------------------------------------------

        function ResolveArrowSpan()
        {
            if (View.Mode === 'Camera')
            {
                const Coverage = ResolveCoverage();
                const Across = Math.max(Coverage.MaximumX - Coverage.MinimumX, Coverage.MaximumZ - Coverage.MinimumZ);
                return Across / Math.max(4, Math.round(Options.CameraCells / 3));
            }
            const Wanted = View.Span / Math.max(1, Width) * Options.ArrowSpacing;
            return Lattice.CellSpan * Math.pow(2, Math.max(0, Math.ceil(Math.log2(Wanted / Lattice.CellSpan))));
        }

        // 📝 eos arrows: white, 1 px, 5 px heads at ±0.55 rad. A cell whose vertical flow dominates shows ⊙ (rising) or ⊗ (sinking).
        function RenderArrows()
        {
            if (!Options.Arrows || Options.Contribution === 'Damp') return;
            const ArrowSpan = ResolveArrowSpan();
            Statistics.ArrowSpan = ArrowSpan;
            const Coverage = ResolveCoverage();
            const SliceY = Options.SliceHeight;
            const Scale = ScaleReading || 1;
            const Vector = new Float64Array(3);
            const Strokes = new Path2D(), Rising = new Path2D(), Sinking = new Path2D();
            const StartX = View.Mode === 'Camera' ? Coverage.MinimumX + ArrowSpan / 2 : Math.floor(Coverage.MinimumX / ArrowSpan) * ArrowSpan + ArrowSpan / 2;
            const StartZ = View.Mode === 'Camera' ? Coverage.MinimumZ + ArrowSpan / 2 : Math.floor(Coverage.MinimumZ / ArrowSpan) * ArrowSpan + ArrowSpan / 2;
            let Drawn = 0;
            for (let Z = StartZ; Z <= Coverage.MaximumZ && Drawn < 4000; Z += ArrowSpan)
            {
                for (let X = StartX; X <= Coverage.MaximumX && Drawn < 4000; X += ArrowSpan)
                {
                    if (!Interpolate(X, Z, Vector)) continue;
                    const Horizontal = Math.hypot(Vector[0], Vector[2]);
                    const Magnitude = Math.hypot(Horizontal, Vector[1] - Lattice.MedianVertical);
                    if (Magnitude < Scale * (Options.QuietCutoff ?? 0.025)) continue;
                    const Tail = Project(X, SliceY, Z);
                    if (!Tail || Tail[0] < -40 || Tail[1] < -40 || Tail[0] > Width + 40 || Tail[1] > Height + 40) continue;
                    const Lifted = Vector[1] - Lattice.MedianVertical;
                    if (Math.abs(Lifted) > Horizontal * 1.4 && Math.abs(Lifted) > Scale * 0.12)
                    {
                        const Target = Lifted > 0 ? Rising : Sinking;
                        Target.moveTo(Tail[0] + 4, Tail[1]);
                        Target.arc(Tail[0], Tail[1], 4, 0, Math.PI * 2);
                        Drawn++;
                        continue;
                    }
                    const Length = Math.min(1, Horizontal / Scale) * ArrowSpan * 0.88 * Options.ArrowWeight;
                    if (Horizontal < 1e-9) continue;
                    const Tip = Project(X + Vector[0] / Horizontal * Length, SliceY, Z + Vector[2] / Horizontal * Length);
                    if (!Tip) continue;
                    const Angle = Math.atan2(Tip[1] - Tail[1], Tip[0] - Tail[0]);
                    const Pixels = Math.hypot(Tip[0] - Tail[0], Tip[1] - Tail[1]);
                    if (Pixels < 1.5) continue;
                    const Head = Math.min(5, Pixels * 0.45);
                    Strokes.moveTo(Tail[0], Tail[1]);
                    Strokes.lineTo(Tip[0], Tip[1]);
                    Strokes.moveTo(Tip[0] - Math.cos(Angle - 0.55) * Head, Tip[1] - Math.sin(Angle - 0.55) * Head);
                    Strokes.lineTo(Tip[0], Tip[1]);
                    Strokes.lineTo(Tip[0] - Math.cos(Angle + 0.55) * Head, Tip[1] - Math.sin(Angle + 0.55) * Head);
                    Drawn++;
                }
            }
            Context.save();
            Context.lineCap = 'round';
            Context.lineJoin = 'round';
            Context.strokeStyle = 'rgba(0,0,0,0.35)';
            Context.lineWidth = 2.6;
            Context.stroke(Strokes);
            Context.strokeStyle = Options.Heatmap
                ? 'rgba(239,255,255,0.82)'
                : Options.Contribution === 'Accelerate' ? 'rgba(232,194,122,0.88)' : 'rgba(143,211,196,0.88)';
            Context.lineWidth = 1.15;
            Context.stroke(Strokes);
            Context.lineWidth = 1.2;
            Context.stroke(Rising);
            Context.stroke(Sinking);
            Context.restore();
            // ⊙ dots and ⊗ crosses inside the circles.
            Context.save();
            Context.fillStyle = 'rgba(239,255,255,0.9)';
            Context.strokeStyle = 'rgba(239,255,255,0.9)';
            Context.lineWidth = 1;
            const Reseed = new Float64Array(3);
            for (let Z = StartZ; Z <= Coverage.MaximumZ; Z += ArrowSpan)
            {
                for (let X = StartX; X <= Coverage.MaximumX; X += ArrowSpan)
                {
                    if (!Interpolate(X, Z, Reseed)) continue;
                    const Horizontal = Math.hypot(Reseed[0], Reseed[2]);
                    const Lifted = Reseed[1] - Lattice.MedianVertical;
                    if (!(Math.abs(Lifted) > Horizontal * 1.4 && Math.abs(Lifted) > Scale * 0.12)) continue;
                    const Point = Project(X, SliceY, Z);
                    if (!Point) continue;
                    if (Lifted > 0) { Context.beginPath(); Context.arc(Point[0], Point[1], 1.3, 0, Math.PI * 2); Context.fill(); }
                    else
                    {
                        Context.beginPath();
                        Context.moveTo(Point[0] - 2.4, Point[1] - 2.4); Context.lineTo(Point[0] + 2.4, Point[1] + 2.4);
                        Context.moveTo(Point[0] + 2.4, Point[1] - 2.4); Context.lineTo(Point[0] - 2.4, Point[1] + 2.4);
                        Context.stroke();
                    }
                }
            }
            Context.restore();
        }

        //--------------------------------------------------------------------------------------------------------------------
        //                                                    TRACERS
        //--------------------------------------------------------------------------------------------------------------------

        // 📝 Tracers ride the sampled lattice at a display rate (a full-scale reading crosses 7 % of the view per second), so
        //    they read the same at 100 m and at 100 km; they show direction and structure, not real-time travel.
        const TrailLength = 10;

        function ReseedTracer(Tracer, Coverage, Scattered)
        {
            Tracer.X = Coverage.MinimumX + Math.random() * (Coverage.MaximumX - Coverage.MinimumX);
            Tracer.Z = Coverage.MinimumZ + Math.random() * (Coverage.MaximumZ - Coverage.MinimumZ);
            Tracer.Age = Scattered ? Math.random() * 2.5 : 0;
            Tracer.Life = 2.2 + Math.random() * 2.4;
            Tracer.Count = 0;
            Tracer.Head = 0;
        }

        function AdvanceTracers(Delta)
        {
            if (!Options.Tracers || Options.Contribution === 'Damp') return;
            const Coverage = ResolveCoverage();
            while (Tracers.length < Options.TracerCount) { const Tracer = { Trail: new Float64Array(TrailLength * 2) }; ReseedTracer(Tracer, Coverage, true); Tracers.push(Tracer); }
            if (Tracers.length > Options.TracerCount) Tracers.length = Options.TracerCount;
            const Across = Math.max(Coverage.MaximumX - Coverage.MinimumX, Coverage.MaximumZ - Coverage.MinimumZ);
            const Rate = (View.Mode === 'Camera' ? Across : View.Span) * 0.07 / (ScaleReading || 1);
            const Vector = new Float64Array(3);
            for (const Tracer of Tracers)
            {
                Tracer.Age += Delta;
                const Outside = Tracer.X < Coverage.MinimumX || Tracer.X > Coverage.MaximumX || Tracer.Z < Coverage.MinimumZ || Tracer.Z > Coverage.MaximumZ;
                if (Tracer.Age > Tracer.Life || Outside || !Interpolate(Tracer.X, Tracer.Z, Vector)) { ReseedTracer(Tracer, Coverage, false); continue; }
                const Horizontal = Math.hypot(Vector[0], Vector[2]);
                if (Horizontal < (ScaleReading || 1) * 0.01 && Tracer.Age > 0.6) { ReseedTracer(Tracer, Coverage, false); continue; }
                Tracer.X += Vector[0] * Rate * Delta;
                Tracer.Z += Vector[2] * Rate * Delta;
                Tracer.Trail[Tracer.Head * 2] = Tracer.X;
                Tracer.Trail[Tracer.Head * 2 + 1] = Tracer.Z;
                Tracer.Head = (Tracer.Head + 1) % TrailLength;
                Tracer.Count = Math.min(TrailLength, Tracer.Count + 1);
            }
        }

        function RenderTracers()
        {
            if (!Options.Tracers || Options.Contribution === 'Damp') return;
            const SliceY = Options.SliceHeight;
            Context.save();
            Context.lineCap = 'round';
            for (let Segment = 1; Segment < TrailLength; ++Segment)
            {
                const Path = new Path2D();
                for (const Tracer of Tracers)
                {
                    if (Tracer.Count <= Segment) continue;
                    const Newer = (Tracer.Head - Segment + TrailLength) % TrailLength;
                    const Older = (Newer - 1 + TrailLength) % TrailLength;
                    const From = Project(Tracer.Trail[Older * 2], SliceY, Tracer.Trail[Older * 2 + 1]);
                    const To = Project(Tracer.Trail[Newer * 2], SliceY, Tracer.Trail[Newer * 2 + 1]);
                    if (!From || !To) continue;
                    if (Math.abs(From[0] - To[0]) + Math.abs(From[1] - To[1]) > 80) continue;
                    Path.moveTo(From[0], From[1]);
                    Path.lineTo(To[0], To[1]);
                }
                const Fade = 1 - Segment / TrailLength;
                Context.strokeStyle = `rgba(255,255,255,${(0.55 * Fade).toFixed(3)})`;
                Context.lineWidth = 1.5 * (1 - Segment / (TrailLength * 1.4));
                Context.stroke(Path);
            }
            Context.restore();
        }

        //--------------------------------------------------------------------------------------------------------------------
        //                                          RK2 FLOW LINES (STREAMLINES)
        //--------------------------------------------------------------------------------------------------------------------

        // 📝 Anti-clutter smooth RK2 streamlines: seeded on a world-snapped sparse grid only inside active field regions,
        //    prevented from piling up by screen-space occupancy bins, and animated with subtle downstream phase pulses.
        function BuildFlowLines()
        {
            Streamlines.length = 0;
            Statistics.Streamlines = 0;
            if (!Options.FlowLines || Options.Contribution === 'Damp') return;
            const Coverage = ResolveCoverage();
            const Scale = ScaleReading || 1;
            const MinMag = Scale * Math.max(0.035, Options.QuietCutoff ?? 0.025);
            const CellPx = Math.max(22, Options.FlowLineSpacing || 46);
            const SeedSpan = View.Mode === 'Camera'
                ? Math.max(Coverage.MaximumX - Coverage.MinimumX, Coverage.MaximumZ - Coverage.MinimumZ) / 11
                : Math.max(Lattice.CellSpan * 2, (View.Span / Math.max(1, Width)) * CellPx);
            const BinPx = Math.max(14, CellPx * 0.56);
            const BinCols = Math.max(4, Math.ceil(Width / BinPx) + 2);
            const BinRows = Math.max(4, Math.ceil(Height / BinPx) + 2);
            const Occupied = new Uint8Array(BinCols * BinRows);

            const MarkScreen = (X, Z) =>
            {
                const P = Project(X, Options.SliceHeight, Z);
                if (!P || P[0] < -12 || P[1] < -12 || P[0] > Width + 12 || P[1] > Height + 12) return false;
                const C = Math.floor(P[0] / BinPx), R = Math.floor(P[1] / BinPx);
                if (C < 0 || R < 0 || C >= BinCols || R >= BinRows) return false;
                Occupied[R * BinCols + C] = 1;
                return true;
            };
            const IsFreeScreen = (X, Z) =>
            {
                const P = Project(X, Options.SliceHeight, Z);
                if (!P || P[0] < 0 || P[1] < 0 || P[0] > Width || P[1] > Height) return false;
                const C = Math.floor(P[0] / BinPx), R = Math.floor(P[1] / BinPx);
                if (C < 0 || R < 0 || C >= BinCols || R >= BinRows) return false;
                return !Occupied[R * BinCols + C];
            };

            const V1 = new Float64Array(3), V2 = new Float64Array(3);
            const MaxHalfSteps = Math.max(6, Math.round((Options.FlowLineLength || 22) * 0.55));
            const WorldStep = View.Mode === 'Camera'
                ? Math.max(Coverage.MaximumX - Coverage.MinimumX, Coverage.MaximumZ - Coverage.MinimumZ) / 72
                : Math.max(Lattice.CellSpan * 0.65, (View.Span / Math.max(1, Width)) * 8);

            const TraceDir = (StartX, StartZ, Sign, OwnBins) =>
            {
                const Pts = [];
                let X = StartX, Z = StartZ;
                let MagSum = 0;
                for (let Step = 0; Step < MaxHalfSteps; ++Step)
                {
                    if (!Interpolate(X, Z, V1)) break;
                    const H1 = Math.hypot(V1[0], V1[2]);
                    if (H1 < MinMag) break;
                    const DX1 = (V1[0] / H1) * Sign, DZ1 = (V1[2] / H1) * Sign;
                    const MX = X + DX1 * WorldStep * 0.5, MZ = Z + DZ1 * WorldStep * 0.5;
                    if (!Interpolate(MX, MZ, V2)) break;
                    const H2 = Math.hypot(V2[0], V2[2]);
                    if (H2 < MinMag) break;
                    const NX = X + (V2[0] / H2) * Sign * WorldStep;
                    const NZ = Z + (V2[2] / H2) * Sign * WorldStep;
                    if (NX < Coverage.MinimumX || NX > Coverage.MaximumX || NZ < Coverage.MinimumZ || NZ > Coverage.MaximumZ) break;
                    const P = Project(NX, Options.SliceHeight, NZ);
                    if (!P || P[0] < -20 || P[1] < -20 || P[0] > Width + 20 || P[1] > Height + 20) break;
                    const C = Math.floor(P[0] / BinPx), R = Math.floor(P[1] / BinPx);
                    if (C >= 0 && R >= 0 && C < BinCols && R < BinRows)
                    {
                        const Key = R * BinCols + C;
                        if (Occupied[Key] && !OwnBins.has(Key)) break;
                        OwnBins.add(Key);
                    }
                    X = NX; Z = NZ;
                    MagSum += H2;
                    Pts.push([X, Z]);
                }
                return { Pts, AvgMag: Pts.length ? MagSum / Pts.length : 0 };
            };

            const StartX = Math.floor(Coverage.MinimumX / SeedSpan) * SeedSpan + SeedSpan * 0.5;
            const StartZ = Math.floor(Coverage.MinimumZ / SeedSpan) * SeedSpan + SeedSpan * 0.5;
            let RowIdx = 0;
            for (let Z = StartZ; Z <= Coverage.MaximumZ && Streamlines.length < 360; Z += SeedSpan, ++RowIdx)
            {
                const Stagger = (RowIdx & 1) ? SeedSpan * 0.5 : 0;
                for (let X = StartX + Stagger; X <= Coverage.MaximumX && Streamlines.length < 360; X += SeedSpan)
                {
                    if (!Interpolate(X, Z, V1)) continue;
                    const H0 = Math.hypot(V1[0], V1[2]);
                    if (H0 < MinMag) continue;
                    if (!IsFreeScreen(X, Z)) continue;
                    const Own = new Set();
                    const Back = TraceDir(X, Z, -1, Own);
                    const Fwd = TraceDir(X, Z, +1, Own);
                    const Combined = [...Back.Pts.reverse(), [X, Z], ...Fwd.Pts];
                    if (Combined.length < 3) continue;
                    for (const [PX, PZ] of Combined) MarkScreen(PX, PZ);
                    const AvgMag = (H0 + Back.AvgMag + Fwd.AvgMag) / (1 + (Back.Pts.length ? 1 : 0) + (Fwd.Pts.length ? 1 : 0));
                    Streamlines.push({ Points: Combined, NormMag: Math.min(1.4, AvgMag / Scale), Phase: (Math.abs(Math.sin(X * 0.13 + Z * 0.29))) });
                }
            }
            Statistics.Streamlines = Streamlines.length;
        }

        function HexToRgba(Hex, Alpha)
        {
            const Clean = (Hex || '#8fd3c4').replace('#', '');
            if (Clean.length !== 6) return `rgba(143,211,196,${Alpha})`;
            const Num = parseInt(Clean, 16);
            return `rgba(${(Num >> 16) & 255},${(Num >> 8) & 255},${Num & 255},${Alpha})`;
        }

        function DominantSourceColour(WX, WZ)
        {
            const Default = Options.Contribution === 'Accelerate' ? '#e8c27a' : '#8fd3c4';
            let Best = null, BestScore = 0;
            for (const Src of Options.Sources)
            {
                if (Src.Dim && Options.Contribution !== 'Flow' && Src.Contribution !== Options.Contribution) continue;
                if ((Src.Active ?? 1) <= 0.01) continue;
                const R = Math.max(20, Src.Radius || Math.max(Src.HalfX || 120, Src.HalfZ || 120));
                const D = Math.hypot(WX - (Src.OutlineX ?? Src.X), WZ - (Src.OutlineZ ?? Src.Z));
                if (D > R * 1.05) continue;
                const Score = (1 - D / (R * 1.05)) * (Src.Contribution === Options.Contribution ? 1.4 : 0.7);
                if (Score > BestScore) { BestScore = Score; Best = Src.Colour || null; }
            }
            return Best || Default;
        }

        function RenderFlowLines()
        {
            if (!Options.FlowLines || Options.Contribution === 'Damp' || !Streamlines.length) return;
            const SliceY = Options.SliceHeight;
            const Buckets = new Map();
            const PulseBuckets = new Map();

            for (const Line of Streamlines)
            {
                const Projected = [];
                for (const [WX, WZ] of Line.Points)
                {
                    const P = Project(WX, SliceY, WZ);
                    if (P) Projected.push(P);
                }
                if (Projected.length < 3) continue;

                const Mid = Line.Points[Math.floor(Line.Points.length * 0.5)];
                const Tint = DominantSourceColour(Mid[0], Mid[1]);
                let BasePath = Buckets.get(Tint);
                if (!BasePath) { BasePath = new Path2D(); Buckets.set(Tint, BasePath); }

                BasePath.moveTo(Projected[0][0], Projected[0][1]);
                for (let I = 1; I < Projected.length - 1; ++I)
                {
                    const MX = (Projected[I][0] + Projected[I + 1][0]) * 0.5;
                    const MY = (Projected[I][1] + Projected[I + 1][1]) * 0.5;
                    BasePath.quadraticCurveTo(Projected[I][0], Projected[I][1], MX, MY);
                }
                const Last = Projected[Projected.length - 1];
                BasePath.lineTo(Last[0], Last[1]);

                if (Options.FlowLinePulse)
                {
                    let PulsePath = PulseBuckets.get(Tint);
                    if (!PulsePath) { PulsePath = new Path2D(); PulseBuckets.set(Tint, PulsePath); }
                    const Head = ((Line.Phase + ElapsedDisplay * (0.42 + Line.NormMag * 0.72)) % 1.0);
                    const Span = 0.28;
                    const Count = Projected.length - 1;
                    const StartIdx = Math.max(0, Math.floor((Head - Span) * Count));
                    const EndIdx = Math.min(Count, Math.ceil(Head * Count));
                    if (EndIdx > StartIdx)
                    {
                        PulsePath.moveTo(Projected[StartIdx][0], Projected[StartIdx][1]);
                        for (let I = StartIdx + 1; I <= EndIdx; ++I) PulsePath.lineTo(Projected[I][0], Projected[I][1]);
                    }
                }
            }

            Context.save();
            Context.lineCap = 'round';
            Context.lineJoin = 'round';
            for (const [Tint, BasePath] of Buckets)
            {
                Context.strokeStyle = HexToRgba(Tint, Options.Heatmap ? 0.34 : 0.28);
                Context.lineWidth = 1.3;
                Context.stroke(BasePath);
            }
            if (Options.FlowLinePulse)
            {
                for (const [Tint, PulsePath] of PulseBuckets)
                {
                    Context.shadowColor = Tint;
                    Context.shadowBlur = 7;
                    Context.strokeStyle = HexToRgba(Tint, 0.92);
                    Context.lineWidth = 2.05;
                    Context.stroke(PulsePath);
                }
            }
            Context.restore();
        }

        //--------------------------------------------------------------------------------------------------------------------
        //                                                    SOURCES
        //--------------------------------------------------------------------------------------------------------------------

        const ContributionColours = { Flow: '#8fd3c4', Accelerate: '#e8c27a', Damp: '#b9a3e8' };

        function CircleOutline(X, Y, Z, Radius, Segments)
        {
            const Points = [];
            for (let Index = 0; Index <= Segments; ++Index)
            {
                const Angle = Index / Segments * Math.PI * 2;
                Points.push([X + Math.cos(Angle) * Radius, Y, Z + Math.sin(Angle) * Radius]);
            }
            return Points;
        }

        function BoxOutline(Source, Y)
        {
            const Turn = (Source.Rotation || 0) * Math.PI / 180;
            const Cosine = Math.cos(Turn), Sine = Math.sin(Turn);
            return [[-1, -1], [1, -1], [1, 1], [-1, 1]].map(([U, V]) =>
            {
                const LocalX = U * Source.HalfX, LocalZ = V * Source.HalfZ;
                const CentreX = Source.OutlineX ?? Source.X, CentreZ = Source.OutlineZ ?? Source.Z;
                return [CentreX + LocalX * Cosine - LocalZ * Sine, Y, CentreZ + LocalX * Sine + LocalZ * Cosine];
            });
        }

        // 📝 Source: { Id, X, Y, Z, Radius, Shape ('Circle' | 'Box' | 'Everywhere'), HalfX, HalfZ, Rotation [°], OutlineX/OutlineZ
        //    (a box centred away from the handle), InnerRadius, Height (column), Bearing [°], Contribution, Label, Active (0–1),
        //    Selected, Dim (another contribution than the one shown), Track: [[X, Z], …] }.
        function RenderSources()
        {
            if (!Options.ShowSources) return;
            const SliceY = Options.SliceHeight;
            const PixelsPerMetre = View.Mode === 'Camera' ? 0 : ResolveAffine().PixelsPerMetre;
            const Labels = [];
            Context.save();
            for (const Source of Options.Sources)
            {
                const Colour = Source.Colour || ContributionColours[Source.Contribution] || '#dfe7f3';
                const Active = Source.Active ?? 1;
                const BaseAlpha = (Source.Selected ? 0.98 : 0.42 + 0.44 * Math.min(1, Active)) * (Source.Dim && !Source.Selected ? 0.48 : 1);
                const Centre = Project(Source.X, SliceY, Source.Z);

                // Soft glowing area wash inside finite influence bounds
                if (Centre && Source.Shape !== 'Everywhere' && Active > 0.01)
                {
                    Context.save();
                    Context.globalAlpha = Source.Dim && !Source.Selected ? 0.45 : 1;
                    if (Source.Shape === 'Box')
                    {
                        const Corners = BoxOutline(Source, SliceY).map(([X, Y, Z]) => Project(X, Y, Z)).filter(Boolean);
                        if (Corners.length === 4)
                        {
                            Context.fillStyle = HexToRgba(Colour, Source.Selected ? 0.14 : 0.08);
                            Context.beginPath();
                            Context.moveTo(Corners[0][0], Corners[0][1]);
                            for (let I = 1; I < 4; ++I) Context.lineTo(Corners[I][0], Corners[I][1]);
                            Context.closePath();
                            Context.fill();
                        }
                    }
                    else if (Source.Radius > 0)
                    {
                        const RimPt = Project(Source.X + Source.Radius, SliceY, Source.Z);
                        const ScreenR = RimPt ? Math.hypot(RimPt[0] - Centre[0], RimPt[1] - Centre[1]) : 0;
                        if (ScreenR >= 4 && typeof Context.createRadialGradient === 'function')
                        {
                            const Grad = Context.createRadialGradient(Centre[0], Centre[1], Math.min(4, ScreenR * 0.1), Centre[0], Centre[1], ScreenR);
                            Grad.addColorStop(0, HexToRgba(Colour, Source.Selected ? 0.24 : 0.15));
                            Grad.addColorStop(0.55, HexToRgba(Colour, Source.Selected ? 0.09 : 0.05));
                            Grad.addColorStop(1, HexToRgba(Colour, 0));
                            Context.fillStyle = Grad;
                            Context.beginPath();
                            Context.arc(Centre[0], Centre[1], ScreenR, 0, Math.PI * 2);
                            Context.fill();
                        }
                    }
                    Context.restore();
                }

                Context.globalAlpha = BaseAlpha;
                Context.shadowColor = Colour;
                Context.shadowBlur = Source.Selected ? 20 : 10;
                Context.strokeStyle = Colour;
                Context.lineWidth = Source.Selected ? 2.1 : 1.25;
                Context.setLineDash(Source.Selected ? [] : [5, 5]);
                if (Source.Track && Source.Track.length > 1)
                {
                    Context.save();
                    Context.setLineDash([2, 5]);
                    Context.lineWidth = 1;
                    StrokeWorldPolygon(Source.Track.map(([X, Z]) => [X, SliceY, Z]), false);
                    Context.restore();
                }
                if (Source.Shape === 'Box') StrokeWorldPolygon(BoxOutline(Source, SliceY), true);
                else if (Source.Shape !== 'Everywhere')
                {
                    const OnScreen = PixelsPerMetre ? Source.Radius * PixelsPerMetre : 99;
                    if (OnScreen >= 3) StrokeWorldPolygon(CircleOutline(Source.X, SliceY, Source.Z, Source.Radius, 72), true);
                    if (Source.InnerRadius && (!PixelsPerMetre || Source.InnerRadius * PixelsPerMetre >= 3))
                    {
                        Context.save();
                        Context.globalAlpha *= 0.65;
                        Context.setLineDash([2, 3]);
                        StrokeWorldPolygon(CircleOutline(Source.X, SliceY, Source.Z, Source.InnerRadius, 48), true);
                        Context.restore();
                    }
                }
                if (!Centre) continue;
                if (Source.Bearing !== undefined && Source.Bearing !== null)
                {
                    const Turn = Source.Bearing * Math.PI / 180;
                    const Reach = PixelsPerMetre ? Math.min(Source.Radius * 0.85 || 56 / PixelsPerMetre, 48 / PixelsPerMetre) : Math.max(0.6, (Source.Radius || 1) * 0.5);
                    const Tip = Project(Source.X + Math.sin(Turn) * Reach, SliceY, Source.Z - Math.cos(Turn) * Reach);
                    if (Tip)
                    {
                        const Angle = Math.atan2(Tip[1] - Centre[1], Tip[0] - Centre[0]);
                        Context.save();
                        Context.setLineDash([]);
                        Context.lineWidth = 1.8;
                        Context.beginPath();
                        Context.moveTo(Centre[0], Centre[1]);
                        Context.lineTo(Tip[0], Tip[1]);
                        Context.moveTo(Tip[0] - Math.cos(Angle - 0.5) * 8, Tip[1] - Math.sin(Angle - 0.5) * 8);
                        Context.lineTo(Tip[0], Tip[1]);
                        Context.lineTo(Tip[0] - Math.cos(Angle + 0.5) * 8, Tip[1] - Math.sin(Angle + 0.5) * 8);
                        Context.stroke();
                        if (Source.Selected && View.Mode !== 'Camera')
                        {
                            Context.fillStyle = '#ffffff';
                            Context.beginPath();
                            Context.arc(Tip[0], Tip[1], 4.5, 0, Math.PI * 2);
                            Context.fill();
                        }
                        Context.restore();
                    }
                }
                if (Source.Selected && Source.Shape !== 'Everywhere' && Source.Radius > 0 && View.Mode !== 'Camera')
                {
                    const Rim = Project(Source.X + Source.Radius, SliceY, Source.Z);
                    if (Rim)
                    {
                        Context.save();
                        Context.setLineDash([]);
                        Context.fillStyle = Colour;
                        Context.strokeStyle = '#0c0e12';
                        Context.lineWidth = 1.5;
                        Context.beginPath();
                        Context.arc(Rim[0], Rim[1], 5.2, 0, Math.PI * 2);
                        Context.fill();
                        Context.stroke();
                        Context.restore();
                    }
                }
                Context.setLineDash([]);
                Context.fillStyle = Colour;
                Context.beginPath();
                Context.arc(Centre[0], Centre[1], Source.Selected ? 5.5 : 4.2, 0, Math.PI * 2);
                Context.fill();
                if (Source.Selected)
                {
                    Context.strokeStyle = '#ffffff';
                    Context.lineWidth = 1.6;
                    Context.beginPath();
                    Context.arc(Centre[0], Centre[1], 9, 0, Math.PI * 2);
                    Context.stroke();
                }
                // 📝 At large spans or when MinimalLabels is enabled, keep clutter away by showing labels only on selected/active major sources.
                const Tiny = PixelsPerMetre && Source.Shape !== 'Everywhere' && Math.max(Source.Radius || 0, Source.HalfX || 0, Source.HalfZ || 0) * PixelsPerMetre < 6;
                const AllowLabel = Options.MinimalLabels ? Source.Selected : (!Tiny || Source.Selected);
                if (Options.ShowLabels && Source.Label && AllowLabel && (!Source.Dim || Source.Selected)) Labels.push([Source, Centre, Colour]);
            }
            Context.globalAlpha = 1;
            Context.shadowBlur = 0;
            Context.font = '500 11px "DM Sans", sans-serif';
            const Placed = [];
            for (const [Source, Centre, Colour] of Labels)
            {
                const Text = Source.Label;
                const Measure = Context.measureText(Text).width;
                let X = Centre[0] + 10, Y = Centre[1] - 9;
                // 📝 Labels step down until they no longer overlap one already placed (crowded large-scale views).
                for (let Attempt = 0; Attempt < 6 && Placed.some(([A, B, C, D]) => X < C && X + Measure + 18 > A && Y - 12 < D && Y + 5 > B); ++Attempt) Y += 17;
                Placed.push([X - 5, Y - 12, X + Measure + 16, Y + 5]);
                Context.fillStyle = Source.Selected ? 'rgba(22,24,28,0.94)' : 'rgba(10,12,16,0.78)';
                Context.strokeStyle = Source.Selected ? Colour : HexToRgba(Colour, 0.35);
                Context.lineWidth = 1;
                Context.beginPath();
                Context.roundRect(X - 6, Y - 12, Measure + 22, 18, 9);
                Context.fill();
                Context.stroke();
                Context.fillStyle = Colour;
                Context.beginPath();
                Context.arc(X + 1.5, Y - 3, 2.8, 0, Math.PI * 2);
                Context.fill();
                Context.fillStyle = Source.Selected ? '#ffffff' : (Source.Active ?? 1) > 0.01 ? '#e9ecef' : '#8a8a8a';
                Context.fillText(Text, X + 8, Y);
            }
            Context.restore();
        }

        //--------------------------------------------------------------------------------------------------------------------
        //                                              SCALE BAR, LEGEND, PROBE
        //--------------------------------------------------------------------------------------------------------------------

        function RoundedPanel(X, Y, PanelWidth, PanelHeight)
        {
            Context.fillStyle = 'rgba(10,10,10,0.72)';
            Context.strokeStyle = 'rgba(255,255,255,0.07)';
            Context.lineWidth = 1;
            Context.beginPath();
            Context.roundRect(X, Y, PanelWidth, PanelHeight, 10);
            Context.fill();
            Context.stroke();
        }

        function RenderScaleBar()
        {
            if (!Options.ShowScaleBar || View.Mode === 'Camera') return;
            const PixelsPerMetre = ResolveAffine().PixelsPerMetre;
            const Metres = NiceLength(View.Span * 0.18);
            const Pixels = Metres * PixelsPerMetre;
            const X = 14, Y = Height - 14;
            RoundedPanel(X - 2, Y - 32, Pixels + 66, 38);
            Context.strokeStyle = '#ffffff';
            Context.lineWidth = 1.5;
            Context.beginPath();
            Context.moveTo(X + 10, Y - 13); Context.lineTo(X + 10, Y - 7); Context.lineTo(X + 10 + Pixels, Y - 7); Context.lineTo(X + 10 + Pixels, Y - 13);
            Context.moveTo(X + 10 + Pixels / 2, Y - 10); Context.lineTo(X + 10 + Pixels / 2, Y - 7);
            Context.stroke();
            Context.font = '400 10.5px "DM Sans", sans-serif';
            Context.fillStyle = '#d8d8d8';
            Context.fillText(FormatDistance(Metres), X + 10, Y - 18);
            Context.fillStyle = '#787878';
            Context.fillText(View.Mode === 'Oblique' ? 'across' : FormatDistance(View.Span), X + 18 + Pixels, Y - 4);
        }

        function RenderLegend()
        {
            if (!Options.ShowLegend) return;
            const Signed = Options.Contribution !== 'Damp' && Quantities[Options.Quantity] && Quantities[Options.Quantity].Signed;
            const Scale = ScaleReading || 1;
            const BarWidth = 168;
            const X = 14, Y = Height - (Options.ShowScaleBar && View.Mode !== 'Camera' ? 64 : 16) - 44;
            RoundedPanel(X - 2, Y, BarWidth + 22, 48);
            const Title = Options.LegendTitle || (Options.Contribution === 'Damp' ? 'Damping rate' : (Quantities[Options.Quantity] || Quantities.Speed).Label);
            Context.font = '500 10.5px "DM Sans", sans-serif';
            Context.fillStyle = '#d8d8d8';
            Context.fillText(`${Title}`, X + 9, Y + 15);
            Context.fillStyle = '#787878';
            const Unit = Lattice.Unit;
            const UnitText = `${Unit}${Options.ScaleMode === 'Automatic' ? ' · auto' : ''}`;
            Context.fillText(UnitText, X + BarWidth + 11 - Context.measureText(UnitText).width, Y + 15);
            const Gradient = Context.createLinearGradient(X + 9, 0, X + 9 + BarWidth, 0);
            ColourStops.forEach((Stop, Index) => Gradient.addColorStop(Index / (ColourStops.length - 1), `rgb(${Stop[0]},${Stop[1]},${Stop[2]})`));
            Context.fillStyle = Gradient;
            Context.beginPath();
            Context.roundRect(X + 9, Y + 21, BarWidth, 7, 3.5);
            Context.fill();
            Context.font = '400 9.5px "DM Sans", sans-serif';
            Context.fillStyle = '#9a9a9a';
            const Ticks = Signed ? [['−' + FormatReading(Scale), 0], ['0', 0.5], ['+' + FormatReading(Scale), 1]] : [['0', 0], [FormatReading(Scale / 2), 0.5], [FormatReading(Scale), 1]];
            for (const [Text, Share] of Ticks)
            {
                const Measure = Context.measureText(Text).width;
                const At = X + 9 + Share * BarWidth - (Share === 0 ? 0 : Share === 1 ? Measure : Measure / 2);
                Context.fillText(Text, At, Y + 41);
            }
        }

        // 📝 In Camera mode with ClipToExtent, everything drawn on the slice stays inside the projected domain, so a reach that
        //    is larger than the domain (an attractor's pull) does not run across the whole view.
        function ClipToExtent()
        {
            if (View.Mode !== 'Camera' || !Options.ClipToExtent || !Options.Extent) return;
            const E = Options.Extent, SliceY = Options.SliceHeight;
            const Corners = [[E.MinimumX, E.MinimumZ], [E.MaximumX, E.MinimumZ], [E.MaximumX, E.MaximumZ], [E.MinimumX, E.MaximumZ]].map(([X, Z]) => Project(X, SliceY, Z));
            if (!Corners.every(Boolean)) return;
            Context.beginPath();
            Corners.forEach(([X, Y], Index) => Index ? Context.lineTo(X, Y) : Context.moveTo(X, Y));
            Context.closePath();
            Context.clip();
        }

        function RenderProbe()
        {
            if (!Options.ProbePoint) return;
            const Point = Project(Options.ProbePoint[0], Options.SliceHeight, Options.ProbePoint[1]);
            if (!Point) return;
            Context.save();
            Context.strokeStyle = '#ffffff';
            Context.lineWidth = 1.4;
            Context.beginPath();
            Context.moveTo(Point[0] - 11, Point[1]); Context.lineTo(Point[0] - 4, Point[1]);
            Context.moveTo(Point[0] + 4, Point[1]); Context.lineTo(Point[0] + 11, Point[1]);
            Context.moveTo(Point[0], Point[1] - 11); Context.lineTo(Point[0], Point[1] - 4);
            Context.moveTo(Point[0], Point[1] + 4); Context.lineTo(Point[0], Point[1] + 11);
            Context.stroke();
            Context.restore();
        }

        //--------------------------------------------------------------------------------------------------------------------
        //                                                    MINIMAP
        //--------------------------------------------------------------------------------------------------------------------

        // 📝 The whole authored extent at 72 × 72 samples (refreshed at most every 400 ms), the sources as dots, and the view
        //    outline: the visible rectangle (Map), the inverse-projected screen (Oblique) or the camera's ground trace (Camera).
        function RenderMinimap(Time, Now)
        {
            const Target = Options.Minimap;
            const Extent = Options.MinimapExtent || Options.Extent;
            if (!Target || !Extent) return;
            const Bounds = Target.getBoundingClientRect();
            const Side = Math.max(32, Math.round(Bounds.width * Ratio)), Tall = Math.max(32, Math.round(Bounds.height * Ratio));
            if (Target.width !== Side || Target.height !== Tall) { Target.width = Side; Target.height = Tall; MinimapStamp = 0; }
            const Size = MinimapLattice.Size;
            const SpanX = Extent.MaximumX - Extent.MinimumX, SpanZ = Extent.MaximumZ - Extent.MinimumZ;
            if (Now - MinimapStamp > 400 || MinimapTime < 0 || Dirty)
            {
                MinimapStamp = Now;
                MinimapTime = Time;
                const Canvas2 = MinimapLattice.Canvas;
                Canvas2.width = Size; Canvas2.height = Size;
                const Pen = Canvas2.getContext('2d');
                const Image = Pen.createImageData(Size, Size);
                const Readings = new Float32Array(Size * Size), Inside = new Uint8Array(Size * Size);
                const CellSpan = Math.max(SpanX, SpanZ) / Size;
                let Peak = 0;
                for (let Row = 0; Row < Size; ++Row)
                {
                    for (let Column = 0; Column < Size; ++Column)
                    {
                        Probe[0] = Probe[1] = Probe[2] = 0;
                        const X = Extent.MinimumX + (Column + 0.5) / Size * SpanX, Z = Extent.MinimumZ + (Row + 0.5) / Size * SpanZ;
                        const Within = Specification.Sample(Options.Contribution === 'Damp' ? 'Damp' : Options.Contribution, X, Options.SliceHeight, Z, Time, CellSpan, Probe) !== false;
                        const Reading = Options.Contribution === 'Damp' ? Probe[0] : Math.hypot(Probe[0], Probe[2]);
                        Readings[Row * Size + Column] = Reading;
                        Inside[Row * Size + Column] = Within ? 1 : 0;
                        if (Within) Peak = Math.max(Peak, Reading);
                    }
                }
                for (let Index = 0; Index < Size * Size; ++Index)
                {
                    const Lut = Math.max(0, Math.min(255, Math.round(Readings[Index] / (Peak || 1) * 255))) * 3;
                    Image.data[Index * 4] = ColourLut[Lut]; Image.data[Index * 4 + 1] = ColourLut[Lut + 1]; Image.data[Index * 4 + 2] = ColourLut[Lut + 2];
                    Image.data[Index * 4 + 3] = Inside[Index] ? 255 : 40;
                }
                Pen.putImageData(Image, 0, 0);
            }
            const Pen = Target.getContext('2d');
            Pen.setTransform(1, 0, 0, 1, 0, 0);
            Pen.clearRect(0, 0, Side, Tall);
            Pen.imageSmoothingEnabled = true;
            Pen.globalAlpha = 0.9;
            Pen.drawImage(MinimapLattice.Canvas, 0, 0, Side, Tall);
            Pen.globalAlpha = 1;
            const ToMap = (X, Z) => [(X - Extent.MinimumX) / SpanX * Side, (Z - Extent.MinimumZ) / SpanZ * Tall];
            for (const Source of Options.Sources)
            {
                const [X, Y] = ToMap(Source.X, Source.Z);
                Pen.fillStyle = Source.Selected ? '#ffffff' : (Source.Colour || ContributionColours[Source.Contribution] || '#dfe7f3');
                Pen.beginPath();
                Pen.arc(X, Y, (Source.Selected ? 3.2 : 2.2) * Ratio, 0, Math.PI * 2);
                Pen.fill();
            }
            const Outline = [[0, 0], [Width, 0], [Width, Height], [0, Height]].map(([X, Y]) => ScreenToPlane(X, Y));
            if (Outline.every(Boolean))
            {
                Pen.strokeStyle = '#ffffff';
                Pen.lineWidth = 1.4 * Ratio;
                Pen.beginPath();
                Outline.forEach(([X, Z], Index) => { const [MapX, MapY] = ToMap(X, Z); if (Index) Pen.lineTo(MapX, MapY); else Pen.moveTo(MapX, MapY); });
                Pen.closePath();
                Pen.stroke();
                Pen.fillStyle = 'rgba(255,255,255,0.08)';
                Pen.fill();
            }
            else if (View.Mode === 'Camera')
            {
                const Centre = ScreenToPlane(Width / 2, Height / 2);
                if (Centre) { const [X, Y] = ToMap(Centre[0], Centre[1]); Pen.strokeStyle = '#ffffff'; Pen.beginPath(); Pen.arc(X, Y, 4 * Ratio, 0, Math.PI * 2); Pen.stroke(); }
            }
        }

        function AttachMinimap()
        {
            const Target = Options.Minimap;
            if (!Target || Target.dataset.navigation) return;
            Target.dataset.navigation = 'attached';
            const Pick = Event =>
            {
                const Extent = Options.MinimapExtent || Options.Extent;
                if (!Extent) return;
                const Bounds = Target.getBoundingClientRect();
                const X = Extent.MinimumX + (Event.clientX - Bounds.left) / Bounds.width * (Extent.MaximumX - Extent.MinimumX);
                const Z = Extent.MinimumZ + (Event.clientY - Bounds.top) / Bounds.height * (Extent.MaximumZ - Extent.MinimumZ);
                if (View.Mode === 'Camera') { if (Specification.OnMinimapPick) Specification.OnMinimapPick(X, Z); return; }
                View.CentreX = X;
                View.CentreZ = Z;
                Dirty = true;
                if (Specification.OnViewChange) Specification.OnViewChange();
            };
            let Dragging = false;
            Target.addEventListener('pointerdown', Event => { Dragging = true; Target.setPointerCapture(Event.pointerId); Pick(Event); Event.stopPropagation(); });
            Target.addEventListener('pointermove', Event => { if (Dragging) Pick(Event); });
            Target.addEventListener('pointerup', () => { Dragging = false; });
        }

        //--------------------------------------------------------------------------------------------------------------------
        //                                                 PUBLIC FUNCTIONS
        //--------------------------------------------------------------------------------------------------------------------

        function Resize()
        {
            const Bounds = Canvas.getBoundingClientRect();
            Ratio = Math.min(2, window.devicePixelRatio || 1);
            Width = Math.max(1, Bounds.width);
            Height = Math.max(1, Bounds.height);
            const PixelWidth = Math.round(Width * Ratio), PixelHeight = Math.round(Height * Ratio);
            if (Canvas.width !== PixelWidth || Canvas.height !== PixelHeight) { Canvas.width = PixelWidth; Canvas.height = PixelHeight; Dirty = true; }
        }

        // 📝 One refresh of the canvas. The lattice is re-sampled when the view or slice changed, when Invalidate() was called,
        //    or every RefreshInterval while time advances; arrows, tracers and overlays are redrawn every call.
        function RenderPlacementPreview()
        {
            const P = Options.PlacementPreview;
            if (!Options.PlacementMode || !P || View.Mode === 'Camera') return;
            const SliceY = Options.SliceHeight;
            const Colour = ContributionColours[P.Contribution] || '#d1f773';
            Context.save();
            Context.strokeStyle = Colour;
            Context.lineWidth = 1.8;
            Context.setLineDash([6, 5]);
            if (P.Shape === 'Box')
            {
                StrokeWorldPolygon(BoxOutline({ X: P.X, Z: P.Z, HalfX: P.HalfX || P.Radius || 120, HalfZ: P.HalfZ || P.Radius || 80, Rotation: P.Bearing || 0 }, SliceY), true);
            }
            else
            {
                StrokeWorldPolygon(CircleOutline(P.X, SliceY, P.Z, P.Radius || 120, 64), true);
            }
            const C = Project(P.X, SliceY, P.Z);
            if (C)
            {
                Context.setLineDash([]);
                Context.fillStyle = Colour;
                Context.beginPath();
                Context.arc(C[0], C[1], 5, 0, Math.PI * 2);
                Context.fill();
                if (P.Bearing !== undefined && P.Bearing !== null)
                {
                    const Turn = P.Bearing * Math.PI / 180;
                    const Reach = Math.max(30, (P.Radius || 120) * 0.48);
                    const Tip = Project(P.X + Math.sin(Turn) * Reach, SliceY, P.Z - Math.cos(Turn) * Reach);
                    if (Tip)
                    {
                        Context.beginPath();
                        Context.moveTo(C[0], C[1]);
                        Context.lineTo(Tip[0], Tip[1]);
                        Context.stroke();
                    }
                }
                const Tag = P.Label ? `Place ${P.Label} · ${FormatDistance(P.Radius || 120)}` : 'Click to place';
                Context.font = '500 11px "DM Sans", sans-serif';
                const W = Context.measureText(Tag).width + 14;
                Context.fillStyle = 'rgba(10,12,16,0.88)';
                Context.beginPath();
                Context.roundRect(C[0] + 12, C[1] - 24, W, 20, 8);
                Context.fill();
                Context.fillStyle = Colour;
                Context.fillText(Tag, C[0] + 19, C[1] - 10);
            }
            Context.restore();
        }

        function Render(Time)
        {
            const Now = performance.now();
            const Delta = LastRender ? Math.min(0.1, (Now - LastRender) / 1000) : 0;
            LastRender = Now;
            if (Options.Animated) ElapsedDisplay += Delta;
            Resize();
            if (View.Mode === 'Camera' && View.ViewProjection) Inverse = InvertProjection(View.ViewProjection);
            const Stamp = ResolveStamp();
            const TimeMoved = Options.Animated && Time !== Lattice.Time && Now - Lattice.RefreshedAt >= Options.RefreshInterval;
            let RefreshedLattice = false;
            if (Dirty || Stamp !== Lattice.Stamp || TimeMoved)
            {
                SampleLattice(Time);
                ColourLattice();
                Lattice.Stamp = Stamp;
                Lattice.RefreshedAt = Now;
                RefreshedLattice = true;
            }
            const LineStamp = [Options.FlowLines, Options.FlowLineSpacing, Options.FlowLineLength, Options.QuietCutoff, Options.Contribution].join('|');
            if (Options.FlowLines && (RefreshedLattice || LineStamp !== StreamlinesStamp))
            {
                BuildFlowLines();
                StreamlinesStamp = LineStamp;
            }
            AdvanceTracers(Delta);
            Context.setTransform(1, 0, 0, 1, 0, 0);
            if (View.Mode === 'Camera') Context.clearRect(0, 0, Canvas.width, Canvas.height);
            else { Context.fillStyle = Options.Background; Context.fillRect(0, 0, Canvas.width, Canvas.height); }
            RenderHeatmap();
            Context.setTransform(Ratio, 0, 0, Ratio, 0, 0);
            RenderGroundGrid();
            RenderCascades();
            Context.save();
            ClipToExtent();
            RenderFlowLines();
            RenderArrows();
            RenderTracers();
            RenderSources();
            RenderPlacementPreview();
            Context.restore();
            RenderProbe();
            RenderLegend();
            RenderScaleBar();
            RenderMinimap(Time, Now);
            Dirty = false;
        }

        function Configure(Changes)
        {
            const Before = JSON.stringify([Options.Quantity, Options.ScaleMode, Options.FixedScale, Options.HeatmapStyle, Options.Contribution, Options.SliceHeight]);
            Object.assign(Options, Changes);
            if (Changes.Minimap) AttachMinimap();
            if (JSON.stringify([Options.Quantity, Options.ScaleMode, Options.FixedScale, Options.HeatmapStyle, Options.Contribution, Options.SliceHeight]) !== Before)
            {
                if (Changes.Contribution !== undefined || Changes.Quantity !== undefined) ScaleReading = 0;
                Dirty = true;
            }
        }

        function Invalidate() { Dirty = true; }

        // 📝 An exact reading at a screen point: one sample there and four more half a cell away for ω and ∇·v.
        function ProbeAt(ScreenX, ScreenY, Time)
        {
            const Point = ScreenToPlane(ScreenX, ScreenY);
            if (!Point) return null;
            return ProbeWorld(Point[0], Point[1], Time);
        }

        function ProbeWorld(X, Z, Time)
        {
            const CellSpan = Lattice.CellSpan || 1, Half = CellSpan / 2, SliceY = Options.SliceHeight;
            const Read = (SampleX, SampleZ) =>
            {
                const Out = new Float64Array(3);
                const Inside = Specification.Sample(Options.Contribution, SampleX, SliceY, SampleZ, Time, CellSpan, Out) !== false;
                return { Out, Inside };
            };
            const Centre = Read(X, Z);
            const East = Read(X + Half, Z).Out, West = Read(X - Half, Z).Out, South = Read(X, Z + Half).Out, North = Read(X, Z - Half).Out;
            const Vorticity = (South[0] - North[0]) / CellSpan - (East[2] - West[2]) / CellSpan;
            const Divergence = (East[0] - West[0]) / CellSpan + (South[2] - North[2]) / CellSpan;
            const [VX, VY, VZ] = Centre.Out;
            const Horizontal = Math.hypot(VX, VZ);
            const Bearing = ((Math.atan2(VX, -VZ) * 180 / Math.PI) + 360) % 360;
            return { X, Z, Y: SliceY, Inside: Centre.Inside, Vector: [VX, VY, VZ], Speed: Math.hypot(VX, VY, VZ), Horizontal, Bearing, Vorticity, Divergence,
                     Unit: ContributionUnits[Options.Contribution], CellSpan };
        }

        function Zoom(Factor, AboutX, AboutY)
        {
            const Before = ScreenToPlane(AboutX ?? Width / 2, AboutY ?? Height / 2);
            View.Span = Math.min(Specification.MaximumSpan || 2.5e5, Math.max(Specification.MinimumSpan || 20, View.Span * Factor));
            const After = ScreenToPlane(AboutX ?? Width / 2, AboutY ?? Height / 2);
            if (Before && After) { View.CentreX += Before[0] - After[0]; View.CentreZ += Before[1] - After[1]; }
            if (Specification.OnViewChange) Specification.OnViewChange();
        }

        function HitSource(ScreenX, ScreenY)
        {
            let Best = null, BestDistance = 12;
            for (const Source of Options.Sources)
            {
                const Point = Project(Source.X, Options.SliceHeight, Source.Z);
                if (!Point) continue;
                const Distance = Math.hypot(Point[0] - ScreenX, Point[1] - ScreenY);
                if (Distance < BestDistance) { Best = Source; BestDistance = Distance; }
            }
            return Best;
        }

        function HitSourceHandle(ScreenX, ScreenY)
        {
            const PixelsPerMetre = View.Mode === 'Camera' ? 0 : ResolveAffine().PixelsPerMetre;
            for (const Source of Options.Sources)
            {
                if (!Source.Selected || View.Mode === 'Camera') continue;
                if (Source.Bearing !== undefined && Source.Bearing !== null)
                {
                    const Turn = Source.Bearing * Math.PI / 180;
                    const Reach = PixelsPerMetre ? Math.min(Source.Radius * 0.85 || 56 / PixelsPerMetre, 48 / PixelsPerMetre) : 1;
                    const Tip = Project(Source.X + Math.sin(Turn) * Reach, Options.SliceHeight, Source.Z - Math.cos(Turn) * Reach);
                    if (Tip && Math.hypot(Tip[0] - ScreenX, Tip[1] - ScreenY) < 11) return { Source, Part: 'Bearing' };
                }
                if (Source.Shape !== 'Everywhere' && Source.Radius > 0)
                {
                    const Rim = Project(Source.X + Source.Radius, Options.SliceHeight, Source.Z);
                    if (Rim && Math.hypot(Rim[0] - ScreenX, Rim[1] - ScreenY) < 11) return { Source, Part: 'Radius' };
                }
            }
            return null;
        }

        // 📝 Map / Oblique navigation: drag empty ground to pan, drag a source's centre/rim/bearing handle to move/resize/aim it,
        //    click in PlacementMode to place a new source, right-drag (or Alt-drag) to turn/tilt oblique view, wheel to zoom.
        function AttachNavigation()
        {
            let Drag = null;
            Canvas.addEventListener('contextmenu', Event => Event.preventDefault());
            Canvas.addEventListener('pointerdown', Event =>
            {
                if (View.Mode === 'Camera') return;
                const Bounds = Canvas.getBoundingClientRect();
                const X = Event.clientX - Bounds.left, Y = Event.clientY - Bounds.top;
                if (Options.PlacementMode && Event.button === 0 && !Event.altKey)
                {
                    const Point = ScreenToPlane(X, Y);
                    if (Point)
                    {
                        Canvas.setPointerCapture(Event.pointerId);
                        Drag = { Mode: 'Place', StartX: Point[0], StartZ: Point[1], CurX: Point[0], CurZ: Point[1] };
                        if (Specification.OnPlacementHover) Specification.OnPlacementHover(Point[0], Point[1], null);
                    }
                    return;
                }
                const Handle = Event.button === 0 && !Event.altKey ? HitSourceHandle(X, Y) : null;
                const Hit = Handle ? Handle.Source : (Event.button === 0 && !Event.altKey ? HitSource(X, Y) : null);
                Canvas.setPointerCapture(Event.pointerId);
                if (Handle)
                {
                    Drag = { Mode: Handle.Part, Id: Handle.Source.Id, SourceX: Handle.Source.X, SourceZ: Handle.Source.Z };
                }
                else if (Hit)
                {
                    if (Specification.OnSourcePick) Specification.OnSourcePick(Hit.Id);
                    const Start = ScreenToPlane(X, Y);
                    Drag = { Mode: 'Source', Id: Hit.Id, OffsetX: Hit.X - (Start ? Start[0] : Hit.X), OffsetZ: Hit.Z - (Start ? Start[1] : Hit.Z) };
                }
                else if (Event.button === 2 || Event.altKey) Drag = { Mode: 'Turn', X, Y };
                else Drag = { Mode: 'Pan', X, Y, Plane: ScreenToPlane(X, Y), Centre: [View.CentreX, View.CentreZ] };
                Canvas.classList.add('dragging');
            });
            Canvas.addEventListener('pointermove', Event =>
            {
                if (View.Mode === 'Camera') return;
                const Bounds = Canvas.getBoundingClientRect();
                const X = Event.clientX - Bounds.left, Y = Event.clientY - Bounds.top;
                if (!Drag)
                {
                    if (Options.PlacementMode)
                    {
                        Canvas.style.cursor = 'crosshair';
                        const P = ScreenToPlane(X, Y);
                        if (P && Specification.OnPlacementHover) Specification.OnPlacementHover(P[0], P[1], null);
                    }
                    else
                    {
                        const H = HitSourceHandle(X, Y);
                        Canvas.style.cursor = H ? (H.Part === 'Radius' ? 'ew-resize' : 'pointer') : HitSource(X, Y) ? 'grab' : 'crosshair';
                    }
                    if (Specification.OnHover) Specification.OnHover({ ScreenX: X, ScreenY: Y });
                    return;
                }
                if (Drag.Mode === 'Place')
                {
                    const P = ScreenToPlane(X, Y);
                    if (P)
                    {
                        Drag.CurX = P[0]; Drag.CurZ = P[1];
                        const Dx = P[0] - Drag.StartX, Dz = P[1] - Drag.StartZ;
                        const Dist = Math.hypot(Dx, Dz);
                        const DragBearing = Dist > (View.Span * 0.015) ? ((Math.atan2(Dx, -Dz) * 180 / Math.PI) + 360) % 360 : null;
                        if (Specification.OnPlacementHover) Specification.OnPlacementHover(Drag.StartX, Drag.StartZ, Dist > (View.Span * 0.025) ? { Radius: Dist, Bearing: DragBearing } : null);
                    }
                    return;
                }
                if (Drag.Mode === 'Radius')
                {
                    const P = ScreenToPlane(X, Y);
                    if (P && Specification.OnSourceRadiusChange)
                    {
                        const NewR = Math.max(5, Math.hypot(P[0] - Drag.SourceX, P[1] - Drag.SourceZ));
                        Specification.OnSourceRadiusChange(Drag.Id, NewR);
                    }
                    return;
                }
                if (Drag.Mode === 'Bearing')
                {
                    const P = ScreenToPlane(X, Y);
                    if (P && Specification.OnSourceBearingChange)
                    {
                        const Deg = ((Math.atan2(P[0] - Drag.SourceX, -(P[1] - Drag.SourceZ)) * 180 / Math.PI) + 360) % 360;
                        Specification.OnSourceBearingChange(Drag.Id, Deg);
                    }
                    return;
                }
                if (Drag.Mode === 'Source')
                {
                    const Point = ScreenToPlane(X, Y);
                    if (Point && Specification.OnSourceMove) Specification.OnSourceMove(Drag.Id, Point[0] + Drag.OffsetX, Point[1] + Drag.OffsetZ);
                }
                else if (Drag.Mode === 'Turn')
                {
                    if (View.Mode === 'Oblique' && Options.AllowOblique !== false)
                    {
                        View.Yaw += (X - Drag.X) * 0.008;
                        View.Pitch = Math.min(1.55, Math.max(0.18, View.Pitch + (Y - Drag.Y) * 0.006));
                    }
                    Drag.X = X; Drag.Y = Y;
                    if (Specification.OnViewChange) Specification.OnViewChange();
                }
                else
                {
                    View.CentreX = Drag.Centre[0]; View.CentreZ = Drag.Centre[1];
                    const Point = ScreenToPlane(X, Y);
                    if (Point && Drag.Plane) { View.CentreX = Drag.Centre[0] + Drag.Plane[0] - Point[0]; View.CentreZ = Drag.Centre[1] + Drag.Plane[1] - Point[1]; }
                    if (Specification.OnViewChange) Specification.OnViewChange();
                }
            });
            const Release = () =>
            {
                if (Drag && Drag.Mode === 'Place' && Specification.OnPlace)
                {
                    const Dx = Drag.CurX - Drag.StartX, Dz = Drag.CurZ - Drag.StartZ;
                    const Dist = Math.hypot(Dx, Dz);
                    const DragBearing = Dist > (View.Span * 0.015) ? ((Math.atan2(Dx, -Dz) * 180 / Math.PI) + 360) % 360 : null;
                    const DragRadius = Dist > (View.Span * 0.025) ? Dist : null;
                    Specification.OnPlace(Drag.StartX, Drag.StartZ, DragBearing, DragRadius);
                }
                Drag = null; Canvas.classList.remove('dragging');
            };
            Canvas.addEventListener('pointerup', Release);
            Canvas.addEventListener('pointercancel', Release);
            Canvas.addEventListener('pointerleave', () => { if (!Drag && Specification.OnHover) Specification.OnHover(null); });
            Canvas.addEventListener('wheel', Event =>
            {
                if (View.Mode === 'Camera') return;
                Event.preventDefault();
                const Bounds = Canvas.getBoundingClientRect();
                Zoom(Math.exp((Event.deltaMode === 1 ? Event.deltaY * 18 : Event.deltaY) * 0.0012), Event.clientX - Bounds.left, Event.clientY - Bounds.top);
            }, { passive: false });
            Canvas.addEventListener('dblclick', Event =>
            {
                if (View.Mode === 'Camera') return;
                const Bounds = Canvas.getBoundingClientRect();
                const Point = ScreenToPlane(Event.clientX - Bounds.left, Event.clientY - Bounds.top);
                if (Point) { View.CentreX = Point[0]; View.CentreZ = Point[1]; if (Specification.OnViewChange) Specification.OnViewChange(); }
            });
        }

        return {
            Options, View, Statistics, Configure, Render, Invalidate, Resize, Project, ScreenToPlane, ProbeAt, ProbeWorld, Zoom,
            AttachNavigation, AttachMinimap, Interpolate, ResolveCellSpan, HitSource,
            get Extents() { return { Width, Height, Ratio }; }
        };
    }

    return {
        Create, ColourStops, ResolveColour, ResolveColourText, Quantities, ContributionUnits, ResolveUnit, FormatDistance, FormatReading,
        NiceLength, Smooth01, InvertProjection
    };
})();
if (typeof globalThis !== 'undefined') globalThis.ForceFieldVisualization = ForceFieldVisualization;
if (typeof module !== 'undefined' && module.exports) module.exports = ForceFieldVisualization;

