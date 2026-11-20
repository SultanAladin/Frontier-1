/*============================================================================================================================================
                                                          AUDIOHARDWAREOUTPUT.CPP
============================================================================================================================================*/
// 🧩 Implementation of WASAPI / dynamic Pulse & ALSA / AAudio / HeadlessBuffer audio sink with bounded capture export

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/AudioSpace/AudioHardwareOutput.h"
#include "Engine/Threading/Sync/SpinLock.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <objbase.h>
    #include <mmreg.h>
    #include <mmdeviceapi.h>
    #include <audioclient.h>
    #ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
        #define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000u
    #endif
    #ifndef AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
        #define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000u
    #endif
#elif defined(__ANDROID__)
    #include <dlfcn.h>
#elif defined(__linux__)
    #include <dlfcn.h>
#endif

namespace Frontier::AudioSpace
{

namespace
{

#ifdef _WIN32
constexpr GUID FrontierClsidMmDeviceEnumerator = {
    0xBCDE0395u, 0xE52Fu, 0x467Cu, { 0x8Eu, 0x3Du, 0xC4u, 0x57u, 0x92u, 0x91u, 0x69u, 0x2Eu }
};
constexpr GUID FrontierIidImmDeviceEnumerator = {
    0xA95664D2u, 0x9614u, 0x4F35u, { 0xA7u, 0x46u, 0xDEu, 0x8Du, 0xB6u, 0x36u, 0x17u, 0xE6u }
};
constexpr GUID FrontierIidIaudioClient = {
    0x1CB9AD4Cu, 0xDBFAu, 0x4C32u, { 0xB1u, 0x78u, 0xC2u, 0xF5u, 0x68u, 0xA7u, 0x03u, 0xB2u }
};
constexpr GUID FrontierIidIaudioRenderClient = {
    0xF294ACFCu, 0x3146u, 0x4483u, { 0xA7u, 0xBFu, 0xADu, 0xDCu, 0xA7u, 0xC2u, 0x60u, 0xE2u }
};
constexpr GUID FrontierKsSubFormatFloat32 = {
    0x00000003u, 0x0000u, 0x0010u, { 0x80u, 0x00u, 0x00u, 0xAAu, 0x00u, 0x38u, 0x9Bu, 0x71u }
};
#endif

#if defined(__linux__) || defined(__ANDROID__)
template <typename FnType>
void ResolveDynamicSymbol(void* LibraryHandle, const char* SymbolName, FnType& OutFunction) noexcept
{
    void* RawAddress = dlsym(LibraryHandle, SymbolName);
    std::memcpy(&OutFunction, &RawAddress, sizeof(OutFunction));
}
#endif

#if defined(__linux__) && !defined(__ANDROID__)
struct PulseSampleSpecification
{
    int32_t  Format     = 5;     // PA_SAMPLE_FLOAT32LE
    uint32_t Rate       = 48000u;
    uint8_t  Channels   = 2u;
};

struct PulseBufferAttr
{
    uint32_t MaxLength  = 0xFFFFFFFFu;
    uint32_t TargetLen  = 0xFFFFFFFFu;
    uint32_t Prebuf     = 0xFFFFFFFFu;
    uint32_t MinReq     = 0xFFFFFFFFu;
    uint32_t FragSize   = 0xFFFFFFFFu;
};
#endif

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PLATFORM ENDPOINT
//------------------------------------------------------------------------------------------------------------------------

struct AudioHardwareOutput::PlatformEndpoint
{
    AudioEndpointConfiguration Config           {};
    AudioOutputDriverCategory ActiveDriver    = AudioOutputDriverCategory::HeadlessBuffer;
    uint32_t                   ActiveSampleRate = Constants::DefaultAcousticSampleRateHz;
    uint32_t                   ActiveChannels   = 2u;
    std::atomic<bool>          EndpointOpen     { false };
    std::atomic<uint64_t>      SubmittedPoints  { 0ull };
    std::atomic<uint32_t>      UnderrunCounter  { 0u };

