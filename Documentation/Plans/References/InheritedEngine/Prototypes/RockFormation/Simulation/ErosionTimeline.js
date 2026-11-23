//========================================================================================================================
//                                                ErosionTimeline.js                                               🧩
//========================================================================================================================
//
// 📝 Transport control over the erosion run: play, pause, and scrub to any round already reached.
//
//    🔴 EROSION HAS NO INVERSE. Every process is a one-way loss — a cell that gave up mass cannot be told
//       what it used to hold, and thermal collapse is not reversible either because the gather that piled
//       material below has no record of which neighbour it came from. So scrubbing BACKWARD cannot be
//       computed. It has to be remembered.
//
//    The transport therefore keeps SNAPSHOTS of the density buffer at regular round intervals, on the GPU:
//
//      scrub forward, past the newest snapshot  -> keep stepping, as normal
//      scrub backward, or to an earlier round   -> restore the nearest snapshot AT OR BEFORE the target,
//                                                  then re-step forward the remainder
//
//    ⚠️ The re-step is what makes an arbitrary round reachable without storing every one. Landing exactly
//       on a snapshot is a copy; landing between two is a copy plus up to Spacing rounds of compute. With
//       the default spacing that worst case is small enough to feel instant.
//
//    📝 Determinism is what makes the re-step legitimate: a round's outcome depends only on the density it
//       reads and the profile, and the profile's only time-varying term is StepIndex, which is derived from
//       the round number rather than a wall clock. Re-stepping from a snapshot therefore reproduces the
//       same rock, not merely a similar one. If a process is ever given a genuinely random input, that
//       stops being true and this module has to store every round instead.

import { DispatchSpan } from "./VoxelField.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                     BUDGET
//------------------------------------------------------------------------------------------------------------------------
//
// 📝 A snapshot is a whole density buffer, so the memory cost is real and has to be budgeted against the
//    grid tier rather than picked as a round number:
//
//      128³  8 MiB each  -> 24 snapshots = 192 MiB   ⚠️ too much beside the 16 MiB ping-pong pair
//      128³  8 MiB each  -> 12 snapshots =  96 MiB   acceptable
//      256³ 64 MiB each  ->  2 snapshots = 128 MiB   bake tier keeps the floor only
//
//    🔴 The ceiling is a BYTE budget, not a count, precisely because the same count means eight times the
//       memory at bake tier — which is how a preview-tier figure that fits becomes an allocation failure
//       the moment someone presses bake.
const SnapshotByteCeiling = 96 * 1024 * 1024;                       // [-]

// 📝 Round 0 is always kept, whatever the budget: it is the unweathered starting body, the one state the
//    author is most likely to scrub back to, and the only one that cannot be recomputed by stepping
//    forward from anything else.
const FloorSnapshotTally = 1;                                       // [idx]

