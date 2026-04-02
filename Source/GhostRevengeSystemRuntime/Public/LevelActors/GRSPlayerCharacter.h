// Copyright (c) Valerii Rotermel & Yevhenii Selivanov

#pragma once

#include "AbilitySystemInterface.h"
#include "ActiveGameplayEffectHandle.h"
#include "Actors/BmrPawn.h"
#include "Components/BmrMapComponent.h"
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStaticsTypes.h"
#include "Net/UnrealNetwork.h"

#include "GRSPlayerCharacter.generated.h"

/**
 * Represents the side of ghost character
 */
UENUM(BlueprintType, DisplayName = "Ghost Character Side")
enum class EGRSCharacterSide : uint8
{
	///< Is not defined
	None,
	///< Star is locked
	Left,
	///< Star is unlocked
	Right,
};

/**
 * Ghost Players (only for players, no AI) whose goal is to perform revenge as ghost (spawned on side of map).
 * Copy the died player mesh and skin.
 */
UCLASS()
class GHOSTREVENGESYSTEMRUNTIME_API AGRSPlayerCharacter : public ACharacter,
                                                          public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	/*********************************************************************************************
	 * Delegates
	 **********************************************************************************************/
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGhostAddedToLevel);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGhostPossesController_Client);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGhostPossesController_Server);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGhostRemovedFromLevel, AController*, CurrentPlayerController, AGRSPlayerCharacter*, GhostPlayerCharacter);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGhostEliminatesPlayer, FVector, AtLocation, AGRSPlayerCharacter*, GhostCharacter);

	/** Is called when a ghost character added to level without possession */
	UPROPERTY(BlueprintCallable, BlueprintAssignable, Transient, Category = "[GhostRevengeSystem]")
	FOnGhostAddedToLevel OnGhostAddedToLevel;

	/** Is called when a ghost character is added to level and possessed a controller on client */
	UPROPERTY(BlueprintCallable, BlueprintAssignable, Transient, Category = "[GhostRevengeSystem]")
	FOnGhostPossesController_Client OnGhostPossesController_Client;

	/** Is called when a ghost character is added to level and possessed a controller on server*/
	UPROPERTY(BlueprintCallable, BlueprintAssignable, Transient, Category = "[GhostRevengeSystem]")
	FOnGhostPossesController_Server OnGhostPossesController_Server;

	/** Is called when a ghost character removed from level */
	UPROPERTY(BlueprintCallable, BlueprintAssignable, Transient, Category = "[GhostRevengeSystem]")
	FOnGhostRemovedFromLevel OnGhostRemovedFromLevel;

	/** Is called when a ghost characters kills another player */
	UPROPERTY(BlueprintCallable, BlueprintAssignable, Transient, Category = "[GhostRevengeSystem]")
	FOnGhostEliminatesPlayer OnGhostEliminatesPlayer;

	/*********************************************************************************************
	 * Initialization
	 **********************************************************************************************/

	/** Sets default values for this character's properties */
	AGRSPlayerCharacter(const FObjectInitializer& ObjectInitializer);

protected:
	/** Set default character parameters such as bCanEverTick, bStartWithTickEnabled, replication etc. */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]")
	void SetDefaultParams();

	/** Initialize skeletal mesh of the character */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]")
	void InitializeSkeletalMesh();

	/** Configure the movement component of the character */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]")
	void MovementComponentConfiguration();

	/** Set up the capsule component of the character */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]")
	void SetupCapsuleComponent();

	/*********************************************************************************************
	 * Nickname component
	 **********************************************************************************************/
public:
	/** Returns the 3D widget component that displays the player name above the character. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "[GhostRevengeSystem]")
	FORCEINLINE class UBmrPlayerNameWidgetComponent* GetPlayerName3DWidgetComponent() const { return PlayerName3DWidgetComponentInternal; }

	/** Sets/Updates player name from possessed pawn player state */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]")
	void UpdatePlayerName(ABmrPlayerState* MyPlayerState);

protected:
	/** 3D widget component that displays the player name above the character. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Transient, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected, DisplayName = "Player Name 3D Widget Component"))
	TObjectPtr<class UBmrPlayerNameWidgetComponent> PlayerName3DWidgetComponentInternal = nullptr;

	/** A GrsPawnComponent that spawned this pawn */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Transient, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected, DisplayName = "Owning Grs Pawn Component"))
	class UGrsPawnComponent* OwningPawnComponent = nullptr;

	/*********************************************************************************************
	 * Main functionality (core loop)
	 **********************************************************************************************/

public:
	friend class UBmrCheatManager;

	/** Basic initialization of the Pawn */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected))
	void InitPawn(int32 NewPlayerId);

	/** Register owning pawn component */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected))
	void RegisterPawnComponent(class UGrsPawnComponent* NewPawnComponent);

