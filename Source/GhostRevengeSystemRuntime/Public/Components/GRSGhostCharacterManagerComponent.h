// Copyright (c) Yevhenii Selivanov

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Data/PoolObjectHandle.h"
#include "Net/UnrealNetwork.h"

#include "GRSGhostCharacterManagerComponent.generated.h"

enum class EBmrCurrentGameState : uint8;

/**
 * Actor component attached to game state to spawn ghost characters
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class GHOSTREVENGESYSTEMRUNTIME_API UGRSGhostCharacterManagerComponent : public UActorComponent
{
	GENERATED_BODY()

	/*********************************************************************************************
	 * Lifecycle
	 **********************************************************************************************/
public:
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPlayerCharacterPreRemovedFromLevel, UBmrMapComponent*, MapComponent, UObject*, DestroyCauser);

	/** Called right before player character is going to be removed from the Map, on both server and clients */
	UPROPERTY(BlueprintCallable, BlueprintAssignable, Transient, Category = "[GhostRevengeSystem]")
	FOnPlayerCharacterPreRemovedFromLevel OnPlayerCharacterPreRemovedFromLevel;

	// Sets default values for this component's properties
	UGRSGhostCharacterManagerComponent();

protected:
	/** Called when the game starts */
	virtual void BeginPlay() override;

	/** Clears all transient data created by this component. */
	virtual void OnUnregister() override;

	/*********************************************************************************************
	 * Main functionality
	 **********************************************************************************************/

protected:
	/** Array of pool actors handlers of characters which should be released */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Transient, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected, DisplayName = "Pool Actors Handlers"))
	TArray<FPoolObjectHandle> PoolActorHandlersInternal;

	/** Contains list of all map components events bounded to. */
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected, DisplayName = "Bound MapComponents"))
	TArray<TWeakObjectPtr<class UBmrMapComponent>> BoundMapComponents;

	/** Called when the GRS data asset is loaded and available */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected))
	void OnDataAssetLoaded(const class UGRSDataAsset* DataAsset);

	/** The component is considered as loaded only when the subsystem is loaded */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected))
	void OnInitialize(const struct FGameplayEventData& Payload);
	
	/** Add ghost character to the current active game (on level map) */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]")
	void AddGhostCharacter();
	
	/** Grabs a Ghost Revenge Player Character from the pool manager (Object pooling patter)
	 * @param CreatedObjects - Handles of objects from Pool Manager
	 */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]")
	void OnTakeActorsFromPoolCompleted(const TArray<FPoolObjectData>& CreatedObjects);
	
	/** Subscribes to PlayerCharacters death events in order to see if a player died */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected))
	void RegisterForPlayerDeath();
	
	/** Grant to a player revive GAS effect */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected))
	void GrantPlayerReviveEffect(class ABmrPawn* PawnToGrant);
	
	/** Listen game states to remove ghost character from level */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected))
	void OnGameStateChanged(const struct FGameplayEventData& Payload);
	
	/** Called right before owner actor going to remove from the Generated Map, on both server and clients.*/
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected))
	void PlayerCharacterOnPreRemovedFromLevel(class UBmrMapComponent* MapComponent, class UObject* DestroyCauser);

	/** Called when the ghost player kills another player and will be swaped with him */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]")
	void OnGhostEliminatesPlayer(FVector AtLocation, class AGRSPlayerCharacter* GhostCharacter);

	/** Called when the ghost character should be removed from level to unpossess controller */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]")
	void OnGhostRemovedFromLevel(class AController* CurrentController, class AGRSPlayerCharacter* GhostCharacter);

	/** Unpossess ghost character and posses it to possess assigned(linked) regular player character */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]")
	void PossessPlayerCharacter(AController* CurrentController, class ABmrPawn* PlayerCharacter);

	/** Unpossess ghost and spawn&possess a regular player character to the level at location */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]")
	void RevivePlayerCharacter(class ABmrPawn* PlayerCharacter);

	/** To unsubscribed from player death events (delegates) and clean ability component */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]")
	void UnregisterFromPlayerDeath();
	
	/** To Remove current active applied gameplay effect */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]")
	void RemoveAppliedReviveGameplayEffect(const ABmrPawn* PlayerCharacter);
};
