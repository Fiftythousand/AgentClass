// PptxViewerWidget.cpp
// PPTX Viewer Widget 实现

#include "PptxViewerWidget.h"
#include "WebBrowser.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformFileManager.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/GameInstance.h"
#include "UObject/UnrealType.h"

UPptxViewerWidget::UPptxViewerWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // [Fix] bIsFocusable 只能在构造函数设置，UE 5.8 中运行时设置会触发 deprecation 警告
    bIsFocusable = true;
}

void UPptxViewerWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (WebBrowser)
    {
        FScriptDelegate Delegate;
        Delegate.BindUFunction(this, "HandleConsoleMessage");
        WebBrowser->OnConsoleMessage.Add(Delegate);

        // [Fix #4] 绑定页面加载完成事件，替代固定2秒延迟
        FScriptDelegate UrlChangedDelegate;
        UrlChangedDelegate.BindUFunction(this, "HandleUrlChanged");
        WebBrowser->OnUrlChanged.Add(UrlChangedDelegate);
    }

    // 直接从GameInstance反射读取SavedViewerUrl
    FString ViewerUrl;
    if (UGameInstance* GI = GetGameInstance())
    {
        if (FProperty* Prop = GI->GetClass()->FindPropertyByName(TEXT("SavedViewerUrl")))
        {
            if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
            {
                StrProp->GetValue_InContainer(GI, &ViewerUrl);
            }
        }
    }

    if (WebBrowser)
    {
        if (!ViewerUrl.IsEmpty())
        {
            LoadPptx(ViewerUrl);
            SavedViewerUrl = ViewerUrl;
            UE_LOG(LogTemp, Log, TEXT("[PPTX] 自动恢复查看器: %s"), *ViewerUrl);
        }
        else
        {
            // [Fix #2] 修复双斜杠：ServerBaseUrl 已有末尾 '/'
            FString BaseUrl = ServerBaseUrl;
            if (!BaseUrl.EndsWith(TEXT("/")))
            {
                BaseUrl += TEXT("/");
            }
            FString UploadPageUrl = BaseUrl + TEXT("index.html");
            WebBrowser->LoadURL(UploadPageUrl);
            UE_LOG(LogTemp, Log, TEXT("[PPTX] 自动加载上传页面: %s"), *UploadPageUrl);
        }
    }

    // [Fix #3] 设置键盘焦点
    SetKeyboardFocus();
}

void UPptxViewerWidget::HandleUrlChanged(const FText& Url)
{
    UE_LOG(LogTemp, Log, TEXT("[PPTX] 页面加载完成: %s"), *Url.ToString());

    // 页面加载完成后延迟 0.5 秒注入 JS 桥接，确保 DOM 就绪
    TWeakObjectPtr<UPptxViewerWidget> WeakThis(this);
    FTimerDelegate TimerDelegate;
    TimerDelegate.BindLambda([WeakThis]()
    {
        if (WeakThis.IsValid() && WeakThis->WebBrowser)
        {
            WeakThis->InjectJsBridge();
        }
    });

    if (UWorld* World = GetWorld())
    {
        FTimerHandle TimerHandle;
        World->GetTimerManager().SetTimer(TimerHandle, TimerDelegate, 0.5f, false);
    }
}

void UPptxViewerWidget::InjectJsBridge()
{
    if (!WebBrowser) return;

    WebBrowser->ExecuteJavascript(
        TEXT("if(typeof broadcastToUE==='function'&&!window._ueBridgeReady){")
        TEXT("window._origBroadcast=broadcastToUE;")
        TEXT("window.broadcastToUE=function(name,data){")
        TEXT("console.log('__UE_MSG__:'+name+':'+(typeof data==='string'?data:JSON.stringify(data)));")
        TEXT("if(window._origBroadcast)window._origBroadcast(name,data);")
        TEXT("};")
        TEXT("window._ueBridgeReady=true;")
        TEXT("console.log('__UE_MSG__:bridgeReady:{}');")
        TEXT("}")
    );
    UE_LOG(LogTemp, Log, TEXT("[PPTX] JS 通信桥接已注入"));
}

FReply UPptxViewerWidget::NativeOnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
    // 按 F 键自动弹出文件选择框上传 PPT
    if (InKeyEvent.GetKey() == EKeys::F)
    {
        UE_LOG(LogTemp, Log, TEXT("[PPTX] F键按下，弹出文件选择框"));
        BrowseAndUploadPptx();
        return FReply::Handled();
    }
    return FReply::Unhandled();
}

