#include "MicrophonePCM16CaptureActor.h"

#include "AudioCaptureCore.h"
#include "Features/IModularFeatures.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Misc/ScopeLock.h"

DEFINE_LOG_CATEGORY_STATIC(LogMicrophonePCM16, Log, All);

namespace
{
    void AppendSignedPCM16LittleEndian(const float Sample, TArray<uint8>& OutputBytes)
    {
        const float Clamped = FMath::Clamp(Sample, -1.0f, 1.0f);
        const int32 Scaled = Clamped >= 0.0f
            ? FMath::RoundToInt(Clamped * 32767.0f)
            : FMath::RoundToInt(Clamped * 32768.0f);
        const int16 SignedSample = static_cast<int16>(FMath::Clamp(Scaled, -32768, 32767));
        const uint16 Bits = static_cast<uint16>(SignedSample);

        // Write the byte order explicitly; do not depend on the CPU's native endianness.
        OutputBytes.Add(static_cast<uint8>(Bits & 0x00ff));
        OutputBytes.Add(static_cast<uint8>((Bits >> 8) & 0x00ff));
    }

    FString MakeSafePCMFileName(FString FileName)
    {
        FileName = FPaths::GetCleanFilename(FileName.TrimStartAndEnd());
        if (FileName.IsEmpty())
        {
            FileName = FString::Printf(TEXT("Microphone_%s.pcm"),
                *FDateTime::Now().ToString(TEXT("yyyyMMdd_HHmmss")));
        }
        if (!FileName.EndsWith(TEXT(".pcm"), ESearchCase::IgnoreCase))
        {
            FileName += TEXT(".pcm");
        }
        return FileName;
    }
}

AMicrophonePCM16CaptureActor::AMicrophonePCM16CaptureActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;
    SetActorTickInterval(0.01f);

    OutputFormat.SampleRate = TargetSampleRate;
    OutputFormat.BitDepth = TargetBitDepth;
    OutputFormat.ChannelCount = TargetChannelCount;
    OutputFormat.bSignedPCM = true;
    OutputFormat.bLittleEndian = true;
    OutputFormat.Encoding = TEXT("PCM_SIGNED");
}

AMicrophonePCM16CaptureActor::~AMicrophonePCM16CaptureActor() = default;

void AMicrophonePCM16CaptureActor::BeginPlay()
{
    Super::BeginPlay();

    if (bAutoStartCapture)
    {
        StartMicrophoneCapture();
    }
}

void AMicrophonePCM16CaptureActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopMicrophoneCapture();
    Super::EndPlay(EndPlayReason);
}

void AMicrophonePCM16CaptureActor::BeginDestroy()
{
    ShutdownCapture(false);
    Super::BeginDestroy();
}

void AMicrophonePCM16CaptureActor::Tick(const float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    OverflowCount = AudioThreadOverflowCount.Load();
    CurrentRMS = AudioThreadRMS.Load();
    DrainPCMChunks(false);
}

