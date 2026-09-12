// PptxViewerWidget.h
// PPTX Viewer Widget - 在 UE5 中通过 WebBrowser 嵌入 PPT 演示查看器
// 支持：PPT 加载、翻页控制、画笔批注、视频播放、文件上传

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "PptxViewerWidget.generated.h"

class UWebBrowser;

// ==================== 事件委托 ====================

// PPT 加载完成
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPptxReadyDelegate, int32, TotalSlides, int32, CurrentSlide);
// 翻页事件
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSlideChangedDelegate, int32, CurrentIndex, int32, TotalSlides);

// 文件上传事件
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUploadCompleteDelegate, const FString&, ViewerUrl);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUploadFailedDelegate, const FString&, ErrorMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUploadProgressDelegate, int32, Percent);

// 视频事件
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnVideoPlayDelegate, int32, SlideIndex, const FString&, FileName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVideoCloseDelegate, int32, SlideIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnVideoStateDelegate, int32, SlideIndex, const FString&, State, float, CurrentTime);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnVideoErrorDelegate, int32, SlideIndex, int32, ErrorCode, const FString&, ErrorMessage);

// 画笔事件
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPenToggleDelegate, bool, bEnabled);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDrawEndDelegate, int32, SlideIndex, int32, StrokeCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPenColorChangedDelegate, const FString&, Color);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPenSizeChangedDelegate, int32, Size);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCanvasClearedDelegate, int32, SlideIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnUndoRedoDelegate, int32, SlideIndex, int32, StrokeCount);

/**
 * PPTX Viewer Widget
 * 在 UE5 中通过 WebBrowser 嵌入 PPT 演示查看器
 * 使用方法：
 *   1. 创建 Widget Blueprint，父类设为 UPptxViewerWidget
 *   2. 在 Blueprint 中添加 WebBrowser 控件，命名为 "WebBrowser"
 *   3. 调用 LoadPptx 或 BrowseAndUploadPptx 加载 PPT
 */