// ==================== PPT 加载与翻页 ====================

void UPptxViewerWidget::LoadPptx(const FString& ViewerUrl)
{
    if (WebBrowser)
    {
        WebBrowser->LoadURL(ViewerUrl);
        UE_LOG(LogTemp, Log, TEXT("[PPTX] 加载查看器: %s"), *ViewerUrl);
        // [Fix #4] JS 桥接注入由 OnUrlChanged 回调触发，不再使用固定2秒定时器
    }
}

void UPptxViewerWidget::NextSlide()
{
    ExecuteJS(TEXT("if(typeof nextSlide==='function'){nextSlide();}else if(typeof ue!=='undefined'&&ue.interface){ue.interface.nextSlide();}"));
}

void UPptxViewerWidget::PrevSlide()
{
    ExecuteJS(TEXT("if(typeof prevSlide==='function'){prevSlide();}else if(typeof ue!=='undefined'&&ue.interface){ue.interface.prevSlide();}"));
}

void UPptxViewerWidget::GoToSlide(int32 SlideIndex)
{
    FString JS = FString::Printf(
        TEXT("if(typeof goToSlide==='function'){goToSlide(%d);}else if(typeof ue!=='undefined'&&ue.interface){ue.interface.goToSlide('{\"index\":%d}');}"),
        SlideIndex, SlideIndex);
    ExecuteJS(JS);
}

// ==================== 文件上传 ====================

void UPptxViewerWidget::SetUploadUrl(const FString& Url)
{
    UploadUrl = Url;
}

void UPptxViewerWidget::SetServerUrl(const FString& Url)
{
    ServerBaseUrl = Url;
    if (!ServerBaseUrl.EndsWith(TEXT("/")))
    {
        ServerBaseUrl += TEXT("/");
    }
    UploadUrl = ServerBaseUrl + TEXT("pptx/upload");
}

bool UPptxViewerWidget::BrowseAndUploadPptx()
{
    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (!DesktopPlatform)
    {
        UE_LOG(LogTemp, Error, TEXT("[PPTX] DesktopPlatform 不可用"));
        OnUploadFailed.Broadcast(TEXT("桌面平台不可用"));
        return false;
    }

    void* ParentWindowHandle = nullptr;
    if (FSlateApplication::IsInitialized())
    {
        TSharedPtr<SWindow> RootWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
        if (RootWindow.IsValid())
        {
            ParentWindowHandle = RootWindow->GetNativeWindow()->GetOSWindowHandle();
        }
    }

    TArray<FString> OutFiles;
    FString Filter = TEXT("PowerPoint 文件 (*.pptx)|*.pptx");
    bool bSelected = DesktopPlatform->OpenFileDialog(
        ParentWindowHandle,
        TEXT("选择 PPTX 文件"),
        FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE")) / TEXT("Desktop"),
        TEXT(""),
        Filter,
        0,
        OutFiles
    );

    if (!bSelected || OutFiles.Num() == 0)
    {
        return false;
    }

    return UploadPptxFile(OutFiles[0]);
}

