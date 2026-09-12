#include "LocalRecorder.h"

#include "AudioDevice.h"
#include "Sound/SoundSubmix.h"
#include "Engine/Engine.h"
#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"

// ===================== FGameAudioTap =====================

void FGameAudioTap::OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock)
{
	if (AudioData == nullptr || NumSamples <= 0)
	{
		return;
	}

	FGameAudioChunk Chunk;
	Chunk.Data.Append(AudioData, NumSamples);
	Chunk.NumChannels = NumChannels;
	Chunk.SampleRate = SampleRate;
	PendingBuffers.Enqueue(MoveTemp(Chunk));
}

// ===================== FLocalWavWriter =====================

FLocalWavWriter::~FLocalWavWriter()
{
	Close();
}

bool FLocalWavWriter::Open(const FString& InPath, int32 InSampleRate, int32 InNumChannels)
{
	Close();

	SampleRate = InSampleRate;
	NumChannels = InNumChannels;

	FileHandle = FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*InPath, false, false);
	if (FileHandle == nullptr)
	{
		return false;
	}

	DataBytes = 0;
	WriteHeader(0, 0);
	return true;
}

void FLocalWavWriter::AppendPCM16(const int16* Samples, int64 NumSamples)
{
	if (FileHandle == nullptr || Samples == nullptr || NumSamples <= 0)
	{
		return;
	}

	FScopeLock ScopeLock(&Lock);
	FileHandle->Write(reinterpret_cast<const uint8*>(Samples), NumSamples * sizeof(int16));
	DataBytes += NumSamples * sizeof(int16);
}

void FLocalWavWriter::AppendSilenceSeconds(float Seconds)
{
	if (FileHandle == nullptr || Seconds <= 0.f)
	{
		return;
	}

	const int64 NumSamples = static_cast<int64>(Seconds * SampleRate) * NumChannels;
	if (NumSamples <= 0)
	{
		return;
	}

	TArray<int16> Silence;
	Silence.Init(0, NumSamples);
	AppendPCM16(Silence.GetData(), NumSamples);
}

void FLocalWavWriter::Finalize()
{
	if (FileHandle == nullptr)
	{
		return;
	}

	FScopeLock ScopeLock(&Lock);
	// WAV 32 位长度字段：50 分钟 48k 立体声约 575MB，不会超 4GB
	const uint32 DataSize = static_cast<uint32>(FMath::Min<int64>(DataBytes, MAX_uint32));
	WriteHeader(36 + DataSize, DataSize);
}

void FLocalWavWriter::Close()
{
	if (FileHandle != nullptr)
	{
		delete FileHandle;
		FileHandle = nullptr;
	}
}

void FLocalWavWriter::WriteHeader(uint32 RiffSize, uint32 DataSize)
{
	if (FileHandle == nullptr)
	{
		return;
	}

	uint8 Header[44];
	FMemory::Memcpy(&Header[0], "RIFF", 4);
	FMemory::Memcpy(&Header[4], &RiffSize, 4);
	FMemory::Memcpy(&Header[8], "WAVE", 4);
	FMemory::Memcpy(&Header[12], "fmt ", 4);
	const uint32 FmtSize = 16;
	FMemory::Memcpy(&Header[16], &FmtSize, 4);
	const uint16 Format = 1;
	FMemory::Memcpy(&Header[20], &Format, 2);
	const uint16 Ch = static_cast<uint16>(NumChannels);
	FMemory::Memcpy(&Header[22], &Ch, 2);
	const uint32 Sr = static_cast<uint32>(SampleRate);
	FMemory::Memcpy(&Header[24], &Sr, 4);
	const uint32 ByteRate = static_cast<uint32>(SampleRate * NumChannels * 2);
	FMemory::Memcpy(&Header[28], &ByteRate, 4);
	const uint16 BlockAlign = static_cast<uint16>(NumChannels * 2);
	FMemory::Memcpy(&Header[32], &BlockAlign, 2);
	const uint16 BitsPerSample = 16;
	FMemory::Memcpy(&Header[34], &BitsPerSample, 2);
	FMemory::Memcpy(&Header[36], "data", 4);
	FMemory::Memcpy(&Header[40], &DataSize, 4);

	FileHandle->Seek(0);
	FileHandle->Write(Header, 44);
}

