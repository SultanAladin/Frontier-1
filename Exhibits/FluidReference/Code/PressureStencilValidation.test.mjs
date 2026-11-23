/*==========================================================================================================================================
                                             PRESSURESTENCILVALIDATION.TEST.MJS
==========================================================================================================================================*/
// 🧩 Periodic-mode audit of the retained demo's centred divergence and gradient against its nearest-neighbour Laplacian.

import test from 'node:test';
import assert from 'node:assert/strict';

//------------------------------------------------------------------------------------------------------------------------
//                                                   PERIODIC STENCIL AUDIT
//------------------------------------------------------------------------------------------------------------------------

function EvaluatePeriodicMode(Cells, Mode)
{
    const Wave = 2 * Math.PI * Mode / Cells;
    const Velocity = Array.from({ length: Cells }, (_, I) => Math.sin(Wave * I));
    const At = (Array, I) => Array[(I + Cells) % Cells];
    const Divergence = Velocity.map((_, I) => .5 * (At(Velocity, I + 1) - At(Velocity, I - 1)));
    // 📝 This exact eigenmode solve isolates the stencil mismatch from finite Jacobi iteration error.
    const LaplacianEigenvalue = 2 * (Math.cos(Wave) - 1);
    const Pressure = Divergence.map((D) => D / LaplacianEigenvalue);
    const Projected = Velocity.map((U, I) => U - .5 * (At(Pressure, I + 1) - At(Pressure, I - 1)));
    const Remaining = Projected.map((_, I) => .5 * (At(Projected, I + 1) - At(Projected, I - 1)));
    const Norm = (Array) => Math.hypot(...Array);
    return { Ratio: Norm(Remaining) / Norm(Divergence), Expected: Math.sin(Wave / 2) ** 2 };
}

test('nearest-neighbour pressure solve leaves a centred-divergence residual in periodic modes', () =>
{
    const Low = EvaluatePeriodicMode(32, 1);
    const High = EvaluatePeriodicMode(32, 15);
    assert.ok(Math.abs(Low.Ratio - Low.Expected) < 1e-12);
    assert.ok(Math.abs(High.Ratio - High.Expected) < 1e-12);
    assert.ok(Low.Ratio < .01 && High.Ratio > .99);
    console.log(`PERIODIC_STENCIL mode=1 ratio=${Low.Ratio.toFixed(9)} expected=${Low.Expected.toFixed(9)}`);
    console.log(`PERIODIC_STENCIL mode=15 ratio=${High.Ratio.toFixed(9)} expected=${High.Expected.toFixed(9)}`);
});

test('centred divergence cannot see an alternating collocated velocity in the periodic interior', () =>
{
    const Velocity = Array.from({ length: 32 }, (_, I) => I % 2 ? -1 : 1);
    const Divergence = Velocity.map((_, I) => .5 * (Velocity[(I + 1) % 32] - Velocity[(I + 31) % 32]));
    assert.deepEqual(Divergence, new Array(32).fill(0));
    console.log('PERIODIC_CHECKERBOARD velocity_amplitude=1 centred_divergence=0');
});
