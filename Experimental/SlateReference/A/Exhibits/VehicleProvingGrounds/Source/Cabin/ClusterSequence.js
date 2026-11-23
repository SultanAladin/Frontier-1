//============================================================================================================================================
//                                                           CLUSTERSEQUENCE.JS
//============================================================================================================================================
// 📦 Driver cluster: irregular display outlines (hexagon, 70 % circle, trapezoid, pill) drawn into one flat texture clipped to the outline.

// 📝 Copied from Experimental/TabletPanel (arena/215a6f39-frontier). Cabin: with Demonstration off the host drives
//    Speed, Revolutions and Gear from the solver, and Tyres (four { Pressure [kPa], Warning }) adds a pressure row.

export const ClusterWidth = 1000;
export const ClusterHeight = 560;   // reference height; each outline rasters at its own physical aspect (see ResolveClusterHeight)

// 📝 The texture is cut to the housing, so it must share the housing's proportions: a square 70 % circle gets a
//    square raster, the wide pill a 3:1 raster. Stretching one fixed canvas onto every outline squashed the gauges.
export function ResolveClusterHeight(Identifier)
{
    const Outline = ClusterOutlines[Identifier];
    return Math.round(ClusterWidth * Outline.Height / Outline.Width);
}

// Outlines are unit polylines over [-1, 1]²; the same path clips the raster, cuts the bezel and sizes the hardware.
export const ClusterOutlines = {
    hexagon:   { Name: "Hexagon",      Caption: "Six faces. One focus.",   Width: 0.34, Height: 0.19,
                 Points: () => [[-0.62, -1], [0.62, -1], [1, 0], [0.62, 1], [-0.62, 1], [-1, 0]] },
    arc:       { Name: "70 % circle",  Caption: "A ring, cut by the hood", Width: 0.26, Height: 0.26,
                 Points: () => { // 📝 70 % of the circle, the missing 30 % cut symmetrically at the top where the hood line runs.
                     const Points = []; const Start = Math.PI * 0.8, End = Math.PI * 2.2;
                     for (let Index = 0; Index <= 72; ++Index) { const Angle = Start + (End - Start) * Index / 72; Points.push([Math.cos(Angle), Math.sin(Angle)]); }
                     return Points; } },
    trapezoid: { Name: "Trapezoid",    Caption: "Wide at the top, grounded", Width: 0.36, Height: 0.17,
                 Points: () => [[-0.78, -1], [0.78, -1], [1, 1], [-1, 1]] },
    pill:      { Name: "Wide pill",    Caption: "A slim band across the dash", Width: 0.40, Height: 0.13,
                 Points: () => { const Points = []; for (let Index = 0; Index <= 24; ++Index) { const Angle = -Math.PI / 2 + Math.PI * Index / 24; Points.push([0.68 + 0.32 * Math.cos(Angle), Math.sin(Angle)]); }
                     for (let Index = 0; Index <= 24; ++Index) { const Angle = Math.PI / 2 + Math.PI * Index / 24; Points.push([-0.68 + 0.32 * Math.cos(Angle), Math.sin(Angle)]); } return Points; } }
};

export class ClusterSequence
{
    constructor(Canvas)
    {
        this.Canvas = Canvas;
        this.Context = Canvas.getContext("2d", { alpha: true });
        this.Outline = "hexagon";
        this.Speed = 0;
        this.Revolutions = 900;
        this.Gear = "P";
        this.Dirty = true;
        this.Revision = 0;
        this.LastRaster = -1;
        this.RateHz = 30;
        this.Demonstration = true;
        this.Height = ClusterHeight;
        this.Resized = false;
        this.PresentOutline("hexagon");
    }

    PresentOutline(Identifier)
    {
        if (!ClusterOutlines[Identifier]) return false;
        this.Outline = Identifier;
        this.Height = ResolveClusterHeight(Identifier);
        const Width = ClusterWidth * 1.5, Height = this.Height * 1.5;
        if (this.Canvas.width !== Width || this.Canvas.height !== Height)
        {
            this.Canvas.width = Width;
            this.Canvas.height = Height;
            this.Resized = true;   // the GPU texture must be reallocated, not sub-uploaded
        }
        this.Dirty = true;
        return true;
    }