// ===================== ULocalRecorder =====================

ULocalRecorder::ULocalRecorder() = default;

ULocalRecorder* ULocalRecorder::CreateLocalRecorder()
{
	return NewObject<ULocalRecorder>();
}

FString ULocalRecorder::ResolveFFmpegPath() const
{
	if (!FFmpegPath.IsEmpty() && IFileManager::Get().FileExists(*FFmpegPath))
	{
		return FFmpegPath;
	}

	const FString ExeDir = FPaths::GetPath(FPlatformProcess::ExecutablePath());
	const FString Candidates[] = {
		FPaths::ProjectDir() + TEXT("ffmpeg.exe"),
		FPaths::ProjectDir() + TEXT("ffmpeg\\ffmpeg.exe"),
		ExeDir + TEXT("\\ffmpeg.exe"),
	};

	for (const FString& Candidate : Candidates)
	{
		if (IFileManager::Get().FileExists(*Candidate))
		{
			return Candidate;
		}
	}

	return FFmpegPath.IsEmpty() ? TEXT("ffmpeg.exe") : FFmpegPath;
}

bool ULocalRecorder::StartRecording(const FString& WindowTitle, int32 Framerate, int32 InMicSampleRate, int32 InMicChannels)
{
	if (bRecording)
	{
		BroadcastFailed(TEXT("已经在录制中"));
		return false;
	}
	if (bProcessing)
	{
		BroadcastFailed(TEXT("上一段视频还在后台合成，请稍候"));
		return false;
	}

	const FString ResolvedFFmpeg = ResolveFFmpegPath();
	if (!IFileManager::Get().FileExists(*ResolvedFFmpeg))
	{
		BroadcastFailed(FString::Printf(
			TEXT("找不到 ffmpeg.exe（%s）。请把它放到项目根目录或游戏 exe 同目录，或用 SetFFmpegPath 指定路径"),
			*ResolvedFFmpeg));
		return false;
	}
	FFmpegPath = ResolvedFFmpeg;

	WorkDir = FPaths::ProjectSavedDir() / TEXT("Recordings") / FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	IFileManager::Get().MakeDirectory(*WorkDir, true);

	ScreenVideoPath = WorkDir / TEXT("screen.mkv");
	GameAudioPath = WorkDir / TEXT("game_audio.wav");
	MicAudioPath = WorkDir / TEXT("mic.wav");
	FinalVideoPath = WorkDir / TEXT("class_recording.mp4");

	MicSampleRate = InMicSampleRate;
	MicChannels = InMicChannels;
	LastMicWriteTime = 0.0;

	if (!StartScreenRecording(WindowTitle, Framerate))
	{
		BroadcastFailed(TEXT("启动 ffmpeg 录屏进程失败，详见日志"));
		return false;
	}

	StartGameAudioTap();

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &ULocalRecorder::TickDrainGameAudio), 0.1f);

	bRecording = true;

	UE_LOG(LogTemp, Log, TEXT("[LocalRecorder] 开始录制: 窗口='%s' fps=%d 目录=%s"), *WindowTitle, Framerate, *WorkDir);
	return true;
}

