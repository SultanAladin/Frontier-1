/*============================================================================================================================================
                                                     VECTORAUTHORINGDOCUMENT.CPP
============================================================================================================================================*/
// 🧩 TOML authoring codec for retained coverage sources, expressions, layer identity and masks; GPU pixels are never serialized.

#include "VectorAuthoringDocument.h"
#include <toml++/toml.hpp>
#include <sstream>
#include <type_traits>

namespace Frontier
{
namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     STRICT OPTIONAL AUTHORING FIELDS
//------------------------------------------------------------------------------------------------------------------------

constexpr std::array<std::string_view, 9> ChannelCaptions{"colour", "height", "normal", "roughness", "metalness", "opacity", "occlusion", "transmission", "clearcoat"};

void ValidateFields(const toml::table &Source, std::initializer_list<std::string_view> Captions)
{
    for (const auto &[Caption, Entry] : Source)
    {
        (void)Entry;
        if (std::find(Captions.begin(), Captions.end(), Caption.str()) == Captions.end())
            throw std::runtime_error("Unknown native vector authoring field: " + std::string(Caption.str()));
    }
}

template<typename Quantity>
Quantity DecodeOptional(const toml::table &Source, std::string_view Caption, Quantity Fallback)
{
    if (!Source.contains(Caption)) return Fallback;
    const auto Field = Source[Caption];
    if constexpr (std::is_same_v<Quantity, bool>)
    {
        if (!Field.is_boolean()) throw std::runtime_error("Expected vector boolean.");
    }
    else if constexpr (std::is_integral_v<Quantity>)
    {
        if (!Field.is_integer()) throw std::runtime_error("Expected vector integer.");
    }
    const auto Decoded = Field.value<Quantity>();
    if (!Decoded) throw std::runtime_error("Invalid native vector field type: " + std::string(Caption));
    return *Decoded;
}

uint32_t DecodeEnclosureToken(const toml::table &Source)
{
    const auto Token = DecodeOptional<int64_t>(Source, "enclosure_token", 0);
    if (Token < 0 || Token > UINT32_MAX) throw std::runtime_error("Invalid vector enclosure token.");
    return uint32_t(Token);
}

toml::array EncodeCoordinates(const std::array<float, 4> &Coordinates)
{
    toml::array Result; for (auto Coordinate : Coordinates) Result.push_back(double(Coordinate)); return Result;
}
toml::table EncodeInstruction(const VectorCoverageInstruction &Instruction)
{
    toml::array Topology; for (auto Token : Instruction.Topology) Topology.push_back(int64_t(Token));
    toml::table Result{{"topology", std::move(Topology)}, {"bounds", EncodeCoordinates(Instruction.Bounds)},
        {"transform", EncodeCoordinates(Instruction.Transform)}, {"colour", EncodeCoordinates(Instruction.Colour)},
        {"colour_end", EncodeCoordinates(Instruction.ColourEnd)}, {"paint", EncodeCoordinates(Instruction.Paint)}};
    if (Instruction.Topology[0] <= 6 &&
        ((Instruction.Topology[3] & VectorCoverageStrokeColourExplicit) != 0 || Instruction.StrokeColour != Instruction.Colour))
        Result.insert("stroke_colour", EncodeCoordinates(Instruction.StrokeColour));
    return Result;
}
std::array<float, 4> DecodeCoordinates(const toml::node_view<const toml::node> &Field)
{
    const auto *Sequence = Field.as_array(); if (!Sequence || Sequence->size() != 4) throw std::runtime_error("Expected four vector coordinates.");
    std::array<float, 4> Result{};
    for (size_t Index = 0; Index < 4; ++Index)
    {
        const auto Coordinate = (*Sequence)[Index].value<double>();
        if (!Coordinate || !std::isfinite(*Coordinate) || std::abs(*Coordinate) > 1e9) throw std::runtime_error("Invalid vector coordinate.");
        Result[Index] = float(*Coordinate);
    }
    return Result;
}
VectorCoverageInstruction DecodeInstruction(const toml::table &Source)
{
    ValidateFields(Source, {"topology", "bounds", "transform", "colour", "colour_end", "stroke_colour", "paint"});
    VectorCoverageInstruction Result;
    const auto *Topology = Source["topology"].as_array(); if (!Topology || Topology->size() != 4) throw std::runtime_error("Expected four vector topology tokens.");
    for (size_t Index = 0; Index < 4; ++Index)
    {
        if (!(*Topology)[Index].is_integer()) throw std::runtime_error("Expected integer vector topology token.");
        auto Token = (*Topology)[Index].value<int64_t>();
        if (!Token || *Token < 0 || *Token > UINT32_MAX) throw std::runtime_error("Invalid vector topology token.");
        Result.Topology[Index] = uint32_t(*Token);
    }
    Result.Bounds = DecodeCoordinates(Source["bounds"]); Result.Transform = DecodeCoordinates(Source["transform"]);
    Result.Colour = DecodeCoordinates(Source["colour"]); Result.ColourEnd = DecodeCoordinates(Source["colour_end"]);
    if (Source.contains("stroke_colour"))
    {
        Result.StrokeColour = DecodeCoordinates(Source["stroke_colour"]);
        if (Result.Topology[0] <= 6) Result.Topology[3] |= VectorCoverageStrokeColourExplicit;
    }
    else Result.StrokeColour = Result.Colour;
    Result.Paint = DecodeCoordinates(Source["paint"]); return Result;
}
}
std::string EncodeVectorAuthoring(const VectorAuthoringDocument &Document)
{
    (void)ConstructVectorAuthoringCoverage(Document);
    toml::array Instructions, Segments, Layers, Enclosures;
    for (const auto &Instruction : Document.Instructions) Instructions.push_back(EncodeInstruction(Instruction));
    for (const auto &Segment : Document.Segments) Segments.push_back(toml::table{{"start_control", EncodeCoordinates(Segment.StartControl)}, {"control_end", EncodeCoordinates(Segment.ControlEnd)}});
    for (const auto &Layer : Document.Layers)
    {
        toml::table Encoded{{"token", int64_t(Layer.Token)}, {"root", int64_t(Layer.Root)}, {"name", Layer.Name}, {"visible", Layer.Visible}, {"locked", Layer.Locked}, {"mask_enabled", Layer.MaskEnabled}, {"mask_inverted", Layer.MaskInverted}, {"mask_luminance", Layer.MaskLuminance}};
        Encoded.insert("enclosure_token", int64_t(Layer.EnclosureToken));
        Encoded.insert("surface_relief", double(Layer.SurfaceRelief));
        const char *Structure = Layer.SurfaceStructure == VectorAuthoringSurfaceStructure::Stitch ? "stitch" : Layer.SurfaceStructure == VectorAuthoringSurfaceStructure::Weave ? "weave" : "freeform";
        Encoded.insert("surface_structure", Structure);
        constexpr const char *TextileGeometryNames[]{"unclassified", "needle_entry", "stitch_thread", "upper_thread_pass", "weave_yarn"};
        Encoded.insert("textile_geometry", TextileGeometryNames[uint32_t(Layer.TextileGeometry)]);
        if (Layer.TextileGeometry == VectorAuthoringTextileGeometry::NeedleEntry) Encoded.insert("stitch_role", "holes");
        else if (Layer.TextileGeometry == VectorAuthoringTextileGeometry::StitchThread) Encoded.insert("stitch_role", "thread");
        else if (Layer.TextileGeometry == VectorAuthoringTextileGeometry::UpperThreadPass) Encoded.insert("stitch_role", "highlight");
        else if (Layer.TextileGeometry == VectorAuthoringTextileGeometry::WeaveYarn) Encoded.insert("weave_role", "yarn");
        toml::table Channels;
        for (size_t Index = 0; Index < ChannelCaptions.size(); ++Index)
            Channels.insert(ChannelCaptions[Index], toml::table{{"enabled", Layer.SurfaceChannels[Index].Enabled}, {"strength", double(Layer.SurfaceChannels[Index].Strength)}});
        Encoded.insert("surface_channels", std::move(Channels));
        if (Layer.Mask) Encoded.insert("mask", EncodeInstruction(*Layer.Mask));
        if (Layer.ComponentKind != VectorAuthoringComponentKind::Freeform)
        {
            constexpr const char *ComponentKindNames[]{"freeform", "text", "imported_vector", "imported_image"};
            Encoded.insert("component_kind", ComponentKindNames[uint32_t(Layer.ComponentKind)]);
            if (!Layer.SourceMarkup.empty()) Encoded.insert("source_markup", Layer.SourceMarkup);
            if (!Layer.EmbeddedImage.empty()) Encoded.insert("embedded_image", Layer.EmbeddedImage);
            if (Layer.ComponentKind == VectorAuthoringComponentKind::Text) Encoded.insert("text_point_size", double(Layer.TextPointSize));
        }
        Layers.push_back(std::move(Encoded));
    }
    for (const auto &Entry : Document.Enclosures)
        Enclosures.push_back(toml::table{{"token", int64_t(Entry.Token)}, {"enclosure_token", int64_t(Entry.EnclosureToken)}, {"name", Entry.Name}, {"folder_enabled", Entry.FolderEnabled}, {"visible", Entry.VisibilityEnabled}, {"locked", Entry.LockEnabled}, {"group_wide_editing", Entry.GroupWideEditingEnabled}});
    toml::table Encoded{{"format", "frontier.vector_artwork"}, {"version", 1}, {"name", Document.Name}, {"stamp", Document.Stamp}, {"token_clock", int64_t(Document.TokenClock)}, {"instructions", std::move(Instructions)}, {"segments", std::move(Segments)}, {"layers", std::move(Layers)}, {"enclosures", std::move(Enclosures)}};
    std::ostringstream Output; Output << Encoded;
    if (Output.str().size() > 4000000) throw std::runtime_error("Native vector authoring source exceeds 4 MB.");
    return Output.str();
}
VectorAuthoringDocument DecodeVectorAuthoring(std::string_view Source)
{
    if (Source.size() > 4000000) throw std::runtime_error("Native vector source exceeds 4 MB.");
    const auto Table = toml::parse(Source);
    if (!Table["format"].is_string() || !Table["version"].is_integer()) throw std::runtime_error("Expected typed native vector format and version.");
    if (Table["format"].value_or("") != std::string("frontier.vector_artwork") || Table["version"].value_or(0) != 1) throw std::runtime_error("Unsupported native vector format.");
    ValidateFields(Table, {"format", "version", "name", "stamp", "token_clock", "instructions", "segments", "layers", "enclosures"});
    VectorAuthoringDocument Document;
    Document.Name = DecodeOptional<std::string>(Table, "name", "Untitled vector");
    Document.Stamp = DecodeOptional<bool>(Table, "stamp", false);
    const auto Clock = Table["token_clock"].is_integer() ? Table["token_clock"].value<int64_t>() : std::nullopt; if (!Clock || *Clock < 0 || *Clock > UINT32_MAX) throw std::runtime_error("Invalid vector identity clock."); Document.TokenClock = uint32_t(*Clock);
    const auto *Instructions = Table["instructions"].as_array(), *Segments = Table["segments"].as_array(), *Layers = Table["layers"].as_array();
    if (!Instructions || !Segments || !Layers || Instructions->size() > 128 || Segments->size() > 32768 || Layers->size() > 64) throw std::runtime_error("Invalid vector sequence bounds.");
    for (const auto &Instruction : *Instructions) { if (!Instruction.as_table()) throw std::runtime_error("Invalid vector instruction."); Document.Instructions.push_back(DecodeInstruction(*Instruction.as_table())); }
    for (const auto &Segment : *Segments)
    {
        if (!Segment.as_table()) throw std::runtime_error("Invalid vector segment.");
        const auto &Coordinates = *Segment.as_table();
        ValidateFields(Coordinates, {"start_control", "control_end"});
        Document.Segments.push_back({DecodeCoordinates(Coordinates["start_control"]), DecodeCoordinates(Coordinates["control_end"])});
    }
    for (const auto &Encoded : *Layers)
    {
        if (!Encoded.as_table()) throw std::runtime_error("Invalid vector layer.");
        const auto &Entry = *Encoded.as_table();
        ValidateFields(Entry, {"token", "root", "name", "visible", "locked", "mask_enabled", "mask_inverted", "mask_luminance", "mask", "enclosure_token", "surface_channels", "surface_structure", "textile_geometry", "stitch_role", "weave_role", "surface_relief", "component_kind", "source_markup", "embedded_image", "text_point_size"});
        VectorAuthoringLayer Layer;
        if (!Entry["token"].is_integer() || !Entry["root"].is_integer()) throw std::runtime_error("Expected integer vector layer identity.");
        const auto Token = Entry["token"].value<int64_t>(), Root = Entry["root"].value<int64_t>();
        if (!Token || !Root || *Token <= 0 || *Token > Document.TokenClock || *Root < 0 || *Root > UINT32_MAX) throw std::runtime_error("Invalid vector layer token.");
        Layer.Token = uint32_t(*Token); Layer.Root = uint32_t(*Root); Layer.Name = DecodeOptional<std::string>(Entry, "name", "VectorAuthoring");
        Layer.Visible = DecodeOptional<bool>(Entry, "visible", true); Layer.Locked = DecodeOptional<bool>(Entry, "locked", false);
        Layer.MaskEnabled = DecodeOptional<bool>(Entry, "mask_enabled", true);
        Layer.MaskInverted = DecodeOptional<bool>(Entry, "mask_inverted", false);
        Layer.MaskLuminance = DecodeOptional<bool>(Entry, "mask_luminance", false);
        Layer.EnclosureToken = DecodeEnclosureToken(Entry);
        Layer.SurfaceRelief = float(DecodeOptional<double>(Entry, "surface_relief", 1));
        const auto Structure = DecodeOptional<std::string>(Entry, "surface_structure", "freeform");
        if (Structure == "stitch") Layer.SurfaceStructure = VectorAuthoringSurfaceStructure::Stitch;
        else if (Structure == "weave") Layer.SurfaceStructure = VectorAuthoringSurfaceStructure::Weave;
        else if (Structure != "freeform") throw std::runtime_error("Unknown vector surface structure.");
        const auto TextileGeometry = DecodeOptional<std::string>(Entry, "textile_geometry", "unclassified");
        if (TextileGeometry == "needle_entry") Layer.TextileGeometry = VectorAuthoringTextileGeometry::NeedleEntry;
        else if (TextileGeometry == "stitch_thread") Layer.TextileGeometry = VectorAuthoringTextileGeometry::StitchThread;
        else if (TextileGeometry == "upper_thread_pass") Layer.TextileGeometry = VectorAuthoringTextileGeometry::UpperThreadPass;
        else if (TextileGeometry == "weave_yarn") Layer.TextileGeometry = VectorAuthoringTextileGeometry::WeaveYarn;
        else if (TextileGeometry != "unclassified") throw std::runtime_error("Unknown native textile geometry.");
        if (Entry.contains("stitch_role") && Entry.contains("weave_role"))
            throw std::runtime_error("A vector layer cannot carry both stitch and weave geometry.");
        if (Entry.contains("stitch_role"))
        {
            const auto StitchGeometry = DecodeOptional<std::string>(Entry, "stitch_role", "");
            const auto DerivedGeometry = StitchGeometry == "holes" ? VectorAuthoringTextileGeometry::NeedleEntry :
                StitchGeometry == "thread" ? VectorAuthoringTextileGeometry::StitchThread :
                StitchGeometry == "highlight" ? VectorAuthoringTextileGeometry::UpperThreadPass :
                VectorAuthoringTextileGeometry::Unclassified;
            if (DerivedGeometry == VectorAuthoringTextileGeometry::Unclassified ||
                (Layer.TextileGeometry != VectorAuthoringTextileGeometry::Unclassified && Layer.TextileGeometry != DerivedGeometry) ||
                (Layer.SurfaceStructure != VectorAuthoringSurfaceStructure::Freeform && Layer.SurfaceStructure != VectorAuthoringSurfaceStructure::Stitch))
                throw std::runtime_error("Unknown or conflicting native stitch geometry.");
            Layer.TextileGeometry = DerivedGeometry;
            Layer.SurfaceStructure = VectorAuthoringSurfaceStructure::Stitch;
        }
        if (Entry.contains("weave_role"))
        {
            if (DecodeOptional<std::string>(Entry, "weave_role", "") != "yarn" ||
                (Layer.TextileGeometry != VectorAuthoringTextileGeometry::Unclassified && Layer.TextileGeometry != VectorAuthoringTextileGeometry::WeaveYarn) ||
                (Layer.SurfaceStructure != VectorAuthoringSurfaceStructure::Freeform && Layer.SurfaceStructure != VectorAuthoringSurfaceStructure::Weave))
                throw std::runtime_error("Unknown or conflicting native weave geometry.");
            Layer.TextileGeometry = VectorAuthoringTextileGeometry::WeaveYarn;
            Layer.SurfaceStructure = VectorAuthoringSurfaceStructure::Weave;
        }
        if (Entry.contains("surface_channels"))
        {
            const auto *Channels = Entry["surface_channels"].as_table();
            if (!Channels) throw std::runtime_error("Expected vector surface channel table.");
            ValidateFields(*Channels, {"colour", "height", "normal", "roughness", "metalness", "opacity", "occlusion", "transmission", "clearcoat"});
            for (size_t Index = 0; Index < ChannelCaptions.size(); ++Index)
            {
                if (!Channels->contains(ChannelCaptions[Index])) continue;
                const auto *Channel = (*Channels)[ChannelCaptions[Index]].as_table();
                if (!Channel) throw std::runtime_error("Expected vector channel contribution table.");
                ValidateFields(*Channel, {"enabled", "strength"});
                Layer.SurfaceChannels[Index].Enabled = DecodeOptional<bool>(*Channel, "enabled", Layer.SurfaceChannels[Index].Enabled);
                Layer.SurfaceChannels[Index].Strength = float(DecodeOptional<double>(*Channel, "strength", 1));
            }
        }
        if (Entry.contains("mask") && !Entry["mask"].is_table()) throw std::runtime_error("Expected vector mask instruction table.");
        if (const auto *Mask = Entry["mask"].as_table()) Layer.Mask = DecodeInstruction(*Mask);
        const auto ComponentKind = DecodeOptional<std::string>(Entry, "component_kind", "freeform");
        if (ComponentKind == "text") Layer.ComponentKind = VectorAuthoringComponentKind::Text;
        else if (ComponentKind == "imported_vector") Layer.ComponentKind = VectorAuthoringComponentKind::ImportedVector;
        else if (ComponentKind == "imported_image") Layer.ComponentKind = VectorAuthoringComponentKind::ImportedImage;
        else if (ComponentKind != "freeform") throw std::runtime_error("Unknown vector component kind.");
        Layer.SourceMarkup = DecodeOptional<std::string>(Entry, "source_markup", "");
        Layer.EmbeddedImage = DecodeOptional<std::string>(Entry, "embedded_image", "");
        Layer.TextPointSize = float(DecodeOptional<double>(Entry, "text_point_size", 64));
        Document.Layers.push_back(std::move(Layer));
    }
    if (Table.contains("enclosures"))
    {
        const auto *Enclosures = Table["enclosures"].as_array();
        if (!Enclosures || Enclosures->size() > 64) throw std::runtime_error("Expected at most 64 vector enclosures.");
        for (const auto &Encoded : *Enclosures)
        {
            const auto *Entry = Encoded.as_table();
            if (!Entry) throw std::runtime_error("Expected vector enclosure table.");
            ValidateFields(*Entry, {"token", "enclosure_token", "name", "folder_enabled", "visible", "locked", "group_wide_editing"});
            VectorAuthoringEnclosure Enclosure;
            if (!(*Entry)["token"].is_integer()) throw std::runtime_error("Expected integer vector enclosure identity.");
            const auto Token = (*Entry)["token"].value<int64_t>();
            if (!Token || *Token <= 0 || *Token > UINT32_MAX) throw std::runtime_error("Invalid vector enclosure identity.");
            Enclosure.Token = uint32_t(*Token);
            Enclosure.EnclosureToken = DecodeEnclosureToken(*Entry);
            Enclosure.Name = DecodeOptional<std::string>(*Entry, "name", "Group");
            Enclosure.FolderEnabled = DecodeOptional<bool>(*Entry, "folder_enabled", false);
            Enclosure.VisibilityEnabled = DecodeOptional<bool>(*Entry, "visible", true);
            Enclosure.LockEnabled = DecodeOptional<bool>(*Entry, "locked", false);
            Enclosure.GroupWideEditingEnabled = DecodeOptional<bool>(*Entry, "group_wide_editing", false);
            Document.Enclosures.push_back(std::move(Enclosure));
        }
    }
    (void)ConstructVectorAuthoringCoverage(Document); return Document;
}
}