bool AMicrophonePCM16CaptureActor::StartMicrophoneCapture()
{
    LastErrorMessage.Reset();
    UE_LOG(LogMicrophonePCM16, Display, TEXT("StartMicrophoneCapture called."));

    if (bCaptureActive)
    {
        return true;
    }

    // Loading AudioCapture normally loads the platform implementation from Engine.ini. In UE 5.7
    // some CDOs instantiate AudioCaptureCore before that startup path runs, so also load the
    // backend explicitly when no capture factory has registered yet.
    if (!FModuleManager::Get().LoadModule(TEXT("AudioCapture")))
    {
        ReportError(TEXT("AudioCapture module could not be loaded."));
        return false;
    }

    const FName CaptureFeatureName = Audio::IAudioCaptureFactory::GetModularFeatureName();
    if (IModularFeatures::Get().GetModularFeatureImplementationCount(CaptureFeatureName) == 0)
    {
#if PLATFORM_WINDOWS
        FModuleManager::Get().LoadModule(TEXT("AudioCaptureWasapi"));
#elif PLATFORM_MAC
        FModuleManager::Get().LoadModule(TEXT("AudioCaptureRtAudio"));
#elif PLATFORM_ANDROID
        FModuleManager::Get().LoadModule(TEXT("AudioCaptureAndroid"));
#elif PLATFORM_IOS
        FModuleManager::Get().LoadModule(TEXT("AudioCaptureAudioUnit"));
#endif
    }

    UE_LOG(LogMicrophonePCM16, Display, TEXT("Registered audio-capture backends: %d."),
        IModularFeatures::Get().GetModularFeatureImplementationCount(CaptureFeatureName));

    if (IModularFeatures::Get().GetModularFeatureImplementationCount(CaptureFeatureName) == 0)
    {
        ReportError(TEXT("No platform audio-capture backend is registered."));
        return false;
    }

    AudioCapture = MakeUnique<Audio::FAudioCapture>();

    Audio::FCaptureDeviceInfo DeviceInfo;
    if (!AudioCapture->GetCaptureDeviceInfo(DeviceInfo, Audio::DefaultDeviceIndex))
    {
        AudioCapture.Reset();
        ReportError(TEXT("No default microphone is available. Check Windows microphone permissions and the input device."));
        return false;
    }

    InputDeviceName = DeviceInfo.DeviceName;
    InputDeviceSampleRate = DeviceInfo.PreferredSampleRate;
    InputDeviceChannelCount = DeviceInfo.InputChannels;

    if (InputDeviceSampleRate <= 0 || InputDeviceChannelCount <= 0)
    {
        AudioCapture.Reset();
        ReportError(TEXT("The default microphone reported an invalid sample rate or channel count."));
        return false;
    }

    if (bClearBufferOnStart)
    {
        ClearBufferedPCM();
    }

    {
        FScopeLock Lock(&PCMBufferCriticalSection);
        PendingPCMBytes.Reset();
        PendingPCMBytes.Reserve(TargetSampleRate * BytesPerSample * 2);
        PendingPCMReadOffset = 0;
        BufferedPCMBytes.Reserve(BufferedPCMBytes.Num() + TargetSampleRate * BytesPerSample * 10);
    }

    ResampleSource.Reset();
    ResampleSource.Reserve(4096);
    ResamplePosition = 0.0;
    ResampleInputRate = 0;
    OverflowCount = 0;
    CurrentRMS = 0.0f;
    AudioThreadOverflowCount.Store(0);
    AudioThreadRMS.Store(0.0f);

    Audio::FAudioCaptureDeviceParams StreamParams;
    StreamParams.DeviceIndex = Audio::DefaultDeviceIndex;
    StreamParams.NumInputChannels = Audio::InvalidDeviceChannelCount;
    StreamParams.SampleRate = Audio::InvalidDeviceSampleRate;
    StreamParams.PCMAudioEncoding = Audio::EPCMAudioEncoding::FLOATING_POINT_32;

    PRAGMA_DISABLE_DEPRECATION_WARNINGS
    Audio::FOnCaptureFunction CaptureCallback =
        [this](const float* AudioData, const int32 NumFrames, const int32 NumChannels,
            const int32 SampleRate, const double StreamTime, const bool bOverflow)
        {
            HandleCapturedAudio(AudioData, NumFrames, NumChannels, SampleRate, bOverflow);
        };

    // OpenCaptureStream is still the exported float32 API in UE 5.7. The newer void-pointer
    // overload is not exported by AudioCaptureCore, so using it from a game module cannot link.
    const bool bOpened = AudioCapture->OpenCaptureStream(StreamParams, MoveTemp(CaptureCallback), 1024);
    PRAGMA_ENABLE_DEPRECATION_WARNINGS

    if (!bOpened)
    {
        AudioCapture.Reset();
        ReportError(TEXT("The default microphone stream could not be opened."));
        return false;
    }

    bAcceptingAudio.Store(true);
    if (!AudioCapture->StartStream())
    {
        bAcceptingAudio.Store(false);
        AudioCapture->CloseStream();
        AudioCapture.Reset();
        ReportError(TEXT("The default microphone stream opened but could not be started."));
        return false;
    }

    bCaptureActive = true;
    UE_LOG(LogMicrophonePCM16, Display,
        TEXT("Microphone capture started: %s, input=%d Hz/%d ch, output=16000 Hz/16-bit/mono/PCM_SIGNED/little-endian."),
        *InputDeviceName, InputDeviceSampleRate, InputDeviceChannelCount);

    OnCaptureStarted.Broadcast(InputDeviceName, InputDeviceSampleRate, InputDeviceChannelCount);
    BlueprintCaptureStarted(InputDeviceName, InputDeviceSampleRate, InputDeviceChannelCount);
    return true;
}