UCLASS(Blueprintable, BlueprintType)
class PPTXVIEWER_API UPptxViewerWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UPptxViewerWidget(const FObjectInitializer& ObjectInitializer);

    // ==================== PPT 加载与翻页 ====================

    /** 加载 PPTX 查看器页面（传入完整的 viewer URL） */
    UFUNCTION(BlueprintCallable, Category = "PPTX")
    void LoadPptx(const FString& ViewerUrl);

    /** 下一页 */
    UFUNCTION(BlueprintCallable, Category = "PPTX")
    void NextSlide();

    /** 上一页 */
    UFUNCTION(BlueprintCallable, Category = "PPTX")
    void PrevSlide();

    /** 跳转到指定页（从1开始） */
    UFUNCTION(BlueprintCallable, Category = "PPTX")
    void GoToSlide(int32 SlideIndex);

    // ==================== 文件上传 ====================

    /** 弹出文件选择对话框，选择 .pptx 文件并上传 */
    UFUNCTION(BlueprintCallable, Category = "PPTX|Upload")
    bool BrowseAndUploadPptx();

    /** 直接上传指定路径的 pptx 文件 */
    UFUNCTION(BlueprintCallable, Category = "PPTX|Upload")
    bool UploadPptxFile(const FString& FilePath);

    /** 设置上传服务器地址 */
    UFUNCTION(BlueprintCallable, Category = "PPTX|Upload")
    void SetUploadUrl(const FString& Url);

    /** 设置查看器服务器基础地址（如 http://localhost:8080） */
    UFUNCTION(BlueprintCallable, Category = "PPTX|Upload")
    void SetServerUrl(const FString& Url);

    // ==================== 视频播放 ====================

    /** 播放当前页的视频 */
    UFUNCTION(BlueprintCallable, Category = "PPTX|Video")
    void PlayVideoOnCurrentSlide();

    /** 播放指定URL的视频 */
    UFUNCTION(BlueprintCallable, Category = "PPTX|Video")
    void PlayVideo(const FString& VideoUrl);

    /** 关闭视频播放 */
    UFUNCTION(BlueprintCallable, Category = "PPTX|Video")
    void CloseVideo();

    // ==================== 画笔功能 ====================

    /** 开启/关闭画笔模式 */
    UFUNCTION(BlueprintCallable, Category = "PPTX|Pen")
    void TogglePen(bool bEnabled);

    /** 设置画笔颜色，如 "#e74c3c" */
    UFUNCTION(BlueprintCallable, Category = "PPTX|Pen")
    void SetPenColor(const FString& Color);

    /** 设置画笔粗细，1-20 */
    UFUNCTION(BlueprintCallable, Category = "PPTX|Pen")
    void SetPenSize(int32 Size);

    /** 切换工具：pen / eraser */
    UFUNCTION(BlueprintCallable, Category = "PPTX|Pen")
    void SetTool(const FString& Tool);

    /** 清空当前页笔迹 */
    UFUNCTION(BlueprintCallable, Category = "PPTX|Pen")
    void ClearCanvas();

    /** 撤销上一笔 */
    UFUNCTION(BlueprintCallable, Category = "PPTX|Pen")
    void UndoStroke();

    /** 重做 */
    UFUNCTION(BlueprintCallable, Category = "PPTX|Pen")
    void RedoStroke();

    // ==================== 事件委托 ====================

    /** PPT 加载完成事件 */
    UPROPERTY(BlueprintAssignable, Category = "PPTX Events")
    FOnPptxReadyDelegate OnPptxReady;

    /** 翻页事件 */
    UPROPERTY(BlueprintAssignable, Category = "PPTX Events")
    FOnSlideChangedDelegate OnSlideChanged;

    // 上传事件
    UPROPERTY(BlueprintAssignable, Category = "PPTX Events|Upload")
    FOnUploadCompleteDelegate OnUploadComplete;

    UPROPERTY(BlueprintAssignable, Category = "PPTX Events|Upload")
    FOnUploadFailedDelegate OnUploadFailed;

    UPROPERTY(BlueprintAssignable, Category = "PPTX Events|Upload")
    FOnUploadProgressDelegate OnUploadProgress;

    // 视频事件
    UPROPERTY(BlueprintAssignable, Category = "PPTX Events|Video")
    FOnVideoPlayDelegate OnVideoPlay;

    UPROPERTY(BlueprintAssignable, Category = "PPTX Events|Video")
    FOnVideoCloseDelegate OnVideoClose;

    UPROPERTY(BlueprintAssignable, Category = "PPTX Events|Video")
    FOnVideoStateDelegate OnVideoState;

    UPROPERTY(BlueprintAssignable, Category = "PPTX Events|Video")
    FOnVideoErrorDelegate OnVideoError;

    // 画笔事件
    UPROPERTY(BlueprintAssignable, Category = "PPTX Events|Pen")
    FOnPenToggleDelegate OnPenToggle;

    UPROPERTY(BlueprintAssignable, Category = "PPTX Events|Pen")
    FOnDrawEndDelegate OnDrawEnd;

    UPROPERTY(BlueprintAssignable, Category = "PPTX Events|Pen")
    FOnPenColorChangedDelegate OnPenColorChanged;

    UPROPERTY(BlueprintAssignable, Category = "PPTX Events|Pen")
    FOnPenSizeChangedDelegate OnPenSizeChanged;

    UPROPERTY(BlueprintAssignable, Category = "PPTX Events|Pen")
    FOnCanvasClearedDelegate OnCanvasCleared;

    UPROPERTY(BlueprintAssignable, Category = "PPTX Events|Pen")
    FOnUndoRedoDelegate OnUndoRedo;

    // ==================== 状态属性（只读） ====================

    /** 当前页码（从1开始） */
    UPROPERTY(BlueprintReadOnly, Category = "PPTX")
    int32 CurrentSlideIndex = 0;

    /** 总页数 */
    UPROPERTY(BlueprintReadOnly, Category = "PPTX")
    int32 TotalSlides = 0;

    /** 画笔是否启用 */
    UPROPERTY(BlueprintReadOnly, Category = "PPTX|Pen")
    bool bPenEnabled = false;

    /** 当前画笔颜色 */
    UPROPERTY(BlueprintReadOnly, Category = "PPTX|Pen")
    FString CurrentPenColor = TEXT("#e74c3c");

    /** 当前画笔粗细 */
    UPROPERTY(BlueprintReadOnly, Category = "PPTX|Pen")
    int32 CurrentPenSize = 3;

    /** 服务器基础地址 */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PPTX|Config")
    FString ServerBaseUrl = TEXT("http://localhost:8080");

    /** 保存的查看器URL，非空时自动加载查看器而非上传页面 */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PPTX|Config")
    FString SavedViewerUrl;

protected:
    virtual void NativeConstruct() override;
    virtual FReply NativeOnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

    /** 处理来自网页的 Console 消息（JS→UE 通信通道） */
    UFUNCTION()
    void HandleConsoleMessage(const FString& Message, const FString& Source, int32 Line);

    /** WebBrowser 页面加载完成回调，触发 JS 桥接注入 */
    UFUNCTION()
    void HandleUrlChanged(const FText& Url);

    /** WebBrowser 控件引用（需要在 Blueprint 中绑定） */
    UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "PPTX")
    UWebBrowser* WebBrowser;

private:
    /** 执行 JavaScript */
    void ExecuteJS(const FString& JS);

    /** 注入 JS 通信桥接代码（重写 broadcastToUE 为 console.log） */
    void InjectJsBridge();

    /** HTTP 上传回调（非 UFUNCTION，避免 UHT 类型解析问题） */
    void HandleUploadComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

    /** FString 转 UTF-8 字节 */
    static void FStringToBytes(const FString& Str, TArray<uint8>& OutBytes);

    /** 上传完整 URL */
    FString UploadUrl = TEXT("http://localhost:8080/pptx/upload");
};