bool UPptxViewerWidget::UploadPptxFile(const FString& FilePath)
{
    if (!FPaths::FileExists(FilePath))
    {
        OnUploadFailed.Broadcast(FString::Printf(TEXT("文件不存在: %s"), *FilePath));
        return false;
    }

    if (!FilePath.EndsWith(TEXT(".pptx"), ESearchCase::IgnoreCase))
    {
        OnUploadFailed.Broadcast(TEXT("仅支持 .pptx 文件"));
        return false;
    }

    TArray<uint8> FileData;
    if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
    {
        OnUploadFailed.Broadcast(TEXT("读取文件失败"));
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("[PPTX] 上传文件: %s (%.2f KB)"), *FilePath, FileData.Num() / 1024.0f);

    FString FileName = FPaths::GetCleanFilename(FilePath);
    FString Boundary = FString::Printf(TEXT("----UE5Boundary%X"), FDateTime::Now().GetTicks());
    FString BoundaryBegin = TEXT("--") + Boundary + TEXT("\r\n");
    FString BoundaryEnd = TEXT("--") + Boundary + TEXT("--\r\n");

    FString FileHeader = FString::Printf(
        TEXT("Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
             "Content-Type: application/vnd.openxmlformats-officedocument.presentationml.presentation\r\n\r\n"),
        *FileName);

    TArray<uint8> RequestBody;
    FStringToBytes(BoundaryBegin, RequestBody);
    FStringToBytes(FileHeader, RequestBody);
    RequestBody.Append(FileData);
    FStringToBytes(TEXT("\r\n"), RequestBody);
    FStringToBytes(BoundaryEnd, RequestBody);

    // [Fix #5] 记录总大小用于进度计算
    int32 TotalSize = RequestBody.Num();

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
    HttpRequest->SetURL(UploadUrl);
    HttpRequest->SetVerb(TEXT("POST"));
    HttpRequest->SetHeader(TEXT("Content-Type"), FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary));
    HttpRequest->SetTimeout(120);  // 120秒超时，PPTX 上传+视频转码需要时间

    TWeakObjectPtr<UPptxViewerWidget> WeakThis(this);

    HttpRequest->OnProcessRequestComplete().BindLambda(
        [WeakThis](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bSuccess)
        {
            if (WeakThis.IsValid())
            {
                WeakThis->HandleUploadComplete(Req, Resp, bSuccess);
            }
        });
    HttpRequest->SetContent(RequestBody);

    UE_LOG(LogTemp, Log, TEXT("[PPTX] 开始上传到: %s (总大小: %d 字节)"), *UploadUrl, TotalSize);

    if (!HttpRequest->ProcessRequest())
    {
        OnUploadFailed.Broadcast(TEXT("HTTP 请求发送失败"));
        return false;
    }

    OnUploadProgress.Broadcast(0);
    return true;
}

void UPptxViewerWidget::HandleUploadComplete(
    FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
    if (!bWasSuccessful || !Response.IsValid())
    {
        OnUploadFailed.Broadcast(TEXT("网络错误，上传失败"));
        OnUploadProgress.Broadcast(-1);
        return;
    }

    int32 ResponseCode = Response->GetResponseCode();
    FString ResponseStr = Response->GetContentAsString();

    if (ResponseCode != 200)
    {
        OnUploadFailed.Broadcast(FString::Printf(TEXT("服务器返回错误: %d"), ResponseCode));
        OnUploadProgress.Broadcast(-1);
        return;
    }

    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseStr);
    if (FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid())
    {
        // [Fix #6] 安全读取字段，避免字段缺失时崩溃
        FString ViewerUrl;
        FString FileId;
        if (Json->HasField(TEXT("viewerUrl")))
        {
            ViewerUrl = Json->GetStringField(TEXT("viewerUrl"));
        }
        if (Json->HasField(TEXT("fileId")))
        {
            FileId = Json->GetStringField(TEXT("fileId"));
        }

        UE_LOG(LogTemp, Log, TEXT("[PPTX] 上传成功! fileId=%s"), *FileId);

        OnUploadProgress.Broadcast(100);
        OnUploadComplete.Broadcast(ViewerUrl);

        // 保存到GameInstance，关卡切换后可恢复
        if (UGameInstance* GI = GetGameInstance())
        {
            if (FProperty* Prop = GI->GetClass()->FindPropertyByName(TEXT("SavedViewerUrl")))
            {
                if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
                {
                    StrProp->SetValue_InContainer(GI, ViewerUrl);
                }
            }
        }
        SavedViewerUrl = ViewerUrl;

        // [Fix #1] 移除重复的保存和加载代码（原来写了两遍）
        if (!ViewerUrl.IsEmpty())
        {
            LoadPptx(ViewerUrl);
        }
    }
    else
    {
        OnUploadFailed.Broadcast(TEXT("服务器响应解析失败"));
        OnUploadProgress.Broadcast(-1);
    }
}

void UPptxViewerWidget::FStringToBytes(const FString& Str, TArray<uint8>& OutBytes)
{
    FTCHARToUTF8 Conv(*Str);
    OutBytes.Append((const uint8*)Conv.Get(), Conv.Length());
}

// ==================== 视频播放 ====================

void UPptxViewerWidget::PlayVideoOnCurrentSlide()
{
    ExecuteJS(TEXT("if(typeof showVideoForSlide==='function'){showVideoForSlide(") +
        FString::FromInt(CurrentSlideIndex) + TEXT(");}"));
}

