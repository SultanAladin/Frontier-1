#include "IconArt.h"
#include "ContentDirectory.h"
#include <thorvg.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>
#include <cctype>
#include <set>
#include <tuple>
#include <string_view>
#include <sstream>
#include <iomanip>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Frontier {
namespace {
constexpr const char* Files[] = {
#define FRONTIER_ICON(Symbol, File) File,
#include "IconSymbols.inc"
#undef FRONTIER_ICON
};
constexpr const char* Names[] = {
#define FRONTIER_ICON(Symbol, File) #Symbol,
#include "IconSymbols.inc"
#undef FRONTIER_ICON
};
static_assert(std::size(Files) == static_cast<size_t>(IconSymbol::Count));
struct ReleasePaint { void operator()(tvg::Picture* Picture) const { tvg::Paint::rel(Picture); } };

// Conservative admission for this pinned ThorVG SVG implementation. This is not an XML security parser:
// IconArt accepts shipped local artwork only, never arbitrary user SVG uploads.
std::string Unsupported(const std::string& Svg) {
    static const std::set<std::string> Allowed={"svg","title","desc","defs","g","path","rect","circle",
        "ellipse","line","polyline","polygon","linearGradient","radialGradient","stop","clipPath",
        "mask","filter","feGaussianBlur"};
    std::set<std::string> Found;
    for(size_t At=0;(At=Svg.find('<',At))!=std::string::npos;){
        ++At;while(At<Svg.size()&&std::isspace(static_cast<unsigned char>(Svg[At])))++At;
        const size_t Begin=At;
        while(At<Svg.size()&&(std::isalnum(static_cast<unsigned char>(Svg[At]))||Svg[At]=='_'||Svg[At]==':'||Svg[At]=='-'||Svg[At]=='.'))++At;
        if(At>Begin){const std::string Tag=Svg.substr(Begin,At-Begin);if(!Allowed.count(Tag))Found.insert(Tag);}
    }
    if(Svg.find("<!ENTITY")!=std::string::npos||Svg.find("<!DOCTYPE")!=std::string::npos) Found.insert("XML declaration");
    if(Svg.find("href=")!=std::string::npos||Svg.find("href =")!=std::string::npos) Found.insert("external/reference resource");
    std::string Message;
    for(const auto& Tag:Found){if(!Message.empty())Message+=", ";Message+=Tag;}
    return Message;
}
// Only the default asset root is discovered. Explicit custom roots must fail honestly.
std::filesystem::path ResolveRoot(const std::filesystem::path& Root)
{
    return ResolveContentDirectory(Root);
}

std::string SourceMarker(const std::string& Source) {
    uint64_t Hash = 14695981039346656037ull;
    for (unsigned char Byte : Source) { Hash ^= Byte; Hash *= 1099511628211ull; }
    std::ostringstream Text;
    Text << "<!-- frontier-source-fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << Hash << " -->";
    return Text.str();
}
// Compatibility files are SVG, not baked pixels. Refuse stale or unsupported variants.
// This fingerprint is a freshness check for trusted local assets, not authentication.
bool LoadVectorVariant(const std::filesystem::path& Path, std::string& Source, std::string& Warning) {
    std::ifstream File(Path, std::ios::binary | std::ios::ate);
    if (!File) { Warning += "; vector variant missing: " + Path.string(); return false; }
    if (File.tellg() <= 0 || File.tellg() > 1024*1024) { Warning += "; invalid vector variant size"; return false; }
    std::string Variant(static_cast<size_t>(File.tellg()), '\0');
    File.seekg(0); File.read(Variant.data(), Variant.size());
    if (!File || Variant.rfind(SourceMarker(Source), 0) != 0) { Warning += "; stale/unreadable vector variant"; return false; }
    if (!Unsupported(Variant).empty()) { Warning += "; vector variant contains unsupported SVG"; return false; }
    Source = std::move(Variant);
    Warning = "ThorVG vector compatibility artwork (see Icons/ThorVG/manifest.json): " + Warning;
    return true;
}
void Substitute(IconRaster& Raster) {
    Raster.Substitute=true;
    Raster.Rgba.assign(size_t(Raster.Width)*Raster.Height*4,0);
    for(uint32_t Y=0;Y<Raster.Height;++Y)for(uint32_t X=0;X<Raster.Width;++X){
        const float U=(X+.5f)/Raster.Width,V=(Y+.5f)/Raster.Height;
        const bool Rim=(U>.1f&&U<.9f&&V>.1f&&V<.9f)&&(U<.18f||U>.82f||V<.18f||V>.82f);
        const bool Cross=U>.25f&&U<.75f&&V>.25f&&V<.75f&&(std::fabs(U-V)<.06f||std::fabs(U+V-1)<.06f);
        if(Rim||Cross){const size_t P=(size_t(Y)*Raster.Width+X)*4;Raster.Rgba[P]=234;Raster.Rgba[P+1]=124;Raster.Rgba[P+2]=150;Raster.Rgba[P+3]=255;}
    }
}
}
struct IconArt::Details {
    std::filesystem::path Root;
    size_t Limit;
    bool Initialized;
    uint64_t Clock=0;
    IconStatistics Counts;
    using Key=std::tuple<IconSymbol,uint32_t,uint32_t,IconPolicy>;
    struct Entry { std::shared_ptr<const IconRaster> Raster; uint64_t Used; };
    std::map<Key,Entry> Completed;
    // Original SVG text is read once per symbol, so alternate display scales do not reopen files.
    struct Document { std::string Text, Warning; IconResult Result=IconResult::Ready; };
    std::map<IconSymbol,Document> Documents;
    Details(std::filesystem::path Path,size_t Budget):Root(ResolveRoot(Path)),Limit(Budget),
        Initialized(tvg::Initializer::init(0)==tvg::Result::Success){if(!Initialized)tvg::Initializer::term();}
    ~Details(){Completed.clear();Documents.clear();if(Initialized)tvg::Initializer::term();}
};
IconArt::IconArt(std::filesystem::path Root,size_t ByteLimit):Details_(std::make_unique<Details>(std::move(Root),ByteLimit)){}
IconArt::~IconArt()=default;
IconDomain IconArt::Domain(IconSymbol Symbol) noexcept
{
    const std::string_view Path = Filename(Symbol);
    if (Path.starts_with("Texture/")) return IconDomain::Texture;
    if (Path.starts_with("Fluid/")) return IconDomain::Fluid;
    return IconDomain::Editor;
}
bool IconArt::Available(IconSymbol Symbol, IconDomain Workspace) noexcept
{
    return Symbol < IconSymbol::Count && Workspace <= IconDomain::Fluid &&
        (Domain(Symbol) == IconDomain::Editor || Domain(Symbol) == Workspace);
}
std::vector<IconSymbol> IconArt::Catalogue(IconDomain Workspace)
{
    std::vector<IconSymbol> Symbols;
    for (uint32_t Index = 0; Index < uint32_t(IconSymbol::Count); ++Index)
        if (Available(IconSymbol(Index), Workspace)) Symbols.push_back(IconSymbol(Index));
    return Symbols;
}
const char* IconArt::Filename(IconSymbol Symbol) noexcept {return Symbol<IconSymbol::Count?Files[static_cast<size_t>(Symbol)]:"";}
const char* IconArt::Name(IconSymbol Symbol) noexcept {return Symbol<IconSymbol::Count?Names[static_cast<size_t>(Symbol)]:"Invalid";}
const char* IconArt::ResultName(IconResult Result) noexcept {
    switch(Result){case IconResult::Ready:return "ready";case IconResult::Unsupported:return "unsupported";
    case IconResult::Missing:return "missing";case IconResult::DecodeFailure:return "decode-failure";
    case IconResult::InvalidRequest:return "invalid-request";default:return "runtime-failure";}
}
void IconArt::Clear(){auto& D=*Details_;D.Completed.clear();D.Documents.clear();D.Counts.ResidentBytes=0;D.Counts.ResidentCount=0;}
IconStatistics IconArt::Statistics()const{return Details_->Counts;}
std::shared_ptr<const IconRaster> IconArt::Rasterize(IconSymbol Symbol,float LogicalWidth,float LogicalHeight,float DisplayScale,IconPolicy Policy,IconDomain Workspace){
    auto& D=*Details_;
    const double W=std::ceil(double(LogicalWidth)*DisplayScale),H=std::ceil(double(LogicalHeight)*DisplayScale);
    if(!Available(Symbol,Workspace)||!std::isfinite(W)||!std::isfinite(H)||LogicalWidth<=0||LogicalHeight<=0||DisplayScale<=0||W<1||H<1||W>512||H>512){
        auto Bad=std::make_shared<IconRaster>();Bad->Width=Bad->Height=24;Bad->Result=IconResult::InvalidRequest;
        Bad->Diagnostic="Expected an icon available in this workspace and positive finite dimensions, at most 512 physical pixels per axis.";Substitute(*Bad);return Bad;
    }
    const auto Width=static_cast<uint32_t>(W),Height=static_cast<uint32_t>(H);
    const Details::Key Key{Symbol,Width,Height,Policy};
    if(auto I=D.Completed.find(Key);I!=D.Completed.end()){I->second.Used=++D.Clock;++D.Counts.Reuses;return I->second.Raster;}
    auto Raster=std::make_shared<IconRaster>();Raster->Width=Width;Raster->Height=Height;
    if(!D.Initialized){Raster->Result=IconResult::RuntimeFailure;Raster->Diagnostic="ThorVG runtime initialization failed.";}
    else{
        auto [I,Inserted]=D.Documents.try_emplace(Symbol);auto& Document=I->second;
        if(Inserted){
            std::ifstream File(D.Root/Filename(Symbol),std::ios::binary|std::ios::ate);
            if(!File){Document.Result=IconResult::Missing;Document.Warning="SVG file could not be opened: "+(D.Root/Filename(Symbol)).string();}
            else if(File.tellg()<=0||File.tellg()>1024*1024){Document.Result=IconResult::DecodeFailure;Document.Warning="SVG must contain 1 to 1048576 bytes.";}
            else {const auto Length=static_cast<size_t>(File.tellg());Document.Text.resize(Length);File.seekg(0);File.read(Document.Text.data(),Length);
                if(!File){Document.Result=IconResult::DecodeFailure;Document.Warning="SVG read failed.";}
                else{Document.Warning=Unsupported(Document.Text);if(!Document.Warning.empty()) {
                    Document.Result=LoadVectorVariant(D.Root/"ThorVG"/Filename(Symbol),Document.Text,Document.Warning)
                        ? IconResult::Ready : IconResult::Unsupported;
                }}}
        }
        Raster->Result=Document.Result;Raster->Diagnostic=Document.Warning;
        if(Document.Result==IconResult::Ready||(Document.Result==IconResult::Unsupported&&Policy==IconPolicy::Diagnostic)){
            std::vector<uint32_t> Packed(size_t(Width)*Height,0);
            std::unique_ptr<tvg::SwCanvas> Canvas(tvg::SwCanvas::gen());
            std::unique_ptr<tvg::Picture,ReleasePaint> Picture(tvg::Picture::gen());
            bool Ok=Canvas&&Picture;
            if(Ok){++D.Counts.Decodes;Ok=Picture->load(Document.Text.data(),static_cast<uint32_t>(Document.Text.size()),"svg",nullptr,true)==tvg::Result::Success;}
            float IntrinsicWidth=0,IntrinsicHeight=0;
            if(Ok)Ok=Picture->size(&IntrinsicWidth,&IntrinsicHeight)==tvg::Result::Success&&std::isfinite(IntrinsicWidth)&&std::isfinite(IntrinsicHeight)&&IntrinsicWidth>0&&IntrinsicHeight>0;
            if(Ok){
                const float Fit=std::min(Width/IntrinsicWidth,Height/IntrinsicHeight);
                Ok=Picture->size(IntrinsicWidth*Fit,IntrinsicHeight*Fit)==tvg::Result::Success;
                if(Ok)Ok=Picture->translate((Width-IntrinsicWidth*Fit)*.5f,(Height-IntrinsicHeight*Fit)*.5f)==tvg::Result::Success;
                if(Ok)Ok=Canvas->target(Packed.data(),Width,Width,Height,tvg::ColorSpace::ABGR8888S)==tvg::Result::Success;
                if(Ok){Ok=Canvas->add(Picture.get())==tvg::Result::Success;if(Ok)Picture.release();}
                if(Ok){++D.Counts.Rasterizations;Ok=Canvas->draw(true)==tvg::Result::Success;if(Ok)Ok=Canvas->sync()==tvg::Result::Success;}
            }
            if(!Ok){Raster->Result=IconResult::DecodeFailure;Raster->Diagnostic="ThorVG could not load, size, or render this SVG. "+Raster->Diagnostic;}
            else{
                Raster->Rgba.resize(Packed.size()*4);
                for(size_t P=0;P<Packed.size();++P){const uint32_t C=Packed[P];Raster->Rgba[P*4]=C&255;Raster->Rgba[P*4+1]=(C>>8)&255;Raster->Rgba[P*4+2]=(C>>16)&255;Raster->Rgba[P*4+3]=C>>24;}
            }
        }
    }
    if(Raster->Rgba.empty())Substitute(*Raster);
    const size_t Bytes=Raster->Rgba.size();
    if(Bytes<=D.Limit){
        while(!D.Completed.empty()&&(D.Counts.ResidentBytes+Bytes>D.Limit||D.Completed.size()>=512)){
            auto Old=std::min_element(D.Completed.begin(),D.Completed.end(),[](const auto& A,const auto& B){return A.second.Used<B.second.Used;});
            D.Counts.ResidentBytes-=Old->second.Raster->Rgba.size();D.Completed.erase(Old);++D.Counts.Evictions;
        }
        D.Completed.emplace(Key,Details::Entry{Raster,++D.Clock});D.Counts.ResidentBytes+=Bytes;D.Counts.ResidentCount=D.Completed.size();
    }
    return Raster;
}
}