    AdvanceDemonstration(Elapsed)
    {
        if (!this.Demonstration) return;
        const Phase = (Math.sin(Elapsed * 0.35) + 1) / 2;
        const Speed = Math.round(Phase * 182);
        const Revolutions = Math.round(1100 + Phase * 5600 + Math.sin(Elapsed * 2.3) * 120);
        const Gear = Speed < 2 ? "N" : String(Math.min(6, 1 + Math.floor(Speed / 32)));
        if (Speed !== this.Speed || Math.abs(Revolutions - this.Revolutions) > 40 || Gear !== this.Gear)
        {
            this.Speed = Speed; this.Revolutions = Revolutions; this.Gear = Gear; this.Dirty = true;
        }
    }

    TraceOutline(Inset = 0)
    {
        const Context = this.Context;
        const Points = ClusterOutlines[this.Outline].Points();
        Context.beginPath();
        for (const [Index, Point] of Points.entries())
        {
            const Across = ClusterWidth / 2 + Point[0] * (ClusterWidth / 2 - Inset);
            const Down = this.Height / 2 - Point[1] * (this.Height / 2 - Inset);
            if (Index === 0) Context.moveTo(Across, Down); else Context.lineTo(Across, Down);
        }
        Context.closePath();
    }

    RasterGauge(CentreX, CentreY, Radius, Fraction, Start, Sweep, Colour, Label, Value, Unit, Redline = 0)
    {
        const Context = this.Context;
        Context.lineCap = "round";
        Context.beginPath(); Context.arc(CentreX, CentreY, Radius, Start, Start + Sweep); Context.strokeStyle = "#ffffff14"; Context.lineWidth = 14; Context.stroke();
        if (Redline > 0)
        {
            Context.beginPath(); Context.arc(CentreX, CentreY, Radius, Start + Sweep * Redline, Start + Sweep); Context.strokeStyle = "#c9563d"; Context.lineWidth = 14; Context.stroke();
        }
        Context.beginPath(); Context.arc(CentreX, CentreY, Radius, Start, Start + Sweep * Math.max(0.001, Fraction)); Context.strokeStyle = Colour; Context.lineWidth = 14; Context.stroke();
        for (let Index = 0; Index <= 10; ++Index)
        {
            const Angle = Start + Sweep * Index / 10;
            Context.beginPath();
            Context.moveTo(CentreX + Math.cos(Angle) * (Radius - 20), CentreY + Math.sin(Angle) * (Radius - 20));
            Context.lineTo(CentreX + Math.cos(Angle) * (Radius - 30), CentreY + Math.sin(Angle) * (Radius - 30));
            Context.strokeStyle = "#b7cfc4"; Context.lineWidth = 2; Context.stroke();
        }
        const Needle = Start + Sweep * Fraction;
        Context.beginPath();
        Context.moveTo(CentreX + Math.cos(Needle) * (Radius - 12), CentreY + Math.sin(Needle) * (Radius - 12));
        Context.lineTo(CentreX + Math.cos(Needle + Math.PI) * 18, CentreY + Math.sin(Needle + Math.PI) * 18);
        Context.strokeStyle = "#f3f6ee"; Context.lineWidth = 3; Context.stroke();
        Context.font = `300 ${Radius * 0.62}px "DM Sans", sans-serif`; Context.textAlign = "center"; Context.fillStyle = "#edf2e8";
        Context.fillText(Value, CentreX, CentreY + Radius * 0.22);
        Context.font = `400 ${Radius * 0.14}px "DM Sans", sans-serif`; Context.fillStyle = "#93a99d";
        Context.fillText(Unit, CentreX, CentreY + Radius * 0.42);
        Context.fillText(Label, CentreX, CentreY + Radius * 0.62);
    }