void UPptxViewerWidget::PlayVideo(const FString& VideoUrl)
{
    FString EscapedUrl = VideoUrl.Replace(TEXT("'"), TEXT("\\'"));
    ExecuteJS(TEXT("if(typeof playVideoOverlay==='function'){playVideoOverlay('") +
        EscapedUrl + TEXT("');}"));
}

void UPptxViewerWidget::CloseVideo()
{
    ExecuteJS(TEXT("if(typeof closeVideoOverlay==='function'){closeVideoOverlay();}"));
}

// ==================== 画笔功能 ====================

void UPptxViewerWidget::TogglePen(bool bEnabled)
{
    bPenEnabled = bEnabled;
    FString JS = FString::Printf(TEXT("if(typeof togglePen==='function'){togglePen(%s);}"),
        bEnabled ? TEXT("true") : TEXT("false"));
    ExecuteJS(JS);
}

void UPptxViewerWidget::SetPenColor(const FString& Color)
{
    CurrentPenColor = Color;
    FString JS = FString::Printf(TEXT("if(typeof setPenColor==='function'){setPenColor('%s');}"), *Color);
    ExecuteJS(JS);
}

void UPptxViewerWidget::SetPenSize(int32 Size)
{
    CurrentPenSize = Size;
    FString JS = FString::Printf(TEXT("if(typeof setPenSize==='function'){setPenSize(%d);}"), Size);
    ExecuteJS(JS);
}

void UPptxViewerWidget::SetTool(const FString& Tool)
{
    FString JS = FString::Printf(TEXT("if(typeof setTool==='function'){setTool('%s');}"), *Tool);
    ExecuteJS(JS);
}

void UPptxViewerWidget::ClearCanvas()
{
    ExecuteJS(TEXT("if(typeof clearCanvas==='function'){clearCanvas();}"));
}

void UPptxViewerWidget::UndoStroke()
{
    ExecuteJS(TEXT("if(typeof undoStroke==='function'){undoStroke();}"));
}

void UPptxViewerWidget::RedoStroke()
{
    ExecuteJS(TEXT("if(typeof redoStroke==='function'){redoStroke();}"));
}

// ==================== 内部方法 ====================

void UPptxViewerWidget::ExecuteJS(const FString& JS)
{
    if (WebBrowser)
    {
        WebBrowser->ExecuteJavascript(JS);
    }
}

// ==================== 事件处理 ====================

