// Copyright (c) Valerii Rotermel & Yevhenii Selivanov

#include "SubSystems/GRSWorldSubSystem.h"

// GRS
#include "LevelActors/GRSPlayerCharacter.h"

// Bmr
#include "Actors/BmrPawn.h"
#include "MyUtilsLibraries/UtilsLibrary.h"
#include "Structures/BmrGameplayTags.h"
#include "Subsystems/GlobalMessageSubsystem.h"

// UE
#include "Abilities/GameplayAbilityTypes.h"
#include "Components/GrsPlayerStateComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/BmrPlayerState.h"
#include "GrsGameplayTags.h"
#include "Kismet/GameplayStatics.h"
#include "Structures/BmrGameStateTag.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GRSWorldSubSystem)

/*********************************************************************************************
 * Subsystem's Lifecycle
 **********************************************************************************************/

// Returns this Subsystem, is checked and will crash if it can't be obtained
UGRSWorldSubSystem& UGRSWorldSubSystem::Get()
{
	const UWorld* World = UUtilsLibrary::GetPlayWorld();
	checkf(World, TEXT("%s: 'World' is null"), *FString(__FUNCTION__));
	UGRSWorldSubSystem* ThisSubsystem = World->GetSubsystem<ThisClass>();
	checkf(ThisSubsystem, TEXT("%s: 'ProgressionSubsystem' is null"), *FString(__FUNCTION__));
	return *ThisSubsystem;
}

// Returns this Subsystem, is checked and will crash if it can't be obtained
UGRSWorldSubSystem& UGRSWorldSubSystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine->GetWorldFromContextObjectChecked(WorldContextObject);
	checkf(World, TEXT("%s: 'World' is null"), *FString(__FUNCTION__));
	UGRSWorldSubSystem* ThisSubsystem = World->GetSubsystem<ThisClass>();
	checkf(ThisSubsystem, TEXT("%s: 'ProgressionSubsystem' is null"), *FString(__FUNCTION__));
	return *ThisSubsystem;
}

// Called when world is ready to start gameplay before the game mode transitions to the correct state and call BeginPlay on all actors
void UGRSWorldSubSystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
}

// Is called to initialize the world subsystem. It's a BeginPlay logic for the GRS module
void UGRSWorldSubSystem::OnWorldSubSystemInitialize_Implementation()
{
	UE_LOG(LogTemp, Log, TEXT("UGRSWorldSubSystem BeginPlay OnWorldSubSystemInitialize_Implementation --- %s"), *this->GetName());
	UGlobalMessageSubsystem::CallOrStartListeningForGlobalMessage(BmrGameplayTags::Event::Player_LocalPawnReady, this, &ThisClass::OnLocalPawnReady);
}

// Called when the local player character is spawned, possessed, and replicated
void UGRSWorldSubSystem::OnLocalPawnReady_Implementation(const FGameplayEventData& Payload)
{
	UE_LOG(LogTemp, Log, TEXT("UGRSWorldSubSystem::OnLocalCharacterReady_Implementation  --- %s"), *this->GetName());

	TryInit(); // try to initialize

	UGlobalMessageSubsystem::CallOrStartListeningForGlobalMessage(BmrGameplayTags::Event::GameState_Changed, this, &ThisClass::OnGameStateChanged);
}

// Checks if all components present and invokes initialization
void UGRSWorldSubSystem::TryInit()
{
	// --- check if managers have characters and collisions spawned if not - broadcast, yes -> return
	if (IsReady())
	{
		FGameplayEventData EventData;
		EventData.EventTag = GrsGameplayTags::Event::GameFeaturePluginReady;
		UGlobalMessageSubsystem::BroadcastGlobalMessage(EventData);
	}
}

// Checks if the system is ready to load
bool UGRSWorldSubSystem::IsReady()
{
	// todo: obtain max player param from Bmr core
	int32 MaxPlayers = 4;

	return CharacterManagerComponent && CollisionMangerComponent && PawnComponents.Num() == MaxPlayers;
}

// Clears all transient data created by this subsystem.
void UGRSWorldSubSystem::Deinitialize()
{
	PerformCleanUp();
	Super::Deinitialize();
}