bool ULocalRecorder::StartScreenRecording(const FString& WindowTitle, int32 Framerate)
{
	// 输出用 Matroska 容器：可随时安全结束进程而不损坏文件（Windows 版 ffmpeg
	// 不读 stdin 管道，无法用 'q' 优雅停止；MP4 硬杀会丢 moov 头导致无法播放）。
	// cluster_time_limit 1 秒：万一截断，最多丢最后 1 秒画面。合成阶段再转成 MP4。
	FString Args;
	if (!WindowTitle.IsEmpty())
	{
		Args = FString::Printf(
			TEXT("-y -f gdigrab -framerate %d -i \"title=%s\" -c:v libx264 -preset fast -crf 23 -pix_fmt yuv420p -f matroska -cluster_time_limit 1000 \"%s\""),
			Framerate, *WindowTitle, *ScreenVideoPath);
	}
	else
	{
		Args = FString::Printf(
			TEXT("-y -f gdigrab -framerate %d -i desktop -c:v libx264 -preset fast -crf 23 -pix_fmt yuv420p -f matroska -cluster_time_limit 1000 \"%s\""),
			Framerate, *ScreenVideoPath);
	}

	uint32 Pid = 0;
	ScreenProc = FPlatformProcess::CreateProc(
		*FFmpegPath, *Args,
		false,  // bLaunchDetached
		true,   // bLaunchHidden
		true,   // bLaunchReallyHidden
		&Pid, 0, *WorkDir,
		nullptr, nullptr);

	return ScreenProc.IsValid();
}

bool ULocalRecorder::StartGameAudioTap()
{
	if (!GEngine)
	{
		return false;
	}

	// UE5.8: GetMainAudioDevice 返回 FAudioDeviceHandle，不能隐式转成 FAudioDevice*
	FAudioDeviceHandle AudioDeviceHandle = GEngine->GetMainAudioDevice();
	if (!AudioDeviceHandle.IsValid())
	{
		return false;
	}
	FAudioDevice* AudioDevice = AudioDeviceHandle.GetAudioDevice();
	if (AudioDevice == nullptr)
	{
		return false;
	}

	GameAudioTap = MakeShared<FGameAudioTap>();
	AudioDevice->RegisterSubmixBufferListener(GameAudioTap.ToSharedRef(), AudioDevice->GetMainSubmixObject());
	UE_LOG(LogTemp, Log, TEXT("[LocalRecorder] 已挂接主 Submix 游戏声音监听"));
	return true;
}

void ULocalRecorder::StopGameAudioTap()
{
	if (!GameAudioTap.IsValid())
	{
		return;
	}

	if (GEngine)
	{
		FAudioDeviceHandle AudioDeviceHandle = GEngine->GetMainAudioDevice();
		if (AudioDeviceHandle.IsValid())
		{
			if (FAudioDevice* AudioDevice = AudioDeviceHandle.GetAudioDevice())
			{
				AudioDevice->UnregisterSubmixBufferListener(GameAudioTap.ToSharedRef(), AudioDevice->GetMainSubmixObject());
			}
		}
	}
	// 不在这里 Reset，StopRecording 还要排空队列
}

bool ULocalRecorder::TickDrainGameAudio(float)
{
	if (!GameAudioTap.IsValid())
	{
		return false;
	}
	DrainGameAudioQueue();
	return true;
}

void ULocalRecorder::DrainGameAudioQueue()
{
	if (!GameAudioTap.IsValid())
	{
		return;
	}

	FGameAudioChunk Chunk;
	while (GameAudioTap->PendingBuffers.Dequeue(Chunk))
	{
		if (!GameWav.IsOpen())
		{
			GameWav.Open(GameAudioPath, Chunk.SampleRate, Chunk.NumChannels);
		}

		const int32 NumSamples = Chunk.Data.Num();
		TArray<int16> PCM16;
		PCM16.SetNumUninitialized(NumSamples);
		for (int32 i = 0; i < NumSamples; ++i)
		{
			const float Sample = FMath::Clamp(Chunk.Data[i], -1.f, 1.f);
			PCM16[i] = static_cast<int16>(Sample * 32767.f);
		}
		GameWav.AppendPCM16(PCM16.GetData(), NumSamples);
	}
}

