/*====================================================================================================================================
                                                     SNAPSHOTSTORE.JS
====================================================================================================================================*/
// 🧩 Hold the pre-stroke atlas so one buffer serves wash capping, blur suppression and undo alike

import { ReplayInto } from "./StrokeRecord.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Blender's undo tiles, Krita's wash mode and Chu's canvas snapshot are the same buffer invented
//    three times for three reasons. Building it once is the single cheapest structural win available:
//
//    | Purpose            | What it needs                                          |
//    |--------------------|--------------------------------------------------------|
//    | Wash-mode capping  | composite each dab from the PRE-STROKE state           |
//    | Blur suppression   | read colour that this stroke has not already smeared   |
//    | Undo               | the state to return to                                 |
//
// 🔴 Deliberately NOT the paint blend's read buffer. In ArmorPaint the undo texture IS that buffer,
//    which means undo cannot be made cheaper without redesigning painting. Cutting that dependency
//    is a day-one decision, not a later optimization.
export class SnapshotStore
{
    constructor(Device, Extent, AtlasFormat)
    {
        this.Device      = Device;
        this.Extent      = Extent;
        this.AtlasFormat = AtlasFormat;

        // The atlas as it stood before any stroke — the replay base.
        this.BaseTexture = Device.createTexture({
            label:  "PaintAtlasBase",
            size:   [Extent, Extent],
            format: AtlasFormat,
            usage:  GPUTextureUsage.TEXTURE_BINDING
                 |  GPUTextureUsage.COPY_DST
                 |  GPUTextureUsage.COPY_SRC
                 |  GPUTextureUsage.RENDER_ATTACHMENT
        });

        // The atlas as it stood before the CURRENT stroke — what wash mode reads.
        this.StrokeTexture = Device.createTexture({
            label:  "PaintAtlasSnapshot",
            size:   [Extent, Extent],
            format: AtlasFormat,
            usage:  GPUTextureUsage.TEXTURE_BINDING
                 |  GPUTextureUsage.COPY_DST
                 |  GPUTextureUsage.COPY_SRC
                 |  GPUTextureUsage.RENDER_ATTACHMENT
        });

        this.BaseCaptured = false;
    }

    // Copy the atlas into a snapshot texture.
    CopyInto(Target, AtlasTexture)
    {
        const Encoder = this.Device.createCommandEncoder({ label: "SnapshotCopy" });

        Encoder.copyTextureToTexture(
            { texture: AtlasTexture },
            { texture: Target },
            [this.Extent, this.Extent]);

        this.Device.queue.submit([Encoder.finish()]);
    }

    // Capture the untouched atlas once, at startup. This is the state replay rewinds to.
    CaptureBase(AtlasTexture)
    {
        this.CopyInto(this.BaseTexture, AtlasTexture);
        this.BaseCaptured = true;
    }

    // Capture the atlas at the start of a stroke.
    CaptureStroke(AtlasTexture)
    {
        this.CopyInto(this.StrokeTexture, AtlasTexture);
    }

    // Read the captured base back to the CPU. 📝 Used by diagnostics that need to tell paint from
    //    substrate: the base IS the substrate, so differencing against it needs no colour heuristic.
    async RetrieveBase(EncodeCapture, ResolveCapture, AtlasFormat)
    {
        const Encoder = this.Device.createCommandEncoder({ label: "SnapshotBaseReadback" });
        const Store   = EncodeCapture(this.Device, Encoder, this.BaseTexture, this.Extent, this.Extent);

        this.Device.queue.submit([Encoder.finish()]);
        return await ResolveCapture(Store, this.Extent, this.Extent, AtlasFormat);
    }

    // Restore the atlas to its untouched state.
    RestoreBase(AtlasTexture)
    {
        if (!this.BaseCaptured) { return false; }

        const Encoder = this.Device.createCommandEncoder({ label: "SnapshotRestore" });

        Encoder.copyTextureToTexture(
            { texture: this.BaseTexture },
            { texture: AtlasTexture },
            [this.Extent, this.Extent]);

        this.Device.queue.submit([Encoder.finish()]);
        return true;
    }
}

// Rewind the atlas to its base and re-lay every stroke the ledger still holds live.
//
// 📝 This is how undo is exact rather than approximate: the overwritten pixels were never stored, so
//    they are recomputed from the record instead. Cost scales with surviving stroke count, which is
//    the trade the tier-A "recipe, not pixels" model makes everywhere.
export function RebuildAtlas(Device, Snapshot, Pass, AtlasTexture, AtlasView, Surface, Ledger)
{
    Snapshot.RestoreBase(AtlasTexture);
    return ReplayInto(Device, Pass, AtlasView, Surface, Ledger);
}