    mutable Threading::SpinLock CaptureLock             {};
    bool                        CaptureEnabled          = false;
    uint32_t                    MaxCapturedSamplePoints = 480000u;
    std::vector<float>          CapturedInterleaved     {};

#ifdef _WIN32
    IMMDevice*          WasapiDevice             = nullptr;
    IAudioClient*       WasapiClient             = nullptr;
    IAudioRenderClient* WasapiRenderClient       = nullptr;
    UINT32              WasapiBufferSamplePoints = 0u;
    bool                ComInitializedHere       = false;
#elif defined(__linux__) && !defined(__ANDROID__)
    void* PulseLibHandle = nullptr;
    void* PulseStreamPtr = nullptr;
    int   (*FnPulseWrite)(void*, const void*, std::size_t, int*) = nullptr;
    void  (*FnPulseFree)(void*)                                   = nullptr;

    void* AlsaLibHandle = nullptr;
    void* AlsaPcmPtr    = nullptr;
    int   (*FnSndPcmWritei)(void*, const void*, unsigned long)    = nullptr;
    int   (*FnSndPcmRecover)(void*, int, int)                     = nullptr;
    int   (*FnSndPcmClose)(void*)                                 = nullptr;
#elif defined(__ANDROID__)
    void* AaudioLibHandle = nullptr;
    void* AaudioStreamPtr = nullptr;
    int   (*FnAaudioWrite)(void*, const void*, int32_t, int64_t)  = nullptr;
    int   (*FnAaudioClose)(void*)                                 = nullptr;
#endif

    [[nodiscard]] bool TryOpenNativeDriver(const AudioEndpointConfiguration& Requested) noexcept
    {
        (void)Requested;
#ifdef _WIN32
        const HRESULT CoHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(CoHr))
        {
            ComInitializedHere = true;
        }
        else if (CoHr != RPC_E_CHANGED_MODE)
        {
            return false;
        }

        IMMDeviceEnumerator* Enumerator = nullptr;
        HRESULT Hr = CoCreateInstance(
            FrontierClsidMmDeviceEnumerator,
            nullptr,
            CLSCTX_ALL,
            FrontierIidImmDeviceEnumerator,
            reinterpret_cast<void**>(&Enumerator));
        if (FAILED(Hr) || Enumerator == nullptr)
        {
            CloseNativeHandles();
            return false;
        }

        Hr = Enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &WasapiDevice);
        Enumerator->Release();
        if (FAILED(Hr) || WasapiDevice == nullptr)
        {
            CloseNativeHandles();
            return false;
        }

        Hr = WasapiDevice->Activate(FrontierIidIaudioClient, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&WasapiClient));
        if (FAILED(Hr) || WasapiClient == nullptr)
        {
            CloseNativeHandles();
            return false;
        }

        WAVEFORMATEXTENSIBLE TargetFmt{};
        TargetFmt.Format.wFormatTag           = WAVE_FORMAT_EXTENSIBLE;
        TargetFmt.Format.nChannels            = static_cast<WORD>(std::clamp(Requested.ChannelCount, 1u, Constants::MaxAcousticOutputChannels));
        TargetFmt.Format.nSamplesPerSec       = Requested.SampleRateHz;
        TargetFmt.Format.wBitsPerSample       = 32u;
        TargetFmt.Format.nBlockAlign          = static_cast<WORD>(TargetFmt.Format.nChannels * 4u);
        TargetFmt.Format.nAvgBytesPerSec      = TargetFmt.Format.nSamplesPerSec * static_cast<DWORD>(TargetFmt.Format.nBlockAlign);
        TargetFmt.Format.cbSize               = static_cast<WORD>(sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX));
        TargetFmt.Samples.wValidBitsPerSample = 32u;
        TargetFmt.dwChannelMask               = (TargetFmt.Format.nChannels == 1u) ? 0x4u :
                                                (TargetFmt.Format.nChannels == 2u) ? 0x3u :
                                                (TargetFmt.Format.nChannels == 6u) ? 0x3Fu : 0x63Fu;
        TargetFmt.SubFormat                   = FrontierKsSubFormatFloat32;

        const REFERENCE_TIME BufferDuration = static_cast<REFERENCE_TIME>(std::max(10u, Requested.HardwareBufferLatencyMs)) * 10000LL;
        Hr = WasapiClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            BufferDuration,
            0,
            &TargetFmt.Format,
            nullptr);
        if (FAILED(Hr))
        {
            CloseNativeHandles();
            return false;
        }

        Hr = WasapiClient->GetBufferSize(&WasapiBufferSamplePoints);
        if (FAILED(Hr) || WasapiBufferSamplePoints == 0u)
        {
            CloseNativeHandles();
            return false;
        }

        Hr = WasapiClient->GetService(FrontierIidIaudioRenderClient, reinterpret_cast<void**>(&WasapiRenderClient));
        if (FAILED(Hr) || WasapiRenderClient == nullptr)
        {
            CloseNativeHandles();
            return false;
        }

        Hr = WasapiClient->Start();
        if (FAILED(Hr))
        {
            CloseNativeHandles();
            return false;
        }

        ActiveDriver    = AudioOutputDriverCategory::WindowsWasapi;
        ActiveSampleRate = Requested.SampleRateHz;
        ActiveChannels   = TargetFmt.Format.nChannels;
        return true;

