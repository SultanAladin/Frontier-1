//============================================================================================================================================
//                                                              TIMINGLOG.JS
//============================================================================================================================================
// 🧩 The timing log: one text block with everything a stays/goes decision needs — quality, extents, every toggle that
//    changes cost, the smoothed frame rate, and EVERY pass duration including the zeroes (a zero "Depth pyramid" is
//    the proof the pyramid gating works, so unlike the panel readout nothing is filtered out). Pure: the host hands a
//    snapshot, this formats it, and the node tests assert the lines.

// 📝 Snapshot: { GradeName, ScreenExtent [w, h], TraceScaleUsed, TraceExtent [w, h], FrameIndex, Settings, FrameSeconds,
//    LowRate, MetricsEnabled, PassNames [names in order], Durations { name: ms }, LightCount, ClusterOverflow }.
export function ComposeTimingLog(Snapshot)
{
    const S = Snapshot.Settings;
    const Flag = (Value) => Value ? "on" : "off";
    const Lines = [`Vehicle Proving Grounds timing log — ${new Date().toISOString()}`,
        `quality=${Snapshot.GradeName} render=${Snapshot.ScreenExtent[0]}x${Snapshot.ScreenExtent[1]} trace=1/${Snapshot.TraceScaleUsed} (${Snapshot.TraceExtent[0]}x${Snapshot.TraceExtent[1]}) frame=${Snapshot.FrameIndex}`,
        `indirect=${Flag(S.Indirect)} screentraces=${Flag(S.ScreenTraversal)} triangles=${Flag(S.TriangleReflection)} reflection=${Flag(S.Reflection)} temporal=${Flag(S.TemporalReuse)} spatial=${Flag(S.SpatialReuse)} visreuse=${Flag(S.VisibilityReuse)} kernel=${Flag(S.VarianceKernel)} radiosity=${Flag(S.CardRadiosity)} shadows=${Flag(S.DirectShadow)} aa=${Flag(S.Antialias)} smoke=${Flag(S.SmokeEnabled)}`,
        `frame ${(1.0 / Math.max(Snapshot.FrameSeconds || 1, 1.0e-4)).toFixed(1)} fps (${((Snapshot.FrameSeconds || 0) * 1000.0).toFixed(1)} ms avg) 1% low ${Snapshot.LowRate || "–"}`,
        `gpu timestamps ${Snapshot.MetricsEnabled ? "on" : "OFF — pass times below are stale/zero"}:`];
    for (const Name of Snapshot.PassNames)
        Lines.push(`  ${Name.padEnd(22)}${(Snapshot.Durations[Name] || 0.0).toFixed(2).padStart(7)} ms`);
    Lines.push(`lights ${Snapshot.LightCount} cluster-overflow ${Snapshot.ClusterOverflow}`);
    return Lines.join("\n");
}