export function ComposeErosionTimeline(Device, Field, RoundCeiling)
{
    // ① How many snapshots fit, and therefore how far apart they sit.
    const Affordable = Math.max(FloorSnapshotTally,
                                Math.floor(SnapshotByteCeiling / Field.ByteSpan));

    // 📝 +1 because a run of N rounds has N+1 states to mark, counting the unweathered one at round 0.
    const Affords = Math.min(Affordable, RoundCeiling + 1);
    const Spacing = Math.max(1, Math.ceil(RoundCeiling / Math.max(1, Affords - 1)));

    // ⚠️ Re-derive the count FROM the spacing rather than keeping what the budget affords. Ceiling division
    //    rounds the spacing up, so the last slot of an affordance-sized ring can sit past the run's end and
    //    never be written -- 12 slots at spacing 24 reach round 264 on a 260-round run, so one slot is
    //    allocated, never recorded into, and costs a whole density buffer of memory for nothing.
    //
    //    🔴 Clamped back to Affordable. The re-derivation can hand back MORE slots than the budget allows:
    //       at bake tier one 64 MiB buffer is all that fits, spacing becomes the whole run, and
    //       floor(260/260)+1 asks for two -- 128 MiB, over the ceiling. Deriving a count from a spacing that
    //       was itself derived from a count needs the clamp reapplied, or the budget silently doubles at
    //       exactly the tier where a buffer is largest.
    const Tally = Math.min(Affordable, Math.floor(RoundCeiling / Spacing) + 1);

    const Timeline =
    {
        Field,
        RoundCeiling,
        Spacing,                                                    // [idx] rounds between snapshots

        // ② The snapshot ring. Each slot records WHICH round it holds, so a slot recycled by a longer run
        //    cannot be mistaken for the round it used to hold.
        //
        //    🔴 EVERY slot starts at -1, slot 0 included. A freshly allocated buffer holds zeroes, not the
        //       unweathered body — claiming it already holds round 0 would let the first scrub-to-start
        //       restore an empty grid, which draws nothing and reads as the sim having broken.
        Slots : Array.from({ length: Tally }, (Unused, Index) => ({
            Round  : -1,                                            // [idx] -1 means never written
            Buffer : Device.createBuffer({
                size  : Field.ByteSpan,
                usage : GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST,
                label : `Snapshot${Index}`
            })
        })),

        // ③ Transport state.
        Playing   : true,
        Round     : 0,                                              // [idx] the round currently DISPLAYED
        Reached   : 0,                                              // [idx] the furthest round ever stepped to
        Rate      : 1.0,                                            // [-] rounds-per-frame multiplier

        // 📝 Fractional rounds accumulate so a rate below one round per frame still advances. Truncating
        //    per frame instead would make every rate under 1.0 mean "stopped", and the slow end of the
        //    speed control is the end that matters for watching erosion happen.
        Debt      : 0.0                                             // [-]
    };

    return Timeline;
}

