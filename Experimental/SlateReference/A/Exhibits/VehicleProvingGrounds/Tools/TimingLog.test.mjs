import test from "node:test";
import assert from "node:assert/strict";

import { ComposeTimingLog } from "../Source/TimingLog.js";

// 📝 The timing log the inspector's button copies: header, toggles, frame rate and every pass duration. The zero
//    durations matter as much as the live ones — a 0.00 ms "Depth pyramid" with the traces off is the gating proof.

function ConstructSnapshot(Overrides = {})
{
    return {
        GradeName: "high", ScreenExtent: [1920, 1080], TraceScaleUsed: 2, TraceExtent: [960, 540], FrameIndex: 120,
        Settings: { Indirect: true, ScreenTraversal: false, TriangleReflection: false, Reflection: true, TemporalReuse: true,
                    SpatialReuse: true, VisibilityReuse: true, VarianceKernel: true, CardRadiosity: true, DirectShadow: true,
                    Antialias: true, SmokeEnabled: true },
        FrameSeconds: 1.0 / 60.0, LowRate: "58", MetricsEnabled: true,
        PassNames: ["Distance union", "Card lighting", "Shadow", "Surface", "Depth pyramid", "Direct"],
        Durations: { "Distance union": 1.25, "Card lighting": 0.5, Shadow: 2.0, Surface: 3.75, "Depth pyramid": 0.0, Direct: 1.0 },
        LightCount: 24, ClusterOverflow: 0,
        ...Overrides
    };
}

test("ComposeTimingLog reports quality, extents and every toggle", () =>
{
    const Log = ComposeTimingLog(ConstructSnapshot());
    assert.ok(Log.includes("Vehicle Proving Grounds timing log"));
    assert.ok(Log.includes("quality=high render=1920x1080 trace=1/2 (960x540) frame=120"));
    assert.ok(Log.includes("screentraces=off triangles=off"));
    assert.ok(Log.includes("60.0 fps (16.7 ms avg) 1% low 58"));
    assert.ok(Log.includes("lights 24 cluster-overflow 0"));
});

test("ComposeTimingLog keeps zero pass durations so gating stays visible", () =>
{
    const Log = ComposeTimingLog(ConstructSnapshot());
    const Pyramid = Log.split("\n").find((Line) => Line.includes("Depth pyramid"));
    assert.ok(Pyramid.includes("0.00 ms"));
    assert.equal(Log.split("\n").filter((Line) => Line.endsWith("ms")).length, 6);
});

test("ComposeTimingLog warns when timestamps are unavailable", () =>
{
    const Log = ComposeTimingLog(ConstructSnapshot({ MetricsEnabled: false, Durations: {} }));
    assert.ok(Log.includes("gpu timestamps OFF"));
});
