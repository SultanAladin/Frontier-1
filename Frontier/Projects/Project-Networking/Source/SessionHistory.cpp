#include "RoomRuntime.h"
#include "HistoryFormat.h"
#include "LocalConfiguration.h"
#include <eos_playerdatastorage.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <fstream>
#include <iterator>
#include <random>

namespace Networking
{
namespace
{
HistoryReading History;
EOS_HPlayerDataStorage Store = nullptr;
EOS_ProductUserId HistoryUser = nullptr;
DiagnosticReception HistoryLog = nullptr;
std::filesystem::path AccountDirectory;
std::uintptr_t Epoch = 1;
void* Cookie() { return reinterpret_cast<void*>(Epoch); }
bool Current(void* C) { return Store && C == Cookie(); }
EOS_HPlayerDataStorageFileTransferRequest Request = nullptr;
std::deque<std::string> Uploads, Downloads;
std::string Payload, TransferName;
size_t Offset = 0;
bool Listing = false, Transfer = false, Reading = false, Paused = false;
void State(const char* Operation, EOS_EResult Result)
{
    History.Status = std::string(Operation) + ": " + EOS_EResult_ToString(Result);
    if (HistoryLog) HistoryLog(History.Status.c_str());
}
std::string NewId()
{
    std::random_device Random;
    constexpr char Hex[] = "0123456789abcdef";
    std::string Id(32, '0');
    for (auto& C : Id) C = Hex[Random() & 15];
    return Id;
}
bool HistoryFilename(const std::string& Name)
{
    return Name.size() == 46 && Name.compare(0, 10, "charge-v1-") == 0 &&
        Name.compare(42, 4, ".txt") == 0 && ValidHistoryId(std::string_view(Name).substr(10, 32));
}
void Cache(const std::string& Name, const std::string& Text, bool Pending)
{
    std::error_code Error;
    std::filesystem::create_directories(AccountDirectory, Error);
    if (Error) { History.Status = "Local history cache unavailable"; return; }
    const auto Target = AccountDirectory / Name;
    auto MarkerPath = Target; MarkerPath += ".pending";
    if (!Pending && std::filesystem::exists(MarkerPath, Error)) return;
    auto Temporary = Target; Temporary += ".tmp";
    std::ofstream File(Temporary, std::ios::binary | std::ios::trunc);
    File.write(Text.data(), static_cast<std::streamsize>(Text.size())); File.close();
    if (!File) { History.Status = "Local history write failed"; return; }
#if defined(_WIN32)
    if (!MoveFileExW(std::filesystem::path(Temporary).c_str(), Target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    { History.Status = "Could not finalize local history cache"; return; }
#else
    std::filesystem::rename(Temporary, Target, Error);
    if (Error) { History.Status = "Could not finalize local history cache"; return; }
#endif
    if (Pending) { std::ofstream Marker(MarkerPath); Marker << "retry\n"; }
}
EOS_PlayerDataStorage_EReadResult EOS_CALL ReadChunk(const EOS_PlayerDataStorage_ReadFileDataCallbackInfo* C)
{
    if (!Current(C->ClientData) || !Transfer || !Reading || C->TotalFileSizeBytes > 4096 ||
        Payload.size() + C->DataChunkLengthBytes > 4096) return EOS_PlayerDataStorage_EReadResult::EOS_RR_FailRequest;
    Payload.append(static_cast<const char*>(C->DataChunk), C->DataChunkLengthBytes);
    return EOS_PlayerDataStorage_EReadResult::EOS_RR_ContinueReading;
}
EOS_PlayerDataStorage_EWriteResult EOS_CALL WriteChunk(const EOS_PlayerDataStorage_WriteFileDataCallbackInfo* C,
    void* Buffer, uint32_t* Written)
{
    *Written = 0;
    if (!Current(C->ClientData) || !Transfer || Reading) return EOS_PlayerDataStorage_EWriteResult::EOS_WR_FailRequest;
    const size_t Bytes = std::min(Payload.size() - Offset, static_cast<size_t>(C->DataBufferLengthBytes));
    std::memcpy(Buffer, Payload.data() + Offset, Bytes); Offset += Bytes;
    *Written = static_cast<uint32_t>(Bytes);
    return Offset == Payload.size() ? EOS_PlayerDataStorage_EWriteResult::EOS_WR_CompleteRequest : EOS_PlayerDataStorage_EWriteResult::EOS_WR_ContinueWriting;
}
void CompleteTransfer(EOS_EResult Result)
{
    if (Request) EOS_PlayerDataStorageFileTransferRequest_Release(Request);
    Request = nullptr;
    if (Result == EOS_EResult::EOS_Success)
    {
        if (Reading)
        {
            HistoryRecord Record;
            if (DecodeHistory(Payload, Record) && TransferName == "charge-v1-" + Record.Id + ".txt")
            { MergeHistory(History, Record); Cache(TransferName, Payload, false); }
            else History.Status = "Rejected malformed cloud history record";
        }
        else
        {
            std::error_code Error;
            std::filesystem::remove(AccountDirectory / (TransferName + ".pending"), Error);
            if (!Uploads.empty()) Uploads.pop_front();
            State("cloud_write", Result);
        }
    }
    else
    {
        State(Reading ? "cloud_read" : "cloud_write", Result);
        // Leave unsynced records on disk. Never silently downgrade to fake cloud success.
        if (!Reading) Paused = true;
    }
    Transfer = false; Payload.clear();
    History.Pending = static_cast<unsigned>(Uploads.size());
}
void EOS_CALL ReadDone(const EOS_PlayerDataStorage_ReadFileCallbackInfo* C)
{ if (Current(C->ClientData) && Transfer && Reading) CompleteTransfer(C->ResultCode); }
void EOS_CALL WriteDone(const EOS_PlayerDataStorage_WriteFileCallbackInfo* C)
{ if (Current(C->ClientData) && Transfer && !Reading) CompleteTransfer(C->ResultCode); }
void EOS_CALL Listed(const EOS_PlayerDataStorage_QueryFileListCallbackInfo* C)
{
    if (!Current(C->ClientData)) return;
    Listing = false;
    if (C->ResultCode != EOS_EResult::EOS_Success) { State("cloud_list", C->ResultCode); return; }
    struct File { std::string Name; std::int64_t Time; };
    std::vector<File> Files;
    // Bound metadata enumeration even if another application/client wrote many files.
    for (uint32_t Index = 0; Index < std::min(C->FileCount, 4096u); ++Index)
    {
        EOS_PlayerDataStorage_CopyFileMetadataAtIndexOptions O{};
        O.ApiVersion = EOS_PLAYERDATASTORAGE_COPYFILEMETADATAATINDEX_API_LATEST;
        O.LocalUserId = HistoryUser; O.Index = Index;
        EOS_PlayerDataStorage_FileMetadata* M = nullptr;
        if (EOS_PlayerDataStorage_CopyFileMetadataAtIndex(Store, &O, &M) == EOS_EResult::EOS_Success && M)
        {
            if (M->Filename && HistoryFilename(M->Filename) && M->UnencryptedDataSizeBytes <= 4096)
                Files.push_back({M->Filename, M->LastModifiedTime});
            EOS_PlayerDataStorage_FileMetadata_Release(M);
        }
    }
    std::sort(Files.begin(), Files.end(), [](const auto& A, const auto& B) { return A.Time > B.Time; });
    for (size_t I = 0; I < std::min<size_t>(64, Files.size()); ++I) Downloads.push_back(Files[I].Name);
    History.Status = "Downloading recent EOS cloud history";
}
}
const HistoryReading& InspectHistory() noexcept { return History; }
void RetryCloudSync()
{
    if (!Store || !History.Enabled || Listing || Transfer || !Downloads.empty()) return;
    Paused = false; Listing = true; History.Busy = true;
    EOS_PlayerDataStorage_QueryFileListOptions O{};
    O.ApiVersion = EOS_PLAYERDATASTORAGE_QUERYFILELIST_API_LATEST; O.LocalUserId = HistoryUser;
    EOS_PlayerDataStorage_QueryFileList(Store, &O, Cookie(), Listed);
}
void BindHistory(EOS_HPlatform Platform, EOS_ProductUserId User, DiagnosticReception Log,
    const std::filesystem::path& Root, bool Enabled)
{
    DetachHistory();
    Store = EOS_Platform_GetPlayerDataStorageInterface(Platform); HistoryUser = User; HistoryLog = Log;
    History.Enabled = Enabled && Store;
    char Id[EOS_PRODUCTUSERID_MAX_LENGTH + 1]{}; int32_t Length = sizeof(Id);
    if (EOS_ProductUserId_ToString(User, Id, &Length) != EOS_EResult::EOS_Success || !SafeRecordIdentifier(Id))
    { Store = nullptr; History.Enabled = false; return; }
    AccountDirectory = Root / "history" / Id;
    std::error_code Error;
    std::filesystem::create_directories(AccountDirectory, Error);
    if (!Error)
    {
        size_t Count = 0;
        for (std::filesystem::directory_iterator It(AccountDirectory, Error), End; !Error && It != End && Count < 4096; It.increment(Error), ++Count)
        {
            auto Name = It->path().filename().string();
            if (!HistoryFilename(Name) || It->file_size(Error) > 4096 || Error) continue;
            std::ifstream F(It->path(), std::ios::binary);
            std::string Text((std::istreambuf_iterator<char>(F)), {});
            HistoryRecord R;
            if (DecodeHistory(Text, R) && Name == "charge-v1-" + R.Id + ".txt")
            {
                MergeHistory(History, R);
                if (std::filesystem::exists(AccountDirectory / (Name + ".pending"), Error)) Uploads.push_back(Name);
            }
        }
    }
    History.Pending = static_cast<unsigned>(Uploads.size());
    if (History.Enabled) RetryCloudSync();
}
void RecordHistory(const char* Kind, const RoomReading& Room, std::int64_t Duration)
{
    if (!HistoryUser) return;
    HistoryRecord R;
    R.Id = NewId(); R.Kind = Kind; R.Lobby = Room.LobbyId; R.Session = Room.SessionId;
    R.Utc = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    R.Duration = std::clamp<std::int64_t>(Duration, 0, 604800);
    for (const auto& P : Room.Players) P.Dummy ? ++R.DummyPlayers : ++R.RealPlayers;
    auto Text = EncodeHistory(R); auto Name = "charge-v1-" + R.Id + ".txt";
    Cache(Name, Text, true); MergeHistory(History, R); Uploads.push_back(Name);
    History.Pending = static_cast<unsigned>(Uploads.size());
}
void TickHistory()
{
    History.Busy = Listing || Transfer || (!Paused && History.Enabled && (!Downloads.empty() || !Uploads.empty()));
    if (!Store || !History.Enabled || Listing || Transfer || Paused) return;
    if (!Downloads.empty())
    {
        TransferName = Downloads.front(); Downloads.pop_front(); Payload.clear(); Transfer = true; Reading = true;
        EOS_PlayerDataStorage_ReadFileOptions O{};
        O.ApiVersion = EOS_PLAYERDATASTORAGE_READFILE_API_LATEST; O.LocalUserId = HistoryUser;
        O.Filename = TransferName.c_str(); O.ReadChunkLengthBytes = 4096; O.ReadFileDataCallback = ReadChunk;
        Request = EOS_PlayerDataStorage_ReadFile(Store, &O, Cookie(), ReadDone);
    }
    else if (!Uploads.empty())
    {
        TransferName = Uploads.front();
        std::error_code E;
        if (std::filesystem::file_size(AccountDirectory / TransferName, E) > 4096 || E)
        { Paused = true; History.Status = "Local pending history file is unavailable"; return; }
        std::ifstream F(AccountDirectory / TransferName, std::ios::binary);
        Payload.assign(std::istreambuf_iterator<char>(F), {});
        HistoryRecord R;
        if (!DecodeHistory(Payload, R)) { Paused = true; History.Status = "Local history rejected; cloud not overwritten"; return; }
        Offset = 0; Transfer = true; Reading = false;
        EOS_PlayerDataStorage_WriteFileOptions O{};
        O.ApiVersion = EOS_PLAYERDATASTORAGE_WRITEFILE_API_LATEST; O.LocalUserId = HistoryUser;
        O.Filename = TransferName.c_str(); O.ChunkLengthBytes = 4096; O.WriteFileDataCallback = WriteChunk;
        Request = EOS_PlayerDataStorage_WriteFile(Store, &O, Cookie(), WriteDone);
    }
    else { History.Busy = false; return; }
    if (!Request) { Transfer = false; Paused = true; History.Status = "EOS cloud transfer could not start; check client-policy permissions and the data key"; }
}
void DetachHistory()
{
    ++Epoch;
    if (Request) { EOS_PlayerDataStorageFileTransferRequest_CancelRequest(Request); EOS_PlayerDataStorageFileTransferRequest_Release(Request); }
    Request = nullptr; Store = nullptr; HistoryUser = nullptr; HistoryLog = nullptr;
    History = {}; Uploads.clear(); Downloads.clear(); Payload.clear(); TransferName.clear();
    Listing = Transfer = Reading = Paused = false; AccountDirectory.clear(); Offset = 0;
}
}
