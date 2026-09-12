// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "IWebSocket.h"
#include "Engine/GameInstance.h"
#include "WebsocketUtil.generated.h"

/**
 * WebSocket 工具类
 * 用于在蓝图中创建和管理 WebSocket 连接
 */
UCLASS(Blueprintable, BlueprintType)
class CLASSBOT_API UWebsocketUtil : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * 发起 WebSocket 连接
	 * @param URL - WebSocket 服务器地址，例如 ws://localhost:8080/ws-test
	 */
	UFUNCTION(BlueprintCallable, Category = "WebsocketUtil")
	void Connect(FString URL, FString Token = TEXT(""));

	/** 关闭 WebSocket 连接 */
	UFUNCTION(BlueprintCallable, Category = "WebsocketUtil")
	void Close();

	/**
	 * 发送文本消息
	 * @param Message - 要发送的消息内容
	 */
	UFUNCTION(BlueprintCallable, Category = "WebsocketUtil")
	void Send(FString Message);
	/**
	 * 发送字节数据（二进制消息）
	 * @param Bytes - 要发送的字节数组
	 */
	UFUNCTION(BlueprintCallable, Category = "WebsocketUtil")
	void SendBytes(const TArray<uint8>& Bytes);

	/**
	 * 获取当前连接状态
	 * @return true 表示已连接，false 表示未连接
	 */
	UFUNCTION(BlueprintCallable, Category = "WebsocketUtil")
	bool IsConnected() const;

	// ========== 蓝图可重写的事件 ==========

	/** 连接成功时触发（蓝图可重写） */
	UFUNCTION(BlueprintImplementableEvent, Category = "WebsocketUtil")
	void OnConnectedEvent();

	/** 连接失败时触发（蓝图可重写） */
	UFUNCTION(BlueprintImplementableEvent, Category = "WebsocketUtil")
	void OnConnectionErrorEvent(const FString& Error);

	/** 连接关闭时触发（蓝图可重写） */
	UFUNCTION(BlueprintImplementableEvent, Category = "WebsocketUtil")
	void OnClosedEvent(int32 StatusCode, const FString& Reason, bool bWasClean);

	/** 收到消息时触发（蓝图可重写） */
	UFUNCTION(BlueprintImplementableEvent, Category = "WebsocketUtil")
	void OnMessageEvent(const FString& Message, UGameInstance* GameInstance);

protected:

	// ========== C++ 内部回调（绑定给 IWebSocket） ==========

	void OnConnected();
	void OnConnectionError(const FString& Error);
	void OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
	void OnMessage(const FString& Message);

private:

	/** WebSocket 实例 */
	TSharedPtr<IWebSocket> WebSocket;
};
