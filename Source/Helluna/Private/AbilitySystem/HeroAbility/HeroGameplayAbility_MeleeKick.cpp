// Capstone Project Helluna — Melee Kick System

#include "AbilitySystem/HeroAbility/HeroGameplayAbility_MeleeKick.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "AIController.h"

#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "Character/HellunaHeroCharacter.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "HellunaGameplayTags.h"
#include "HellunaFunctionLibrary.h"
#include "Engine/OverlapResult.h"

DEFINE_LOG_CATEGORY(LogMeleeKick);

// ═══════════════════════════════════════════════════════════
// 생성자
// ═══════════════════════════════════════════════════════════

UHeroGameplayAbility_MeleeKick::UHeroGameplayAbility_MeleeKick()
{
	AbilityActivationPolicy = EHellunaAbilityActivationPolicy::OnTriggered;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

// ═══════════════════════════════════════════════════════════
// CanActivateAbility
// ═══════════════════════════════════════════════════════════

bool UHeroGameplayAbility_MeleeKick::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
		return false;

	// 전방에 Staggered 적이 있을 때만 활성화
	AHellunaEnemyCharacter* StaggeredEnemy = FindStaggeredEnemy();
	return StaggeredEnemy != nullptr;
}

// ═══════════════════════════════════════════════════════════
// FindStaggeredEnemy
// ═══════════════════════════════════════════════════════════

AHellunaEnemyCharacter* UHeroGameplayAbility_MeleeKick::FindStaggeredEnemy() const
{
	const AHellunaHeroCharacter* Hero = nullptr;
	if (GetCurrentActorInfo() && GetCurrentActorInfo()->AvatarActor.IsValid())
	{
		Hero = Cast<AHellunaHeroCharacter>(GetCurrentActorInfo()->AvatarActor.Get());
	}
	if (!Hero) return nullptr;

	UWorld* World = Hero->GetWorld();
	if (!World) return nullptr;

	const FVector HeroLocation = Hero->GetActorLocation();
	const FVector HeroForward = Hero->GetActorForwardVector();
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(KickDetectionHalfAngle));

	AHellunaEnemyCharacter* BestEnemy = nullptr;
	float BestDistSq = KickDetectionRange * KickDetectionRange;

	TArray<FOverlapResult> Overlaps;
	FCollisionShape Sphere = FCollisionShape::MakeSphere(KickDetectionRange);
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Hero);

	if (!World->OverlapMultiByObjectType(
		Overlaps, HeroLocation, FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn), Sphere, Params))
	{
		return nullptr;
	}

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(Overlap.GetActor());
		if (!Enemy) continue;

		// Staggered 태그 체크
		if (!UHellunaFunctionLibrary::NativeDoesActorHaveTag(Enemy, HellunaGameplayTags::Enemy_State_Staggered))
			continue;

		// 사망 체크
		if (UHellunaHealthComponent* HC = Enemy->FindComponentByClass<UHellunaHealthComponent>())
		{
			if (HC->IsDead()) continue;
		}

		// 전방각 체크
		const FVector ToEnemy = (Enemy->GetActorLocation() - HeroLocation).GetSafeNormal();
		if (FVector::DotProduct(HeroForward, ToEnemy) < CosHalfAngle)
			continue;

		const float DistSq = FVector::DistSquared(HeroLocation, Enemy->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestEnemy = Enemy;
		}
	}

	return BestEnemy;
}

// ═══════════════════════════════════════════════════════════
// ActivateAbility
// ═══════════════════════════════════════════════════════════