void ULocalRecorder::WriteMicChunk(const TArray<uint8>& PCM16Chunk, int32 SampleRate, int32 NumChannels)
{
	if (!bRecording || PCM16Chunk.Num() < 2)
	{
		return;
	}

	const double Now = FPlatformTime::Seconds();

	if (!MicWav.IsOpen())
	{
		if (!MicWav.Open(MicAudioPath, SampleRate, NumChannels))
		{
			UE_LOG(LogTemp, Warning, TEXT("[LocalRecorder] 打开麦克风 WAV 失败: %s"), *MicAudioPath);
			return;
		}
		LastMicWriteTime = Now;
	}
	else if (LastMicWriteTime > 0.0 && Now - LastMicWriteTime > MicGapThreshold)
	{
		// 中途切换录音设备等造成的断流：补静音保持与画面对齐
		MicWav.AppendSilenceSeconds(static_cast<float>(Now - LastMicWriteTime));
	}

	MicWav.AppendPCM16(reinterpret_cast<const int16*>(PCM16Chunk.GetData()), PCM16Chunk.Num() / 2);
	LastMicWriteTime = Now;
}

void ULocalRecorder::StopRecording()
{
	if (!bRecording)
	{
		return;
	}
	bRecording = false;

	// 1. 停止声音采集并排空残留数据
	StopGameAudioTap();
	DrainGameAudioQueue();
	GameAudioTap.Reset();
	GameWav.Finalize();
	GameWav.Close();
	MicWav.Finalize();
	MicWav.Close();

	// 2. 移除 ticker
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	// 3. 直接结束录屏进程（Matroska 容器可安全硬杀，不影响已录内容）
	if (ScreenProc.IsValid())
	{
		FPlatformProcess::TerminateProc(ScreenProc, true);
	}

	// 4. 后台线程：等录屏进程退出 → 合成 MP4 → 游戏线程广播
	bProcessing = true;

	const TWeakObjectPtr<ULocalRecorder> WeakThis(this);
	// WaitForProc/CloseProc 参数是 FProcHandle&（非 const），lambda 需 mutable
	FProcHandle ProcToWait = ScreenProc;
	const FString FFmpeg = FFmpegPath;
	const FString VideoFile = ScreenVideoPath;
	const FString GameFile = GameAudioPath;
	const FString MicFile = MicAudioPath;
	const FString FinalFile = FinalVideoPath;
	const FString Dir = WorkDir;

	Async(EAsyncExecution::Thread, [WeakThis, ProcToWait, FFmpeg, VideoFile, GameFile, MicFile, FinalFile, Dir]() mutable
	{
		// 等录屏进程完全退出，释放文件句柄后才能开始合成
		if (ProcToWait.IsValid())
		{
			FPlatformProcess::WaitForProc(ProcToWait);
			FPlatformProcess::CloseProc(ProcToWait);
		}

		// 根据实际生成的音频文件决定合成参数
		const bool bHasGame = IFileManager::Get().FileSize(*GameFile) > 44;
		const bool bHasMic = IFileManager::Get().FileSize(*MicFile) > 44;

		FString MuxArgs;
		if (bHasGame && bHasMic)
		{
			// 游戏声音和麦克风混成一条音轨
			MuxArgs = FString::Printf(
				TEXT("-y -i \"%s\" -i \"%s\" -i \"%s\" ")
				TEXT("-filter_complex \"[1:a][2:a]amix=inputs=2:duration=longest:dropout_transition=0:normalize=0[aout]\" ")
				TEXT("-map 0:v -map \"[aout]\" -c:v copy -c:a aac -movflags +faststart \"%s\""),
				*VideoFile, *GameFile, *MicFile, *FinalFile);
		}
		else if (bHasGame || bHasMic)
		{
			const FString AudioFile = bHasGame ? GameFile : MicFile;
			MuxArgs = FString::Printf(
				TEXT("-y -i \"%s\" -i \"%s\" -map 0:v -map 1:a -c:v copy -c:a aac -movflags +faststart \"%s\""),
				*VideoFile, *AudioFile, *FinalFile);
		}
		else
		{
			MuxArgs = FString::Printf(
				TEXT("-y -i \"%s\" -c:v copy -movflags +faststart \"%s\""),
				*VideoFile, *FinalFile);
		}

		uint32 Pid = 0;
		// WaitForProc/CloseProc 参数是 FProcHandle&（非 const）
		FProcHandle MuxProc = FPlatformProcess::CreateProc(
			*FFmpeg, *MuxArgs, false, true, true, &Pid, 0, *Dir, nullptr, nullptr);

		if (MuxProc.IsValid())
		{
			FPlatformProcess::WaitForProc(MuxProc);
			FPlatformProcess::CloseProc(MuxProc);
		}

		const bool bOk = IFileManager::Get().FileSize(*FinalFile) > 0;

		AsyncTask(ENamedThreads::GameThread, [WeakThis, bOk, FinalFile]()
		{
			if (ULocalRecorder* This = WeakThis.Get())
			{
				This->bProcessing = false;
				if (This->IsRooted())
				{
					This->RemoveFromRoot();
				}

				if (bOk)
				{
					UE_LOG(LogTemp, Log, TEXT("[LocalRecorder] 合成完成: %s"), *FinalFile);
					This->OnRecordingComplete.Broadcast(FinalFile);
				}
				else
				{
					This->BroadcastFailed(TEXT("合成视频失败（ffmpeg 合成进程异常退出或未生成文件）"));
				}
			}
		});
	});
}

