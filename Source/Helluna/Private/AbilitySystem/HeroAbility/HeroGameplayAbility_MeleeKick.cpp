// Capstone Project Helluna — Melee Kick System

#include "AbilitySystem/HeroAbility/HeroGameplayAbility_MeleeKick.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
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

	// 점프/공중 상태에서는 발차기 불가
	if (ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		if (const ACharacter* Char = Cast<ACharacter>(ActorInfo->AvatarActor.Get()))
		{
			if (const UCharacterMovementComponent* CMC = Char->GetCharacterMovement())
			{
				if (CMC->IsFalling()) return false;
			}
		}
	}

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

	// Kicking + Invincible 태그 부여 (킥 중 피격 방지)
	if (UHellunaAbilitySystemComponent* HeroASC = Hero->GetHellunaAbilitySystemComponent())
	{
		HeroASC->AddStateTag(HellunaGameplayTags::Hero_State_Kicking);
		HeroASC->AddStateTag(HellunaGameplayTags::Player_State_Invincible);
		UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] Kicking + Invincible 태그 부여"));
	}

	// 킥 중 완전 잠금 (떨림 방지)
	if (UCharacterMovementComponent* CMC = Hero->GetCharacterMovement())
	{
		CMC->StopMovementImmediately();  // Velocity 즉시 0
		CMC->DisableMovement();           // MOVE_None → 중력+이동 OFF
		bSavedOrientRotation = CMC->bOrientRotationToMovement;
		CMC->bOrientRotationToMovement = false;  // CMC 회전 OFF
		UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] 이동+회전 완전 잠금"));
	}
	bSavedUseControllerYaw = Hero->bUseControllerRotationYaw;
	Hero->bUseControllerRotationYaw = false;  // 컨트롤러 회전 OFF
	Hero->LockMoveInput();  // 입력 차단

	// === 카메라 연출 시작 (CLIENT만) ===
	if (Hero->IsLocallyControlled())
	{
		if (USpringArmComponent* SpringArm = Hero->FindComponentByClass<USpringArmComponent>())
		{
			SavedKickArmLength = SpringArm->TargetArmLength;
			SavedKickSocketOffset = SpringArm->SocketOffset;
		}
		if (APlayerController* PC = Cast<APlayerController>(Hero->GetController()))
		{
			if (PC->PlayerCameraManager)
			{
				SavedKickFOV = PC->PlayerCameraManager->GetFOVAngle();
			}
		}

		const float TargetArmLength = SavedKickArmLength * KickArmLengthMultiplier;
		const float TargetFOV = SavedKickFOV * KickFOVMultiplier;
		const FVector TargetSocketOffset = SavedKickSocketOffset + KickCameraSocketOffset;

		bKickCameraActive = true;

		UWorld* World = Hero->GetWorld();
		World->GetTimerManager().SetTimer(KickCameraTickTimer,
			FTimerDelegate::CreateWeakLambda(Hero,
			[this, Hero, TargetArmLength, TargetFOV, TargetSocketOffset]()
			{
				if (!bKickCameraActive || !Hero) { return; }

				float DeltaTime = Hero->GetWorld()->GetDeltaSeconds();
				USpringArmComponent* SA = Hero->FindComponentByClass<USpringArmComponent>();
				APlayerController* PC = Cast<APlayerController>(Hero->GetController());

				if (SA)
				{
					SA->TargetArmLength = FMath::FInterpTo(SA->TargetArmLength, TargetArmLength, DeltaTime, KickCameraInterpSpeed);
					SA->SocketOffset = FMath::VInterpTo(SA->SocketOffset, TargetSocketOffset, DeltaTime, KickCameraInterpSpeed);
				}
				if (PC && PC->PlayerCameraManager)
				{
					float CurrentFOV = PC->PlayerCameraManager->GetFOVAngle();
					PC->PlayerCameraManager->SetFOV(FMath::FInterpTo(CurrentFOV, TargetFOV, DeltaTime, KickCameraInterpSpeed));
				}
			}), 0.016f, true);

		UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] 카메라 진입 — ArmLength=%.0f→%.0f, FOV=%.0f→%.0f, SocketOffset=%s"),
			SavedKickArmLength, TargetArmLength, SavedKickFOV, TargetFOV, *KickCameraSocketOffset.ToString());
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

	// FullBody 몽타주 재생 (ABP에서 전신 오버라이드)
	Hero->PlayFullBody = true;
	UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] Hero.PlayFullBody = true"));

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
	// 취소 시 카메라 즉시 원복
	if (bWasCancelled && bKickCameraActive && ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		AHellunaHeroCharacter* CamHero = Cast<AHellunaHeroCharacter>(ActorInfo->AvatarActor.Get());
		if (CamHero && CamHero->IsLocallyControlled())
		{
			CamHero->GetWorld()->GetTimerManager().ClearTimer(KickCameraTickTimer);
			CamHero->GetWorld()->GetTimerManager().ClearTimer(KickCameraReturnTimer);
			if (USpringArmComponent* SA = CamHero->FindComponentByClass<USpringArmComponent>())
			{
				SA->TargetArmLength = SavedKickArmLength;
				SA->SocketOffset = SavedKickSocketOffset;
			}
			if (APlayerController* PC = Cast<APlayerController>(CamHero->GetController()))
			{
				if (PC->PlayerCameraManager) PC->PlayerCameraManager->SetFOV(SavedKickFOV);
			}
			UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] 카메라 취소 즉시 원복"));
		}
		bKickCameraActive = false;
	}

	// 정상 종료 시 카메라 복귀 (CLIENT만)
	if (!bWasCancelled && ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		AHellunaHeroCharacter* CamHero = Cast<AHellunaHeroCharacter>(ActorInfo->AvatarActor.Get());
		if (CamHero && CamHero->IsLocallyControlled() && bKickCameraActive)
		{
			CamHero->GetWorld()->GetTimerManager().ClearTimer(KickCameraTickTimer);

			const float ReturnArmLength = SavedKickArmLength;
			const float ReturnFOV = SavedKickFOV;
			const FVector ReturnSocketOffset = SavedKickSocketOffset;
			const float ReturnSpeed = KickCameraReturnSpeed;

			FTimerHandle DelayHandle;
			CamHero->GetWorld()->GetTimerManager().SetTimer(DelayHandle,
				FTimerDelegate::CreateWeakLambda(CamHero, [this, CamHero, ReturnArmLength, ReturnFOV, ReturnSocketOffset, ReturnSpeed]()
				{
					CamHero->GetWorld()->GetTimerManager().SetTimer(KickCameraReturnTimer,
						FTimerDelegate::CreateWeakLambda(CamHero, [this, CamHero, ReturnArmLength, ReturnFOV, ReturnSocketOffset, ReturnSpeed]()
						{
							float DT = CamHero->GetWorld()->GetDeltaSeconds();
							USpringArmComponent* SA = CamHero->FindComponentByClass<USpringArmComponent>();
							APlayerController* PC = Cast<APlayerController>(CamHero->GetController());
							bool bDone = true;

							if (SA)
							{
								SA->TargetArmLength = FMath::FInterpTo(SA->TargetArmLength, ReturnArmLength, DT, ReturnSpeed);
								SA->SocketOffset = FMath::VInterpTo(SA->SocketOffset, ReturnSocketOffset, DT, ReturnSpeed);
								if (!FMath::IsNearlyEqual(SA->TargetArmLength, ReturnArmLength, 1.f)) bDone = false;
							}
							if (PC && PC->PlayerCameraManager)
							{
								float Cur = PC->PlayerCameraManager->GetFOVAngle();
								PC->PlayerCameraManager->SetFOV(FMath::FInterpTo(Cur, ReturnFOV, DT, ReturnSpeed));
								if (!FMath::IsNearlyEqual(Cur, ReturnFOV, 1.f)) bDone = false;
							}

							if (bDone)
							{
								if (SA)
								{
									SA->TargetArmLength = ReturnArmLength;
									SA->SocketOffset = ReturnSocketOffset;
								}
								if (PC && PC->PlayerCameraManager) PC->PlayerCameraManager->SetFOV(ReturnFOV);
								CamHero->GetWorld()->GetTimerManager().ClearTimer(KickCameraReturnTimer);
								UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] 카메라 복귀 완료 — ArmLength=%.0f, FOV=%.0f"),
									ReturnArmLength, ReturnFOV);
							}
						}), 0.016f, true);
				}), KickCameraReturnDelay, false);

			UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] 카메라 복귀 시작 — 딜레이=%.2f초"), KickCameraReturnDelay);
			bKickCameraActive = false;
		}
	}

	// Kicking 태그 해제
	if (ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		if (AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(ActorInfo->AvatarActor.Get()))
		{
			// FullBody 몽타주 해제
			Hero->PlayFullBody = false;
			UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] Hero.PlayFullBody = false"));

			// 이동+회전 완전 복원
			if (UCharacterMovementComponent* CMC = Hero->GetCharacterMovement())
			{
				CMC->SetMovementMode(MOVE_Walking);
				CMC->bOrientRotationToMovement = bSavedOrientRotation;
			}
			Hero->bUseControllerRotationYaw = bSavedUseControllerYaw;
			Hero->UnlockMoveInput();
			UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] 이동+회전 잠금 해제"));

			if (UHellunaAbilitySystemComponent* HeroASC = Hero->GetHellunaAbilitySystemComponent())
			{
				HeroASC->RemoveStateTag(HellunaGameplayTags::Hero_State_Kicking);
				HeroASC->RemoveStateTag(HellunaGameplayTags::Player_State_Invincible);
				UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] Kicking + Invincible 태그 해제"));
			}
		}
	}

	KickTarget = nullptr;
	bKickImpactProcessed = false;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