#elif defined(__linux__) && !defined(__ANDROID__)
        const bool WantPulse = (Requested.RequestedDriver == AudioOutputDriverCategory::AutoSelect ||
                                Requested.RequestedDriver == AudioOutputDriverCategory::LinuxPulse);
        const bool WantAlsa  = (Requested.RequestedDriver == AudioOutputDriverCategory::AutoSelect ||
                                Requested.RequestedDriver == AudioOutputDriverCategory::LinuxAlsa);

        if (WantPulse)
        {
            PulseLibHandle = dlopen("libpulse-simple.so.0", RTLD_LAZY);
            if (PulseLibHandle != nullptr)
            {
                using FnPulseNew = void* (*)(const char*, const char*, int, const char*, const char*,
                                             const PulseSampleSpecification*, const void*, const PulseBufferAttr*, int*);
                FnPulseNew FnNew = nullptr;
                ResolveDynamicSymbol(PulseLibHandle, "pa_simple_new", FnNew);
                ResolveDynamicSymbol(PulseLibHandle, "pa_simple_write", FnPulseWrite);
                ResolveDynamicSymbol(PulseLibHandle, "pa_simple_free", FnPulseFree);

                if (FnNew != nullptr && FnPulseWrite != nullptr && FnPulseFree != nullptr)
                {
                    PulseSampleSpecification Spec{};
                    Spec.Format   = 5; // PA_SAMPLE_FLOAT32LE
                    Spec.Rate     = Requested.SampleRateHz;
                    Spec.Channels = static_cast<uint8_t>(std::clamp(Requested.ChannelCount, 1u, Constants::MaxAcousticOutputChannels));

                    PulseBufferAttr Attr{};
                    const uint32_t LatencyBytes = (Requested.SampleRateHz * Requested.HardwareBufferLatencyMs / 1000u) *
                                                  Spec.Channels * 4u;
                    Attr.TargetLen = std::max<uint32_t>(LatencyBytes, 1024u);

                    int ErrorCode = 0;
                    PulseStreamPtr = FnNew(
                        nullptr,
                        "FrontierEngine",
                        1, // PA_STREAM_PLAYBACK
                        nullptr,
                        "AcousticMix",
                        &Spec,
                        nullptr,
                        &Attr,
                        &ErrorCode);
                    if (PulseStreamPtr != nullptr)
                    {
                        ActiveDriver    = AudioOutputDriverCategory::LinuxPulse;
                        ActiveSampleRate = Requested.SampleRateHz;
                        ActiveChannels   = Spec.Channels;
                        return true;
                    }
                }
                dlclose(PulseLibHandle);
                PulseLibHandle = nullptr;
            }
        }

        if (WantAlsa)
        {
            AlsaLibHandle = dlopen("libasound.so.2", RTLD_LAZY);
            if (AlsaLibHandle != nullptr)
            {
                using FnOpen      = int (*)(void**, const char*, int, int);
                using FnSetParams = int (*)(void*, int, int, unsigned int, unsigned int, int, unsigned int);
                FnOpen      FnPcmOpen = nullptr;
                FnSetParams FnSetPar  = nullptr;
                ResolveDynamicSymbol(AlsaLibHandle, "snd_pcm_open", FnPcmOpen);
                ResolveDynamicSymbol(AlsaLibHandle, "snd_pcm_set_params", FnSetPar);
                ResolveDynamicSymbol(AlsaLibHandle, "snd_pcm_writei", FnSndPcmWritei);
                ResolveDynamicSymbol(AlsaLibHandle, "snd_pcm_recover", FnSndPcmRecover);
                ResolveDynamicSymbol(AlsaLibHandle, "snd_pcm_close", FnSndPcmClose);

                if (FnPcmOpen != nullptr && FnSetPar != nullptr && FnSndPcmWritei != nullptr && FnSndPcmClose != nullptr)
                {
                    if (FnPcmOpen(&AlsaPcmPtr, "default", 0, 0) >= 0 && AlsaPcmPtr != nullptr)
                    {
                        // SND_PCM_FORMAT_FLOAT_LE = 14, SND_PCM_ACCESS_RW_INTERLEAVED = 3
                        const unsigned int Channels   = std::clamp(Requested.ChannelCount, 1u, Constants::MaxAcousticOutputChannels);
                        const unsigned int LatencyUs  = std::max(10000u, Requested.HardwareBufferLatencyMs * 1000u);
                        if (FnSetPar(AlsaPcmPtr, 14, 3, Channels, Requested.SampleRateHz, 1, LatencyUs) >= 0)
                        {
                            ActiveDriver    = AudioOutputDriverCategory::LinuxAlsa;
                            ActiveSampleRate = Requested.SampleRateHz;
                            ActiveChannels   = Channels;
                            return true;
                        }
                        FnSndPcmClose(AlsaPcmPtr);
                        AlsaPcmPtr = nullptr;
                    }
                }
                dlclose(AlsaLibHandle);
                AlsaLibHandle = nullptr;
            }
        }
        return false;