void AMicrophonePCM16CaptureActor::StopMicrophoneCapture()
{
    ShutdownCapture(true);
}

void AMicrophonePCM16CaptureActor::ShutdownCapture(const bool bNotifyBlueprint)
{
    if (!bCaptureActive && !AudioCapture)
    {
        return;
    }

    bAcceptingAudio.Store(false);

    if (AudioCapture)
    {
        if (AudioCapture->IsCapturing())
        {
            AudioCapture->StopStream();
        }
        if (AudioCapture->IsStreamOpen())
        {
            AudioCapture->CloseStream();
        }
        AudioCapture.Reset();
    }

    bCaptureActive = false;
    DrainPCMChunks(true);

    const int32 TotalBytes = GetBufferedPCM().Num();
    const float TotalDuration = static_cast<float>(TotalBytes)
        / static_cast<float>(TargetSampleRate * TargetChannelCount * BytesPerSample);

    UE_LOG(LogMicrophonePCM16, Display, TEXT("Microphone capture stopped: %d PCM bytes, %.3f seconds."),
        TotalBytes, TotalDuration);

    if (bNotifyBlueprint)
    {
        OnCaptureStopped.Broadcast(TotalBytes, TotalDuration);
        BlueprintCaptureStopped(TotalBytes, TotalDuration);
    }
}

bool AMicrophonePCM16CaptureActor::IsMicrophoneCapturing() const
{
    return bCaptureActive && AudioCapture && AudioCapture->IsCapturing();
}

