#include "FileUploader.h"
// 基于 VaRestX 插件的文件上传实现（直接新增到现有项目，非独立模块/插件）
// 用 VaRestX 的 UVaRestRequestJSON（binary 模式）发送手工拼好的 multipart/form-data 报文体。
// VaRestX 与原版 VaRest API 完全一致。

#include "VaRestRequestJSON.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"

UFileUploader::UFileUploader()
	: VaRestRequest(nullptr)
	, LastResponseCode(0)
{
}

//////////////////////////////////////////////////////////////////////////
// 构造

UFileUploader* UFileUploader::CreateUploadRequest()
{
	return NewObject<UFileUploader>();
}

//////////////////////////////////////////////////////////////////////////
// MIME 推断

FString UFileUploader::GuessMimeType(const FString& FileName)
{
	const FString Ext = FPaths::GetExtension(FileName).ToLower();

	if (Ext == TEXT("png"))							return TEXT("image/png");
	if (Ext == TEXT("jpg") || Ext == TEXT("jpeg"))	return TEXT("image/jpeg");
	if (Ext == TEXT("bmp"))							return TEXT("image/bmp");
	if (Ext == TEXT("gif"))							return TEXT("image/gif");
	if (Ext == TEXT("webp"))						return TEXT("image/webp");
	if (Ext == TEXT("wav"))							return TEXT("audio/wav");
	if (Ext == TEXT("mp3"))							return TEXT("audio/mpeg");
	if (Ext == TEXT("mp4"))							return TEXT("video/mp4");
	if (Ext == TEXT("pdf"))							return TEXT("application/pdf");
	if (Ext == TEXT("zip"))							return TEXT("application/zip");
	if (Ext == TEXT("json"))						return TEXT("application/json");
	if (Ext == TEXT("txt"))							return TEXT("text/plain");

	return TEXT("application/octet-stream");
}

//////////////////////////////////////////////////////////////////////////
// 拼装 multipart/form-data 报文体

TArray<uint8> UFileUploader::BuildMultipartBody(
	const TArray<uint8>& FileData,
	const FString& FileName,
	const FString& FieldName,
	const FString& MimeType,
	const TMap<FString, FString>& FormFields,
	const FString& Boundary)
{
	TArray<uint8> Body;

	// 工具 lambda：把 FString 按 UTF-8 追加到字节数组
	auto AppendString = [&Body](const FString& Str)
	{
		const FTCHARToUTF8 UTF8(*Str);
		Body.Append(reinterpret_cast<const uint8*>(UTF8.Get()), UTF8.Length());
	};

	// 1) 普通表单字段
	for (const TPair<FString, FString>& Elem : FormFields)
	{
		AppendString(FString::Printf(TEXT("--%s\r\n"), *Boundary));
		AppendString(FString::Printf(TEXT("Content-Disposition: form-data; name=\"%s\"\r\n\r\n"), *Elem.Key));
		AppendString(Elem.Value);
		AppendString(TEXT("\r\n"));
	}

	// 2) 文件字段
	AppendString(FString::Printf(TEXT("--%s\r\n"), *Boundary));
	AppendString(FString::Printf(
		TEXT("Content-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\n"),
		*FieldName, *FileName));
	AppendString(FString::Printf(TEXT("Content-Type: %s\r\n\r\n"), *MimeType));

	// 文件二进制内容（直接追加原始字节，不做任何编码）
	Body.Append(FileData);
	AppendString(TEXT("\r\n"));

	// 3) 结束边界
	AppendString(FString::Printf(TEXT("--%s--\r\n"), *Boundary));

	return Body;
}

//////////////////////////////////////////////////////////////////////////
// 通用发起逻辑（底层用 VaRest）

