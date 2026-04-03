// Copyright (c) Valerii Rotermel & Yevhenii Selivanov

#include "LevelActors/GRSPlayerCharacter.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Actors/BmrBombAbilityActor.h"
#include "Actors/BmrPawn.h"
#include "Animation/AnimInstance.h"
#include "Components/BmrMapComponent.h"
#include "Components/BmrPlayerNameWidgetComponent.h"
#include "Components/BmrSkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/GRSGhostCharacterManagerComponent.h"
#include "Components/GrsPawnComponent.h"
#include "Components/GrsPlayerStateComponent.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Controllers/BmrPlayerController.h"
#include "Data/GRSDataAsset.h"
#include "DataAssets/BmrPlayerDataAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/BmrPlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GrsGameplayTags.h"
#include "Kismet/GameplayStatics.h"
#include "LevelActors/GRSBombProjectile.h"
#include "Structures/BmrGameStateTag.h"
#include "Structures/BmrGameplayTags.h"
#include "SubSystems/GRSWorldSubSystem.h"
#include "Subsystems/GlobalMessageSubsystem.h"
#include "UI/Widgets/BmrPlayerNameWidget.h"
#include "UObject/ConstructorHelpers.h"
#include "UtilityLibraries/BmrBlueprintFunctionLibrary.h"
#include "UtilityLibraries/BmrCellUtilsLibrary.h"

class UBmrPlayerRow;

/*********************************************************************************************
 * Nickname component
 **********************************************************************************************/

//  Sets/Updates player name from possessed pawn player state
void AGRSPlayerCharacter::UpdatePlayerName(ABmrPlayerState* MyPlayerState)
{
	if (!ensureMsgf(MyPlayerState, TEXT("ASSERT: [%i] %hs:\n'MyPlayerState' is not valid!"), __LINE__, __FUNCTION__))
	{
		return;
	}

	checkf(PlayerName3DWidgetComponentInternal, TEXT("ERROR: [%i] %hs:\n'PlayerName3DWidgetComponent' is null!"), __LINE__, __FUNCTION__);
	PlayerName3DWidgetComponentInternal->Init(MyPlayerState);
}

/*********************************************************************************************
 * Initialization
 **********************************************************************************************/

// Sets default values for this character's properties
AGRSPlayerCharacter::AGRSPlayerCharacter(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer.SetDefaultSubobjectClass<UBmrSkeletalMeshComponent>(MeshComponentName)) // Init UBmrSkeletalMeshComponent instead of USkeletalMeshComponent
{
	// Set default character parameters such as bCanEverTick, bStartWithTickEnabled, replication etc.
	SetDefaultParams();

	// Initialize skeletal mesh of the character
	InitializeSkeletalMesh();

	// Configure the movement component
	MovementComponentConfiguration();

	// Setup capsule component
	SetupCapsuleComponent();

	// Initialize 3D widget component for the player name
	PlayerName3DWidgetComponentInternal = CreateDefaultSubobject<UBmrPlayerNameWidgetComponent>(TEXT("PlayerName3DWidgetComponent"));
	PlayerName3DWidgetComponentInternal->SetupAttachment(RootComponent);

	// setup spline component
	ProjectileSplineComponentInternal = CreateDefaultSubobject<USplineComponent>(TEXT("ProjectileSplineComponent"));
	ProjectileSplineComponentInternal->AttachToComponent(MeshComponentInternal, FAttachmentTransformRules::KeepRelativeTransform);

	AimingSphereComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SphereComp"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		AimingSphereComponent->SetStaticMesh(SphereMesh.Object);
	}
}

// Set default character parameters such as bCanEverTick, bStartWithTickEnabled, replication etc.
void AGRSPlayerCharacter::SetDefaultParams()
{
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// Replicate an actor
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(true);

	// Do not rotate player by camera
	bUseControllerRotationYaw = false;
}

