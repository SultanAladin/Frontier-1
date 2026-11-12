/*============================================================================================================================================
                                                              VECTORCODEC.CPP
============================================================================================================================================*/
// 🧩 .VectorSpace encoder & decoder implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/VectorCodec.h"

#include <cstring>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 VECTOR CODEC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

SpaceContainerPayload VectorEncoder::BuildPayload(
    const VectorSpaceDocumentModel&    Model,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    SpaceContainerPayload Payload{};
    Payload.ExtensionCategory  = SpaceExtensionCategory::VectorSpace;
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
    if (Payload.ExtensionCategory != SpaceExtensionCategory::VectorSpace)
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