void UHeroGameplayAbility_MeleeKick::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	if (!Hero)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// Staggered 적 찾기
	KickTarget = FindStaggeredEnemy();
	if (!KickTarget)
	{
		UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] ActivateAbility — Staggered 적 없음 → 취소"));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	bKickImpactProcessed = false;

	UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] ActivateAbility — 타겟=%s, 워프거리=%.0f"),
		*KickTarget->GetName(), KickWarpDistance);

	// 워프: 적 앞으로 이동
	{
		const FVector EnemyLoc = KickTarget->GetActorLocation();
		const FVector ToHero = (Hero->GetActorLocation() - EnemyLoc).GetSafeNormal();
		FVector WarpLoc = EnemyLoc + ToHero * KickWarpDistance;
		WarpLoc.Z = Hero->GetActorLocation().Z; // Z 유지

		Hero->SetActorLocation(WarpLoc, false, nullptr, ETeleportType::TeleportPhysics);

		// 적 방향으로 회전
		const FRotator LookRot = (EnemyLoc - WarpLoc).Rotation();
		Hero->SetActorRotation(FRotator(0.f, LookRot.Yaw, 0.f));

		if (APlayerController* PC = Cast<APlayerController>(Hero->GetController()))
		{
			FRotator Ctrl = PC->GetControlRotation();
			Ctrl.Yaw = LookRot.Yaw;
			PC->SetControlRotation(Ctrl);
		}
	}

	// Kicking 태그 부여
	if (UHellunaAbilitySystemComponent* HeroASC = Hero->GetHellunaAbilitySystemComponent())
	{
		HeroASC->AddStateTag(HellunaGameplayTags::Hero_State_Kicking);
	}

	// 몽타주 재생
	if (!KickMontage)
	{
		UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] KickMontage=nullptr → 취소"));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, KickMontage, 1.0f);

	if (MontageTask)
	{
		MontageTask->OnCompleted.AddDynamic(this, &ThisClass::OnKickMontageCompleted);
		MontageTask->OnBlendOut.AddDynamic(this, &ThisClass::OnKickMontageCompleted);
		MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::OnKickMontageInterrupted);
		MontageTask->OnCancelled.AddDynamic(this, &ThisClass::OnKickMontageInterrupted);
		MontageTask->ReadyForActivation();
	}

	// Event.Kick.Impact 대기
	UAbilityTask_WaitGameplayEvent* EventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, HellunaGameplayTags::Event_Kick_Impact, nullptr, false, true);

	if (EventTask)
	{
		EventTask->EventReceived.AddDynamic(this, &ThisClass::OnKickImpactEvent);
		EventTask->ReadyForActivation();
	}
}

// ═══════════════════════════════════════════════════════════
// OnKickImpactEvent — 노티파이 시점
// ═══════════════════════════════════════════════════════════

