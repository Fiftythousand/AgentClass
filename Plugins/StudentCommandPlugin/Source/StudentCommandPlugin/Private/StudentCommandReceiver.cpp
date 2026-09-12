#include "StudentCommandReceiver.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "IWebSocket.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "WebSocketsModule.h"

UStudentCommandReceiver::UStudentCommandReceiver()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UStudentCommandReceiver::BeginPlay()
{
    Super::BeginPlay();
    PrimaryComponentTick.SetTickFunctionEnable(false);
    SetComponentTickEnabled(false);
}

void UStudentCommandReceiver::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Disconnect();
    Super::EndPlay(EndPlayReason);
}

void UStudentCommandReceiver::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UStudentCommandReceiver::Connect()
{
    if (Socket.IsValid())
    {
        return;
    }

    FWebSocketsModule& WebSocketsModule = FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
    Socket = WebSocketsModule.CreateWebSocket(ServerUrl);

    Socket->OnConnected().AddLambda([this]()
    {
        UE_LOG(LogTemp, Log, TEXT("StudentCommandReceiver connected to %s"), *ServerUrl);
        OnConnectionChanged.Broadcast(true);
    });

    Socket->OnConnectionError().AddLambda([this](const FString& Error)
    {
        UE_LOG(LogTemp, Error, TEXT("StudentCommandReceiver connection error: %s"), *Error);
        OnConnectionChanged.Broadcast(false);
    });

    Socket->OnClosed().AddLambda([this](int32, const FString& Reason, bool)
    {
        UE_LOG(LogTemp, Warning, TEXT("StudentCommandReceiver closed: %s"), *Reason);
        OnConnectionChanged.Broadcast(false);
    });

    Socket->OnMessage().AddLambda([this](const FString& Message)
    {
        HandleMessage(Message);
    });

    Socket->Connect();
}

void UStudentCommandReceiver::Disconnect()
{
    if (Socket.IsValid())
    {
        Socket->Close();
        Socket.Reset();
    }
}

bool UStudentCommandReceiver::IsConnected() const
{
    return Socket.IsValid() && Socket->IsConnected();
}

void UStudentCommandReceiver::HandleMessage(const FString& Message)
{
    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("StudentCommandReceiver ignored invalid JSON: %s"), *Message);
        return;
    }

    FString Type;
    FString StudentId;
    FString Action;
    FString Text;
    if (!Root->TryGetStringField(TEXT("type"), Type) || Type != TEXT("student_action") ||
        !Root->TryGetStringField(TEXT("studentId"), StudentId) ||
        !Root->TryGetStringField(TEXT("action"), Action))
    {
        return;
    }
    Root->TryGetStringField(TEXT("text"), Text);

    AsyncTask(ENamedThreads::GameThread, [this, StudentId, Action, Text]()
    {
        if (!IsValid(this))
        {
            return;
        }

        UE_LOG(LogTemp, Log, TEXT("Student action received: %s -> %s | %s"), *StudentId, *Action, *Text);
        OnStudentActionReceived.Broadcast(StudentId, Action, Text);
    });
}

void UStudentCommandReceiver::PollForAction()
{
    if (bPollInFlight)
    {
        return;
    }

    bPollInFlight = true;
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(TEXT("http://127.0.0.1:18081/next"));
    Request->SetVerb(TEXT("GET"));
    Request->SetHeader(TEXT("X-Unreal-Local-Request"), TEXT("1"));
    Request->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
    {
        bPollInFlight = false;
        if (bSucceeded && Response.IsValid() && Response->GetResponseCode() == 200)
        {
            const FString ResponseText = Response->GetContentAsString();
            UE_LOG(LogTemp, Log, TEXT("StudentCommandReceiver HTTP action: %s"), *ResponseText);
            HandleMessage(ResponseText);
        }
        else if (!bSucceeded || !Response.IsValid())
        {
            UE_LOG(LogTemp, Warning, TEXT("StudentCommandReceiver HTTP poll failed"));
        }
    });
    Request->ProcessRequest();
}
