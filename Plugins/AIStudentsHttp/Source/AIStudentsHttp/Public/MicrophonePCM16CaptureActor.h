#pragma once

#include "AudioCaptureCore.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HAL/CriticalSection.h"
#include "Templates/Atomic.h"
#include "MicrophonePCM16CaptureActor.generated.h"

/** The exact byte format emitted by AMicrophonePCM16CaptureActor. */
USTRUCT(BlueprintType)
struct AISTUDENTSHTTP_API FMicrophonePCMFormat
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Microphone PCM")
    int32 SampleRate = 16000;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Microphone PCM")
    int32 BitDepth = 16;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Microphone PCM")
    int32 ChannelCount = 1;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Microphone PCM")
    bool bSignedPCM = true;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Microphone PCM")
    bool bLittleEndian = true;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Microphone PCM")
    FString Encoding = TEXT("PCM_SIGNED");
};

/** Result of a pre-capture device check. Call CheckMicrophoneDevice() on a menu/startup screen. */
USTRUCT(BlueprintType)
struct AISTUDENTSHTTP_API FMicrophoneDeviceCheckResult
{
    GENERATED_BODY()

    /** True if a usable microphone was found and a capture stream can be opened. */
    UPROPERTY(BlueprintReadOnly, Category = "Microphone PCM|Device")
    bool bDeviceAvailable = false;

    /** True if the platform audio-capture backend (WASAPI/RtAudio/etc.) is loaded. */
    UPROPERTY(BlueprintReadOnly, Category = "Microphone PCM|Device")
    bool bBackendLoaded = false;

    /** Display name of the default input device. */
    UPROPERTY(BlueprintReadOnly, Category = "Microphone PCM|Device")
    FString DeviceName;

    /** Native sample rate reported by the device (e.g. 44100, 48000). May differ from 16000. */
    UPROPERTY(BlueprintReadOnly, Category = "Microphone PCM|Device")
    int32 NativeSampleRate = 0;

    /** Native channel count reported by the device (e.g. 1, 2). May differ from 1. */
    UPROPERTY(BlueprintReadOnly, Category = "Microphone PCM|Device")
    int32 NativeChannelCount = 0;

    /** True if the plugin will software-resample/down-mix to reach 16 kHz mono. */
    UPROPERTY(BlueprintReadOnly, Category = "Microphone PCM|Device")
    bool bWillResample = false;

    /** Human-readable status message suitable for UI display. */
    UPROPERTY(BlueprintReadOnly, Category = "Microphone PCM|Device")
    FString StatusMessage;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FMicrophoneCaptureStartedSignature,
    const FString&, DeviceName,
    int32, InputSampleRate,
    int32, InputChannelCount);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FMicrophonePCMChunkSignature,
    const TArray<uint8>&, PCMData,
    int32, SampleCount,
    float, DurationSeconds);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FMicrophoneCaptureStoppedSignature,
    int32, TotalPCMBytes,
    float, TotalDurationSeconds);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FMicrophoneCaptureErrorSignature,
    const FString&, ErrorMessage);

/**
 * Blueprint-facing microphone recorder.
 *
 * The hardware stream is down-mixed and continuously resampled before Blueprint sees it.
 * Every byte array emitted by this actor is 16000 Hz, 16-bit, mono, signed PCM in
 * little-endian byte order (raw PCM, with no WAV header).
 */
UCLASS(Blueprintable, BlueprintType)
class AISTUDENTSHTTP_API AMicrophonePCM16CaptureActor : public AActor
{
    GENERATED_BODY()

public:
    static constexpr int32 TargetSampleRate = 16000;
    static constexpr int32 TargetBitDepth = 16;
    static constexpr int32 TargetChannelCount = 1;
    static constexpr int32 BytesPerSample = TargetBitDepth / 8;

    AMicrophonePCM16CaptureActor();
    virtual ~AMicrophonePCM16CaptureActor() override;

    virtual void Tick(float DeltaSeconds) override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void BeginDestroy() override;

    /** Starts the default microphone. Calling this again while active is harmless. */
    UFUNCTION(BlueprintCallable, Category = "Microphone PCM|Capture")
    bool StartMicrophoneCapture();

    /** Stops the microphone and emits any final partial PCM chunk. */
    UFUNCTION(BlueprintCallable, Category = "Microphone PCM|Capture")
    void StopMicrophoneCapture();

    UFUNCTION(BlueprintPure, Category = "Microphone PCM|Capture")
    bool IsMicrophoneCapturing() const;

    /** Returns a copy of all raw PCM bytes captured in the current session. */
    UFUNCTION(BlueprintCallable, Category = "Microphone PCM|Data")
    TArray<uint8> GetBufferedPCM() const;

    UFUNCTION(BlueprintPure, Category = "Microphone PCM|Data")
    float GetBufferedDurationSeconds() const;

    UFUNCTION(BlueprintCallable, Category = "Microphone PCM|Data")
    void ClearBufferedPCM();