export function DiscardErosionTimeline(Timeline)
{
    if (!Timeline) return;
    Timeline.Slots.forEach(Slot => Slot.Buffer.destroy());
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SNAPSHOTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which ring slot a round belongs in. Round 0 pins slot 0; the rest divide by spacing.
function SlotForRound(Timeline, Round)
{
    if (Round === 0) return 0;
    const Index = Math.round(Round / Timeline.Spacing);
    return Index < Timeline.Slots.length ? Index : -1;
}

// 📝 Is this round one the ring is meant to hold?
function IsSnapshotRound(Timeline, Round)
{
    return Round === 0 || (Round % Timeline.Spacing === 0 && SlotForRound(Timeline, Round) >= 0);
}

// 📝 Copy the live density into the slot for this round, if it is a snapshot round and not already held.
//
//    🔴 Reads DensityPair[Parity] — the buffer holding the CURRENT density. The pair alternates, so a
//       fixed index would capture the half the next step is about to overwrite, and every restore would
//       land one step off in a way that looks like the scrub being imprecise rather than reading the wrong
//       buffer.
export function RecordSnapshot(Device, Timeline, Round)
{
    if (!IsSnapshotRound(Timeline, Round)) return false;

    const Index = SlotForRound(Timeline, Round);
    if (Index < 0) return false;

    const Slot = Timeline.Slots[Index];
    if (Slot.Round === Round) return false;                         // already held; nothing to redo

    const Field = Timeline.Field;
    const Encoder = Device.createCommandEncoder({ label: `Record${Round}` });
    Encoder.copyBufferToBuffer(Field.DensityPair[Field.Parity], 0, Slot.Buffer, 0, Field.ByteSpan);
    Device.queue.submit([Encoder.finish()]);

    Slot.Round = Round;
    return true;
}

// 📝 The newest snapshot at or before Target. Returns null when even round 0 is missing, which happens only
//    before the first seed.
function NearestSnapshot(Timeline, Target)
{
    let Best = null;
    for (const Slot of Timeline.Slots)
    {
        if (Slot.Round < 0 || Slot.Round > Target) continue;
        if (Best === null || Slot.Round > Best.Round) Best = Slot;
    }
    return Best;
}

// 📝 Restore a snapshot into BOTH halves of the ping-pong pair and reset parity.
//
//    🔴 Both halves, for the same reason the seed writes both: the next erosion step reads one and writes
//       the other, and leaving the other holding a different round means the first step after a scrub
//       gathers neighbours from two different points in history. That shows up as a one-frame tear along
//       the surface, which reads as a shader bug.
function RestoreSnapshot(Device, Timeline, Slot)
{
    const Field = Timeline.Field;
    const Encoder = Device.createCommandEncoder({ label: `Restore${Slot.Round}` });
    Encoder.copyBufferToBuffer(Slot.Buffer, 0, Field.DensityPair[0], 0, Field.ByteSpan);
    Encoder.copyBufferToBuffer(Slot.Buffer, 0, Field.DensityPair[1], 0, Field.ByteSpan);
    Device.queue.submit([Encoder.finish()]);

    Field.Parity    = 0;
    Field.StepTally = Slot.Round;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SCRUB
//------------------------------------------------------------------------------------------------------------------------
//
// 📝 Move the displayed state to Target. Returns how many rounds must be RE-STEPPED by the caller, because
//    stepping needs the weather profile and the pipeline sequence, which live in DeviceHost — this module
//    owns history, not erosion.
//
//    The three cases:
//      Target === Round          nothing to do
//      Target  >  Round          step forward the difference, no restore
//      Target  <  Round          restore the nearest snapshot, then step forward the remainder

export function ApplyScrub(Device, Timeline, Target)
{
    const Wanted = Math.max(0, Math.min(Math.round(Target), Timeline.RoundCeiling));
    if (Wanted === Timeline.Round) return 0;

    if (Wanted > Timeline.Round)
    {
        const Ahead = Wanted - Timeline.Round;
        Timeline.Round = Wanted;
        return Ahead;
    }

    // ⚠️ Backward. Without a snapshot there is nothing to restore to and the request cannot be honoured —
    //    report zero rather than silently leaving the display claiming a round it is not showing.
    const Slot = NearestSnapshot(Timeline, Wanted);
    if (!Slot) return 0;                                            // ring empty: leave the display where it is

    RestoreSnapshot(Device, Timeline, Slot);
    Timeline.Round = Wanted;
    return Wanted - Slot.Round;
}

// 📝 How many rounds to advance this frame under the current rate, given the frame's elapsed time.
//
//    ⚠️ Elapsed is CLAMPED. A tab left in the background delivers one enormous frame on return, and an
//       unclamped rate would consume the entire remaining run in that single step — the rock would jump
//       from young to settled with nothing drawn in between.
const ElapsedCeiling = 50;                                          // [ms]
const ReferenceFrame = 16.7;                                        // [ms] the rate is quoted per 60 Hz frame

export function AdvanceRounds(Timeline, Elapsed, RoundsPerFrame)
{
    if (!Timeline.Playing) return 0;
    if (Timeline.Round >= Timeline.RoundCeiling) return 0;

    const Scaled = Math.min(Elapsed, ElapsedCeiling) / ReferenceFrame;
    Timeline.Debt += RoundsPerFrame * Timeline.Rate * Scaled;

    const Whole = Math.floor(Timeline.Debt);
    if (Whole <= 0) return 0;

    Timeline.Debt -= Whole;

    // 📝 Never step past the ceiling, and never past it by accumulating debt either — the debt is dropped
    //    with the clamp so resuming does not immediately owe a burst.
    const Room = Timeline.RoundCeiling - Timeline.Round;
    const Ran  = Math.min(Whole, Room);
    if (Ran < Whole) Timeline.Debt = 0.0;
    return Ran;
}

// 📝 Record that rounds were actually run, so Round and Reached stay honest about what the buffer holds.
export function CommitRounds(Timeline, Ran)
{
    Timeline.Round   = Math.min(Timeline.Round + Ran, Timeline.RoundCeiling);
    Timeline.Reached = Math.max(Timeline.Reached, Timeline.Round);
}

// 📝 Reset to the unweathered body. Called on reseed — a new seed invalidates every snapshot, because they
//    all describe a rock that no longer exists.
//
//    🔴 EVERY slot is invalidated, slot 0 included. Slot 0 is the one that looks safe to keep — it holds
//       "round 0", and the new run also starts at round 0 — but its BUFFER still holds the previous body,
//       so keeping it would let a scrub back to the start restore the rock the author just edited away.
//       That presents as the dial having no effect, which is the single most misleading symptom available.
//       The caller re-records slot 0 after the seed dispatch; until then the ring is legitimately empty.
export function RewindTimeline(Timeline)
{
    Timeline.Round   = 0;
    Timeline.Reached = 0;
    Timeline.Debt    = 0.0;
    Timeline.Slots.forEach(Slot => { Slot.Round = -1; });
}
