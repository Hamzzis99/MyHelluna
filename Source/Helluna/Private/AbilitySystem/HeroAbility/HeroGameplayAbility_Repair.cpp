// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/HeroAbility/HeroGameplayAbility_Repair.h"
#include "GameMode/HellunaDefenseGameState.h"
#include "Character/HellunaHeroCharacter.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "Component/RepairComponent.h"
#include "Widgets/RepairWidget.h"
#include "Blueprint/UserWidget.h"
#include "InventoryManagement/Utils/Inv_InventoryStatics.h"

#include "DebugHelper.h"
#include "Helluna.h"

void UHeroGameplayAbility_Repair::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	Repair(ActorInfo);

	EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
}

void UHeroGameplayAbility_Repair::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UHeroGameplayAbility_Repair::Repair(const FGameplayAbilityActorInfo* ActorInfo)
{
	AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(ActorInfo->AvatarActor.Get());
	if (!Hero) return;

	if (!Hero->IsLocallyControlled()) return;

	// F키 토글: Widget이 이미 열려있으면 닫기
	if (CurrentWidget && CurrentWidget->IsInViewport())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Repair] Widget already open, closing..."));
		if (URepairWidget* RepairWidget = Cast<URepairWidget>(CurrentWidget))
		{
			RepairWidget->CloseWidget();
		}
		else
		{
			CurrentWidget->RemoveFromParent();
		}
		CurrentWidget = nullptr;
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[Repair] Opening widget..."));

	AHellunaDefenseGameState* GS = GetWorld()->GetGameState<AHellunaDefenseGameState>();
	if (!GS) return;

	AResourceUsingObject_SpaceShip* Ship = GS->GetSpaceShip();
	if (!Ship) return;

	URepairComponent* RepairComp = Ship->FindComponentByClass<URepairComponent>();
	if (!RepairComp)
	{
		UE_LOG(LogTemp, Error, TEXT("[Repair] RepairComponent not found!"));
		return;
	}

	APlayerController* PC = Hero->GetController<APlayerController>();
	if (!PC) return;

	UInv_InventoryComponent* InvComp = UInv_InventoryStatics::GetInventoryComponent(PC);
	if (!InvComp) return;

	if (!RepairMaterialWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[Repair] RepairMaterialWidgetClass not set!"));
		return;
	}

	CurrentWidget = CreateWidget<UUserWidget>(PC, RepairMaterialWidgetClass);
	if (!CurrentWidget) return;

	// RepairWidget이면 InitializeWidget 호출
	if (URepairWidget* RepairWidget = Cast<URepairWidget>(CurrentWidget))
	{
		RepairWidget->InitializeWidget(RepairComp, InvComp);
	}

	CurrentWidget->AddToViewport(100);

	PC->FlushPressedKeys();
	PC->SetInputMode(FInputModeUIOnly());
	PC->bShowMouseCursor = true;

	UE_LOG(LogTemp, Warning, TEXT("[Repair] Widget created and added to viewport."));
}
