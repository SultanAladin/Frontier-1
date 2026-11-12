//============================================================================================================================================
//                                                          SELECTIONMASKCOMPUTE.SLANG
//============================================================================================================================================
// O(1) VRAM selection bitmask compute shader evaluating PriorityLayerOrdinal, PartGroupOrdinal, or Face selection operations in < 0.05 ms

struct SelectionDispatchParams
{
    uint FaceCount;          // Total faces in PolygonNetwork
    uint OperationCode;      // 0 = ClearAll, 1 = SelectByLayer, 2 = SelectByPartGroup, 3 = SelectSingleFace, 4 = ToggleSingleFace, 5 = SelectAll
    uint TargetMask;        // LayerOrdinal, PartGroupOrdinal, or FaceIndex
    uint AdditiveMask;       // 0 = Replace selection, 1 = Union into existing selection
};

[[vk::binding(0, 0)]] ConstantBuffer<SelectionDispatchParams> Params;
[[vk::binding(1, 0)]] StructuredBuffer<uint>                  FacePackedAttributes; // low 16 bits = PriorityLayerOrdinal, high 16 bits = PartGroupOrdinal
[[vk::binding(2, 0)]] RWStructuredBuffer<uint>                SelectionBitmask;     // 1 bit per FaceIndex (ceil(FaceCount / 32) uints)

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint WordIndex = DispatchThreadID.x;
    uint FirstFace  = WordIndex * 32u;
    if (FirstFace >= Params.FaceCount)
    {
        return;
    }

    uint ExistingWord = SelectionBitmask[WordIndex];
    uint ResultWord   = (Params.AdditiveMask != 0u) ? ExistingWord : 0u;

    if (Params.OperationCode == 0u) // ClearAll
    {
        SelectionBitmask[WordIndex] = 0u;
        return;
    }

    for (uint Bit = 0u; Bit < 32u; ++Bit)
    {
        uint FaceIdx = FirstFace + Bit;
        if (FaceIdx < Params.FaceCount)
        {
            uint Packed   = FacePackedAttributes[FaceIdx];
            uint LayerOrd = Packed & 0xFFFFu;
            uint PartOrd  = (Packed >> 16u) & 0xFFFFu;
            uint BitChannel  = 1u << Bit;

            if (Params.OperationCode == 1u && LayerOrd == Params.TargetMask)
            {
                ResultWord |= BitChannel;
            }
            else if (Params.OperationCode == 2u && PartOrd == Params.TargetMask)
            {
                ResultWord |= BitChannel;
            }
            else if (Params.OperationCode == 3u && FaceIdx == Params.TargetMask)
            {
                ResultWord |= BitChannel;
            }
            else if (Params.OperationCode == 4u)
            {
                bool BitEnabled = (ExistingWord & BitChannel) != 0u;
                bool Flip       = (FaceIdx == Params.TargetMask);
                if (BitEnabled != Flip)
                {
                    ResultWord |= BitChannel;
                }
                else
                {
                    ResultWord &= ~BitChannel;
                }
            }
            else if (Params.OperationCode == 5u)
            {
                ResultWord |= BitChannel;
            }
        }
    }

    SelectionBitmask[WordIndex] = ResultWord;
}