// Initialize skeletal mesh of the character
void AGRSPlayerCharacter::InitializeSkeletalMesh()
{
	// Initialize skeletal mesh
	USkeletalMeshComponent* SkeletalMeshComponent = GetMesh();
	checkf(SkeletalMeshComponent, TEXT("ERROR: [%i] %hs:\n'SkeletalMeshComponent' is null!"), __LINE__, __FUNCTION__);
	static const FVector MeshRelativeLocation(0, 0, -90.f);
	SkeletalMeshComponent->SetRelativeLocation_Direct(MeshRelativeLocation);
	static const FRotator MeshRelativeRotation(0, -90.f, 0);
	SkeletalMeshComponent->SetRelativeRotation_Direct(MeshRelativeRotation);
	SkeletalMeshComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	// Enable all lighting channels, so it's clearly visible in the dark
	SkeletalMeshComponent->SetLightingChannels(/*bChannel0*/ true, /*bChannel1*/ true, /*bChannel2*/ true);
}

// Configure the movement component of the character
void AGRSPlayerCharacter::MovementComponentConfiguration()
{
	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		// Rotate player by movement
		MovementComponent->bOrientRotationToMovement = true;
		static const FRotator RotationRate(0.f, 540.f, 0.f);
		MovementComponent->RotationRate = RotationRate;

		// Do not push out clients from collision
		MovementComponent->MaxDepenetrationWithGeometryAsProxy = 0.f;
	}
}

// Set up the capsule component of the character
void AGRSPlayerCharacter::SetupCapsuleComponent()
{
	if (UCapsuleComponent* RootCapsuleComponent = GetCapsuleComponent())
	{
		// Setup collision to allow overlap players with each other, but block all other actors
		RootCapsuleComponent->CanCharacterStepUpOn = ECB_Yes;
		RootCapsuleComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		RootCapsuleComponent->SetCollisionProfileName(UCollisionProfile::CustomCollisionProfileName);
		RootCapsuleComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		RootCapsuleComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		RootCapsuleComponent->SetCollisionResponseToChannel(ECC_Player0, ECR_Overlap);
		RootCapsuleComponent->SetCollisionResponseToChannel(ECC_Player1, ECR_Overlap);
		RootCapsuleComponent->SetCollisionResponseToChannel(ECC_Player2, ECR_Overlap);
		RootCapsuleComponent->SetCollisionResponseToChannel(ECC_Player3, ECR_Overlap);

		RootCapsuleComponent->SetIsReplicated(true);
	}
}

/*********************************************************************************************
 * Main functionality (core loop)
 **********************************************************************************************/

// Basic initialization of the Pawn
void AGRSPlayerCharacter::InitPawn(int32 NewPlayerId)
{
	if (PlayerID != NewPlayerId)
	{
		PlayerID = NewPlayerId;
	}
}

//  Register owning pawn component
void AGRSPlayerCharacter::RegisterPawnComponent(UGrsPawnComponent* NewPawnComponent)
{
	if (NewPawnComponent || OwningPawnComponent != NewPawnComponent)
	{
		OwningPawnComponent = NewPawnComponent;

		ABmrPawn* MyPawn = &OwningPawnComponent->GetBmrPawnChecked();
		if (MyPawn)
		{
			UBmrMapComponent* MapComponent = UBmrMapComponent::GetMapComponent(MyPawn);
			if (!ensureMsgf(MapComponent, TEXT("ASSERT: [%i] %hs:\n 'MapComponent' is null!"), __LINE__, __FUNCTION__))
			{
				return;
			}

			MapComponent->OnPreRemovedFromLevel.AddUniqueDynamic(this, &ThisClass::OnPreRemovedFromLevel);
		}
	}
}

// Called when the game starts or when spawned (on spawned on the level)
void AGRSPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogTemp, Log, TEXT("AGRSPlayerCharacter::OnInitialize ghost character  --- %s - %s"), *this->GetName(), this->HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT"));
	UGlobalMessageSubsystem::CallOrStartListeningForGlobalMessage(GrsGameplayTags::Event::GameFeaturePluginReady, this, &ThisClass::OnInitialize);
}

// Overridable function called whenever this actor is being removed from a level
void AGRSPlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UGlobalMessageSubsystem::StopListeningForAllGlobalMessages(this);

	Super::EndPlay(EndPlayReason);
}

// Returns the Ability System Component from the Player State
UAbilitySystemComponent* AGRSPlayerCharacter::GetAbilitySystemComponent() const
{
	const ABmrPlayerState* InPlayerState = UGRSWorldSubSystem::Get().GetPlayerStateComponent(PlayerID)->GetCurrentPlayerStateChecked();
	return InPlayerState ? InPlayerState->GetAbilitySystemComponent() : nullptr;
}

void AGRSPlayerCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	DOREPLIFETIME_WITH_PARAMS_FAST(ThisClass, PlayerID, Params);
}

// The player character could be replicated faster than MGF(GFP) is loaded on client so the only we have to wait/check for subsystem to initialize as it is central loading point
void AGRSPlayerCharacter::OnInitialize(const struct FGameplayEventData& Payload)
{
	GetMeshChecked().SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);

	// --- Activate aiming point
	AimingSphereComponent->SetMaterial(0, UGRSDataAsset::Get().GetAimingMaterial());
	AimingSphereComponent->SetVisibility(true);

	// --- bind to  clear ghost data
	UGlobalMessageSubsystem::CallOrStartListeningForGlobalMessage(BmrGameplayTags::Event::GameState_Changed, this, &ThisClass::OnGameStateChanged);

	// --- bind to track player death status
	// ABmrPlayerState* InPlayerState = GetPlayerState<ABmrPlayerState>();
	// checkf(InPlayerState, TEXT("ERROR: [%i] %hs:\n'InPlayerState' is null!"), __LINE__, __FUNCTION__);
	// InPlayerState->OnPlayerDeadChanged.AddUniqueDynamic(this, &ThisClass::OnPlayerDeadChanged);

	// --- default params required for the fist start to have character prepared
	SetPlayerMeshData();

	UpdatePlayerName(UGRSWorldSubSystem::Get().GetPlayerStateComponent(PlayerID)->GetCurrentPlayerStateChecked());
	// InitializePlayerArrow(UBmrBlueprintFunctionLibrary::GetLocalPawn());

	// --- Init character visuals (animations, skin)
	SetCharacterVisual(&UGRSWorldSubSystem::Get().GetPlayerStateComponent(PlayerID)->GetCurrentPlayerStateChecked()->GetPawnChecked());

	// --- ghost added to level
	OnGhostAddedToLevel.Broadcast();
}

// Listen for Player State property bIsDead. It assumes that PlayerState is not respawned on player join and not destroyed on player leave, but is reused for both human and bot characters
void AGRSPlayerCharacter::OnPlayerDeadChanged_Implementation(bool bIsDead)
{
	if (!bIsDead)
	{
		return;
	}

	if (this != UGRSWorldSubSystem::Get().GetAvailableGhostCharacter())
	{
		return;
	}

	ABmrPlayerState* InPlayerState = GetPlayerState<ABmrPlayerState>();
	checkf(InPlayerState, TEXT("ERROR: [%i] %hs:\n'InPlayerState' is null!"), __LINE__, __FUNCTION__);
}

// Listen game states to remove ghost character from level
void AGRSPlayerCharacter::OnGameStateChanged_Implementation(const struct FGameplayEventData& Payload)
{
	if (!Payload.InstigatorTags.HasTag(FBmrGameStateTag::InGame))
	{
		// -- release (unpossess) all ghosts
		RemoveGhostCharacterFromMap();
	}

	if (Payload.InstigatorTags.HasTag(FBmrGameStateTag::GameStarting))
	{
		UGRSWorldSubSystem::Get().ResetRevivedPlayers();
		PossessedPlayerCharacter = nullptr;
	}
}

// Activates ghost with required initiation
void AGRSPlayerCharacter::TryActivateGhostCharacter(AGRSPlayerCharacter* GhostCharacter, ABmrPawn* FromPlayerCharacter)
{
	if (!GhostCharacter
	    || GhostCharacter != this
	    || !FromPlayerCharacter
	    || !UGRSWorldSubSystem::Get().IsRevivable(FromPlayerCharacter))
	{
		return;
	}

	AController* PlayerController = FromPlayerCharacter->GetController();
	if (!PlayerController)
	{
		return;
	}

	// --- check if the player already possessed by other ghost
	AGRSPlayerCharacter* CurrentGhostCharacter = Cast<AGRSPlayerCharacter>(PlayerController->GetPawn());
	if (CurrentGhostCharacter)
	{
		return;
	}

	if (PossessedPlayerCharacter != FromPlayerCharacter)
	{
		PossessedPlayerCharacter = FromPlayerCharacter;
	}

	// --- authority calls:
	TryPossessController(PlayerController);
	// --- set pawn location (side)
	SetPawnSide();

	// --- clients calls:
	// --- change visibility for this character
	// --- update collision settings
	// --- activate arrow

	// --- just refresh visibility of player name needed, to be changed. Player name to be set by default
	// ABmrPlayerState* BmrPlayerState = Cast<ABmrPlayerState>(FromPlayerCharacter->GetPlayerState());
	// UpdatePlayerName(BmrPlayerState);
}

