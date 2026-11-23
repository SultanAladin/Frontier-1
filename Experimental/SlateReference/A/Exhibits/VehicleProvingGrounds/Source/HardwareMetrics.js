//============================================================================================================================================
//                                                            HARDWAREMETRICS.JS
//============================================================================================================================================
// 🧩 Device timestamp pairs around the named passes, resolved off the critical path and smoothed for the panel.

export class HardwareMetrics
{
    constructor(Exchange, PassNames)
    {
        this.Exchange  = Exchange;
        this.PassNames = PassNames;
        this.Durations = Object.fromEntries(PassNames.map((Name) => [Name, 0.0]));
        this.Enabled   = Exchange.TimestampEnabled;
        this.Pending   = false;
        this.Written   = new Set();
        if (!this.Enabled)
            return;
        const Device = Exchange.Device;
        const Count = PassNames.length * 2;
        this.QuerySet = Device.createQuerySet({ type: "timestamp", count: Count });
        this.ResolveRecords = Device.createBuffer({ size: Count * 8, usage: GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage.COPY_SRC });
        this.ReadRecords = Device.createBuffer({ size: Count * 8, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
    }

    QueryWrites(Name)
    {
        if (!this.Enabled || this.Pending)
            return undefined;
        const Index = this.PassNames.indexOf(Name);
        if (Index < 0)
            return undefined;
        this.Written.add(Name);
        return { querySet: this.QuerySet, beginningOfPassWriteIndex: Index * 2, endOfPassWriteIndex: Index * 2 + 1 };
    }

    RecordResolve(Encoder)
    {
        if (!this.Enabled || this.Pending || this.Written.size === 0)
            return false;
        Encoder.resolveQuerySet(this.QuerySet, 0, this.PassNames.length * 2, this.ResolveRecords, 0);
        Encoder.copyBufferToBuffer(this.ResolveRecords, 0, this.ReadRecords, 0, this.PassNames.length * 16);
        return true;
    }

    async CollectDurations()
    {
        if (!this.Enabled || this.Pending || this.Written.size === 0)
            return;
        this.Pending = true;
        const Written = new Set(this.Written);
        this.Written.clear();
        try
        {
            await this.ReadRecords.mapAsync(GPUMapMode.READ);
            const Ticks = new BigUint64Array(this.ReadRecords.getMappedRange().slice(0));
            this.ReadRecords.unmap();
            this.PassNames.forEach((Name, Index) =>
            {
                if (!Written.has(Name))
                    return;
                // 📝 The ceiling only rejects corrupt pairs (unordered or wrapped ticks); a software adapter can spend
                //    whole seconds in one pass, and those durations are genuine. The first sample seeds the average.
                const Elapsed = Number(Ticks[Index * 2 + 1] - Ticks[Index * 2]) / 1.0e6;
                if (Elapsed >= 0.0 && Elapsed < 60000.0)
                    this.Durations[Name] = this.Durations[Name] > 0.0 ? this.Durations[Name] * 0.85 + Elapsed * 0.15 : Elapsed;
            });
        }
        catch (Failure)
        {
            this.Enabled = false;
        }
        this.Pending = false;
    }
}