    RasterCluster(Elapsed)
    {
        if (!this.Dirty) return false;
        if (this.LastRaster >= 0 && Elapsed - this.LastRaster < 1 / this.RateHz) return false;
        this.LastRaster = Elapsed;
        const Context = this.Context;
        Context.setTransform(1.5, 0, 0, 1.5, 0, 0);
        Context.clearRect(0, 0, ClusterWidth, this.Height);
        Context.save();
        this.TraceOutline();
        Context.clip();
        const Colour = Context.createLinearGradient(0, 0, 0, this.Height);
        Colour.addColorStop(0, "#111c20"); Colour.addColorStop(1, "#0b1113");
        Context.fillStyle = Colour;
        Context.fillRect(0, 0, ClusterWidth, this.Height);
        const Narrow = this.Outline === "arc";
        const Flat = this.Outline === "pill";
        const Radius = Narrow ? 190 : Flat ? 112 : Math.min(160, this.Height * 0.3);
        const Row = Narrow ? 470 : this.Height / 2 + (Flat ? 6 : 14);
        const LeftCentre = Narrow ? 275 : Flat ? 230 : 250;
        const RightCentre = Narrow ? 725 : Flat ? 770 : 750;
        this.RasterGauge(LeftCentre, Row, Radius, this.Speed / 240, Math.PI * 0.75, Math.PI * 1.5, "#8fd1ac", "SPEED", String(this.Speed), "km/h");
        this.RasterGauge(RightCentre, Row, Radius, this.Revolutions / 8000, Math.PI * 0.75, Math.PI * 1.5, "#f0c27a", "ENGINE", String(Math.round(this.Revolutions / 100) / 10), "× 1000 rpm", 0.84);
        Context.font = `300 ${Flat ? 66 : 84}px "DM Sans", sans-serif`; Context.textAlign = "center"; Context.fillStyle = "#f2f6ef";
        Context.fillText(this.Gear, ClusterWidth / 2, Row + (Flat ? 24 : 30));
        Context.font = '400 13px "DM Sans", sans-serif'; Context.fillStyle = "#8aa394";
        Context.fillText(this.Demonstration ? "DEMONSTRATION DRIVE · NO LIVE VEHICLE" : this.Tyres ? "LIVE · CONTROLVEHICLE" : "DEMONSTRATION PAUSED · NO LIVE VEHICLE",
                         ClusterWidth / 2, Narrow ? 760 : this.Height - 44);
        // Cabin: tyre pressures, a warning tint on a soft or failed corner.
        if (this.Tyres)
        {
            const Row = Narrow ? 700 : this.Height - 74;
            for (const [Index, Tyre] of this.Tyres.entries())
            {
                const Across = ClusterWidth / 2 - 135 + Index * 90;
                Context.font = '400 15px "DM Sans", sans-serif'; Context.fillStyle = Tyre.Warning ? "#f08a6a" : "#9fc4b0";
                Context.fillText(`${["FL", "FR", "RL", "RR"][Index]} ${Math.round(Tyre.Pressure)}`, Across, Row);
            }
        }
        Context.fillText("CONTROLVEHICLE", ClusterWidth / 2, Row - (Flat ? 46 : 60));
        for (const [Index, Lamp] of [["#8fd1ac", "ABS"], ["#f0c27a", "TCS"], ["#c9563d", "OIL"], ["#94caff", "LOW BEAM"]].entries())
        {
            const Across = ClusterWidth / 2 - 90 + Index * 60;
            const Down = Narrow ? 215 : Flat ? 36 : 54;
            Context.beginPath(); Context.arc(Across, Down, 6, 0, Math.PI * 2);
            Context.fillStyle = Index === 2 && this.Speed > 0 ? "#3a2b26" : Lamp[0]; Context.fill();
            Context.font = '400 9px "DM Sans", sans-serif'; Context.fillStyle = "#7f978a"; Context.fillText(Lamp[1], Across, Down + 20);
        }
        Context.restore();
        this.TraceOutline(2);
        Context.strokeStyle = "#ffffff22"; Context.lineWidth = 3; Context.stroke();
        this.Dirty = false;
        ++this.Revision;
        return true;
    }

    QueryReading()
    {
        return { Outline: this.Outline, Speed: this.Speed, Revolutions: this.Revolutions, Gear: this.Gear, Revision: this.Revision,
            RateHz: this.RateHz, Width: ClusterWidth, Height: this.Height, Demonstration: this.Demonstration,
            Hardware: { Width: ClusterOutlines[this.Outline].Width, Height: ClusterOutlines[this.Outline].Height } };
    }
}
