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
	/** Array of pool actors handlers of characters which should be released */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Transient, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected, DisplayName = "GrsPawn Pool Manager Handlers"))
	TArray<FPoolObjectHandle> GrsPawnPoolManagerHandlers;
	
	
protected:
	/** Called when the game starts */
	virtual void BeginPlay() override;
	
	/** Clears all transient data created by this component */
	virtual void OnUnregister() override;
	
	/** A pawn could be loaded/replicated faster than MGF(GFP) is fully loaded therefore waiting for whole module to be initialized is required */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected))
	void OnInitialize(const struct FGameplayEventData& Payload);
	
	/** Listen game states to grant revive ability for player character  */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected))
	void OnGameStateChanged(const struct FGameplayEventData& Payload);
	
	/** Grant to a player revive GAS effect */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected))
	void GrantPlayerReviveEffect(class ABmrPawn* PawnToGrant);
	
	/** To Remove current active applied gameplay effect */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]")
	void RemoveAppliedReviveGameplayEffect(const ABmrPawn* PlayerCharacter);
	
	/** Add ghost character to the current active game (on level map) */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]")
	void AddGhostCharacter();
	
	/** Grabs a Ghost Revenge Player Character from the pool manager (Object pooling patter)
	 * @param CreatedObjects - Handles of objects from Pool Manager
	 */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]")
	void OnTakeActorsFromPoolCompleted(const TArray<FPoolObjectData>& CreatedObjects);
	
	/** Called when the ghost character should be removed from level to unpossess controller */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]")
	void OnGhostRemovedFromLevel(class AController* CurrentController, class AGRSPlayerCharacter* GhostCharacter);
	
public:
	

public:
};