void UPptxViewerWidget::HandleConsoleMessage(const FString& Message, const FString& Source, int32 Line)
{
    // 只处理 __UE_MSG__ 前缀的消息
    if (!Message.StartsWith(TEXT("__UE_MSG__:")))
    {
        return;
    }

    // 解析格式: __UE_MSG__:eventName:jsonData
    FString Rest = Message.Mid(11); // 去掉 "__UE_MSG__:" 前缀
    int32 ColonPos;
    if (!Rest.FindChar(TEXT(':'), ColonPos))
    {
        return;
    }

    FString Name = Rest.Left(ColonPos);
    FString Data = Rest.Mid(ColonPos + 1);

    UE_LOG(LogTemp, Log, TEXT("[PPTX] Event: %s | %s"), *Name, *Data);

    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Data);

    // [Fix #7] 统一解析 JSON，解析失败时跳过该事件，避免空指针崩溃
    bool bJsonValid = FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid();

    // PPT 加载完成
    if (Name == TEXT("onPptxReady"))
    {
        if (bJsonValid)
        {
            TotalSlides = Json->GetIntegerField(TEXT("total"));
            CurrentSlideIndex = Json->GetIntegerField(TEXT("current"));
            OnPptxReady.Broadcast(TotalSlides, CurrentSlideIndex);
        }
    }
    // 翻页
    else if (Name == TEXT("onSlideChanged"))
    {
        if (bJsonValid)
        {
            CurrentSlideIndex = Json->GetIntegerField(TEXT("index"));
            TotalSlides = Json->GetIntegerField(TEXT("total"));
            OnSlideChanged.Broadcast(CurrentSlideIndex, TotalSlides);
        }
    }
    // 视频播放
    else if (Name == TEXT("onVideoPlay"))
    {
        int32 SlideIdx = 0;
        FString FileName;
        if (bJsonValid)
        {
            SlideIdx = Json->GetIntegerField(TEXT("slide"));
            FileName = Json->HasField(TEXT("fileName")) ? Json->GetStringField(TEXT("fileName")) : TEXT("");
        }
        OnVideoPlay.Broadcast(SlideIdx, FileName);
    }
    // 视频关闭
    else if (Name == TEXT("onVideoClose"))
    {
        int32 SlideIdx = 0;
        if (bJsonValid)
        {
            SlideIdx = Json->GetIntegerField(TEXT("slide"));
        }
        OnVideoClose.Broadcast(SlideIdx);
    }
    // 视频状态变化
    else if (Name == TEXT("onVideoState"))
    {
        if (bJsonValid)
        {
            int32 SlideIdx = Json->GetIntegerField(TEXT("slide"));
            FString State = Json->GetStringField(TEXT("state"));
            double CurrentTime = Json->GetNumberField(TEXT("currentTime"));
            OnVideoState.Broadcast(SlideIdx, State, (float)CurrentTime);
        }
    }
    // 视频错误
    else if (Name == TEXT("onVideoError"))
    {
        if (bJsonValid)
        {
            int32 SlideIdx = Json->GetIntegerField(TEXT("slide"));
            int32 ErrorCode = Json->GetIntegerField(TEXT("code"));
            FString ErrorMsg = Json->GetStringField(TEXT("message"));
            OnVideoError.Broadcast(SlideIdx, ErrorCode, ErrorMsg);
        }
    }
    // 视频进度等频繁事件（仅日志）
    else if (Name == TEXT("onVideoProgress") || Name == TEXT("onVideoSeek") ||
             Name == TEXT("onVideoVolume") || Name == TEXT("onVideoRate") ||
             Name == TEXT("onVideoBuffer") || Name == TEXT("onVideoLoaded") ||
             Name == TEXT("onVideoCanPlay"))
    {
        UE_LOG(LogTemp, Verbose, TEXT("[PPTX] %s: %s"), *Name, *Data);
    }
    // 画笔开关
    else if (Name == TEXT("onPenToggle"))
    {
        if (bJsonValid)
        {
            bPenEnabled = Json->GetBoolField(TEXT("enabled"));
        }
        OnPenToggle.Broadcast(bPenEnabled);
    }
    // 画笔绘制结束
    else if (Name == TEXT("onDrawEnd"))
    {
        int32 SlideIdx = 0, StrokeCount = 0;
        if (bJsonValid)
        {
            SlideIdx = Json->GetIntegerField(TEXT("index"));
            StrokeCount = Json->GetIntegerField(TEXT("strokeCount"));
        }
        OnDrawEnd.Broadcast(SlideIdx, StrokeCount);
    }
    // 画笔颜色变化
    else if (Name == TEXT("onPenColorChanged"))
    {
        if (bJsonValid)
        {
            CurrentPenColor = Json->GetStringField(TEXT("color"));
            OnPenColorChanged.Broadcast(CurrentPenColor);
        }
    }
    // 画笔粗细变化
    else if (Name == TEXT("onPenSizeChanged"))
    {
        if (bJsonValid)
        {
            CurrentPenSize = Json->GetIntegerField(TEXT("size"));
            OnPenSizeChanged.Broadcast(CurrentPenSize);
        }
    }
    // 画布清空
    else if (Name == TEXT("onCanvasCleared"))
    {
        int32 SlideIdx = 0;
        if (bJsonValid)
        {
            SlideIdx = Json->GetIntegerField(TEXT("index"));
        }
        OnCanvasCleared.Broadcast(SlideIdx);
    }
    // 撤销/重做
    else if (Name == TEXT("onUndo") || Name == TEXT("onRedo"))
    {
        int32 SlideIdx = 0, StrokeCount = 0;
        if (bJsonValid)
        {
            SlideIdx = Json->GetIntegerField(TEXT("index"));
            StrokeCount = Json->GetIntegerField(TEXT("strokeCount"));
        }
        OnUndoRedo.Broadcast(SlideIdx, StrokeCount);
    }
    // 桥接就绪
    else if (Name == TEXT("bridgeReady"))
    {
        UE_LOG(LogTemp, Log, TEXT("[PPTX] JS 通信桥接就绪"));
    }
    // 未知事件 + JSON 解析失败日志
    else if (!bJsonValid)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PPTX] 事件 %s 的 JSON 解析失败: %s"), *Name, *Data);
    }
}
