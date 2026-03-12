// Copyright (c) Yevhenii Selivanov

#include "Components/GRSGhostCharacterManagerComponent.h"

// GRS
#include "Data/GRSDataAsset.h"
#include "GrsGameplayTags.h"
#include "LevelActors/GRSPlayerCharacter.h"
#include "PoolManagerSubsystem.h"
#include "SubSystems/GRSWorldSubSystem.h"

// Bmr
#include "Actors/BmrGeneratedMap.h"
#include "Components/BmrSkeletalMeshComponent.h"
#include "Controllers/BmrPlayerController.h"
#include "DalSubsystem.h"
#include "GameFramework/BmrGameState.h"
#include "Structures/BmrGameplayTags.h"
#include "Subsystems/BmrGameplayMessageSubsystem.h"
#include "UtilityLibraries/BmrBlueprintFunctionLibrary.h"
#include "UtilityLibraries/BmrCellUtilsLibrary.h"

// UE
#include "Abilities/GameplayAbilityTypes.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Components/GrsPawnComponent.h"
#include "Engine/World.h"
#include "Structures/BmrGameStateTag.h"

/*********************************************************************************************
 * Lifecycle
 **********************************************************************************************/

// Sets default values for this component's properties
UGRSGhostCharacterManagerComponent::UGRSGhostCharacterManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	SetIsReplicatedByDefault(true);
}

// Called when the game starts
void UGRSGhostCharacterManagerComponent::BeginPlay()
{
	Super::BeginPlay();

	UDalSubsystem::Get().ListenForDataAsset<UGRSDataAsset>(this, &ThisClass::OnDataAssetLoaded);
}

// Called when the GRS data asset is loaded and available
void UGRSGhostCharacterManagerComponent::OnDataAssetLoaded_Implementation(const UGRSDataAsset* DataAsset)
{
	UGRSWorldSubSystem& WorldSubsystem = UGRSWorldSubSystem::Get();
	WorldSubsystem.RegisterCharacterManagerComponent(this);

	BIND_ON_INITIALIZE(this, ThisClass::OnInitialize);
}

// Clears all transient data created by this component.
void UGRSGhostCharacterManagerComponent::OnUnregister()
{
	Super::OnUnregister();

	if (PoolActorHandlersInternal.Num() > 0)
	{
		UPoolManagerSubsystem::Get().ReturnToPoolArray(PoolActorHandlersInternal);
		PoolActorHandlersInternal.Empty();
		UPoolManagerSubsystem::Get().EmptyPool(AGRSPlayerCharacter::StaticClass());
	}

	// --- clean delegates
	UnregisterFromPlayerDeath();

	OnPlayerCharacterPreRemovedFromLevel.Clear();

	// --- perform clean up from subsystem MGF is not possible so we have to call directly to clean cached references
	UGRSWorldSubSystem::Get().UnregisterCharacterManagerComponent();
}

/*********************************************************************************************
 * Main functionality
 **********************************************************************************************/

// The component is considered as loaded only when the subsystem is loaded
void UGRSGhostCharacterManagerComponent::OnInitialize(const struct FGameplayEventData& Payload)
{
	UE_LOG(LogTemp, Log, TEXT("UGRSGhostCharacterManagerComponent OnInitialize  --- %s"), *this->GetName());

	if (GetOwner()->HasAuthority())
	{
		// spawn 2 characters right away
		AddGhostCharacter();
	}

	RegisterForPlayerDeath();

	// --- bind to  clear ghost data
	BIND_ON_GAME_STATE_CHANGED(this, ThisClass::OnGameStateChanged);
}

// Add ghost character to the current active game (on level map)
void UGRSGhostCharacterManagerComponent::AddGhostCharacter()
{
	// --- Return to Pool Manager the list of handles which is not needed (if there are any)
	if (!PoolActorHandlersInternal.IsEmpty())
	{
		UPoolManagerSubsystem::Get().ReturnToPoolArray(PoolActorHandlersInternal);
		PoolActorHandlersInternal.Empty();
	}

	// --- Prepare spawn request
	const TWeakObjectPtr<ThisClass> WeakThis = this;
	const FOnSpawnAllCallback OnTakeActorsFromPoolCompleted = [WeakThis](const TArray<FPoolObjectData>& CreatedObjects)
	{
		if (UGRSGhostCharacterManagerComponent* This = WeakThis.Get())
		{
			This->OnTakeActorsFromPoolCompleted(CreatedObjects);
		}
	};

	// --- Spawn actor
	UPoolManagerSubsystem::Get().TakeFromPoolArray(PoolActorHandlersInternal, AGRSPlayerCharacter::StaticClass(), 2, OnTakeActorsFromPoolCompleted, ESpawnRequestPriority::High);
}

