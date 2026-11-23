//============================================================================================================================================
//                                                          BENCHMARKMETRICS.JS
//============================================================================================================================================
// 🧩 Frame-timing log for a WebGPU machine: runs each capture configuration for a fixed frame count, summarises it, and builds
//    plain text the user can copy back. Nothing is written to disk; the text lives in memory until it is copied.
// 📝 The log reports what this machine measured. It makes no claim about speed or quality on any other machine.

export const CaptureFrameCount = 120;

// 📝 Each configuration names the settings it changes; the others keep their current values.
export const CaptureMatrix = Object.freeze([
    Object.freeze({ Name: "Distance field (default)",       Settings: Object.freeze({ TriangleReflection: false, ScreenTraversal: false }) }),
    Object.freeze({ Name: "Triangle BVH reflections",        Settings: Object.freeze({ TriangleReflection: true,  ScreenTraversal: false }) }),
    Object.freeze({ Name: "Screen traces (GI and reflections)", Settings: Object.freeze({ TriangleReflection: false, ScreenTraversal: true,
                                                                                          ScreenIndirect: true, ScreenReflection: true }) })
]);

// 📝 The settings the log may change; every other setting is only read.
const GateKeys = ["TriangleReflection", "ScreenTraversal", "ScreenIndirect", "ScreenReflection"];

function Summarise(Values)
{
    const Sorted = [...Values].sort((First, Second) => First - Second);
    const Count = Sorted.length;
    if (Count === 0)
        return { Median: 0, Mean: 0, P95: 0, Worst: 0 };
    const Mean = Sorted.reduce((Sum, Value) => Sum + Value, 0) / Count;
    return { Median: Sorted[Math.floor(Count / 2)], Mean, P95: Sorted[Math.min(Count - 1, Math.floor(Count * 0.95))], Worst: Sorted[Count - 1] };
}

export class BenchmarkMetrics
{
    constructor()
    {
        this.Matrix = null;
        this.Index = 0;
        this.Frames = [];
        this.Results = [];
        this.Saved = null;
        this.Active = false;
        this.Finished = false;
        this.LastText = "";
        this.CopyStatus = "";
    }

    // 📝 Starts a capture. The gate settings of the plan are applied per configuration and restored when it finishes.
    Begin(Host, Matrix)
    {
        if (this.Active)
            return;
        this.Matrix = Matrix;
        this.Index = 0;
        this.Results = [];
        this.Frames = [];
        this.Finished = false;
        this.Saved = Object.fromEntries(GateKeys.map((Key) => [Key, Host.Settings[Key]]));
        this.Active = true;
        this.Apply(Host, Matrix[0]);
    }

    // 📝 A configuration sets every gate: the keys it leaves out keep the values the user had set before the capture.
    Apply(Host, Configuration)
    {
        for (const Key of GateKeys)
            Host.Settings[Key] = Key in Configuration.Settings ? Configuration.Settings[Key] : this.Saved[Key];
        Host.PriorValid = false;
    }

    // 📝 Called once per presented frame. Frame times are the raw interval between frames; pass durations are the latest resolved values.
    Sample(Host)
    {
        if (!this.Active)
            return;
        const Durations = Host.Metrics ? { ...Host.Metrics.Raw } : {};
        this.Frames.push({ Milliseconds: Host.LastFrameMilliseconds || 0.0, Durations });
        if (this.Frames.length < CaptureFrameCount)
            return;
        this.Results.push(this.Finish(Host, this.Matrix[this.Index]));
        this.Frames = [];
        this.Index += 1;
        if (this.Index < this.Matrix.length)
        {
            this.Apply(Host, this.Matrix[this.Index]);
            return;
        }
        Object.assign(Host.Settings, this.Saved);
        Host.PriorValid = false;
        this.Active = false;
        this.Finished = true;
        this.LastText = this.BuildText(Host);
    }

    Finish(Host, Configuration)
    {
        const Milliseconds = this.Frames.map((Frame) => Frame.Milliseconds);
        const Passes = {};
        for (const Name of Object.keys(this.Frames[0].Durations))
            Passes[Name] = Summarise(this.Frames.map((Frame) => Frame.Durations[Name] || 0.0)).Median;
        return { Name: Configuration.Name, Settings: { ...Configuration.Settings }, Frame: Summarise(Milliseconds), Passes, Count: this.Frames.length };
    }

    // 📝 The status line the panel shows while a capture runs or after it ends.
    StatusLines()
    {
        if (this.Active)
            return [`Capturing "${this.Matrix[this.Index].Name}" · ${this.Frames.length} / ${CaptureFrameCount} frames`,
                    `configuration ${this.Index + 1} of ${this.Matrix.length}`];
        if (this.Finished)
            return this.Results.map((Result) => `${Result.Name.padEnd(34)} median ${Result.Frame.Median.toFixed(1)} ms · p95 ${Result.Frame.P95.toFixed(1)} ms`)
                               .concat([this.CopyStatus || "Press Copy log to copy the full text."]);
        return ["Idle. Capture current settings, or the matrix of the three gate configurations."];
    }

    // 📝 Plain text for the clipboard: the machine, the settings, and each configuration's frame and pass figures.
    BuildText(Host)
    {
        const Lines = [];
        const Information = Host.Exchange && Host.Exchange.Adapter ? Host.Exchange.QueryAdapterDescription() : "unknown adapter";
        Lines.push("Slate IlluminationHost · hardware log");
        Lines.push(`recorded ${new Date().toISOString()}`);
        Lines.push(`adapter ${Information}`);
        Lines.push(`timestamp queries ${Host.Metrics && Host.Metrics.Enabled ? "on" : "off"} · quality ${Host.GradeName} · `
                   + `screen ${Host.ScreenExtent ? Host.ScreenExtent.join("x") : "-"} · trace ${Host.TraceExtent ? Host.TraceExtent.join("x") : "-"}`);
        Lines.push(`frames per configuration ${CaptureFrameCount}`);
        Lines.push("");
        for (const Result of this.Results)
        {
            Lines.push(`[${Result.Name}]`);
            Lines.push(`  gates ${JSON.stringify(Result.Settings)}`);
            Lines.push(`  frame ms  median ${Result.Frame.Median.toFixed(2)}  mean ${Result.Frame.Mean.toFixed(2)}  p95 ${Result.Frame.P95.toFixed(2)}  worst ${Result.Frame.Worst.toFixed(2)}`);
            for (const [Name, Milliseconds] of Object.entries(Result.Passes))
                if (Milliseconds > 0.0)
                    Lines.push(`  ${Name.padEnd(24)} ${Milliseconds.toFixed(3)} ms (median)`);
            Lines.push("");
        }
        Lines.push(`settings ${JSON.stringify(Object.fromEntries(GateKeys.map((Key) => [Key, Host.Settings[Key]])))}`);
        return Lines.join("\n");
    }

    async Copy()
    {
        if (!this.LastText)
            return;
        try
        {
            await navigator.clipboard.writeText(this.LastText);
            this.CopyStatus = "Copied the log to the clipboard.";
        }
        catch (Failure)
        {
            this.CopyStatus = "The clipboard refused. Click Copy log again after clicking the panel.";
        }
    }
}