    /** Saves raw headerless .pcm data under Saved/MicrophoneCaptures. */
    UFUNCTION(BlueprintCallable, Category = "Microphone PCM|Data")
    bool SaveBufferedPCMToFile(const FString& FileName, FString& SavedAbsolutePath) const;

    UFUNCTION(BlueprintPure, Category = "Microphone PCM|Format")
    FMicrophonePCMFormat GetOutputFormat() const { return OutputFormat; }

    UFUNCTION(BlueprintPure, Category = "Microphone PCM|Format")
    FString GetOutputFormatDescription() const;

    /**
     * Checks the default microphone WITHOUT starting a capture stream.
     * Safe to call from a menu / startup screen before BeginPlay.
     * The plugin always outputs 16 kHz mono via software resampling,
     * so the device does NOT need to natively support 16 kHz mono.
     */
    UFUNCTION(BlueprintCallable, Category = "Microphone PCM|Device")
    static FMicrophoneDeviceCheckResult CheckMicrophoneDevice();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Microphone PCM|Capture")
    bool bAutoStartCapture = true;

    /** Chunk size delivered to Blueprint. 100 ms equals 3200 PCM bytes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Microphone PCM|Capture",
        meta = (ClampMin = "20", ClampMax = "2000", Units = "ms"))
    int32 ChunkDurationMilliseconds = 100;

    /** Clears the accumulated session buffer whenever a new capture starts. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Microphone PCM|Capture")
    bool bClearBufferOnStart = true;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Microphone PCM|Format")
    FMicrophonePCMFormat OutputFormat;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Microphone PCM|Status")
    FString InputDeviceName;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Microphone PCM|Status")
    int32 InputDeviceSampleRate = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Microphone PCM|Status")
    int32 InputDeviceChannelCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Microphone PCM|Status")
    int32 OverflowCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Microphone PCM|Status")
    float CurrentRMS = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Microphone PCM|Status")
    FString LastErrorMessage;

    /** Updated by the generated Blueprint graph whenever a PCM chunk event arrives. */
    UPROPERTY(BlueprintReadWrite, Category = "Microphone PCM|Blueprint")
    TArray<uint8> LatestPCMChunk;

    UPROPERTY(BlueprintAssignable, Category = "Microphone PCM|Events")
    FMicrophoneCaptureStartedSignature OnCaptureStarted;

    UPROPERTY(BlueprintAssignable, Category = "Microphone PCM|Events")
    FMicrophonePCMChunkSignature OnPCMChunkReady;

    UPROPERTY(BlueprintAssignable, Category = "Microphone PCM|Events")
    FMicrophoneCaptureStoppedSignature OnCaptureStopped;

    UPROPERTY(BlueprintAssignable, Category = "Microphone PCM|Events")
    FMicrophoneCaptureErrorSignature OnCaptureError;

    UFUNCTION(BlueprintImplementableEvent, Category = "Microphone PCM|Events",
        meta = (DisplayName = "On Microphone Capture Started"))
    void BlueprintCaptureStarted(const FString& DeviceName, int32 InputSampleRate, int32 InputChannelCount);

    UFUNCTION(BlueprintImplementableEvent, Category = "Microphone PCM|Events",
        meta = (DisplayName = "On PCM16 Chunk Ready"))
    void BlueprintPCMChunkReady(const TArray<uint8>& PCMData, int32 SampleCount, float DurationSeconds);

    UFUNCTION(BlueprintImplementableEvent, Category = "Microphone PCM|Events",
        meta = (DisplayName = "On Microphone Capture Stopped"))
    void BlueprintCaptureStopped(int32 TotalPCMBytes, float TotalDurationSeconds);

    UFUNCTION(BlueprintImplementableEvent, Category = "Microphone PCM|Events",
        meta = (DisplayName = "On Microphone Capture Error"))
    void BlueprintCaptureError(const FString& ErrorMessage);

private:
    void HandleCapturedAudio(const float* InterleavedAudio, int32 NumFrames, int32 NumChannels,
        int32 SampleRate, bool bOverflow);
    void DrainPCMChunks(bool bFlushRemainder);
    void ShutdownCapture(bool bNotifyBlueprint);
    void ReportError(const FString& Message);
    int32 GetChunkByteCount() const;

    TUniquePtr<Audio::FAudioCapture> AudioCapture;

    mutable FCriticalSection PCMBufferCriticalSection;
    TArray<uint8> BufferedPCMBytes;
    TArray<uint8> PendingPCMBytes;
    int32 PendingPCMReadOffset = 0;

    // Stateful linear resampler data. Only the audio callback thread touches these fields.
    TArray<float> ResampleSource;
    double ResamplePosition = 0.0;
    int32 ResampleInputRate = 0;

    TAtomic<bool> bAcceptingAudio { false };
    TAtomic<int32> AudioThreadOverflowCount { 0 };
    TAtomic<float> AudioThreadRMS { 0.0f };
    bool bCaptureActive = false;
};
