// Copyright (c) Yevhenii Selivanov

#include "Components/GrsPawnComponent.h"

#include "Abilities/GameplayAbilityTypes.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Actors/BmrPawn.h"
#include "Data/GRSDataAsset.h"
#include "GameFramework/BmrGameState.h"
#include "GrsGameplayTags.h"
#include "LevelActors/GRSPlayerCharacter.h"
#include "PoolManagerSubsystem.h"
#include "Structures/BmrGameStateTag.h"
#include "Structures/BmrGameplayTags.h"
#include "SubSystems/GRSWorldSubSystem.h"
#include "Subsystems/BmrGameplayMessageSubsystem.h"
#include "UtilityLibraries/BmrBlueprintFunctionLibrary.h"
#include "UtilityLibraries/BmrCellUtilsLibrary.h"

class UGRSWorldSubSystem;
// Sets default values for this component's properties
UGrsPawnComponent::UGrsPawnComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

//  Returns BmrPawn of this component
ABmrPawn* UGrsPawnComponent::GetBmrPawn() const
{
	return Cast<ABmrPawn>(GetOwner());
}

ABmrPawn& UGrsPawnComponent::GetBmrPawnChecked() const
{
	ABmrPawn* MyBmrPawn = GetBmrPawn();
	checkf(MyBmrPawn, TEXT("%s: 'MyBmrPawn' is null"), *FString(__FUNCTION__));
	return *MyBmrPawn;
}

// Called when the game starts
void UGrsPawnComponent::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Log, TEXT("UGrsPawnComponent::BeginPlay  --- %s - %s"), *this->GetName(), GetOwner()->HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT"));
	UGRSWorldSubSystem& WorldSubsystem = UGRSWorldSubSystem::Get();
	WorldSubsystem.RegisterPawnComponent(this);
	BIND_ON_INITIALIZE(this, ThisClass::OnInitialize);
}

// Clears all transient data created by this component
void UGrsPawnComponent::OnUnregister()
{
	Super::OnUnregister();

	if (GrsPawnPoolManagerHandlers.Num() > 0)
	{
		UPoolManagerSubsystem::Get().ReturnToPoolArray(GrsPawnPoolManagerHandlers);
		GrsPawnPoolManagerHandlers.Empty();
		UPoolManagerSubsystem::Get().EmptyPool(AGRSPlayerCharacter::StaticClass());
	}
}

// A pawn could be loaded/replicated faster than MGF(GFP) is fully loaded therefore waiting for whole module to be initialized is required
void UGrsPawnComponent::OnInitialize(const struct FGameplayEventData& Payload)
{
	BIND_ON_GAME_STATE_CHANGED(this, ThisClass::OnGameStateChanged);

	if (GetOwner()->HasAuthority())
	{
		AddGhostCharacter();
	}
}

// Listen game states to grant revive ability for player character
void UGrsPawnComponent::OnGameStateChanged_Implementation(const struct FGameplayEventData& Payload)
{
	if (!Payload.InstigatorTags.HasTag(FBmrGameStateTag::InGame))
	{
		// -- release (unpossess) all ghosts
		RemoveAppliedReviveGameplayEffect(GetBmrPawn());
	}

	if (Payload.InstigatorTags.HasTag(FBmrGameStateTag::GameStarting))
	{
		GrantPlayerReviveEffect(GetBmrPawn());
	}
}

// Grant to a player revive GAS effect
void UGrsPawnComponent::GrantPlayerReviveEffect(ABmrPawn* PawnToGrant)
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

// To Remove current active applied gameplay effect
void UGrsPawnComponent::RemoveAppliedReviveGameplayEffect(const ABmrPawn* PlayerCharacter)
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
	if (ensureMsgf(PlayerReviveEffect, TEXT("ASSERT: [%i] %hs:\n'PlayerDeathEffect' is not returned from data asset!"), __LINE__, __FUNCTION__))
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

void UGrsPawnComponent::AddGhostCharacter()
{
	// --- Return to Pool Manager the list of handles which is not needed (if there are any)
	if (!GrsPawnPoolManagerHandlers.IsEmpty())
	{
		UPoolManagerSubsystem::Get().ReturnToPoolArray(GrsPawnPoolManagerHandlers);
		GrsPawnPoolManagerHandlers.Empty();
	}

	// --- Prepare spawn request
	const TWeakObjectPtr<ThisClass> WeakThis = this;
	const FOnSpawnAllCallback OnTakeActorsFromPoolCompleted = [WeakThis](const TArray<FPoolObjectData>& CreatedObjects)
	{
		if (UGrsPawnComponent* This = WeakThis.Get())
		{
			This->OnTakeActorsFromPoolCompleted(CreatedObjects);
		}
	};

	// --- Spawn actor
	UPoolManagerSubsystem::Get().TakeFromPoolArray(GrsPawnPoolManagerHandlers, AGRSPlayerCharacter::StaticClass(), 1, OnTakeActorsFromPoolCompleted, ESpawnRequestPriority::High);
}

//  Grabs a Ghost Revenge Player Character from the pool manager (Object pooling patter)
void UGrsPawnComponent::OnTakeActorsFromPoolCompleted(const TArray<FPoolObjectData>& CreatedObjects)
{
	// --- Setup spawned characters
	for (const FPoolObjectData& CreatedObject : CreatedObjects)
	{
		AGRSPlayerCharacter& GhostCharacter = CreatedObject.GetChecked<AGRSPlayerCharacter>();
		UE_LOG(LogTemp, Log, TEXT("Spawned ghost character --- %s - %s"), *GhostCharacter.GetName(), GhostCharacter.HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT"));

		GhostCharacter.InitPawn(GetBmrPawn()->GetPlayerId());
		GhostCharacter.RegisterPawnComponent(this);

		// we can path a current local player since it needed only for the skin init
		// GhostCharacter.OnGhostEliminatesPlayer.AddUniqueDynamic(this, &ThisClass::OnGhostEliminatesPlayer);
		GhostCharacter.OnGhostRemovedFromLevel.AddUniqueDynamic(this, &ThisClass::OnGhostRemovedFromLevel);
	}
}

// Called when the ghost character should be removed from level to unpossess controller
void UGrsPawnComponent::OnGhostRemovedFromLevel(class AController* CurrentController, class AGRSPlayerCharacter* GhostCharacter)
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
	// PlayerCharacter->GetMeshComponentChecked().SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
	// PossessPlayerCharacter(CurrentController, PlayerCharacter);
	// RevivePlayerCharacter(PlayerCharacter);
}
