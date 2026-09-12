// FileUploader.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "HttpFwd.h"
#include "FileUploader.generated.h"

class UVaRestRequestJSON;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUploadComplete, int32, ResponseCode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUploadFailed, const FString&, ErrorMessage);

UCLASS(BlueprintType, Blueprintable)
class UFileUploader : public UObject
{
GENERATED_BODY()
public:
UFileUploader();

UPROPERTY(BlueprintAssignable, Category = "FileUpload|Event")
FOnUploadComplete OnUploadComplete;

UPROPERTY(BlueprintAssignable, Category = "FileUpload|Event")
FOnUploadFailed OnUploadFailed;

UFUNCTION(BlueprintCallable, Category = "FileUpload")
static UFileUploader* CreateUploadRequest();

UFUNCTION(BlueprintCallable, Category = "FileUpload", meta = (AutoCreateRefTerm = "FormFields"))
void UploadFile(const FString& URL, const FString& FilePath, const FString& FieldName, const TMap<FString, FString>& FormFields, const FString& MimeType, const FString& AuthToken);

UFUNCTION(BlueprintCallable, Category = "FileUpload", meta = (AutoCreateRefTerm = "FormFields"))
void UploadFileFromMemory(const FString& URL, const TArray<uint8>& FileData, const FString& FileName, const FString& FieldName, const TMap<FString, FString>& FormFields, const FString& MimeType, const FString& AuthToken);

UFUNCTION(BlueprintPure, Category = "FileUpload")
int32 GetResponseCode() const { return LastResponseCode; }

UFUNCTION(BlueprintPure, Category = "FileUpload")
FString GetResponseContent() const { return LastResponseContent; }

private:
void LaunchRequest(const FString& URL, const TArray<uint8>& FileData, const FString& FileName, const FString& FieldName, const TMap<FString, FString>& FormFields, const FString& MimeType, const FString& AuthToken);

UFUNCTION()
void OnVaRestComplete(UVaRestRequestJSON* Request);

UFUNCTION()
void OnVaRestFail(UVaRestRequestJSON* Request);

static FString GuessMimeType(const FString& FileName);

static TArray<uint8> BuildMultipartBody(const TArray<uint8>& FileData, const FString& FileName, const FString& FieldName, const FString& MimeType, const TMap<FString, FString>& FormFields, const FString& Boundary);

UPROPERTY()
UVaRestRequestJSON* VaRestRequest;

int32 LastResponseCode;
FString LastResponseContent;
};
