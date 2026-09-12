#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StudentCommandReceiver.generated.h"

class IWebSocket;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnStudentActionReceived,
    const FString&, StudentId,
    const FString&, Action,
    const FString&, Text);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStudentConnectionChanged, bool, bConnected);

UCLASS(ClassGroup=(Classroom), BlueprintType, Blueprintable, meta=(BlueprintSpawnableComponent))
class STUDENTCOMMANDPLUGIN_API UStudentCommandReceiver : public UActorComponent
{
    GENERATED_BODY()

public:
    UStudentCommandReceiver();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Classroom|WebSocket")
    FString ServerUrl = TEXT("ws://127.0.0.1:18080/ue");

    UPROPERTY(BlueprintAssignable, Category="Classroom|WebSocket")
    FOnStudentActionReceived OnStudentActionReceived;

    UPROPERTY(BlueprintAssignable, Category="Classroom|WebSocket")
    FOnStudentConnectionChanged OnConnectionChanged;

    UFUNCTION(BlueprintCallable, Category="Classroom|WebSocket")
    void Connect();

    UFUNCTION(BlueprintCallable, Category="Classroom|WebSocket")
    void Disconnect();

    UFUNCTION(BlueprintPure, Category="Classroom|WebSocket")
    bool IsConnected() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    TSharedPtr<IWebSocket> Socket;
    bool bPollInFlight = false;
    float PollElapsedSeconds = 0.0f;

    void HandleMessage(const FString& Message);
    void PollForAction();
};
