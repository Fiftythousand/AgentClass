// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "JasonUtil.generated.h"

/**
 * 
 */
UCLASS(BlueprintType)
class CLASSBOT_API UJasonUtil : public UObject
{
	GENERATED_BODY()
	UFUNCTION(BlueprintCallable, Category = "JasonUtil")
	void LoadJasonFromString(FString JasonString);
};