void UHeroGameplayAbility_MeleeKick::OnKickImpactEvent(FGameplayEventData Payload)
{
	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	if (!Hero || !Hero->HasAuthority() || bKickImpactProcessed) return;
	bKickImpactProcessed = true;

	AHellunaEnemyCharacter* Target = KickTarget.Get();

	UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] KickImpact — 타겟=%s, Frame=%llu"),
		Target ? *Target->GetName() : TEXT("nullptr"), GFrameCounter);

	// 1. 타겟 데미지
	if (Target && KickDamage > 0.f)
	{
		if (UHellunaHealthComponent* HC = Target->FindComponentByClass<UHellunaHealthComponent>())
		{
			const float NewHP = FMath::Max(0.f, HC->GetHealth() - KickDamage);
			HC->SetHealth(NewHP);
			UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] 타겟 데미지 %.0f → HP=%.1f"), KickDamage, NewHP);
		}
	}

	// 2. 타겟 Staggered 해제 + 넉다운
	if (Target)
	{
		if (UHellunaAbilitySystemComponent* TargetASC = Target->GetHellunaAbilitySystemComponent())
		{
			TargetASC->RemoveStateTag(HellunaGameplayTags::Enemy_State_Staggered);
		}

		FVector KnockDir = (Target->GetActorLocation() - Hero->GetActorLocation()).GetSafeNormal();
		KnockDir.Z = 0.5f;
		KnockDir.Normalize();
		Target->LaunchCharacter(KnockDir * KickAOEKnockback * 1.5f, true, false);
	}

	// 3. AOE: 주변 적 넉백 + Staggered 연쇄
	if (KickAOERadius > 0.f)
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionQueryParams Params;
		Params.AddIgnoredActor(Hero);
		if (Target) Params.AddIgnoredActor(Target);

		Hero->GetWorld()->OverlapMultiByChannel(
			Overlaps,
			Hero->GetActorLocation(),
			FQuat::Identity,
			ECC_Pawn,
			FCollisionShape::MakeSphere(KickAOERadius),
			Params
		);

		int32 AOECount = 0;
		for (const FOverlapResult& Overlap : Overlaps)
		{
			AHellunaEnemyCharacter* NearbyEnemy = Cast<AHellunaEnemyCharacter>(Overlap.GetActor());
			if (!NearbyEnemy) continue;

			if (UHellunaHealthComponent* HC = NearbyEnemy->FindComponentByClass<UHellunaHealthComponent>())
			{
				if (HC->IsDead()) continue;

				// AOE 데미지
				if (KickAOEDamage > 0.f)
				{
					const float NewHP = FMath::Max(0.f, HC->GetHealth() - KickAOEDamage);
					HC->SetHealth(NewHP);
				}
			}

			// AOE 넉백
			FVector KnockDir = (NearbyEnemy->GetActorLocation() - Hero->GetActorLocation()).GetSafeNormal();
			KnockDir.Z = 0.3f;
			KnockDir.Normalize();
			NearbyEnemy->LaunchCharacter(KnockDir * KickAOEKnockback, true, false);

			// 연쇄 Staggered 부여
			if (KickAOEStaggerDuration > 0.f)
			{
				if (UHellunaAbilitySystemComponent* NearbyASC = NearbyEnemy->GetHellunaAbilitySystemComponent())
				{
					NearbyASC->AddStateTag(HellunaGameplayTags::Enemy_State_Staggered);
					AOECount++;

					FTimerHandle StaggerTimer;
					TWeakObjectPtr<AHellunaEnemyCharacter> WeakEnemy = NearbyEnemy;
					const float Duration = KickAOEStaggerDuration;
					Hero->GetWorld()->GetTimerManager().SetTimer(StaggerTimer,
						FTimerDelegate::CreateWeakLambda(NearbyEnemy, [WeakEnemy]()
						{
							if (WeakEnemy.IsValid())
							{
								if (UHellunaAbilitySystemComponent* ASC = WeakEnemy->GetHellunaAbilitySystemComponent())
								{
									ASC->RemoveStateTag(HellunaGameplayTags::Enemy_State_Staggered);
								}
							}
						}), Duration, false);
				}
			}
		}

		if (AOECount > 0)
		{
			UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] AOE 넉백 — 주변 적 %d마리, 반경=%.0f, 강도=%.0f"),
				AOECount, KickAOERadius, KickAOEKnockback);
			UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] AOE Staggered 부여 — %d마리 (연쇄 가능!)"), AOECount);
		}
	}

	// 4. 카메라 셰이크
	if (KickCameraShake && Hero->IsLocallyControlled())
	{
		if (APlayerController* PC = Cast<APlayerController>(Hero->GetController()))
		{
			PC->ClientStartCameraShake(KickCameraShake, KickShakeScale);
		}
	}
}

// ═══════════════════════════════════════════════════════════
// 몽타주 콜백
// ═══════════════════════════════════════════════════════════

void UHeroGameplayAbility_MeleeKick::OnKickMontageCompleted()
{
	UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] 몽타주 완료"));
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
}

void UHeroGameplayAbility_MeleeKick::OnKickMontageInterrupted()
{
	UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] 몽타주 중단"));
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
}

// ═══════════════════════════════════════════════════════════
// EndAbility
// ═══════════════════════════════════════════════════════════

void UHeroGameplayAbility_MeleeKick::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// Kicking 태그 해제
	if (ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		if (AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(ActorInfo->AvatarActor.Get()))
		{
			if (UHellunaAbilitySystemComponent* HeroASC = Hero->GetHellunaAbilitySystemComponent())
			{
				HeroASC->RemoveStateTag(HellunaGameplayTags::Hero_State_Kicking);
			}
		}
	}

	KickTarget = nullptr;
	bKickImpactProcessed = false;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
