// LocalRecorder.h
// 课堂本地录制：UE5 内启动本地 ffmpeg 录屏 + Submix 监听录游戏声音 + 麦克风 PCM 双写，
// 停止时后台线程用 ffmpeg 把三路合成为一个 MP4，完成后广播 OnRecordingComplete。
// 服务器只负责接收上传（/api/recording/upload），不在服务器端录屏。

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Containers/Ticker.h"
#include "Containers/Queue.h"
#include "ISubmixBufferListener.h"
#include "LocalRecorder.generated.h"

class IFileHandle;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLocalRecordingComplete, const FString&, FinalVideoPath);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLocalRecordingFailed, const FString&, ErrorMessage);

/** 音频渲染线程 → 游戏线程的一帧游戏声音 */
struct FGameAudioChunk
{
	TArray<float> Data;
	int32 NumChannels = 2;
	int32 SampleRate = 48000;
};

/** 挂在主 Submix 上的监听器：音频渲染线程回调，只做拷贝入队 */
class FGameAudioTap : public ISubmixBufferListener
{
public:
	TQueue<FGameAudioChunk, EQueueMode::Mpsc> PendingBuffers;

	virtual void OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock) override;

	// 录制期间保持 submix 持续渲染，避免无声段落被休眠跳过
	virtual bool IsRenderingAudio() const override { return true; }
};

/** PCM16 WAV 追加写入器：开文件时写占位头，Finalize 时回填长度 */
class FLocalWavWriter
{
public:
	~FLocalWavWriter();

	bool Open(const FString& InPath, int32 InSampleRate, int32 InNumChannels);
	void AppendPCM16(const int16* Samples, int64 NumSamples);
	void AppendSilenceSeconds(float Seconds);
	void Finalize();
	void Close();
	bool IsOpen() const { return FileHandle != nullptr; }
	int64 GetDataBytes() const { return DataBytes; }

private:
	void WriteHeader(uint32 RiffSize, uint32 DataSize);

	IFileHandle* FileHandle = nullptr;
	int32 SampleRate = 48000;
	int32 NumChannels = 2;
	int64 DataBytes = 0;
	FCriticalSection Lock;
};

UCLASS(BlueprintType, Blueprintable)
class ULocalRecorder : public UObject
{
	GENERATED_BODY()

public:
	ULocalRecorder();

	/** 本地合成完成（后台线程结束后在游戏线程广播），参数为最终 MP4 完整路径 */
	UPROPERTY(BlueprintAssignable, Category = "Recording|Event")
	FOnLocalRecordingComplete OnRecordingComplete;

	/** 录制/合成失败 */
	UPROPERTY(BlueprintAssignable, Category = "Recording|Event")
	FOnLocalRecordingFailed OnRecordingFailed;

	/** 创建录制器（蓝图里保存到 BP_Instance 变量，长期使用） */
	UFUNCTION(BlueprintCallable, Category = "Recording")
	static ULocalRecorder* CreateLocalRecorder();

	/**
	 * 开始本地录制
	 * @param WindowTitle 游戏窗口标题（如 ClassBot 5.8），为空则录制整个桌面
	 * @param Framerate 录屏帧率
	 * @param MicSampleRate 麦克风 PCM 采样率（与 AudioCapture 一致，一般 16000）
	 * @param MicChannels 麦克风声道数（一般 1）
	 */
	UFUNCTION(BlueprintCallable, Category = "Recording")
	bool StartRecording(const FString& WindowTitle = "", int32 Framerate = 30, int32 MicSampleRate = 16000, int32 MicChannels = 1);

	/** 停止录制并后台合成 MP4，完成后广播 OnRecordingComplete */
	UFUNCTION(BlueprintCallable, Category = "Recording")
	void StopRecording();

	/**
	 * 写入一路麦克风 PCM16 数据（在蓝图的 "On PCM16 Chunk Ready" 事件里调用，
	 * 与 WebSocket 语音识别同源双写）。中途切换录音设备会按时间差自动补静音。
	 */
	UFUNCTION(BlueprintCallable, Category = "Recording")
	void WriteMicChunk(const TArray<uint8>& PCM16Chunk, int32 SampleRate = 16000, int32 NumChannels = 1);

	UFUNCTION(BlueprintPure, Category = "Recording")
	bool IsRecording() const { return bRecording; }

	/** 正在后台合成 MP4 */
	UFUNCTION(BlueprintPure, Category = "Recording")
	bool IsProcessing() const { return bProcessing; }

	UFUNCTION(BlueprintPure, Category = "Recording")
	FString GetFinalVideoPath() const { return FinalVideoPath; }

	/**
	 * 返回可直接喂给 Media Player "Open Url" 的本地播放地址
	 * （file:/// 前缀 + 正斜杠，形如 file:///D:/UE/.../class_recording.mp4）。
	 * 边上传边播本地文件时用它，零等待。
	 */
	UFUNCTION(BlueprintPure, Category = "Recording")
	FString GetFinalVideoFileUrl() const;

	/** 手动指定 ffmpeg.exe 完整路径；不指定则自动在项目根目录/exe 同目录查找 */
	UFUNCTION(BlueprintCallable, Category = "Recording")
	void SetFFmpegPath(const FString& InPath) { FFmpegPath = InPath; }

	/** 上传成功后调用：删除本地临时目录（录屏、两个 wav、合成结果） */
	UFUNCTION(BlueprintCallable, Category = "Recording")
	void DeleteLocalRecordingFiles();

	virtual void BeginDestroy() override;

private:
	bool StartScreenRecording(const FString& WindowTitle, int32 Framerate);
	bool StartGameAudioTap();
	void StopGameAudioTap();
	bool TickDrainGameAudio(float DeltaTime);
	void DrainGameAudioQueue();
	FString ResolveFFmpegPath() const;
	void BroadcastFailed(const FString& Message);

	bool bRecording = false;
	bool bProcessing = false;

	FString FFmpegPath;
	FString WorkDir;
	FString ScreenVideoPath;
	FString GameAudioPath;
	FString MicAudioPath;
	FString FinalVideoPath;

	FProcHandle ScreenProc;

	TSharedPtr<FGameAudioTap, ESPMode::ThreadSafe> GameAudioTap;
	FTSTicker::FDelegateHandle TickerHandle;

	FLocalWavWriter GameWav;
	FLocalWavWriter MicWav;

	int32 MicSampleRate = 16000;
	int32 MicChannels = 1;
	double LastMicWriteTime = 0.0;

	/** 超过该间隔（秒）没有麦克风数据，视为切换设备/断流，补静音保持时间轴 */
	static constexpr float MicGapThreshold = 0.5f;
};