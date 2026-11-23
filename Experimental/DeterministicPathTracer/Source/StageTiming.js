/*====================================================================================================================================
                                                         STAGETIMING.JS
====================================================================================================================================*/
// 🧩 GPU time per pipeline stage from WebGPU timestamp queries, read back without stalling the frame loop

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

export const TimedStages   = Object.freeze(['Reprojection', 'Features', 'Integration', 'Reconstruction', 'Display']);
const ReadbackRingLength   = 3;      // [-] - Resolve buffers in flight; a frame is skipped (not waited on) when all are busy
const AverageWeight        = 0.15;   // [-] - Exponential moving average weight of the newest frame


//------------------------------------------------------------------------------------------------------------------------
//                                                    STAGE TIMING
//------------------------------------------------------------------------------------------------------------------------

// 📝 Each stage owns two timestamps (begin, end). Passes ask for their timestampWrites; Encode() resolves the stages
//    written since the last frame into a free ring buffer and Collect() maps it asynchronously after submission.
//    Unsupported adapters (no 'timestamp-query') get Enabled = false and every call is a no-op.
export class StageTiming
{
    constructor(Device, Enabled)
    {
        this.Enabled  = Enabled;
        this.Averages = Object.fromEntries(TimedStages.map(Stage => [Stage, 0]));
        this.Latest   = Object.fromEntries(TimedStages.map(Stage => [Stage, 0]));
        this.Frames   = 0;
        this.Written  = new Set();
        if (!Enabled) return;
        const QueryCount = TimedStages.length * 2;
        this.QuerySet     = Device.createQuerySet({ type: 'timestamp', count: QueryCount });
        this.ResolveStore = Device.createBuffer({ size: QueryCount * 8, usage: GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage.COPY_SRC });
        this.Ring = Array.from({ length: ReadbackRingLength }, () => ({
            Buffer: Device.createBuffer({ size: QueryCount * 8, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST }),
            Busy: false, Stages: []
        }));
    }

    // 📝 BeginEnabled / EndEnabled let a stage spread over several passes (first pass begins, last pass ends).
    PassWrites(Stage, BeginEnabled = true, EndEnabled = true)
    {
        if (!this.Enabled) return undefined;
        const Slot = TimedStages.indexOf(Stage) * 2;
        if (EndEnabled) this.Written.add(Stage);
        const Writes = { querySet: this.QuerySet };
        if (BeginEnabled) Writes.beginningOfPassWriteIndex = Slot;
        if (EndEnabled)   Writes.endOfPassWriteIndex = Slot + 1;
        return Writes;
    }

    Encode(Encoder)
    {
        if (!this.Enabled || !this.Written.size) return null;
        const Target = this.Ring.find(Entry => !Entry.Busy);
        const Stages = [...this.Written];
        this.Written.clear();
        if (!Target) return null;
        Target.Busy   = true;
        Target.Stages = Stages;
        Encoder.resolveQuerySet(this.QuerySet, 0, TimedStages.length * 2, this.ResolveStore, 0);
        Encoder.copyBufferToBuffer(this.ResolveStore, 0, Target.Buffer, 0, TimedStages.length * 16);
        return Target;
    }

    async Collect(Target)
    {
        if (!Target) return;
        await Target.Buffer.mapAsync(GPUMapMode.READ);
        const Stamps = new BigUint64Array(Target.Buffer.getMappedRange().slice(0));
        Target.Buffer.unmap();
        Target.Busy = false;
        for (const Stage of Target.Stages)
        {
            const Slot = TimedStages.indexOf(Stage) * 2;
            const Milliseconds = Stamps[Slot + 1] > Stamps[Slot] ? Number(Stamps[Slot + 1] - Stamps[Slot]) / 1e6 : 0;
            this.Latest[Stage]   = Milliseconds;
            this.Averages[Stage] = this.Averages[Stage] === 0 ? Milliseconds : (1 - AverageWeight) * this.Averages[Stage] + AverageWeight * Milliseconds;
        }
        // 📝 Stages that did not run in this frame decay, so one that stopped (reprojection while still) leaves the readout.
        for (const Stage of TimedStages) if (!Target.Stages.includes(Stage)) { this.Averages[Stage] *= 1 - AverageWeight; this.Latest[Stage] = 0; }
        this.Frames++;
    }
}