// Grabs a Ghost Revenge Player Character from the pool manager (Object pooling patter)
void UGRSGhostCharacterManagerComponent::OnTakeActorsFromPoolCompleted(const TArray<FPoolObjectData>& CreatedObjects)
{
	// --- something wrong if there are less than 1 object found
	if (CreatedObjects.Num() < 1)
	{
		return;
	}

	// --- Setup spawned characters
	for (const FPoolObjectData& CreatedObject : CreatedObjects)
	{
		AGRSPlayerCharacter& GhostCharacter = CreatedObject.GetChecked<AGRSPlayerCharacter>();
		UE_LOG(LogTemp, Log, TEXT("Spawned ghost character --- %s - %s"), *GhostCharacter.GetName(), GhostCharacter.HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT"));

		// we can path a current local player since it needed only for the skin init
		GhostCharacter.OnGhostEliminatesPlayer.AddUniqueDynamic(this, &ThisClass::OnGhostEliminatesPlayer);
		GhostCharacter.OnGhostRemovedFromLevel.AddUniqueDynamic(this, &ThisClass::OnGhostRemovedFromLevel);
	}
}

// Subscribes to PlayerCharacters death events in order to see if a player died
void UGRSGhostCharacterManagerComponent::RegisterForPlayerDeath()
{
	// --- subscribe to PlayerCharacters death event in order to see if a ghost player killed somebody
	TArray<UGrsPawnComponent*> GrsPawnComponents = UGRSWorldSubSystem::Get().GetPawnComponents();
	for (UGrsPawnComponent* GrsPawnComponent : GrsPawnComponents)
	{
		ABmrPawn* MyPawn = &GrsPawnComponent->GetBmrPawnChecked();
		if (MyPawn)
		{
			UBmrMapComponent* MapComponent = UBmrMapComponent::GetMapComponent(MyPawn);
			if (!ensureMsgf(MapComponent, TEXT("ASSERT: [%i] %hs:\n 'MapComponent' is null!"), __LINE__, __FUNCTION__))
			{
				continue;
			}

			MapComponent->OnPreRemovedFromLevel.AddUniqueDynamic(this, &ThisClass::PlayerCharacterOnPreRemovedFromLevel);
			BoundMapComponents.AddUnique(MapComponent);
			GrantPlayerReviveEffect(MyPawn);
		}
	}
}

//  Grant to a player revive GAS effect
void UGRSGhostCharacterManagerComponent::GrantPlayerReviveEffect(ABmrPawn* PawnToGrant)
{
	if (!PawnToGrant
	    || !GetOwner()->HasAuthority()
	    || PawnToGrant->IsPlayerControlled())
	{
		return;
	}

	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PawnToGrant);
	if (!ensureMsgf(ASC, TEXT("ASSERT: [%i] %hs:\n 'ASC' is not set!"), __LINE__, __FUNCTION__))
	{
		return;
	}

	TSubclassOf<UGameplayEffect> PlayerReviveEffect = UGRSDataAsset::Get().GetPlayerReviveEffect();
	if (ensureMsgf(PlayerReviveEffect, TEXT("ASSERT: [%i] %hs:\n'PlayerDeathEffect' is not set!"), __LINE__, __FUNCTION__))
	{
		ASC->ApplyGameplayEffectToSelf(PlayerReviveEffect.GetDefaultObject(), /*Level*/ 1.f, ASC->MakeEffectContext());
	}
}

// Listen game states to remove ghost character from level
void UGRSGhostCharacterManagerComponent::OnGameStateChanged_Implementation(const FGameplayEventData& Payload)
{
	if (!Payload.InstigatorTags.HasTag(FBmrGameStateTag::InGame))
	{
		// --- clean delegates
		UnregisterFromPlayerDeath();
	}

	if (Payload.InstigatorTags.HasTag(FBmrGameStateTag::GameStarting))
	{
		RegisterForPlayerDeath();
	}
}

// Called right before owner actor going to remove from the Generated Map, on both server and clients
void UGRSGhostCharacterManagerComponent::PlayerCharacterOnPreRemovedFromLevel_Implementation(UBmrMapComponent* MapComponent, class UObject* DestroyCauser)
{
	ABmrPawn* PlayerCharacter = MapComponent->GetOwner<ABmrPawn>();
	if (!ensureMsgf(PlayerCharacter, TEXT("ASSERT: [%i] %hs:\n'PlayerCharacter' is not valid!"), __LINE__, __FUNCTION__)
	    || PlayerCharacter->IsBotControlled()
	    || !DestroyCauser)
	{
		return;
	}

	// --- notify ghost characters
	if (OnPlayerCharacterPreRemovedFromLevel.IsBound())
	{
		OnPlayerCharacterPreRemovedFromLevel.Broadcast(MapComponent, DestroyCauser);
	}
}

// Called when the ghost player kills another player and will be swaped with him
void UGRSGhostCharacterManagerComponent::OnGhostEliminatesPlayer(FVector AtLocation, AGRSPlayerCharacter* GhostCharacter)
{
	// RevivePlayerCharacter(GhostCharacter);
}

