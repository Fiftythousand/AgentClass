// Fill out your copyright notice in the Description page of Project Settings.

#include "WebsocketUtil.h"
#include "WebSocketsModule.h"
#include "IWebSocket.h"

void UWebsocketUtil::Connect(FString URL, FString Token)
{
	UE_LOG(LogTemp, Log, TEXT("Connecting to WebSocket at URL: %s"), *URL);

	// 确保 WebSockets 模块已加载
	if (!FModuleManager::Get().IsModuleLoaded("WebSockets"))
	{
		FModuleManager::Get().LoadModule("WebSockets");
	}

	// 如果已有连接，先关闭
	if (WebSocket.IsValid() && WebSocket->IsConnected())
	{
		UE_LOG(LogTemp, Warning, TEXT("已有连接，先关闭旧连接..."));
		WebSocket->Close();
	}

	// 添加 Origin 头和 Token 认证头
	TMap<FString, FString> UpgradeHeaders;
	UpgradeHeaders.Add(TEXT("Origin"), TEXT("http://localhost"));
	if (!Token.IsEmpty())
	{
		UpgradeHeaders.Add(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Token));
		UE_LOG(LogTemp, Log, TEXT("WebSocket 连接携带 Token 认证头"));
	}
	else {
		UE_LOG(LogTemp, Log, TEXT("WebSocket 连接未携带 Token 认证头"));
	}
	for (const auto& Header : UpgradeHeaders)
	{
		UE_LOG(LogTemp, Log, TEXT("WebSocket 头部: %s = %s"), *Header.Key, *Header.Value);
	}
	WebSocket = FWebSocketsModule::Get().CreateWebSocket(URL, FString(), UpgradeHeaders);

	// 绑定事件回调
	WebSocket->OnConnected().AddUObject(this, &UWebsocketUtil::OnConnected);
	WebSocket->OnConnectionError().AddUObject(this, &UWebsocketUtil::OnConnectionError);
	WebSocket->OnClosed().AddUObject(this, &UWebsocketUtil::OnClosed);
	WebSocket->OnMessage().AddUObject(this, &UWebsocketUtil::OnMessage);

	// 发起连接
	WebSocket->Connect();
}

void UWebsocketUtil::Close()
{
	if (WebSocket.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("关闭 WebSocket 连接..."));
		WebSocket->Close();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("WebSocket 未初始化，无法关闭"));
	}
}

void UWebsocketUtil::Send(FString Message)
{
	if (WebSocket.IsValid() && WebSocket->IsConnected())
	{
		UE_LOG(LogTemp, Log, TEXT("发送消息: %s"), *Message);
		WebSocket->Send(Message);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("WebSocket 未连接，无法发送消息"));
	}
}

void UWebsocketUtil::SendBytes(const TArray<uint8>& Bytes)
{
	if (WebSocket.IsValid() && WebSocket->IsConnected())
	{
		UE_LOG(LogTemp, Log, TEXT("发送字节数据: %d bytes"), Bytes.Num());
		WebSocket->Send(Bytes.GetData(), Bytes.Num(), true);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("WebSocket 未连接，无法发送字节数据"));
	}
}

bool UWebsocketUtil::IsConnected() const
{
	return WebSocket.IsValid() && WebSocket->IsConnected();
}

// ========== 内部回调实现 ==========

void UWebsocketUtil::OnConnected()
{
	UE_LOG(LogTemp, Log, TEXT("WebSocket 连接成功!"));
	OnConnectedEvent();
}

void UWebsocketUtil::OnConnectionError(const FString& Error)
{
	UE_LOG(LogTemp, Error, TEXT("WebSocket 连接失败: %s"), *Error);
	OnConnectionErrorEvent(Error);
}

void UWebsocketUtil::OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
	UE_LOG(LogTemp, Warning, TEXT("WebSocket 已关闭 - 状态码: %d, 原因: %s, 正常关闭: %s"),
		StatusCode, *Reason, bWasClean ? TEXT("是") : TEXT("否"));
	OnClosedEvent(StatusCode, Reason, bWasClean);
}

void UWebsocketUtil::OnMessage(const FString& Message)
{
	UE_LOG(LogTemp, Log, TEXT("[WebSocket] 收到消息: %s"), *Message);
	UGameInstance* GI = nullptr;
	if (UWorld* World = GetWorld())
	{
		GI = World->GetGameInstance();
	}
	OnMessageEvent(Message, GI);
}