void UFileUploader::LaunchRequest(
	const FString& URL,
	const TArray<uint8>& FileData,
	const FString& FileName,
	const FString& FieldName,
	const TMap<FString, FString>& FormFields,
	const FString& MimeType,
	const FString& AuthToken)
{
	// 创建 VaRest 请求对象
	VaRestRequest = NewObject<UVaRestRequestJSON>();
	if (!VaRestRequest)
	{
		OnUploadFailed.Broadcast(TEXT("创建 VaRest 请求对象失败"));
		return;
	}

	// 生成唯一边界，避免与文件内容冲突
	const FString Boundary = FString::Printf(
		TEXT("----FileUploadBoundary%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));

	// 构造 multipart 报文体
	const TArray<uint8> Body = BuildMultipartBody(
		FileData, FileName, FieldName, MimeType, FormFields, Boundary);

	// 配置 VaRest 请求（关键：使用 binary 模式）
	VaRestRequest->SetVerb(EVaRestRequestVerb::POST);
	VaRestRequest->SetContentType(EVaRestRequestContentType::binary);
	// SetBinaryContentType 设置 Content-Type 头（含 boundary）
	VaRestRequest->SetBinaryContentType(
		FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary));
	// SetBinaryRequestContent 设置原始字节数组请求体
	VaRestRequest->SetBinaryRequestContent(Body);

	// 可选鉴权头
	if (!AuthToken.IsEmpty())
	{
		VaRestRequest->SetHeader(TEXT("Authorization"),
			FString::Printf(TEXT("Bearer %s"), *AuthToken));
	}

	// 绑定 VaRest 委托
	VaRestRequest->OnRequestComplete.AddDynamic(this, &UFileUploader::OnVaRestComplete);
	VaRestRequest->OnRequestFail.AddDynamic(this, &UFileUploader::OnVaRestFail);

	// 设置 URL
	VaRestRequest->SetURL(URL);

	// 防止本对象被 GC（VaRest 内部会 AddToRoot 请求对象，但本对象需自己保护）
	AddToRoot();

	// 发起请求（VaRest 的 ExecuteProcessRequest 会触发 ProcessRequest）
	VaRestRequest->ExecuteProcessRequest();

	UE_LOG(LogTemp, Log,
		TEXT("[FileUploader/VaRest] 上传 %s (%s, %d bytes) -> %s"),
		*FileName, *MimeType, FileData.Num(), *URL);
}

//////////////////////////////////////////////////////////////////////////
// VaRest 回调

void UFileUploader::OnVaRestComplete(UVaRestRequestJSON* Request)
{
	if (!Request)
	{
		RemoveFromRoot();
		OnUploadFailed.Broadcast(TEXT("VaRest 回调 Request 为空"));
		return;
	}

	LastResponseCode = Request->GetResponseCode();
	LastResponseContent = Request->GetResponseContentAsString();

	UE_LOG(LogTemp, Log,
		TEXT("[FileUploader/VaRest] 响应 %d: %s"),
		LastResponseCode, *LastResponseContent);

	// 解除 GC 保护
	RemoveFromRoot();

	// HTTP 层成功即广播 Complete，业务状态码（200/4xx）在蓝图里自行判断
	OnUploadComplete.Broadcast(LastResponseCode);
}

void UFileUploader::OnVaRestFail(UVaRestRequestJSON* Request)
{
	LastResponseCode = Request ? Request->GetResponseCode() : 0;
	LastResponseContent = TEXT("");

	UE_LOG(LogTemp, Warning,
		TEXT("[FileUploader/VaRest] 请求失败 (网络层): %d"),
		LastResponseCode);

	// 解除 GC 保护
	RemoveFromRoot();

	OnUploadFailed.Broadcast(FString::Printf(
		TEXT("请求失败 (网络层): HTTP %d"),
		LastResponseCode));
}

//////////////////////////////////////////////////////////////////////////
// 入口：从磁盘文件上传

void UFileUploader::UploadFile(
	const FString& URL,
	const FString& FilePath,
	const FString& FieldName,
	const TMap<FString, FString>& FormFields,
	const FString& MimeType,
	const FString& AuthToken)
{
	if (FilePath.IsEmpty())
	{
		OnUploadFailed.Broadcast(TEXT("FilePath 为空"));
		return;
	}

	// 读取文件到字节数组
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
	{
		OnUploadFailed.Broadcast(FString::Printf(TEXT("读取文件失败: %s"), *FilePath));
		return;
	}

	const FString FileName = FPaths::GetCleanFilename(FilePath);
	const FString ResolvedMime = MimeType.IsEmpty() ? GuessMimeType(FilePath) : MimeType;

	LaunchRequest(URL, FileData, FileName, FieldName, FormFields, ResolvedMime, AuthToken);
}

//////////////////////////////////////////////////////////////////////////
// 入口：从内存字节上传

void UFileUploader::UploadFileFromMemory(
	const FString& URL,
	const TArray<uint8>& FileData,
	const FString& FileName,
	const FString& FieldName,
	const TMap<FString, FString>& FormFields,
	const FString& MimeType,
	const FString& AuthToken)
{
	if (FileData.Num() == 0)
	{
		OnUploadFailed.Broadcast(TEXT("FileData 为空"));
		return;
	}

	const FString ResolvedMime = MimeType.IsEmpty() ? TEXT("application/octet-stream") : MimeType;

	LaunchRequest(URL, FileData, FileName, FieldName, FormFields, ResolvedMime, AuthToken);
}