// Cleanup used on unloading module to remove properties that should not be available by other objects.
void UGRSWorldSubSystem::PerformCleanUp()
{
	UnregisterCharacterManagerComponent();
	UnregisterCollisionManagerComponent();
	ClearGhostCharacters();
	ClearCollisions();
}

/*********************************************************************************************
 * Side Collisions actors
 **********************************************************************************************/

// Register collision manager component used to track if all components loaded and MGF ready to initialize
void UGRSWorldSubSystem::RegisterCollisionManagerComponent(UGhostRevengeCollisionComponent* NewCollisionManagerComponent)
{
	if (NewCollisionManagerComponent && NewCollisionManagerComponent != CollisionMangerComponent)
	{
		CollisionMangerComponent = NewCollisionManagerComponent;
	}

	TryInit(); // try to initialize
}

// Add spawned collision actors to be cached
void UGRSWorldSubSystem::AddCollisionActor(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	if (!LeftSideCollision)
	{
		LeftSideCollision = Actor;
	}
	else if (!RightSideCollision)
	{
		RightSideCollision = Actor;
	}
}

// Returns TRUE if collision are spawned
bool UGRSWorldSubSystem::IsCollisionsSpawned()
{
	if (LeftSideCollision && RightSideCollision)
	{
		return true;
	}

	return false;
}

// Clears cached collision manager component
void UGRSWorldSubSystem::UnregisterCollisionManagerComponent()
{
	CollisionMangerComponent = nullptr;
}

// Clear cached collisions
void UGRSWorldSubSystem::ClearCollisions()
{
	if (LeftSideCollision)
	{
		LeftSideCollision->Destroy();
	}

	if (RightSideCollision)
	{
		RightSideCollision->Destroy();
	}

	LeftSideCollision = nullptr;
	RightSideCollision = nullptr;
}

// Checks if the target Player was already revived. Player can be revived only once
bool UGRSWorldSubSystem::IsRevivable(const ABmrPawn* PlayerToRevive)
{
	if (!PlayerToRevive || RevivedPlayerCharacters.Contains(PlayerToRevive))
	{
		return false;
	}

	return true;
}

// Set a player character as it was revived once
void UGRSWorldSubSystem::SetRevivedPlayer(ABmrPawn* PlayerToRevive)
{
	if (!PlayerToRevive)
	{
		return;
	}
	RevivedPlayerCharacters.AddUnique(PlayerToRevive);
}

// Reset revived players so they can be ghosts again
void UGRSWorldSubSystem::ResetRevivedPlayers()
{
	RevivedPlayerCharacters.Empty();
}

/*********************************************************************************************
 * Ghost Characters
 **********************************************************************************************/

// Register character manager component
void UGRSWorldSubSystem::RegisterCharacterManagerComponent(UGRSGhostCharacterManagerComponent* NewCharacterManagerComponent)
{
	if (NewCharacterManagerComponent && NewCharacterManagerComponent != CharacterManagerComponent)
	{
		CharacterManagerComponent = NewCharacterManagerComponent;
	}

	TryInit();
}

/*********************************************************************************************
 * Pawn Component
 **********************************************************************************************/

// Register ghost character
EGRSCharacterSide UGRSWorldSubSystem::RegisterGhostCharacter(AGRSPlayerCharacter* GhostPlayerCharacter)
{
	checkf(GhostPlayerCharacter, TEXT("ERROR: [%i] %hs:\n'GhostPlayerCharacter' is null!"), __LINE__, __FUNCTION__);

	if (!GhostCharacterLeftSide)
	{
		GhostCharacterLeftSide = GhostPlayerCharacter;
		return EGRSCharacterSide::Left;
	}

	if (!GhostCharacterRightSide)
	{
		GhostCharacterRightSide = GhostPlayerCharacter;
		return EGRSCharacterSide::Right;
	}

	return EGRSCharacterSide::None;
}

// Returns currently available ghost character or nullptr if there is no available ghosts.
AGRSPlayerCharacter* UGRSWorldSubSystem::GetAvailableGhostCharacter()
{
	if (!GhostCharacterLeftSide->GetController())
	{
		return GhostCharacterLeftSide;
	}

	if (!GhostCharacterRightSide->GetController())
	{
		return GhostCharacterRightSide;
	}

	return nullptr;
}