void AMicrophonePCM16CaptureActor::HandleCapturedAudio(const float* InterleavedAudio,
    const int32 NumFrames, const int32 NumChannels, const int32 SampleRate, const bool bOverflow)
{
    if (!bAcceptingAudio.Load() || !InterleavedAudio || NumFrames <= 0 || NumChannels <= 0 || SampleRate <= 0)
    {
        return;
    }

    if (bOverflow)
    {
        ++AudioThreadOverflowCount;
    }

    if (ResampleInputRate != SampleRate)
    {
        ResampleSource.Reset();
        ResamplePosition = 0.0;
        ResampleInputRate = SampleRate;
    }

    const int32 FirstNewSample = ResampleSource.AddUninitialized(NumFrames);
    double SumSquares = 0.0;
    for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
    {
        double Mixed = 0.0;
        const int32 FrameOffset = FrameIndex * NumChannels;
        for (int32 ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
        {
            Mixed += static_cast<double>(InterleavedAudio[FrameOffset + ChannelIndex]);
        }
        const float MonoSample = FMath::Clamp(
            static_cast<float>(Mixed / static_cast<double>(NumChannels)), -1.0f, 1.0f);
        ResampleSource[FirstNewSample + FrameIndex] = MonoSample;
        SumSquares += static_cast<double>(MonoSample) * static_cast<double>(MonoSample);
    }
    AudioThreadRMS.Store(static_cast<float>(FMath::Sqrt(SumSquares / static_cast<double>(NumFrames))));

    TArray<uint8> ConvertedBytes;
    const double SourceFramesPerOutputFrame = static_cast<double>(SampleRate)
        / static_cast<double>(TargetSampleRate);
    const int32 EstimatedOutputFrames = FMath::Max(1,
        FMath::CeilToInt(static_cast<double>(NumFrames) / SourceFramesPerOutputFrame) + 2);
    ConvertedBytes.Reserve(EstimatedOutputFrames * BytesPerSample);

    while (ResamplePosition + 1.0 < static_cast<double>(ResampleSource.Num()))
    {
        const int32 LeftIndex = FMath::FloorToInt(ResamplePosition);
        const double Fraction = ResamplePosition - static_cast<double>(LeftIndex);
        const float Resampled = FMath::Lerp(
            ResampleSource[LeftIndex], ResampleSource[LeftIndex + 1], static_cast<float>(Fraction));
        AppendSignedPCM16LittleEndian(Resampled, ConvertedBytes);
        ResamplePosition += SourceFramesPerOutputFrame;
    }

    const int32 ConsumedSourceFrames = FMath::FloorToInt(ResamplePosition);
    if (ConsumedSourceFrames > 0)
    {
        ResampleSource.RemoveAt(0, ConsumedSourceFrames, EAllowShrinking::No);
        ResamplePosition -= static_cast<double>(ConsumedSourceFrames);
    }

    if (ConvertedBytes.Num() > 0)
    {
        FScopeLock Lock(&PCMBufferCriticalSection);
        BufferedPCMBytes.Append(ConvertedBytes);
        PendingPCMBytes.Append(ConvertedBytes);
    }
}

void AMicrophonePCM16CaptureActor::DrainPCMChunks(const bool bFlushRemainder)
{
    TArray<TArray<uint8>> ReadyChunks;
    const int32 ChunkBytes = GetChunkByteCount();

    {
        FScopeLock Lock(&PCMBufferCriticalSection);
        int32 AvailableBytes = PendingPCMBytes.Num() - PendingPCMReadOffset;

        while (AvailableBytes >= ChunkBytes || (bFlushRemainder && AvailableBytes > 0))
        {
            int32 BytesToCopy = AvailableBytes >= ChunkBytes ? ChunkBytes : AvailableBytes;
            BytesToCopy -= BytesToCopy % BytesPerSample;
            if (BytesToCopy <= 0)
            {
                break;
            }

            TArray<uint8>& Chunk = ReadyChunks.AddDefaulted_GetRef();
            Chunk.Append(PendingPCMBytes.GetData() + PendingPCMReadOffset, BytesToCopy);
            PendingPCMReadOffset += BytesToCopy;
            AvailableBytes -= BytesToCopy;
        }

        if (PendingPCMReadOffset == PendingPCMBytes.Num())
        {
            PendingPCMBytes.Reset();
            PendingPCMReadOffset = 0;
        }
        else if (PendingPCMReadOffset > ChunkBytes * 8)
        {
            PendingPCMBytes.RemoveAt(0, PendingPCMReadOffset, EAllowShrinking::No);
            PendingPCMReadOffset = 0;
        }
    }

    for (const TArray<uint8>& Chunk : ReadyChunks)
    {
        const int32 SampleCount = Chunk.Num() / BytesPerSample;
        const float Duration = static_cast<float>(SampleCount) / static_cast<float>(TargetSampleRate);
        OnPCMChunkReady.Broadcast(Chunk, SampleCount, Duration);
        BlueprintPCMChunkReady(Chunk, SampleCount, Duration);
    }
}

int32 AMicrophonePCM16CaptureActor::GetChunkByteCount() const
{
    const int32 SafeMilliseconds = FMath::Clamp(ChunkDurationMilliseconds, 20, 2000);
    const int64 Bytes = static_cast<int64>(TargetSampleRate) * BytesPerSample * SafeMilliseconds / 1000;
    return FMath::Max(BytesPerSample, static_cast<int32>(Bytes - (Bytes % BytesPerSample)));
}

TArray<uint8> AMicrophonePCM16CaptureActor::GetBufferedPCM() const
{
    FScopeLock Lock(&PCMBufferCriticalSection);
    return BufferedPCMBytes;
}

float AMicrophonePCM16CaptureActor::GetBufferedDurationSeconds() const
{
    FScopeLock Lock(&PCMBufferCriticalSection);
    return static_cast<float>(BufferedPCMBytes.Num())
        / static_cast<float>(TargetSampleRate * TargetChannelCount * BytesPerSample);
}

void AMicrophonePCM16CaptureActor::ClearBufferedPCM()
{
    FScopeLock Lock(&PCMBufferCriticalSection);
    BufferedPCMBytes.Reset();
}

bool AMicrophonePCM16CaptureActor::SaveBufferedPCMToFile(const FString& FileName,
    FString& SavedAbsolutePath) const
{
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MicrophoneCaptures"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    SavedAbsolutePath = FPaths::ConvertRelativePathToFull(
        FPaths::Combine(Directory, MakeSafePCMFileName(FileName)));

    const TArray<uint8> PCMData = GetBufferedPCM();
    return FFileHelper::SaveArrayToFile(PCMData, *SavedAbsolutePath);
}

FString AMicrophonePCM16CaptureActor::GetOutputFormatDescription() const
{
    return TEXT("16000 Hz | 16 bit | mono | PCM_SIGNED | little-endian | raw/headerless");
}