// Called right before owner actor going to remove from the Generated Map, on both server and clients.
void AGRSPlayerCharacter::OnPreRemovedFromLevel_Implementation(class UBmrMapComponent* MapComponent, class UObject* DestroyCauser)
{
	ABmrPawn* PlayerCharacter = MapComponent->GetOwner<ABmrPawn>();
	if (!ensureMsgf(PlayerCharacter, TEXT("ASSERT: [%i] %hs:\n'PlayerCharacter' is not valid!"), __LINE__, __FUNCTION__)
	    || PlayerCharacter->IsBotControlled()
	    || !DestroyCauser)
	{
		return;
	}

	const ABmrPlayerState* DestroyCauserPlayerState = Cast<ABmrPlayerState>(DestroyCauser);
	if (!DestroyCauserPlayerState)
	{
		return;
	}

	const APawn* Causer = DestroyCauserPlayerState->GetPawn();
	if (!Causer)
	{
		return;
	}

	// --- ghost eliminates a player - remove ghost from map
	const AGRSPlayerCharacter* GRSCharacter = Cast<AGRSPlayerCharacter>(Causer);
	if (GRSCharacter && GRSCharacter == this)
	{
		OnGhostEliminatesPlayer.Broadcast(PlayerCharacter->GetActorLocation(), this);
		UE_LOG(LogTemp, Log, TEXT("[%i] %hs: --- OnGhostEliminatesPlayer.Broadcast"), __LINE__, __FUNCTION__);
		RemoveGhostCharacterFromMap();

		UGRSWorldSubSystem::Get().GetPlayerStateComponent(PlayerID)->RevivePlayerCharacter(PlayerCharacter);
	}

	// --- a player was eliminated - activate ghost character
	if (DestroyCauserPlayerState->GetPlayerId() == PlayerID)
	{
		TryActivateGhostCharacter(this, PlayerCharacter);
	}
}

// Remove ghost character from the level
void AGRSPlayerCharacter::RemoveGhostCharacterFromMap()
{
	// --- move all functional part such as posses to ability
	// --- possess back to player character for any cases
	// PlayerCharacter->GetMeshComponentChecked().SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);

	// --- Disable aiming point
	if (AimingSphereComponent)
	{
		AimingSphereComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		AimingSphereComponent->SetVisibility(false);
	}

	UGRSWorldSubSystem::Get().SetRevivedPlayer(PossessedPlayerCharacter);
	UGRSWorldSubSystem::Get().UnregisterGhostCharacter(this);

	// --- change visibility of this pawn
	// -- change nickname visibility of this pawn
	// --- update collision mod of this pawn if needed

	AController* CurrentController = GetController();
	if (!PossessedPlayerCharacter || !HasAuthority() || !CurrentController)
	{
		return;
	}

	if (CurrentController->GetPawn())
	{
		// At first, unpossess previous controller
		CurrentController->UnPossess();
	}

	// --- Always possess to player character when ghost character is no longer in control
	CurrentController->Possess(PossessedPlayerCharacter);

	UE_LOG(LogTemp, Log, TEXT("[%i] %hs: --- PlayerController is %s"), __LINE__, __FUNCTION__, CurrentController ? TEXT("TRUE") : TEXT("FALSE"));
	UE_LOG(LogTemp, Log, TEXT("[%i] %hs: --- PlayerCharacter is %s"), __LINE__, __FUNCTION__, PossessedPlayerCharacter ? TEXT("TRUE") : TEXT("FALSE"));
	UE_LOG(LogTemp, Log, TEXT("[%i] %hs: --- PlayerCharacter: %s"), __LINE__, __FUNCTION__, *GetNameSafe(PossessedPlayerCharacter));

	// --- reset player character reference
	PossessedPlayerCharacter = nullptr;

	OnGhostRemovedFromLevel.Broadcast(CurrentController, this);
	UE_LOG(LogTemp, Log, TEXT("[%i] %hs: --- OnGhostRemovedFromLevel.Broadcast"), __LINE__, __FUNCTION__);
}