protected:
	/** Called when the game starts or when spawned (on spawned on the level) */
	virtual void BeginPlay() override;
	
	/** Returns the Ability System Component from the Player State.
	 * In blueprints, call 'Get Ability System Component' as interface function. */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]")
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	/** Returns properties that are replicated for the lifetime of the actor channel. */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	

	/** The player character could be replicated faster than MGF(GFP) is loaded on client so the only we have to wait/check for subsystem to initialize as it is central loading point */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected))
	void OnInitialize(const struct FGameplayEventData& Payload);

	/** Listen for Player State property bIsDead. It assumes that PlayerState is not respawned on player join and not destroyed on player leave, but is reused for both human and bot characters */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected))
	void OnPlayerDeadChanged(bool bIsDead);

	/** Listen game states to remove ghost character from level */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected))
	void OnGameStateChanged(const struct FGameplayEventData& Payload);

	/** Activates ghost with required initiation  */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected))
	void TryActivateGhostCharacter(AGRSPlayerCharacter* GhostCharacter, ABmrPawn* FromPlayerCharacter);

	/** Called right before owner actor going to remove from the Generated Map, on both server and clients.*/
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected))
	void OnPreRemovedFromLevel(class UBmrMapComponent* MapComponent, class UObject* DestroyCauser);

	/** Remove ghost character from the level */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected))
	void RemoveGhostCharacterFromMap();

	/*********************************************************************************************
	 * Player Character
	 **********************************************************************************************/
protected:
	/** Reference to a player character that was eliminated (original player, not ghost)  */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Transient, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected, DisplayName = "Bmr Player Character"))
	class ABmrPawn* PossessedPlayerCharacter = nullptr;

	/** Player id of related BmrPlayerCharacter */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Transient, ReplicatedUsing = "OnRep_PlayerID", Category = "[GhostRevengeSystem]", meta = (BlueprintProtected, DisplayName = "Id of Bmr Player Character"))
	int32 PlayerID = 0;

public:
	/** Retrieves current possessed character by ghost */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected))
	FORCEINLINE class ABmrPawn* GetPossessedPlayerCharacter(AGRSPlayerCharacter* GhostCharacter) const { return GhostCharacter == this ? PossessedPlayerCharacter : nullptr; }

	/** Called on client when player ID is changed. */
	UFUNCTION()
	void OnRep_PlayerID();

	/*********************************************************************************************
	 * Utils
	 **********************************************************************************************/

	/** Returns the Skeletal Mesh of ghost revenge character. */
	UBmrSkeletalMeshComponent& GetMeshChecked() const;

	/** Set visibility of the player character */
	void SetVisibility(bool Visibility);

	/** Set character visual once added to the level from a refence character (visuals, animations) */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]")
	void SetCharacterVisual(const ABmrPawn* PlayerCharacter);

	/** Set and apply skeletal mesh for ghost player. Copy mesh from current player
	 * @param bForcePlayerSkin If true, will force the bot to change own skin to look like a player. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "[GhostRevengeSystem]")
	void SetPlayerMeshData(bool bForcePlayerSkin = false);

protected:
	/** Possess a player controller */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "[GhostRevengeSystem]")
	void TryPossessController(AController* PlayerController);

	/** Set side for this pawn (left or right) */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "[GhostRevengeSystem]")
	void SetPawnSide();

	/*********************************************************************************************
	 * Aiming
	 **********************************************************************************************/
protected:
	/** Mesh of component. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Transient, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected, DisplayName = "Mesh Component"))
	TObjectPtr<class UMeshComponent> MeshComponentInternal = nullptr;

	/** Spline component used for show the projectile trajectory path */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "[GhostRevengeSystem]")
	class USplineComponent* ProjectileSplineComponentInternal;

	/** Spline component used for show the projectile trajectory path */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "[GhostRevengeSystem]")
	TArray<class USplineMeshComponent*> SplineMeshArrayInternal;

	/** Aiming sphere used when a player aiming*/
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "[GhostRevengeSystem")
	class UStaticMeshComponent* AimingSphereComponent;

	/** Projectile to be spawned once bomb is ready to be thrown */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "[GhostRevengeSystem")
	TObjectPtr<class AGRSBombProjectile> BombProjectileInternal = nullptr;

public:
	/** Add a mesh to the last element of the predict Projectile path results */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected))
	void AddMeshToEndProjectilePath(FVector Location);

	/** Applies spawn bomb gameplay effect */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected))
	void ApplyExplosionGameplayEffect();

	/** Removes spawn bomb gameplay effect */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected))
	void RemoveExplosionGameplayEffect();

	/** Add spline points to the spline component */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected))
	void AddSplinePoints(FPredictProjectilePathResult& Result);

	/** Hide spline elements (trajectory) */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected, AutoCreateRefTerm = "ActionValue"))
	void ClearTrajectorySplines();

	/** Add spline mesh to spline points */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]", meta = (BlueprintProtected))
	void AddSplineMesh(FPredictProjectilePathResult& Result);

	/*********************************************************************************************
	 * Bomb
	 **********************************************************************************************/
public:
	/** Throw projectile event, bound to onetime button press */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]")
	void ThrowProjectile();

	/** Spawn bomb on aiming sphere position. */
	UFUNCTION(BlueprintCallable, Category = "[GhostRevengeSystem]")
	void SpawnBomb(FBmrCell TargetCell);

public:
	/** Clean up the character for the MGF unload */
	void PerformCleanUp();
};