#elif defined(__ANDROID__)
        AaudioLibHandle = dlopen("libaaudio.so", RTLD_LAZY);
        if (AaudioLibHandle != nullptr)
        {
            using FnCreateBuilder = int (*)(void**);
            using FnSetInt32      = void (*)(void*, int32_t);
            using FnOpenStream    = int (*)(void*, void**);
            using FnStartStream   = int (*)(void*);
            using FnDelBuilder    = int (*)(void*);

            FnCreateBuilder FnCreate   = nullptr;
            FnSetInt32      FnRate     = nullptr;
            FnSetInt32      FnCh       = nullptr;
            FnSetInt32      FnFmt      = nullptr;
            FnOpenStream    FnOpen     = nullptr;
            FnStartStream   FnStart    = nullptr;
            FnDelBuilder    FnDelBuild = nullptr;
            ResolveDynamicSymbol(AaudioLibHandle, "AAudio_createStreamBuilder", FnCreate);
            ResolveDynamicSymbol(AaudioLibHandle, "AAudioStreamBuilder_setSampleRate", FnRate);
            ResolveDynamicSymbol(AaudioLibHandle, "AAudioStreamBuilder_setChannelCount", FnCh);
            ResolveDynamicSymbol(AaudioLibHandle, "AAudioStreamBuilder_setFormat", FnFmt);
            ResolveDynamicSymbol(AaudioLibHandle, "AAudioStreamBuilder_openStream", FnOpen);
            ResolveDynamicSymbol(AaudioLibHandle, "AAudioStream_requestStart", FnStart);
            ResolveDynamicSymbol(AaudioLibHandle, "AAudioStreamBuilder_delete", FnDelBuild);
            ResolveDynamicSymbol(AaudioLibHandle, "AAudioStream_write", FnAaudioWrite);
            ResolveDynamicSymbol(AaudioLibHandle, "AAudioStream_close", FnAaudioClose);

            if (FnCreate && FnRate && FnCh && FnFmt && FnOpen && FnStart && FnDelBuild && FnAaudioWrite && FnAaudioClose)
            {
                void* Builder = nullptr;
                if (FnCreate(&Builder) == 0 && Builder != nullptr)
                {
                    FnRate(Builder, static_cast<int32_t>(Requested.SampleRateHz));
                    FnCh(Builder, static_cast<int32_t>(Requested.ChannelCount));
                    FnFmt(Builder, 2); // AAUDIO_FORMAT_PCM_FLOAT
                    const int OpenRes = FnOpen(Builder, &AaudioStreamPtr);
                    FnDelBuild(Builder);
                    if (OpenRes == 0 && AaudioStreamPtr != nullptr && FnStart(AaudioStreamPtr) == 0)
                    {
                        ActiveDriver    = AudioOutputDriverCategory::AndroidAaudio;
                        ActiveSampleRate = Requested.SampleRateHz;
                        ActiveChannels   = Requested.ChannelCount;
                        return true;
                    }
                    if (AaudioStreamPtr != nullptr)
                    {
                        FnAaudioClose(AaudioStreamPtr);
                        AaudioStreamPtr = nullptr;
                    }
                }
            }
            dlclose(AaudioLibHandle);
            AaudioLibHandle = nullptr;
        }
        return false;