FString ULocalRecorder::GetFinalVideoFileUrl() const
{
	if (FinalVideoPath.IsEmpty())
	{
		return FString();
	}
	// 编辑器里 ProjectSavedDir 返回相对路径（../../../../ClassBot...），Media Player 打不开，必须转绝对路径
	FString Path = FPaths::ConvertRelativePathToFull(FinalVideoPath);
	Path.ReplaceInline(TEXT("\\"), TEXT("/"), ESearchCase::IgnoreCase);

	// 安装路径含空格/中文时 file:/// 必须百分号编码，否则 Media Player 打不开
	FString Encoded;
	Encoded.Reserve(Path.Len() + 16);
	for (const TCHAR Char : Path)
	{
		const bool bSafe = (Char >= TEXT('a') && Char <= TEXT('z'))
			|| (Char >= TEXT('A') && Char <= TEXT('Z'))
			|| (Char >= TEXT('0') && Char <= TEXT('9'))
			|| Char == TEXT('/') || Char == TEXT(':') || Char == TEXT('.')
			|| Char == TEXT('-') || Char == TEXT('_') || Char == TEXT('~');
		if (bSafe)
		{
			Encoded.AppendChar(Char);
		}
		else
		{
			Encoded.Appendf(TEXT("%%%02X"), static_cast<uint8>(Char));
		}
	}
	return TEXT("file:///") + Encoded;
}

void ULocalRecorder::DeleteLocalRecordingFiles()
{
	if (WorkDir.IsEmpty())
	{
		return;
	}
	IFileManager::Get().DeleteDirectory(*WorkDir, false, true);
	UE_LOG(LogTemp, Log, TEXT("[LocalRecorder] 已删除本地录制文件: %s"), *WorkDir);
}

void ULocalRecorder::BroadcastFailed(const FString& Message)
{
	UE_LOG(LogTemp, Error, TEXT("[LocalRecorder] %s"), *Message);
	OnRecordingFailed.Broadcast(Message);
}

void ULocalRecorder::BeginDestroy()
{
	if (bRecording)
	{
		bRecording = false;
		bProcessing = false;

		if (TickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
			TickerHandle.Reset();
		}

		StopGameAudioTap();
		GameAudioTap.Reset();
		GameWav.Close();
		MicWav.Close();

		if (ScreenProc.IsValid())
		{
			FPlatformProcess::TerminateProc(ScreenProc, true);
			FPlatformProcess::CloseProc(ScreenProc);
		}
	}

	if (IsRooted())
	{
		RemoveFromRoot();
	}

	Super::BeginDestroy();
}