// Called on client when player ID is changed
void AGRSPlayerCharacter::OnRep_PlayerID()
{
	// --- Init Grs Pawn logic
	InitPawn(PlayerID);
}

/*********************************************************************************************
 * Utils
 **********************************************************************************************/
// Returns the Skeletal Mesh of ghost revenge character
UBmrSkeletalMeshComponent& AGRSPlayerCharacter::GetMeshChecked() const
{
	return *CastChecked<UBmrSkeletalMeshComponent>(GetMesh());
}

// Set visibility of the player character
void AGRSPlayerCharacter::SetVisibility(bool Visibility)
{
	GetMesh()->SetVisibility(Visibility, true);
}

//  Set character visual once added to the level from a refence character (visuals, animations)
void AGRSPlayerCharacter::SetCharacterVisual(const ABmrPawn* PlayerCharacter)
{
	checkf(PlayerCharacter, TEXT("ERROR: [%i] %hs:\n'PlayerCharacter' is null!"), __LINE__, __FUNCTION__);

	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		const TSubclassOf<UAnimInstance> AnimInstanceClass = UBmrPlayerDataAsset::Get().GetAnimInstanceClass();
		MeshComp->SetAnimInstanceClass(AnimInstanceClass);
	}

	const UBmrSkeletalMeshComponent* MainCharacterMeshComponent = &PlayerCharacter->GetMeshComponentChecked();

	if (!ensureMsgf(MainCharacterMeshComponent, TEXT("ASSERT: [%i] %hs:\n'MainCharacterMeshComponent' is not valid!"), __LINE__, __FUNCTION__))
	{
		return;
	}
	const int32 CurrentSkinIndex = MainCharacterMeshComponent->GetAppliedSkinIndex();

	UBmrSkeletalMeshComponent* CurrentMeshComponent = &GetMeshChecked();
	if (!ensureMsgf(CurrentMeshComponent, TEXT("ASSERT: [%i] %hs:\n'CurrentMeshComponent' is not valid!"), __LINE__, __FUNCTION__))
	{
		return;
	}
	CurrentMeshComponent->InitSkeletalMesh(MainCharacterMeshComponent->GetMeshData());
	CurrentMeshComponent->ApplySkinByIndex(CurrentSkinIndex);
}

// Set and apply skeletal mesh for ghost player. Copy mesh from current player
void AGRSPlayerCharacter::SetPlayerMeshData(bool bForcePlayerSkin /* = false*/)
{
	ABmrPawn* PlayerCharacter = &UGRSWorldSubSystem::Get().GetPlayerStateComponent(PlayerID)->GetCurrentPlayerStateChecked()->GetPawnChecked();
	checkf(PlayerCharacter, TEXT("ERROR: [%i] %hs:\n'PlayerCharacter' is null!"), __LINE__, __FUNCTION__);

	const EBmrLevelType PlayerFlag = UBmrBlueprintFunctionLibrary::GetLevelType();
	EBmrLevelType LevelType = PlayerFlag;

	const UBmrPlayerRow* Row = UBmrPlayerDataAsset::Get().GetRowByLevelType<UBmrPlayerRow>(TO_ENUM(EBmrLevelType, LevelType));
	if (!ensureMsgf(Row, TEXT("ASSERT: [%i] %hs:\n'Row' is not found!"), __LINE__, __FUNCTION__))
	{
		return;
	}

	const int32 SkinsNum = Row->GetSkinTexturesNum();
	FBmrMeshData MeshData;
	MeshData.Row = Row;
	MeshData.SkinIndex = PlayerCharacter->GetPlayerId() % SkinsNum;
	GetMeshChecked().InitSkeletalMesh(MeshData);
}

//  Possess a player controller
void AGRSPlayerCharacter::TryPossessController(AController* PlayerController)
{
	if (!PlayerController || !PlayerController->HasAuthority())
	{
		return;
	}

	if (PlayerController)
	{
		// Unpossess current pawn first
		if (PlayerController->GetPawn())
		{
			PlayerController->UnPossess();
		}
	}

	PlayerController->Possess(this);
}