// Called when the ghost character should be removed from level to unpossess controller
void UGRSGhostCharacterManagerComponent::OnGhostRemovedFromLevel(AController* CurrentController, AGRSPlayerCharacter* GhostCharacter)
{
	if (!ensureMsgf(CurrentController, TEXT("ASSERT: [%i] %hs:\n'PlayerController' is not valid!"), __LINE__, __FUNCTION__)
	    || !CurrentController->HasAuthority()
	    || !GhostCharacter)
	{
		return;
	}

	ABmrPawn* PlayerCharacter = GhostCharacter->GetPossessedPlayerCharacter(GhostCharacter);
	if (!PlayerCharacter)
	{
		return;
	}
	
	// --- move all functional part such as posses to ability
	// --- possess back to player character for any cases
	//PlayerCharacter->GetMeshComponentChecked().SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
	PossessPlayerCharacter(CurrentController, PlayerCharacter);
	RevivePlayerCharacter(PlayerCharacter);
}

// Unpossess ghost character and posses it to possess assigned(linked) regular player character
void UGRSGhostCharacterManagerComponent::PossessPlayerCharacter(AController* CurrentController, ABmrPawn* PlayerCharacter)
{
	if (!PlayerCharacter || !PlayerCharacter->HasAuthority() || !CurrentController)
	{
		return;
	}

	if (CurrentController->GetPawn())
	{
		// At first, unpossess previous controller
		CurrentController->UnPossess();
	}

	// --- Always possess to player character when ghost character is no longer in control
	CurrentController->Possess(PlayerCharacter);
	UE_LOG(LogTemp, Log, TEXT("[%i] %hs: --- PlayerController is %s"), __LINE__, __FUNCTION__, CurrentController ? TEXT("TRUE") : TEXT("FALSE"));
	UE_LOG(LogTemp, Log, TEXT("[%i] %hs: --- PlayerCharacter is %s"), __LINE__, __FUNCTION__, PlayerCharacter ? TEXT("TRUE") : TEXT("FALSE"));
	UE_LOG(LogTemp, Log, TEXT("[%i] %hs: --- PlayerCharacter: %s"), __LINE__, __FUNCTION__, *GetNameSafe(PlayerCharacter));
}

// Spawn and possess a regular player character to the level at location
void UGRSGhostCharacterManagerComponent::RevivePlayerCharacter(ABmrPawn* PlayerCharacter)
{
	const ABmrGameState& GameState = ABmrGameState::Get();
	if (!PlayerCharacter || !GameState.HasMatchingGameplayTag(FBmrGameStateTag::InGame))
	{
		return;
	}

	// --- Activate revive ability if player was NOT revived previously
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PlayerCharacter);
	FGameplayEventData EventData;
	EventData.EventMagnitude = UBmrCellUtilsLibrary::GetIndexByCellOnLevel(PlayerCharacter->GetActorLocation());
	ASC->HandleGameplayEvent(UGRSDataAsset::Get().GetReviePlayerCharacterTriggerTag(), &EventData);
}

// To unsubscribed from player death events (delegates) and clean ability component
void UGRSGhostCharacterManagerComponent::UnregisterFromPlayerDeath()
{
	if (BoundMapComponents.Num() > 0)
	{
		for (int32 Index = 0; Index < BoundMapComponents.Num(); Index++)
		{
			if (BoundMapComponents[Index].IsValid())
			{
				BoundMapComponents[Index]->OnPreRemovedFromLevel.RemoveDynamic(this, &ThisClass::PlayerCharacterOnPreRemovedFromLevel);
				const ABmrPawn* PlayerCharacter = BoundMapComponents[Index]->GetOwner<ABmrPawn>();
				RemoveAppliedReviveGameplayEffect(PlayerCharacter);
			}
		}

		BoundMapComponents.Empty();
	}
}

// To Remove current active applied gameplay effect
void UGRSGhostCharacterManagerComponent::RemoveAppliedReviveGameplayEffect(const ABmrPawn* PlayerCharacter)
{
	if (!PlayerCharacter)
	{
		return;
	}

	// Actor has ASC: apply effect through GAS
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PlayerCharacter);
	if (!ensureMsgf(ASC, TEXT("ASSERT: [%i] %hs:\n 'ASC' is not set!"), __LINE__, __FUNCTION__))
	{
		return;
	}
	TSubclassOf<UGameplayEffect> PlayerReviveEffect = UGRSDataAsset::Get().GetPlayerReviveEffect();
	if (ensureMsgf(PlayerReviveEffect, TEXT("ASSERT: [%i] %hs:\n'PlayerDeathEffect' is not set!"), __LINE__, __FUNCTION__))
	{
		FGameplayEffectQuery Query;
		Query.EffectDefinition = PlayerReviveEffect;
		TArray<FActiveGameplayEffectHandle> Handles = ASC->GetActiveEffects(Query);
		for (FActiveGameplayEffectHandle Handle : Handles)
		{
			ASC->RemoveActiveGameplayEffect(Handle);
			Handle.Invalidate();
		}
	}
}