#else
        return false;
#endif
    }

    void CloseNativeHandles() noexcept
    {
#ifdef _WIN32
        if (WasapiClient != nullptr)
        {
            WasapiClient->Stop();
        }
        if (WasapiRenderClient != nullptr)
        {
            WasapiRenderClient->Release();
            WasapiRenderClient = nullptr;
        }
        if (WasapiClient != nullptr)
        {
            WasapiClient->Release();
            WasapiClient = nullptr;
        }
        if (WasapiDevice != nullptr)
        {
            WasapiDevice->Release();
            WasapiDevice = nullptr;
        }
        if (ComInitializedHere)
        {
            CoUninitialize();
            ComInitializedHere = false;
        }
        WasapiBufferSamplePoints = 0u;
#elif defined(__linux__) && !defined(__ANDROID__)
        if (PulseStreamPtr != nullptr && FnPulseFree != nullptr)
        {
            FnPulseFree(PulseStreamPtr);
            PulseStreamPtr = nullptr;
        }
        if (PulseLibHandle != nullptr)
        {
            dlclose(PulseLibHandle);
            PulseLibHandle = nullptr;
        }
        if (AlsaPcmPtr != nullptr && FnSndPcmClose != nullptr)
        {
            FnSndPcmClose(AlsaPcmPtr);
            AlsaPcmPtr = nullptr;
        }
        if (AlsaLibHandle != nullptr)
        {
            dlclose(AlsaLibHandle);
            AlsaLibHandle = nullptr;
        }
#elif defined(__ANDROID__)
        if (AaudioStreamPtr != nullptr && FnAaudioClose != nullptr)
        {
            FnAaudioClose(AaudioStreamPtr);
            AaudioStreamPtr = nullptr;
        }
        if (AaudioLibHandle != nullptr)
        {
            dlclose(AaudioLibHandle);
            AaudioLibHandle = nullptr;
        }
#endif
    }

    [[nodiscard]] bool SubmitToActiveDriver(const float* Samples, uint32_t SamplePointCount) noexcept
    {
        if (Samples == nullptr || SamplePointCount == 0u)
        {
            return true;
        }

        if (CaptureEnabled)
        {
            Threading::ScopedSpinGuard Guard(CaptureLock);
            const std::size_t MaxFloats = static_cast<std::size_t>(MaxCapturedSamplePoints) * ActiveChannels;
            if (CapturedInterleaved.size() < MaxFloats)
            {
                const std::size_t Desired   = static_cast<std::size_t>(SamplePointCount) * ActiveChannels;
                const std::size_t Remaining = MaxFloats - CapturedInterleaved.size();
                const std::size_t AppendCnt = std::min(Desired, Remaining);
                CapturedInterleaved.insert(CapturedInterleaved.end(), Samples, Samples + AppendCnt);
            }
        }

        bool WriteOk = true;
        switch (ActiveDriver)
        {
            case AudioOutputDriverCategory::HeadlessBuffer:
            case AudioOutputDriverCategory::AutoSelect:
            {
                if (Config.RealTimePacingInHeadless)
                {
                    const uint64_t BlockUs = (static_cast<uint64_t>(SamplePointCount) * 1000000ull) / std::max(8000u, ActiveSampleRate);
                    if (BlockUs > 0ull)
                    {
                        std::this_thread::sleep_for(std::chrono::microseconds(BlockUs));
                    }
                }
                break;
            }
            case AudioOutputDriverCategory::WindowsWasapi:
            {
#ifdef _WIN32
                if (WasapiClient != nullptr && WasapiRenderClient != nullptr)
                {
                    uint32_t Offset = 0u;
                    while (Offset < SamplePointCount)
                    {
                        UINT32 Padding = 0u;
                        if (FAILED(WasapiClient->GetCurrentPadding(&Padding)))
                        {
                            WriteOk = false;
                            break;
                        }
                        if (Padding == 0u)
                        {
                            UnderrunCounter.fetch_add(1u, std::memory_order_relaxed);
                        }
                        const UINT32 Available = (WasapiBufferSamplePoints > Padding) ? (WasapiBufferSamplePoints - Padding) : 0u;
                        if (Available == 0u)
                        {
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                            continue;
                        }
                        const UINT32 Batch = std::min<UINT32>(Available, SamplePointCount - Offset);
                        BYTE*        Dest  = nullptr;
                        if (FAILED(WasapiRenderClient->GetBuffer(Batch, &Dest)) || Dest == nullptr)
                        {
                            WriteOk = false;
                            break;
                        }
                        std::memcpy(
                            Dest,
                            Samples + static_cast<std::size_t>(Offset) * ActiveChannels,
                            static_cast<std::size_t>(Batch) * ActiveChannels * sizeof(float));
                        WasapiRenderClient->ReleaseBuffer(Batch, 0u);
                        Offset += Batch;
                    }
                }
#endif
                break;
            }
            case AudioOutputDriverCategory::LinuxPulse:
            {
#if defined(__linux__) && !defined(__ANDROID__)
                if (PulseStreamPtr != nullptr && FnPulseWrite != nullptr)
                {
                    int Err = 0;
                    const std::size_t ByteCount = static_cast<std::size_t>(SamplePointCount) * ActiveChannels * sizeof(float);
                    if (FnPulseWrite(PulseStreamPtr, Samples, ByteCount, &Err) < 0)
                    {
                        UnderrunCounter.fetch_add(1u, std::memory_order_relaxed);
                        WriteOk = false;
                    }
                }
#endif
                break;
            }
            case AudioOutputDriverCategory::LinuxAlsa:
            {
#if defined(__linux__) && !defined(__ANDROID__)
                if (AlsaPcmPtr != nullptr && FnSndPcmWritei != nullptr)
                {
                    uint32_t Offset = 0u;
                    while (Offset < SamplePointCount)
                    {
                        const int Written = FnSndPcmWritei(
                            AlsaPcmPtr,
                            Samples + static_cast<std::size_t>(Offset) * ActiveChannels,
                            static_cast<unsigned long>(SamplePointCount - Offset));
                        if (Written < 0)
                        {
                            UnderrunCounter.fetch_add(1u, std::memory_order_relaxed);
                            if (FnSndPcmRecover != nullptr && FnSndPcmRecover(AlsaPcmPtr, Written, 1) >= 0)
                            {
                                continue;
                            }
                            WriteOk = false;
                            break;
                        }
                        Offset += static_cast<uint32_t>(Written);
                    }
                }
#endif
                break;
            }
            case AudioOutputDriverCategory::AndroidAaudio:
            {
#ifdef __ANDROID__
                if (AaudioStreamPtr != nullptr && FnAaudioWrite != nullptr)
                {
                    const int Res = FnAaudioWrite(AaudioStreamPtr, Samples, static_cast<int32_t>(SamplePointCount), 20000000LL);
                    if (Res < 0)
                    {
                        UnderrunCounter.fetch_add(1u, std::memory_order_relaxed);
                        WriteOk = false;
                    }
                }
#endif
                break;
            }
        }

        SubmittedPoints.fetch_add(SamplePointCount, std::memory_order_relaxed);
        return WriteOk;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                              AUDIO HARDWARE OUTPUT METHODS
//------------------------------------------------------------------------------------------------------------------------

AudioHardwareOutput::AudioHardwareOutput() noexcept
    : Platform(std::make_unique<PlatformEndpoint>())
{
}

AudioHardwareOutput::~AudioHardwareOutput() noexcept
{
    CloseEndpoint();
}

bool AudioHardwareOutput::OpenEndpoint(const AudioEndpointConfiguration& Configuration) noexcept
{
    CloseEndpoint();

    Platform->Config           = Configuration;
    Platform->ActiveSampleRate = std::max(8000u, Configuration.SampleRateHz);
    Platform->ActiveChannels   = std::clamp(Configuration.ChannelCount, 1u, Constants::MaxAcousticOutputChannels);
    Platform->SubmittedPoints.store(0ull, std::memory_order_release);
    Platform->UnderrunCounter.store(0u, std::memory_order_release);

    if (Configuration.RequestedDriver == AudioOutputDriverCategory::HeadlessBuffer)
    {
        Platform->ActiveDriver = AudioOutputDriverCategory::HeadlessBuffer;
        Platform->EndpointOpen.store(true, std::memory_order_release);
        return true;
    }

    if (Platform->TryOpenNativeDriver(Configuration))
    {
        Platform->EndpointOpen.store(true, std::memory_order_release);
        return true;
    }

    if (Configuration.RequestedDriver == AudioOutputDriverCategory::AutoSelect)
    {
        Platform->ActiveDriver = AudioOutputDriverCategory::HeadlessBuffer;
        Platform->EndpointOpen.store(true, std::memory_order_release);
        return true;
    }

    return false;
}

void AudioHardwareOutput::CloseEndpoint() noexcept
{
    if (Platform != nullptr)
    {
        Platform->EndpointOpen.store(false, std::memory_order_release);
        Platform->CloseNativeHandles();
    }
}

bool AudioHardwareOutput::SubmitInterleavedBlock(
    const float* InterleavedSamples,
    uint32_t     SamplePointCount) noexcept
{
    if (Platform == nullptr || !Platform->EndpointOpen.load(std::memory_order_acquire))
    {
        return false;
    }
    return Platform->SubmitToActiveDriver(InterleavedSamples, SamplePointCount);
}

bool AudioHardwareOutput::QueryEndpointOpen() const noexcept
{
    return Platform != nullptr && Platform->EndpointOpen.load(std::memory_order_acquire);
}

AudioOutputDriverCategory AudioHardwareOutput::QueryActiveDriver() const noexcept
{
    return (Platform != nullptr) ? Platform->ActiveDriver : AudioOutputDriverCategory::HeadlessBuffer;
}

uint32_t AudioHardwareOutput::QueryActiveSampleRateHz() const noexcept
{
    return (Platform != nullptr) ? Platform->ActiveSampleRate : Constants::DefaultAcousticSampleRateHz;
}

uint32_t AudioHardwareOutput::QueryActiveChannelCount() const noexcept
{
    return (Platform != nullptr) ? Platform->ActiveChannels : 2u;
}

uint64_t AudioHardwareOutput::QuerySubmittedSamplePoints() const noexcept
{
    return (Platform != nullptr) ? Platform->SubmittedPoints.load(std::memory_order_acquire) : 0ull;
}

uint32_t AudioHardwareOutput::QueryUnderrunCount() const noexcept
{
    return (Platform != nullptr) ? Platform->UnderrunCounter.load(std::memory_order_acquire) : 0u;
}

void AudioHardwareOutput::ConfigureCaptureBuffer(bool CaptureEnabled, uint32_t MaxCapturedSamplePoints)
{
    if (Platform == nullptr)
    {
        return;
    }
    Threading::ScopedSpinGuard Guard(Platform->CaptureLock);
    Platform->CaptureEnabled          = CaptureEnabled;
    Platform->MaxCapturedSamplePoints = MaxCapturedSamplePoints;
    Platform->CapturedInterleaved.clear();
    if (CaptureEnabled && MaxCapturedSamplePoints > 0u)
    {
        Platform->CapturedInterleaved.reserve(static_cast<std::size_t>(MaxCapturedSamplePoints) * Platform->ActiveChannels);
    }
}

void AudioHardwareOutput::ResetCaptureBuffer() noexcept
{
    if (Platform == nullptr)
    {
        return;
    }
    Threading::ScopedSpinGuard Guard(Platform->CaptureLock);
    Platform->CapturedInterleaved.clear();
}

WaveformBuffer AudioHardwareOutput::ExtractCapturedWaveform() const
{
    WaveformBuffer Buffer{};
    if (Platform == nullptr)
    {
        return Buffer;
    }

    Threading::ScopedSpinGuard Guard(Platform->CaptureLock);
    Buffer.ChannelCount     = std::max(1u, Platform->ActiveChannels);
    Buffer.SampleRateHz     = Platform->ActiveSampleRate;
    Buffer.SamplePointCount = static_cast<uint32_t>(Platform->CapturedInterleaved.size() / Buffer.ChannelCount);
    Buffer.Samples          = Platform->CapturedInterleaved;
    return Buffer;
}

bool AudioHardwareOutput::WriteCapturedWavFile(
    const std::filesystem::path& FilePath,
    std::string&                 OutDiagnostic) const
{
    const WaveformBuffer Captured = ExtractCapturedWaveform();
    return WaveformDecoder::WriteWavToFile(FilePath, Captured, OutDiagnostic);
}

} // namespace Frontier::AudioSpace