//  Set side for this pawn (left or right)
void AGRSPlayerCharacter::SetPawnSide()
{
	if (!HasAuthority())
	{
		return;
	}
	EGRSCharacterSide CharacterSide = UGRSWorldSubSystem::Get().RegisterGhostCharacter(this);

	checkf(!(CharacterSide == EGRSCharacterSide::None), TEXT("ERROR: [%i] %hs:\n'CharacterSide' is none!"), __LINE__, __FUNCTION__);

	FBmrCell ActorSpawnLocation;
	float CellSize = FBmrCell::CellSize + (FBmrCell::CellSize / 2);

	if (CharacterSide == EGRSCharacterSide::Left)
	{
		ActorSpawnLocation = UBmrCellUtilsLibrary::GetCellByCornerOnLevel(EBmrGridCorner::TopLeft);
		ActorSpawnLocation.Location.X = ActorSpawnLocation.Location.X - CellSize;
		ActorSpawnLocation.Location.Y = ActorSpawnLocation.Location.Y + (CellSize / 2); // temporary, debug row
	}
	else if (CharacterSide == EGRSCharacterSide::Right)
	{
		ActorSpawnLocation = UBmrCellUtilsLibrary::GetCellByCornerOnLevel(EBmrGridCorner::TopRight);
		ActorSpawnLocation.Location.X = ActorSpawnLocation.Location.X + CellSize;
		ActorSpawnLocation.Location.Y = ActorSpawnLocation.Location.Y + (CellSize / 2); // temporary, debug row
	}

	// Match the Z axis to what we have on the level
	ActorSpawnLocation.Location.Z = 100.0f;
	SetActorLocation(ActorSpawnLocation);
}

// Add a mesh to the last element of the predict Projectile path results
void AGRSPlayerCharacter::AddMeshToEndProjectilePath(FVector Location)
{
	AimingSphereComponent->SetVisibility(true);
	AimingSphereComponent->SetWorldLocation(Location);
}

// Applies spawn bomb gameplay effect
void AGRSPlayerCharacter::ApplyExplosionGameplayEffect()
{
	UGRSWorldSubSystem::Get().GetPlayerStateComponent(PlayerID)->ApplyBombSpawningGameplayEffect();
}

// Removes spawn bomb gameplay effect
void AGRSPlayerCharacter::RemoveExplosionGameplayEffect()
{
	UGRSWorldSubSystem::Get().GetPlayerStateComponent(PlayerID)->RemoveBombSpawningGameplayEffect();
}

// Add spline points to the spline component
void AGRSPlayerCharacter::AddSplinePoints(FPredictProjectilePathResult& Result)
{
	ClearTrajectorySplines();

	for (int32 i = 0; i < Result.PathData.Num(); i++)
	{
		FVector SplinePoint = Result.PathData[i].Location;
		ProjectileSplineComponentInternal->AddSplinePointAtIndex(SplinePoint, i, ESplineCoordinateSpace::World);
		ProjectileSplineComponentInternal->Mobility = EComponentMobility::Static;
	}

	ProjectileSplineComponentInternal->SetSplinePointType(Result.PathData.Num() - 1, ESplinePointType::CurveClamped, true);
	ProjectileSplineComponentInternal->UpdateSpline();
}

// Hide spline elements (trajectory)
void AGRSPlayerCharacter::ClearTrajectorySplines()
{
	for (USplineMeshComponent* SplineMeshComponent : SplineMeshArrayInternal)
	{
		SplineMeshComponent->DestroyComponent();
	}

	SplineMeshArrayInternal.Empty();
	ProjectileSplineComponentInternal->ClearSplinePoints();
}

