// Copyright (c) Yevhenii Selivanov

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"

#include "GrsPawnComponent.generated.h"

/**
 * Component attached to main BmrPawn to track their state
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class GHOSTREVENGESYSTEMRUNTIME_API UGrsPawnComponent : public UActorComponent
{
	GENERATED_BODY()

	/*********************************************************************************************
	 * Initialization
	 **********************************************************************************************/
	
public:
	// Sets default values for this component's properties
	UGrsPawnComponent();
	
	/** Returns BmrPawn of this component */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "[GhostRevengeSystem]")
	ABmrPawn* GetBmrPawn() const;
	ABmrPawn& GetBmrPawnChecked() const;

	/*********************************************************************************************
	 * Main functionality (core loop)
	 **********************************************************************************************/
	
protected:
	/** Called when the game starts */
	virtual void BeginPlay() override;
	
	/** Clears all transient data created by this component */
	virtual void OnUnregister() override;
	
	/** A pawn could be loaded/replicated faster than MGF(GFP) is fully loaded therefore waiting for whole module to be initialized is required */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected))
	void OnInitialize(const struct FGameplayEventData& Payload);
	
public:
	

public:
};