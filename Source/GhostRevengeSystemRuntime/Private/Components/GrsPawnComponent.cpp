// Copyright (c) Yevhenii Selivanov

#include "Components/GrsPawnComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Actors/BmrPawn.h"
#include "Components/GrsPlayerStateComponent.h"
#include "GrsGameplayTags.h"
#include "LevelActors/GRSPlayerCharacter.h"
#include "PoolManagerSubsystem.h"
#include "Structures/BmrGameplayTags.h"
#include "SubSystems/GRSWorldSubSystem.h"
#include "Subsystems/GlobalMessageSubsystem.h"
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

// Returns GrsPlayerStateComponent obtaining from pawn's player id
UGrsPlayerStateComponent* UGrsPawnComponent::GetGrsPlayerStateComponent() const
{
	return UGRSWorldSubSystem::Get().GetPlayerStateComponent(GetBmrPawn()->GetPlayerId());
}

UGrsPlayerStateComponent* UGrsPawnComponent::GetGrsPlayerStateComponentChecked() const
{
	UGrsPlayerStateComponent* GrsPlayerStateComponent = GetGrsPlayerStateComponent();
	checkf(GrsPlayerStateComponent, TEXT("%s: 'GrsPlayerStateComponent' is null"), *FString(__FUNCTION__));
	return GrsPlayerStateComponent;
}

// Called when the game starts
void UGrsPawnComponent::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Log, TEXT("UGrsPawnComponent::BeginPlay  --- %s - %s"), *this->GetName(), GetOwner()->HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT"));
	UGRSWorldSubSystem& WorldSubsystem = UGRSWorldSubSystem::Get();
	WorldSubsystem.RegisterPawnComponent(this);
	UGlobalMessageSubsystem::CallOrStartListeningForGlobalMessage(GrsGameplayTags::Event::GameFeaturePluginReady, this, &ThisClass::OnInitialize);
}

// Clears all transient data created by this component
void UGrsPawnComponent::OnUnregister()
{
	Super::OnUnregister();

	UGRSWorldSubSystem::Get().UnRegisterPawnComponent(this);

	if (GrsPawnPoolManagerHandlers.Num() > 0)
	{
		for (FPoolObjectHandle GrsPoolObjectHandle : GrsPawnPoolManagerHandlers)
		{
			if (UPoolManagerSubsystem::Get().FindPoolObjectByHandle(GrsPoolObjectHandle).IsValid())
			{
				UPoolManagerSubsystem::Get().ReturnToPool(GrsPoolObjectHandle);
			}
		}

		GrsPawnPoolManagerHandlers.Empty();

		FPoolObjectHandle GrsObjectHandle = UPoolManagerSubsystem::Get().FindPoolHandleByObject(AGRSPlayerCharacter::StaticClass());
		if (GrsObjectHandle.IsValid())
		{
			UPoolManagerSubsystem::Get().EmptyPool(AGRSPlayerCharacter::StaticClass());
		}
	}
}

// A pawn could be loaded/replicated faster than MGF(GFP) is fully loaded therefore waiting for whole module to be initialized is required
void UGrsPawnComponent::OnInitialize(const struct FGameplayEventData& Payload)
{
	if (GetOwner()->HasAuthority())
	{
		AddGhostCharacter();
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

		FBmrCell ActorSpawnLocation;
		float CellSize = FBmrCell::CellSize + (FBmrCell::CellSize / 2);

		ActorSpawnLocation = UBmrCellUtilsLibrary::GetCellByCornerOnLevel(EBmrGridCorner::TopLeft);
		ActorSpawnLocation.Location.X = ActorSpawnLocation.Location.X - CellSize;
		ActorSpawnLocation.Location.Y = ActorSpawnLocation.Location.Y + (CellSize / 2); // temporary, debug row

		GhostCharacter.SetActorLocation(ActorSpawnLocation);

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