void AMicrophonePCM16CaptureActor::ReportError(const FString& Message)
{
    LastErrorMessage = Message;
    UE_LOG(LogMicrophonePCM16, Error, TEXT("%s"), *Message);
    OnCaptureError.Broadcast(Message);
    BlueprintCaptureError(Message);
}

FMicrophoneDeviceCheckResult AMicrophonePCM16CaptureActor::CheckMicrophoneDevice()
{
    FMicrophoneDeviceCheckResult Result;

    // 1. Try loading the AudioCapture module.
    if (!FModuleManager::Get().LoadModule(TEXT("AudioCapture")))
    {
        Result.StatusMessage = TEXT("AudioCapture module could not be loaded.");
        UE_LOG(LogMicrophonePCM16, Warning, TEXT("%s"), *Result.StatusMessage);
        return Result;
    }

    // 2. Ensure a platform backend is registered (WASAPI on Windows, etc.).
    const FName CaptureFeatureName = Audio::IAudioCaptureFactory::GetModularFeatureName();
    if (IModularFeatures::Get().GetModularFeatureImplementationCount(CaptureFeatureName) == 0)
    {
#if PLATFORM_WINDOWS
        FModuleManager::Get().LoadModule(TEXT("AudioCaptureWasapi"));
#elif PLATFORM_MAC
        FModuleManager::Get().LoadModule(TEXT("AudioCaptureRtAudio"));
#elif PLATFORM_ANDROID
        FModuleManager::Get().LoadModule(TEXT("AudioCaptureAndroid"));
#elif PLATFORM_IOS
        FModuleManager::Get().LoadModule(TEXT("AudioCaptureAudioUnit"));
#endif
    }

    Result.bBackendLoaded =
        IModularFeatures::Get().GetModularFeatureImplementationCount(CaptureFeatureName) > 0;
    if (!Result.bBackendLoaded)
    {
        Result.StatusMessage = TEXT("No platform audio-capture backend is registered.");
        UE_LOG(LogMicrophonePCM16, Warning, TEXT("%s"), *Result.StatusMessage);
        return Result;
    }

    // 3. Query the default input device info without opening a stream.
    Audio::FAudioCapture TempCapture;
    Audio::FCaptureDeviceInfo DeviceInfo;
    if (!TempCapture.GetCaptureDeviceInfo(DeviceInfo, Audio::DefaultDeviceIndex))
    {
        Result.StatusMessage =
            TEXT("No default microphone is available. Check Windows microphone permissions and the input device.");
        UE_LOG(LogMicrophonePCM16, Warning, TEXT("%s"), *Result.StatusMessage);
        return Result;
    }

    Result.DeviceName = DeviceInfo.DeviceName;
    Result.NativeSampleRate = DeviceInfo.PreferredSampleRate;
    Result.NativeChannelCount = DeviceInfo.InputChannels;

    if (Result.NativeSampleRate <= 0 || Result.NativeChannelCount <= 0)
    {
        Result.StatusMessage =
            TEXT("The default microphone reported an invalid sample rate or channel count.");
        UE_LOG(LogMicrophonePCM16, Warning, TEXT("%s"), *Result.StatusMessage);
        return Result;
    }

    // 4. Determine whether software resampling will be needed.
    Result.bWillResample =
        (Result.NativeSampleRate != TargetSampleRate) ||
        (Result.NativeChannelCount != TargetChannelCount);
    Result.bDeviceAvailable = true;

    if (Result.bWillResample)
    {
        Result.StatusMessage = FString::Printf(
            TEXT("Device: %s | Native: %d Hz / %d ch -> Output: 16000 Hz / 1 ch (software resampled)"),
            *Result.DeviceName, Result.NativeSampleRate, Result.NativeChannelCount);
    }
    else
    {
        Result.StatusMessage = FString::Printf(
            TEXT("Device: %s | Native: %d Hz / %d ch -> Output: 16000 Hz / 1 ch (native match)"),
            *Result.DeviceName, Result.NativeSampleRate, Result.NativeChannelCount);
    }

    UE_LOG(LogMicrophonePCM16, Display, TEXT("Device check: %s"), *Result.StatusMessage);
    return Result;
}