// Register a new Pawn component to track the pawn state
void UGRSWorldSubSystem::RegisterPawnComponent(class UGrsPawnComponent* NewPawnComponent)
{
	if (!NewPawnComponent)
	{
		return;
	}

	PawnComponents.AddUnique(NewPawnComponent);
	TryInit();
}

// Pawn Components attached to BmrPawn to track Pawn's state change
TArray<class UGrsPawnComponent*> UGRSWorldSubSystem::GetPawnComponents() const
{
	return PawnComponents;
}

// Clears the registered pawn component once it deleted
void UGRSWorldSubSystem::UnRegisterPawnComponent(class UGrsPawnComponent* PawnComponentToUnregister)
{
	if (!PawnComponentToUnregister || PawnComponents.IsEmpty() || !PawnComponents.Contains(PawnComponentToUnregister))
	{
		return;
	}

	PawnComponents.Remove(PawnComponentToUnregister);
}

// Clears cached character manager component
void UGRSWorldSubSystem::UnregisterCharacterManagerComponent()
{
	CharacterManagerComponent = nullptr;
}

// Clear cached ghost character by reference
void UGRSWorldSubSystem::UnregisterGhostCharacter(AGRSPlayerCharacter* GhostPlayerCharacter)
{
	if (!GhostPlayerCharacter)
	{
		return;
	}

	if (GhostCharacterLeftSide == GhostPlayerCharacter)
	{
		GhostCharacterLeftSide = nullptr;

		return;
	}

	if (GhostCharacterRightSide == GhostPlayerCharacter)
	{
		GhostCharacterRightSide = nullptr;
	}
}

// Clear cached ghost character references
void UGRSWorldSubSystem::ClearGhostCharacters()
{
	if (GhostCharacterLeftSide)
	{
		GhostCharacterLeftSide->Destroy();
		GhostCharacterLeftSide = nullptr;
	}

	if (GhostCharacterRightSide)
	{
		GhostCharacterRightSide->Destroy();
		GhostCharacterRightSide = nullptr;
	}
}

// Register a new player state component
void UGRSWorldSubSystem::RegisterPlayerStateComponent(UGrsPlayerStateComponent* NewPlayerStateComponent)
{
	if (!NewPlayerStateComponent)
	{
		return;
	}

	PlayerStateComponents.AddUnique(NewPlayerStateComponent);
}

// Unregister a player state component
void UGRSWorldSubSystem::UnRegisterPlayerStateComponent(UGrsPlayerStateComponent* PlayerStateComponent)
{
	if (!PlayerStateComponent)
	{
		return;
	}

	PlayerStateComponents.Remove(PlayerStateComponent);
}

// Find a player state component by player ID
UGrsPlayerStateComponent* UGRSWorldSubSystem::GetPlayerStateComponent(int32 TargetPlayerID)
{
	for (UGrsPlayerStateComponent* PlayerStateComponent : PlayerStateComponents)
	{
		ABmrPlayerState* BmrPlayerState = Cast<ABmrPlayerState>(PlayerStateComponent->GetOwner());
		if (!ensureMsgf(BmrPlayerState, TEXT("ASSERT: [%i] %hs:\n'BmrPlayerState' is not valid!"), __LINE__, __FUNCTION__))
		{
			return nullptr;
		}

		if (BmrPlayerState->GetPlayerId() == TargetPlayerID)
		{
			return PlayerStateComponent;
		}
	}
	return nullptr;
}

/*********************************************************************************************
 * Treasury (temp)
 **********************************************************************************************/

// Listen game states to switch character skin.
void UGRSWorldSubSystem::OnGameStateChanged_Implementation(const FGameplayEventData& Payload)
{
	/** EngGameState:
//HUD = UWidgetsSubsystem::Get().GetWidgetByTag();
if (!ensureMsgf(HUD, TEXT("ASSERT: [%i] %hs:\n'HUD' is not valid!"), __LINE__, __FUNCTION__))
{
	break;
}
HUD->SetVisibility(ESlateVisibility::Collapsed);
PlayerStateInternal->SetCharacterDead(false);
PlayerStateInternal->SetOpponentKilledNum(0);
PlayerStateInternal->SetEndGameState(EBmrEndGameState::None);
*/
}
