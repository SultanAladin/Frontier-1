// 🧩 Emit unchanged demo PlumeSpace footprints for all four quality grades, without a GPU or vehicle solver.
import { PlumeSpace, PlumeGrades, PlumeDefaults } from '../../../Experimental/VehicleHmiReference/Source/PlumeSpace.js';

for (let Index = 0; Index < PlumeGrades.length; ++Index)
{
    const Plume = new PlumeSpace({}, {}, PlumeGrades[Index]);
    Plume.Origin = [-17 + Index, 9 - Index];
    Plume.PriorOrigin = [1 << 24, -(1 << 24)];
    Plume.SunDirection = [.6, 0, .8];
    Plume.ElapsedSeconds = .25 * Index;
    Plume.StepOrdinal = Index + 3;
    Plume.QuietSeconds = Index ? 2.0 : 0.0;
    Plume.QuietPrior = Index ? 1.9 : 0.0;
    const Sources = { Count: 1, ResolveBounds: () => ({ Minimum: [-1, -2, -3], Maximum: [4, 5, 6] }) };
    Plume.EncodeRecord(1 / 60, 1 / 60, Sources, { ...PlumeDefaults, Inspection: Index === 2 ? 10 : 0 });
    process.stdout.write(Buffer.from(Plume.RecordBytes));
}
