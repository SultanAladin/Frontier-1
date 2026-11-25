/*============================================================================================================================================
                                                               VECTORCODEC.CPP
============================================================================================================================================*/
// 🧩 .VectorSpace encoder & decoder implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/VectorCodec.h"

#include <algorithm>
#include <cstring>

namespace Frontier::Codecs
{

namespace
{

[[nodiscard]] toml::array PackRgbaArray(const std::array<uint8_t, 4u>& Tint)
{
    toml::array Out;
    Out.push_back(static_cast<int64_t>(Tint[0]));
    Out.push_back(static_cast<int64_t>(Tint[1]));
    Out.push_back(static_cast<int64_t>(Tint[2]));
    Out.push_back(static_cast<int64_t>(Tint[3]));
    return Out;
}

[[nodiscard]] std::array<uint8_t, 4u> UnpackRgbaArray(
    const toml::array*              Arr,
    const std::array<uint8_t, 4u>&  DefaultTint) noexcept
{
    if (Arr == nullptr || Arr->size() < 3u)
    {
        return DefaultTint;
    }
    std::array<uint8_t, 4u> Out = DefaultTint;
    for (std::size_t C = 0u; C < std::min<std::size_t>(4u, Arr->size()); ++C)
    {
        const int64_t Clamped = std::clamp<int64_t>((*Arr)[C].value_or(int64_t{ DefaultTint[C] }), 0, 255);
        Out[C] = static_cast<uint8_t>(Clamped);
    }
    return Out;
}

[[nodiscard]] float ReadTomlFloat(const toml::table& SectionRef, std::string_view Key, float DefaultScalar) noexcept
{
    if (const auto* Entry = SectionRef.get(Key))
    {
        if (const auto FloatOpt = Entry->value<double>())
        {
            return static_cast<float>(*FloatOpt);
        }
        if (const auto IntOpt = Entry->value<int64_t>())
        {
            return static_cast<float>(*IntOpt);
        }
    }
    return DefaultScalar;
}

[[nodiscard]] uint32_t ReadTomlUint(const toml::table& SectionRef, std::string_view Key, uint32_t DefaultUint) noexcept
{
    return static_cast<uint32_t>(std::max<int64_t>(0, SectionRef[Key].value_or(static_cast<int64_t>(DefaultUint))));
}

[[nodiscard]] int32_t ReadTomlInt(const toml::table& SectionRef, std::string_view Key, int32_t DefaultInt) noexcept
{
    return static_cast<int32_t>(SectionRef[Key].value_or(static_cast<int64_t>(DefaultInt)));
}

[[nodiscard]] uint8_t ReadTomlByte(const toml::table& SectionRef, std::string_view Key, uint8_t DefaultByte) noexcept
{
    return static_cast<uint8_t>(std::clamp<int64_t>(SectionRef[Key].value_or(static_cast<int64_t>(DefaultByte)), 0, 255));
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 VECTOR CODEC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

SpaceContainerPayload VectorEncoder::BuildPayload(
    const VectorSpaceDocumentModel&    Model,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    SpaceContainerPayload Payload{};
    Payload.SuffixCategory  = SpaceSuffixCategory::VectorSpace;
    Payload.DocumentTitle      = Model.DocumentTitle;
    Payload.AuthoringWorkspace = "Frontier.VectorWorkspace";

    toml::table StrokeMeta;
    StrokeMeta.insert_or_assign("DefaultStrokeWidthMm", Model.DefaultStrokeWidthMm);
    StrokeMeta.insert_or_assign("ViewBoxAspectRatio", Model.ViewBoxAspectRatio);
    Payload.DomainManifest.insert_or_assign(".StrokeAppearance", std::move(StrokeMeta));

    if (!Model.Curves.empty())
    {
        toml::array CurveArray;
        for (const VectorPathCurveDescriptor& Curve : Model.Curves)
        {
            toml::table Entry;
            Entry.insert_or_assign("CurveTitle", Curve.CurveTitle);
            Entry.insert_or_assign("ClosedLoopEnabled", Curve.ClosedLoopEnabled);
            Entry.insert_or_assign("ControlPointCount", static_cast<int64_t>(Curve.ControlPointCount));
            CurveArray.push_back(std::move(Entry));
        }
        Payload.DomainManifest.insert_or_assign(".PathCurve", std::move(CurveArray));
    }

    if (!Model.ControlPointKnots.empty())
    {
        BinarySectionBlock KnotSection{};
        KnotSection.SectionIdentifier = "BezierKnotStream";
        KnotSection.SubFormatTag      = ".PathCurve";
        const std::size_t ByteLength  = Model.ControlPointKnots.size() * sizeof(double);
        KnotSection.PayloadBytes.resize(ByteLength);
        std::memcpy(KnotSection.PayloadBytes.data(), Model.ControlPointKnots.data(), ByteLength);
        Payload.BinarySections.push_back(std::move(KnotSection));
    }

    if (Model.WorkspaceDocumentPresent || !Model.WorkspaceLayers.empty() || !Model.EmbeddedSvgXml.empty())
    {
        toml::table WorkspaceSection;
        WorkspaceSection.insert_or_assign("WorkspaceProfile", Model.WorkspaceProfile);
        WorkspaceSection.insert_or_assign("ResolutionPx", static_cast<int64_t>(Model.ResolutionPx));
        WorkspaceSection.insert_or_assign("GroundEnabled", Model.GroundEnabled);
        WorkspaceSection.insert_or_assign("GroundFillSource", static_cast<int64_t>(Model.GroundFillSource));
        WorkspaceSection.insert_or_assign("GroundTintA", PackRgbaArray(Model.GroundTintARgba));
        WorkspaceSection.insert_or_assign("GroundTintB", PackRgbaArray(Model.GroundTintBRgba));
        WorkspaceSection.insert_or_assign("GroundAngleDeg", static_cast<double>(Model.GroundAngleDeg));
        WorkspaceSection.insert_or_assign("GhostTileBordersVisible", Model.GhostTileBordersVisible);
        Payload.DomainManifest.insert_or_assign("VectorWorkspace", std::move(WorkspaceSection));

        toml::array LayersArray;
        for (const VectorWorkspaceLayerEntry& Layer : Model.WorkspaceLayers)
        {
            toml::table L;
            L.insert_or_assign("Identity", static_cast<int64_t>(Layer.Identity));
            L.insert_or_assign("EnclosingFolderId", static_cast<int64_t>(Layer.EnclosingFolderId));
            L.insert_or_assign("Title", Layer.Title);
            L.insert_or_assign("Category", static_cast<int64_t>(Layer.Category));
            L.insert_or_assign("Visible", Layer.Visible);
            L.insert_or_assign("Locked", Layer.Locked);
            L.insert_or_assign("ExpandedDrawer", Layer.ExpandedDrawer);
            L.insert_or_assign("CollapsedFolder", Layer.CollapsedFolder);
            L.insert_or_assign("BlendMode", static_cast<int64_t>(Layer.BlendMode));
            L.insert_or_assign("Opacity", static_cast<double>(Layer.Opacity));
            L.insert_or_assign("MotifShape", static_cast<int64_t>(Layer.MotifShape));
            L.insert_or_assign("ClosedCurveLoop", Layer.ClosedCurveLoop);
            L.insert_or_assign("CurveSmoothness", static_cast<double>(Layer.CurveSmoothness));
            L.insert_or_assign("TextContent", Layer.TextContent);
            L.insert_or_assign("FontSizePx", static_cast<double>(Layer.FontSizePx));
            L.insert_or_assign("FontFilePath", Layer.FontFilePath);
            L.insert_or_assign("TypefaceTitle", Layer.TypefaceTitle);
            L.insert_or_assign("LetterSpacingEm", static_cast<double>(Layer.LetterSpacingEm));
            if (!Layer.SvgPathSnippet.empty())
            {
                L.insert_or_assign("SvgPathSnippet", Layer.SvgPathSnippet);
            }
            L.insert_or_assign("FillEnabled", Layer.FillEnabled);
            L.insert_or_assign("FillSource", static_cast<int64_t>(Layer.FillSource));
            L.insert_or_assign("FillTint", PackRgbaArray(Layer.FillTintRgba));
            L.insert_or_assign("FillTintB", PackRgbaArray(Layer.FillTintBRgba));
            L.insert_or_assign("GradientAngleDeg", static_cast<double>(Layer.GradientAngleDeg));

            if (!Layer.FillStops.empty())
            {
                toml::array StopsArr;
                for (const auto& S : Layer.FillStops)
                {
                    toml::table St;
                    St.insert_or_assign("Tint", PackRgbaArray(S.TintRgba));
                    St.insert_or_assign("Position", static_cast<double>(S.Position));
                    StopsArr.push_back(std::move(St));
                }
                L.insert_or_assign("FillStops", std::move(StopsArr));
            }

            L.insert_or_assign("StrokeWidthPx", static_cast<double>(Layer.StrokeWidthPx));
            L.insert_or_assign("StrokeSource", static_cast<int64_t>(Layer.StrokeSource));
            L.insert_or_assign("StrokeTint", PackRgbaArray(Layer.StrokeTintRgba));
            L.insert_or_assign("StrokeCap", static_cast<int64_t>(Layer.StrokeCap));
            L.insert_or_assign("StrokeJoin", static_cast<int64_t>(Layer.StrokeJoin));
            L.insert_or_assign("EvenOddFillRule", Layer.EvenOddFillRule);
            L.insert_or_assign("CenterX", static_cast<double>(Layer.CenterX));
            L.insert_or_assign("CenterY", static_cast<double>(Layer.CenterY));
            L.insert_or_assign("WidthPx", static_cast<double>(Layer.WidthPx));
            L.insert_or_assign("HeightPx", static_cast<double>(Layer.HeightPx));
            L.insert_or_assign("RotationDeg", static_cast<double>(Layer.RotationDeg));
            L.insert_or_assign("FinishPreset", static_cast<int64_t>(Layer.FinishPreset));

            {
                toml::array PinsArr;
                for (const auto& Pin : Layer.PointPins)
                {
                    toml::table Pt;
                    Pt.insert_or_assign("Active", Pin.Active);
                    Pt.insert_or_assign("NormX", static_cast<double>(Pin.NormX));
                    Pt.insert_or_assign("NormY", static_cast<double>(Pin.NormY));
                    Pt.insert_or_assign("Tint", PackRgbaArray(Pin.TintRgba));
                    PinsArr.push_back(std::move(Pt));
                }
                L.insert_or_assign("PointPins", std::move(PinsArr));
            }

            if (!Layer.Subpaths.empty())
            {
                toml::array SubArr;
                for (const auto& Sub : Layer.Subpaths)
                {
                    toml::table Sb;
                    Sb.insert_or_assign("ClosedLoop", Sub.ClosedLoop);
                    toml::array KnotArr;
                    for (const auto& K : Sub.Knots)
                    {
                        toml::array Kn;
                        Kn.push_back(static_cast<double>(K.AnchorX));
                        Kn.push_back(static_cast<double>(K.AnchorY));
                        Kn.push_back(static_cast<double>(K.InHandleX));
                        Kn.push_back(static_cast<double>(K.InHandleY));
                        Kn.push_back(static_cast<double>(K.OutHandleX));
                        Kn.push_back(static_cast<double>(K.OutHandleY));
                        Kn.push_back(K.Smooth);
                        KnotArr.push_back(std::move(Kn));
                    }
                    Sb.insert_or_assign("Knots", std::move(KnotArr));
                    SubArr.push_back(std::move(Sb));
                }
                L.insert_or_assign("Subpaths", std::move(SubArr));
            }

            if (!Layer.Channels.empty())
            {
                toml::array ChArr;
                for (const auto& Ch : Layer.Channels)
                {
                    toml::table Ct;
                    Ct.insert_or_assign("Key", static_cast<int64_t>(Ch.ChannelKey));
                    Ct.insert_or_assign("Weight", static_cast<double>(Ch.ScalarWeight));
                    Ct.insert_or_assign("Tint", PackRgbaArray(Ch.TintRgba));
                    ChArr.push_back(std::move(Ct));
                }
                L.insert_or_assign("Channels", std::move(ChArr));
            }

            {
                toml::table Mk;
                Mk.insert_or_assign("Enabled", Layer.Mask.Enabled);
                Mk.insert_or_assign("Invert", Layer.Mask.Invert);
                Mk.insert_or_assign("Mode", static_cast<int64_t>(Layer.Mask.Mode));
                Mk.insert_or_assign("Frequency", static_cast<double>(Layer.Mask.Frequency));
                Mk.insert_or_assign("Threshold", static_cast<double>(Layer.Mask.Threshold));
                Mk.insert_or_assign("Softness", static_cast<double>(Layer.Mask.Softness));
                Mk.insert_or_assign("Seed", static_cast<int64_t>(Layer.Mask.Seed));
                L.insert_or_assign("Mask", std::move(Mk));
            }

            if (!Layer.Modifiers.empty())
            {
                toml::array ModArr;
                for (const auto& Mod : Layer.Modifiers)
                {
                    toml::table Mt;
                    Mt.insert_or_assign("Category", static_cast<int64_t>(Mod.Category));
                    Mt.insert_or_assign("Enabled", Mod.Enabled);
                    Mt.insert_or_assign("NoiseAlgorithm", static_cast<int64_t>(Mod.NoiseAlgorithm));
                    Mt.insert_or_assign("ModulateTarget", static_cast<int64_t>(Mod.ModulateTarget));
                    Mt.insert_or_assign("MinScale", static_cast<double>(Mod.MinScale));
                    Mt.insert_or_assign("MaxScale", static_cast<double>(Mod.MaxScale));
                    Mt.insert_or_assign("Frequency", static_cast<double>(Mod.Frequency));
                    Mt.insert_or_assign("TargetTintB", PackRgbaArray(Mod.TargetTintBRgba));
                    Mt.insert_or_assign("GridLayout", static_cast<int64_t>(Mod.GridLayout));
                    Mt.insert_or_assign("Columns", static_cast<int64_t>(Mod.Columns));
                    Mt.insert_or_assign("Rows", static_cast<int64_t>(Mod.Rows));
                    Mt.insert_or_assign("RadialCount", static_cast<int64_t>(Mod.RadialCount));
                    Mt.insert_or_assign("RadiusPx", static_cast<double>(Mod.RadiusPx));
                    Mt.insert_or_assign("Rings", static_cast<int64_t>(Mod.Rings));
                    Mt.insert_or_assign("MirrorAxis", static_cast<int64_t>(Mod.MirrorAxis));
                    Mt.insert_or_assign("EchoCopies", static_cast<int64_t>(Mod.EchoCopies));
                    Mt.insert_or_assign("InsetStepPx", static_cast<double>(Mod.InsetStepPx));
                    Mt.insert_or_assign("AltTint", PackRgbaArray(Mod.AltTintRgba));
                    Mt.insert_or_assign("PosJitterPx", static_cast<double>(Mod.PosJitterPx));
                    Mt.insert_or_assign("RotJitterDeg", static_cast<double>(Mod.RotJitterDeg));
                    Mt.insert_or_assign("Dropout", static_cast<double>(Mod.Dropout));
                    Mt.insert_or_assign("WarpMode", static_cast<int64_t>(Mod.WarpMode));
                    Mt.insert_or_assign("WarpAmplitude", static_cast<double>(Mod.WarpAmplitude));
                    Mt.insert_or_assign("ChipWearAmount", static_cast<double>(Mod.ChipWearAmount));
                    ModArr.push_back(std::move(Mt));
                }
                L.insert_or_assign("Modifiers", std::move(ModArr));
            }

            LayersArray.push_back(std::move(L));
        }
        Payload.DomainManifest.insert_or_assign("VectorLayers", std::move(LayersArray));

        if (!Model.EmbeddedSvgXml.empty())
        {
            BinarySectionBlock SvgSection{};
            SvgSection.SectionIdentifier = "EmbeddedSvgDocument";
            SvgSection.SubFormatTag      = ".SvgXml";
            SvgSection.PayloadBytes.resize(Model.EmbeddedSvgXml.size());
            std::memcpy(SvgSection.PayloadBytes.data(), Model.EmbeddedSvgXml.data(), Model.EmbeddedSvgXml.size());
            Payload.BinarySections.push_back(std::move(SvgSection));
        }
    }

    if (OptionalRevision != nullptr)
    {
        RevisionEncoder::AttachRevisionSections(*OptionalRevision, Payload);
    }
    return Payload;
}

bool VectorDecoder::ExtractModel(
    const SpaceContainerPayload& Payload,
    VectorSpaceDocumentModel&    OutModel,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    if (Payload.SuffixCategory != SpaceSuffixCategory::VectorSpace)
    {
        OutDiagnostic = "Expected .VectorSpace container category.";
        return false;
    }

    OutModel               = VectorSpaceDocumentModel{};
    OutModel.DocumentTitle = Payload.DocumentTitle;
    if (const toml::table* StrokeMeta = Payload.DomainManifest[".StrokeAppearance"].as_table())
    {
        OutModel.DefaultStrokeWidthMm = (*StrokeMeta)["DefaultStrokeWidthMm"].value_or(0.35);
        OutModel.ViewBoxAspectRatio   = (*StrokeMeta)["ViewBoxAspectRatio"].value_or(1.0);
    }

    if (const toml::array* CurveArray = Payload.DomainManifest[".PathCurve"].as_array())
    {
        for (const auto& CurveRef : *CurveArray)
        {
            if (const toml::table* Entry = CurveRef.as_table())
            {
                VectorPathCurveDescriptor Curve{};
                Curve.CurveTitle        = (*Entry)["CurveTitle"].value_or(std::string{});
                Curve.ClosedLoopEnabled = (*Entry)["ClosedLoopEnabled"].value_or(true);
                Curve.ControlPointCount = static_cast<uint32_t>((*Entry)["ControlPointCount"].value_or(int64_t{ 0 }));
                OutModel.Curves.push_back(std::move(Curve));
            }
        }
    }

    if (const toml::table* WorkspaceSection = Payload.DomainManifest["VectorWorkspace"].as_table())
    {
        OutModel.WorkspaceDocumentPresent = true;
        OutModel.WorkspaceProfile         = (*WorkspaceSection)["WorkspaceProfile"].value_or(std::string{ "Pattern" });
        OutModel.ResolutionPx             = ReadTomlUint(*WorkspaceSection, "ResolutionPx", 1024u);
        OutModel.GroundEnabled            = (*WorkspaceSection)["GroundEnabled"].value_or(true);
        OutModel.GroundFillSource         = ReadTomlByte(*WorkspaceSection, "GroundFillSource", 0u);
        OutModel.GroundTintARgba          = UnpackRgbaArray((*WorkspaceSection)["GroundTintA"].as_array(), { 23u, 26u, 31u, 255u });
        OutModel.GroundTintBRgba          = UnpackRgbaArray((*WorkspaceSection)["GroundTintB"].as_array(), { 11u, 11u, 11u, 255u });
        OutModel.GroundAngleDeg           = ReadTomlFloat(*WorkspaceSection, "GroundAngleDeg", 45.0f);
        OutModel.GhostTileBordersVisible  = (*WorkspaceSection)["GhostTileBordersVisible"].value_or(true);
    }

    if (const toml::array* LayersArr = Payload.DomainManifest["VectorLayers"].as_array())
    {
        OutModel.WorkspaceDocumentPresent = true;
        for (const auto& LayerRef : *LayersArr)
        {
            const toml::table* L = LayerRef.as_table();
            if (L == nullptr)
            {
                continue;
            }
            VectorWorkspaceLayerEntry Layer{};
            Layer.Identity          = ReadTomlUint(*L, "Identity", 1u);
            Layer.EnclosingFolderId = ReadTomlUint(*L, "EnclosingFolderId", 0u);
            Layer.Title             = (*L)["Title"].value_or(std::string{});
            Layer.Category          = ReadTomlByte(*L, "Category", 0u);
            Layer.Visible           = (*L)["Visible"].value_or(true);
            Layer.Locked            = (*L)["Locked"].value_or(false);
            Layer.ExpandedDrawer    = (*L)["ExpandedDrawer"].value_or(false);
            Layer.CollapsedFolder   = (*L)["CollapsedFolder"].value_or(false);
            Layer.BlendMode         = ReadTomlByte(*L, "BlendMode", 0u);
            Layer.Opacity           = ReadTomlFloat(*L, "Opacity", 1.0f);
            Layer.MotifShape        = ReadTomlByte(*L, "MotifShape", 0u);
            Layer.ClosedCurveLoop   = (*L)["ClosedCurveLoop"].value_or(true);
            Layer.CurveSmoothness   = ReadTomlFloat(*L, "CurveSmoothness", 0.5f);
            Layer.TextContent       = (*L)["TextContent"].value_or(std::string{});
            Layer.FontSizePx        = ReadTomlFloat(*L, "FontSizePx", 54.0f);
            Layer.FontFilePath     = (*L)["FontFilePath"].value_or(std::string{});
            Layer.TypefaceTitle   = (*L)["TypefaceTitle"].value_or(std::string{});
            Layer.LetterSpacingEm   = ReadTomlFloat(*L, "LetterSpacingEm", 0.0f);
            Layer.SvgPathSnippet    = (*L)["SvgPathSnippet"].value_or(std::string{});
            Layer.FillEnabled       = (*L)["FillEnabled"].value_or(true);
            Layer.FillSource        = ReadTomlByte(*L, "FillSource", 0u);
            Layer.FillTintRgba      = UnpackRgbaArray((*L)["FillTint"].as_array(), { 239u, 244u, 207u, 255u });
            Layer.FillTintBRgba     = UnpackRgbaArray((*L)["FillTintB"].as_array(), { 90u, 118u, 146u, 255u });
            Layer.GradientAngleDeg  = ReadTomlFloat(*L, "GradientAngleDeg", 45.0f);

            if (const toml::array* StopsArr = (*L)["FillStops"].as_array())
            {
                for (const auto& StopRef : *StopsArr)
                {
                    if (const toml::table* St = StopRef.as_table())
                    {
                        VectorWorkspaceGradientStopEntry Stop{};
                        Stop.TintRgba = UnpackRgbaArray((*St)["Tint"].as_array(), { 255u, 255u, 255u, 255u });
                        Stop.Position = ReadTomlFloat(*St, "Position", 0.0f);
                        Layer.FillStops.push_back(Stop);
                    }
                }
            }

            Layer.StrokeWidthPx   = ReadTomlFloat(*L, "StrokeWidthPx", 0.0f);
            Layer.StrokeSource    = ReadTomlByte(*L, "StrokeSource", 0u);
            Layer.StrokeTintRgba  = UnpackRgbaArray((*L)["StrokeTint"].as_array(), { 22u, 24u, 28u, 255u });
            Layer.StrokeCap       = ReadTomlByte(*L, "StrokeCap", 1u);
            Layer.StrokeJoin      = ReadTomlByte(*L, "StrokeJoin", 1u);
            Layer.EvenOddFillRule = (*L)["EvenOddFillRule"].value_or(false);
            Layer.CenterX         = ReadTomlFloat(*L, "CenterX", 256.0f);
            Layer.CenterY         = ReadTomlFloat(*L, "CenterY", 256.0f);
            Layer.WidthPx         = ReadTomlFloat(*L, "WidthPx", 128.0f);
            Layer.HeightPx        = ReadTomlFloat(*L, "HeightPx", 128.0f);
            Layer.RotationDeg     = ReadTomlFloat(*L, "RotationDeg", 0.0f);
            Layer.FinishPreset    = ReadTomlByte(*L, "FinishPreset", 0u);

            if (const toml::array* PinsArr = (*L)["PointPins"].as_array())
            {
                const std::size_t N = std::min<std::size_t>(12u, PinsArr->size());
                for (std::size_t P = 0u; P < N; ++P)
                {
                    if (const toml::table* Pt = (*PinsArr)[P].as_table())
                    {
                        Layer.PointPins[P].Active   = (*Pt)["Active"].value_or(false);
                        Layer.PointPins[P].NormX    = ReadTomlFloat(*Pt, "NormX", 0.5f);
                        Layer.PointPins[P].NormY    = ReadTomlFloat(*Pt, "NormY", 0.5f);
                        Layer.PointPins[P].TintRgba = UnpackRgbaArray((*Pt)["Tint"].as_array(), { 255u, 255u, 255u, 255u });
                    }
                }
            }

            if (const toml::array* SubArr = (*L)["Subpaths"].as_array())
            {
                for (const auto& SubRef : *SubArr)
                {
                    if (const toml::table* Sb = SubRef.as_table())
                    {
                        VectorWorkspaceSubpathEntry Sub{};
                        Sub.ClosedLoop = (*Sb)["ClosedLoop"].value_or(true);
                        if (const toml::array* KnotArr = (*Sb)["Knots"].as_array())
                        {
                            for (const auto& KRef : *KnotArr)
                            {
                                if (const toml::array* Kn = KRef.as_array(); Kn != nullptr && Kn->size() >= 6u)
                                {
                                    VectorWorkspaceKnotEntry K{};
                                    auto GetIdxFloat = [&](std::size_t Idx) noexcept -> float
                                    {
                                        if (const auto F = (*Kn)[Idx].value<double>()) return static_cast<float>(*F);
                                        if (const auto I = (*Kn)[Idx].value<int64_t>()) return static_cast<float>(*I);
                                        return 0.0f;
                                    };
                                    K.AnchorX    = GetIdxFloat(0u);
                                    K.AnchorY    = GetIdxFloat(1u);
                                    K.InHandleX  = GetIdxFloat(2u);
                                    K.InHandleY  = GetIdxFloat(3u);
                                    K.OutHandleX = GetIdxFloat(4u);
                                    K.OutHandleY = GetIdxFloat(5u);
                                    K.Smooth     = (Kn->size() > 6u) ? (*Kn)[6u].value_or(true) : true;
                                    Sub.Knots.push_back(K);
                                }
                            }
                        }
                        Layer.Subpaths.push_back(std::move(Sub));
                    }
                }
            }

            if (const toml::array* ChArr = (*L)["Channels"].as_array())
            {
                for (const auto& ChRef : *ChArr)
                {
                    if (const toml::table* Ct = ChRef.as_table())
                    {
                        VectorWorkspaceChannelEntry Ch{};
                        Ch.ChannelKey   = ReadTomlUint(*Ct, "Key", 0u);
                        Ch.ScalarWeight = ReadTomlFloat(*Ct, "Weight", 0.5f);
                        Ch.TintRgba     = UnpackRgbaArray((*Ct)["Tint"].as_array(), { 232u, 194u, 122u, 255u });
                        Layer.Channels.push_back(Ch);
                    }
                }
            }

            if (const toml::table* Mk = (*L)["Mask"].as_table())
            {
                Layer.Mask.Enabled   = (*Mk)["Enabled"].value_or(false);
                Layer.Mask.Invert    = (*Mk)["Invert"].value_or(false);
                Layer.Mask.Mode      = ReadTomlByte(*Mk, "Mode", 0u);
                Layer.Mask.Frequency = ReadTomlFloat(*Mk, "Frequency", 4.0f);
                Layer.Mask.Threshold = ReadTomlFloat(*Mk, "Threshold", 0.45f);
                Layer.Mask.Softness  = ReadTomlFloat(*Mk, "Softness", 0.25f);
                Layer.Mask.Seed      = ReadTomlInt(*Mk, "Seed", 211);
            }

            if (const toml::array* ModArr = (*L)["Modifiers"].as_array())
            {
                for (const auto& ModRef : *ModArr)
                {
                    if (const toml::table* Mt = ModRef.as_table())
                    {
                        VectorWorkspaceModifierEntry Mod{};
                        Mod.Category        = ReadTomlByte(*Mt, "Category", 0u);
                        Mod.Enabled         = (*Mt)["Enabled"].value_or(true);
                        Mod.NoiseAlgorithm  = ReadTomlByte(*Mt, "NoiseAlgorithm", 0u);
                        Mod.ModulateTarget  = ReadTomlByte(*Mt, "ModulateTarget", 0u);
                        Mod.MinScale        = ReadTomlFloat(*Mt, "MinScale", 0.40f);
                        Mod.MaxScale        = ReadTomlFloat(*Mt, "MaxScale", 1.60f);
                        Mod.Frequency       = ReadTomlFloat(*Mt, "Frequency", 3.5f);
                        Mod.TargetTintBRgba = UnpackRgbaArray((*Mt)["TargetTintB"].as_array(), { 118u, 178u, 216u, 255u });
                        Mod.GridLayout      = ReadTomlByte(*Mt, "GridLayout", 2u);
                        Mod.Columns         = ReadTomlInt(*Mt, "Columns", 8);
                        Mod.Rows            = ReadTomlInt(*Mt, "Rows", 8);
                        Mod.RadialCount     = ReadTomlInt(*Mt, "RadialCount", 8);
                        Mod.RadiusPx        = ReadTomlFloat(*Mt, "RadiusPx", 110.0f);
                        Mod.Rings           = ReadTomlInt(*Mt, "Rings", 1);
                        Mod.MirrorAxis      = ReadTomlByte(*Mt, "MirrorAxis", 2u);
                        Mod.EchoCopies      = ReadTomlInt(*Mt, "EchoCopies", 3);
                        Mod.InsetStepPx     = ReadTomlFloat(*Mt, "InsetStepPx", 24.0f);
                        Mod.AltTintRgba     = UnpackRgbaArray((*Mt)["AltTint"].as_array(), { 24u, 27u, 31u, 255u });
                        Mod.PosJitterPx     = ReadTomlFloat(*Mt, "PosJitterPx", 16.0f);
                        Mod.RotJitterDeg    = ReadTomlFloat(*Mt, "RotJitterDeg", 25.0f);
                        Mod.Dropout         = ReadTomlFloat(*Mt, "Dropout", 0.15f);
                        Mod.WarpMode        = ReadTomlByte(*Mt, "WarpMode", 0u);
                        Mod.WarpAmplitude   = ReadTomlFloat(*Mt, "WarpAmplitude", 24.0f);
                        Mod.ChipWearAmount  = ReadTomlFloat(*Mt, "ChipWearAmount", 0.25f);
                        Layer.Modifiers.push_back(Mod);
                    }
                }
            }

            OutModel.WorkspaceLayers.push_back(std::move(Layer));
        }
    }

    for (const BinarySectionBlock& Section : Payload.BinarySections)
    {
        if (Section.SubFormatTag == ".PathCurve" && (Section.PayloadBytes.size() % sizeof(double)) == 0u)
        {
            const std::size_t KnotCount = Section.PayloadBytes.size() / sizeof(double);
            OutModel.ControlPointKnots.resize(KnotCount);
            if (KnotCount > 0u)
            {
                std::memcpy(OutModel.ControlPointKnots.data(), Section.PayloadBytes.data(), Section.PayloadBytes.size());
            }
        }
        else if (Section.SubFormatTag == ".SvgXml")
        {
            OutModel.WorkspaceDocumentPresent = true;
            OutModel.EmbeddedSvgXml.resize(Section.PayloadBytes.size());
            if (!Section.PayloadBytes.empty())
            {
                std::memcpy(OutModel.EmbeddedSvgXml.data(), Section.PayloadBytes.data(), Section.PayloadBytes.size());
            }
        }
    }

    if (OptionalOutRevision != nullptr)
    {
        return RevisionDecoder::ExtractRevisionSections(Payload, *OptionalOutRevision, OutDiagnostic);
    }
    return true;
}

bool VectorEncoder::EncodeToFile(
    const std::filesystem::path&       TargetPath,
    const VectorSpaceDocumentModel&    Model,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    return SpaceContainerEncoder::EncodeToFile(TargetPath, BuildPayload(Model, OptionalRevision), OutDiagnostic);
}

bool VectorDecoder::DecodeFromFile(
    const std::filesystem::path& SourcePath,
    VectorSpaceDocumentModel&    OutModel,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    SpaceContainerPayload Payload{};
    if (!SpaceContainerDecoder::DecodeFromFile(SourcePath, Payload, OutDiagnostic))
    {
        return false;
    }
    return ExtractModel(Payload, OutModel, OutDiagnostic, OptionalOutRevision);
}

} // namespace Frontier::Codecs
