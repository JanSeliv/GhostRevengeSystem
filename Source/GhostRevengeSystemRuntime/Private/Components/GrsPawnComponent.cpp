// Copyright (c) Yevhenii Selivanov

#include "Components/GrsPawnComponent.h"

#include "Abilities/GameplayAbilityTypes.h"
#include "Actors/BmrPawn.h"
#include "GrsGameplayTags.h"
#include "SubSystems/GRSWorldSubSystem.h"
#include "Subsystems/BmrGameplayMessageSubsystem.h"

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
}

// A pawn could be loaded/replicated faster than MGF(GFP) is fully loaded therefore waiting for whole module to be initialized is required
void UGrsPawnComponent::OnInitialize(const struct FGameplayEventData& Payload)
{
}