//  Add spline mesh to spline points
void AGRSPlayerCharacter::AddSplineMesh(FPredictProjectilePathResult& Result)
{
	for (int32 i = 0; i < ProjectileSplineComponentInternal->GetNumberOfSplinePoints() - 2; i++)
	{
		// Create and attach the spline mesh component
		USplineMeshComponent* SplineMesh = NewObject<USplineMeshComponent>(this); // 'this' is usually your actor
		SplineMesh->AttachToComponent(ProjectileSplineComponentInternal, FAttachmentTransformRules::KeepRelativeTransform);
		SplineMesh->ForwardAxis = ESplineMeshAxis::Z;
		SplineMesh->Mobility = EComponentMobility::Static;
		SplineMesh->SetStartScale(UGRSDataAsset::Get().GetTrajectoryMeshScale());
		SplineMesh->SetEndScale(UGRSDataAsset::Get().GetTrajectoryMeshScale());

		// Set mesh and material
		SplineMesh->SetStaticMesh(UGRSDataAsset::Get().GetChargeMesh());
		SplineMesh->SetMaterial(0, UGRSDataAsset::Get().GetTrajectoryMaterial());
		FVector TangentStart = ProjectileSplineComponentInternal->GetTangentAtSplinePoint(i, ESplineCoordinateSpace::World);
		FVector TangentEnd = ProjectileSplineComponentInternal->GetTangentAtSplinePoint(i + 1, ESplineCoordinateSpace::World);

		// Set start and end
		SplineMesh->SetStartAndEnd(Result.PathData[i].Location, TangentStart, Result.PathData[i + 1].Location, TangentEnd);
		// Register the component so it appears in the game
		SplineMesh->RegisterComponent();

		SplineMeshArrayInternal.AddUnique(SplineMesh);
	}
}

// Throw projectile event, bound to onetime button press
void AGRSPlayerCharacter::ThrowProjectile()
{
	//--- Calculate Cell to spawn bomb
	FBmrCell CurrentCell;
	CurrentCell.Location = AimingSphereComponent->GetComponentLocation();

	//--- hide aiming sphere from ui
	AimingSphereComponent->SetVisibility(false);

	SpawnBomb(CurrentCell);

	FVector ThrowDirection = GetActorForwardVector() + FVector(5, 5, 0.0f);
	ThrowDirection.Normalize();
	FVector LaunchVelocity = ThrowDirection * 100;

	ClearTrajectorySplines();

	// Spawn projectile and launch projectile
	if (BombProjectileInternal == nullptr)
	{
		// BombProjectileInternal = GetWorld()->SpawnActor<AGRSBombProjectile>(UGRSDataAsset::Get().GetProjectileClass(), SpawnLocation, GetActorRotation());
	}

	if (BombProjectileInternal)
	{
		// BombProjectileInternal->Launch(LaunchVelocity);
	}
}

// Spawn bomb on aiming sphere position.
void AGRSPlayerCharacter::SpawnBomb(FBmrCell TargetCell)
{
	const FBmrCell& SpawnBombCell = UBmrCellUtilsLibrary::GetNearestFreeCell(TargetCell);

	// Activate bomb ability
	FGameplayEventData EventData;
	EventData.EventTag = UGRSDataAsset::Get().GetTriggerBombTag();
	EventData.Instigator = this;
	EventData.EventMagnitude = UBmrCellUtilsLibrary::GetIndexByCellOnLevel(SpawnBombCell);
	UGlobalMessageSubsystem::BroadcastGlobalMessage(EventData);
}

//  Clean up the character for the MGF unload
void AGRSPlayerCharacter::PerformCleanUp()
{
	if (UCapsuleComponent* RootCapsuleComponent = GetCapsuleComponent())
	{
		RootCapsuleComponent->DestroyComponent();
	}

	if (UMovementComponent* MovementComponent = GetMovementComponent())
	{
		MovementComponent->DestroyComponent();
	}

	if (ProjectileSplineComponentInternal)
	{
		ProjectileSplineComponentInternal->DestroyComponent();
		ProjectileSplineComponentInternal = nullptr;
	}

	if (MeshComponentInternal)
	{
		MeshComponentInternal->DestroyComponent();
		MeshComponentInternal = nullptr;
	}

	if (BombProjectileInternal)
	{
		BombProjectileInternal->Destroy();
		BombProjectileInternal = nullptr;
	}

	if (AimingSphereComponent)
	{
		AimingSphereComponent->DestroyComponent();
		AimingSphereComponent = nullptr;
	}

	if (PlayerName3DWidgetComponentInternal)
	{
		PlayerName3DWidgetComponentInternal = nullptr;
	}

	OwningPawnComponent = nullptr;
	PossessedPlayerCharacter = nullptr;
	PlayerID = 0;

	// --- perform clean up from subsystem MGF is not possible so we have to call directly to clean cached references
	UGRSWorldSubSystem::Get().UnregisterGhostCharacter(this);
	UGRSWorldSubSystem::Get().ResetRevivedPlayers();
}