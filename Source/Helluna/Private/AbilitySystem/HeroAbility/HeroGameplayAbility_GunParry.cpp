// Capstone Project Helluna — Gun Parry System

#include "AbilitySystem/HeroAbility/HeroGameplayAbility_GunParry.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MotionWarpingComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"

#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "Character/HellunaHeroCharacter.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Weapon/HeroWeapon_GunBase.h"
#include "HellunaGameplayTags.h"
#include "HellunaFunctionLibrary.h"
#include "DebugHelper.h"

#include "Kismet/KismetSystemLibrary.h"
#include "Engine/OverlapResult.h"
#include "GameMode/HellunaDefenseGameMode.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogGunParry, Log, All);

// ═══════════════════════════════════════════════════════════
// 생성자
// ═══════════════════════════════════════════════════════════

UHeroGameplayAbility_GunParry::UHeroGameplayAbility_GunParry()
{
	AbilityActivationPolicy = EHellunaAbilityActivationPolicy::OnTriggered;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	SetAssetTags(FGameplayTagContainer(HellunaGameplayTags::Player_Ability_GunParry));
}

// ═══════════════════════════════════════════════════════════
// Static 헬퍼 — 팀원 코드 간접 참조용
// ═══════════════════════════════════════════════════════════

bool UHeroGameplayAbility_GunParry::ShouldBlockDamage(const AActor* Target)
{
	if (!Target) return false;
	AActor* MutableTarget = const_cast<AActor*>(Target);
	return UHellunaFunctionLibrary::NativeDoesActorHaveTag(MutableTarget, HellunaGameplayTags::Player_State_Invincible)
		|| UHellunaFunctionLibrary::NativeDoesActorHaveTag(MutableTarget, HellunaGameplayTags::Player_State_PostParryInvincible);
}

bool UHeroGameplayAbility_GunParry::ShouldBlockHitReact(const AActor* Target)
{
	if (!Target) return false;
	AActor* MutableTarget = const_cast<AActor*>(Target);
	if (UHellunaFunctionLibrary::NativeDoesActorHaveTag(MutableTarget, HellunaGameplayTags::Enemy_State_AnimLocked))
		return true;
	if (UHellunaFunctionLibrary::NativeDoesActorHaveTag(MutableTarget, HellunaGameplayTags::Player_State_Invincible))
		return true;
	return false;
}

bool UHeroGameplayAbility_GunParry::ShouldDeferDeath(const AActor* Enemy)
{
	if (!Enemy) return false;
	return UHellunaFunctionLibrary::NativeDoesActorHaveTag(const_cast<AActor*>(Enemy), HellunaGameplayTags::Enemy_State_AnimLocked);
}

bool UHeroGameplayAbility_GunParry::TryParryInstead(UHellunaAbilitySystemComponent* ASC, const AHeroWeapon_GunBase* Weapon)
{
	if (!ASC || !Weapon) return false;
	if (Weapon->FireMode == EWeaponFireMode::FullAuto) return false;
	if (!Weapon->bCanParry) return false;

	const AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(ASC->GetAvatarActor());
	if (!Hero) return false;

	if (!FindParryableEnemyStatic(Hero))
	{
		UE_LOG(LogGunParry, Verbose, TEXT("[TryParryInstead] FindParryableEnemyStatic → nullptr, 패링 스킵"));
		return false;
	}

	UE_LOG(LogGunParry, Warning, TEXT("[TryParryInstead] 패링 가능한 적 발견! TryActivateAbilityByTag 호출"));
	return ASC->TryActivateAbilityByTag(HellunaGameplayTags::Player_Ability_GunParry);
}

// ═══════════════════════════════════════════════════════════
// CanActivateAbility
// ═══════════════════════════════════════════════════════════

bool UHeroGameplayAbility_GunParry::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		UE_LOG(LogGunParry, Warning, TEXT("[CanActivate] Super::CanActivateAbility FAILED"));
		return false;
	}

	if (!ActorInfo || !ActorInfo->AvatarActor.IsValid())
	{
		UE_LOG(LogGunParry, Warning, TEXT("[CanActivate] ActorInfo 또는 AvatarActor 없음"));
		return false;
	}

	const AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(ActorInfo->AvatarActor.Get());
	if (!Hero) { UE_LOG(LogGunParry, Warning, TEXT("[CanActivate] Hero 캐스트 실패")); return false; }

	const AHeroWeapon_GunBase* Weapon = Cast<AHeroWeapon_GunBase>(Hero->GetCurrentWeapon());
	if (!Weapon) { UE_LOG(LogGunParry, Warning, TEXT("[CanActivate] Weapon 없음")); return false; }
	if (!Weapon->bCanParry) { UE_LOG(LogGunParry, Warning, TEXT("[CanActivate] bCanParry=false")); return false; }
	if (!Weapon->CanFire()) { UE_LOG(LogGunParry, Warning, TEXT("[CanActivate] CanFire=false (탄약 없음)")); return false; }

	AHellunaEnemyCharacter* FoundEnemy = FindParryableEnemy(Hero);
	UE_LOG(LogGunParry, Warning, TEXT("[CanActivate] FindParryableEnemy → %s"),
		FoundEnemy ? *FoundEnemy->GetName() : TEXT("nullptr"));
	return FoundEnemy != nullptr;
}

// ═══════════════════════════════════════════════════════════
// ActivateAbility
// ═══════════════════════════════════════════════════════════

void UHeroGameplayAbility_GunParry::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	UE_LOG(LogGunParry, Warning, TEXT("══════════════════════════════════════════"));
	UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] 시작 — Authority=%s, ActivationMode=%d"),
		(ActorInfo && ActorInfo->AvatarActor.IsValid() && ActorInfo->AvatarActor->HasAuthority()) ? TEXT("SERVER") : TEXT("CLIENT"),
		(int32)ActivationInfo.ActivationMode);

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	if (!Hero)
	{
		UE_LOG(LogGunParry, Error, TEXT("[ActivateAbility] Hero nullptr → EndAbility"));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 서버 Authority에서만 핵심 로직 실행
	if (!Hero->HasAuthority())
	{
		UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] CLIENT — 카메라 연출만 시작"));
		BeginCameraEffect(Hero);
		return;
	}

	UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] SERVER — 핵심 로직 실행"));

	// 무기 확인 + 탄약 소모
	AHeroWeapon_GunBase* Weapon = Cast<AHeroWeapon_GunBase>(Hero->GetCurrentWeapon());
	if (!Weapon || !Weapon->CanFire())
	{
		UE_LOG(LogGunParry, Error, TEXT("[ActivateAbility] Weapon 없음 또는 CanFire=false → EndAbility"));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 패링 대상 찾기
	AHellunaEnemyCharacter* Enemy = FindParryableEnemy(Hero);
	if (!Enemy)
	{
		UE_LOG(LogGunParry, Error, TEXT("[ActivateAbility] FindParryableEnemy → nullptr → EndAbility"));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ParryTarget = Enemy;
	UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] 타겟: %s"), *Enemy->GetName());

	// 1) 탄약 1발 소모
	Weapon->CurrentMag = FMath::Max(Weapon->CurrentMag - 1, 0);
	Weapon->BroadcastAmmoChanged();
	UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] 탄약 소모 → 남은 탄: %d"), Weapon->CurrentMag);

	// 2) 적 ASC에 AnimLocked 태그 부여
	if (UHellunaAbilitySystemComponent* EnemyASC = Enemy->GetHellunaAbilitySystemComponent())
	{
		EnemyASC->AddStateTag(HellunaGameplayTags::Enemy_State_AnimLocked);
		UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] Enemy AnimLocked 태그 부여"));
	}

	// 3) 플레이어 ASC에 무적 + 처형 태그 부여
	if (UHellunaAbilitySystemComponent* HeroASC = Hero->GetHellunaAbilitySystemComponent())
	{
		HeroASC->AddStateTag(HellunaGameplayTags::Player_State_Invincible);
		HeroASC->AddStateTag(HellunaGameplayTags::Player_State_ParryExecution);
		UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] Hero Invincible+ParryExecution 태그 부여"));
	}

	// 4) 적 이동 잠금
	Enemy->LockMovementAndFaceTarget(Hero);
	UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] Enemy 이동 잠금"));

	// 5) Motion Warping
	if (UMotionWarpingComponent* WarpComp = Hero->FindComponentByClass<UMotionWarpingComponent>())
	{
		const FVector EnemyForward = Enemy->GetActorForwardVector();
		const FVector EnemyLocation = Enemy->GetActorLocation();
		const FVector OffsetDir = EnemyForward.RotateAngleAxis(WarpAngleOffset, FVector::UpVector);
		const FVector WarpLocation = EnemyLocation + OffsetDir * ExecutionDistance;
		const FRotator WarpRotation = bFaceEnemyAfterWarp
			? (EnemyLocation - WarpLocation).Rotation()
			: Hero->GetActorRotation();

		FMotionWarpingTarget WarpTarget;
		WarpTarget.Name = WarpTargetName;
		WarpTarget.Location = WarpLocation;
		WarpTarget.Rotation = WarpRotation;
		WarpComp->AddOrUpdateWarpTarget(WarpTarget);
		UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] MotionWarp 설정 — Offset=%.0f도, Loc=%s, Rot=%s"),
			WarpAngleOffset, *WarpLocation.ToString(), *WarpRotation.ToString());
	}
	else
	{
		UE_LOG(LogGunParry, Error, TEXT("[ActivateAbility] MotionWarpingComponent 없음!"));
	}

	// 6) 처형 몽타주 재생 (무기의 ParryExecutionMontage)
	UAnimMontage* ExecutionMontage = Weapon->ParryExecutionMontage;
	UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] ExecutionMontage=%s"),
		ExecutionMontage ? *ExecutionMontage->GetName() : TEXT("NULL"));

	if (!ExecutionMontage)
	{
		UE_LOG(LogGunParry, Error, TEXT("[ActivateAbility] ExecutionMontage NULL → HandleExecutionFinished(true)"));
		HandleExecutionFinished(true);
		return;
	}

	// 스켈레톤 일치 확인 (디버그)
	if (USkeletalMeshComponent* HeroMesh = Hero->GetMesh())
	{
		UAnimInstance* AnimInst = HeroMesh->GetAnimInstance();
		USkeleton* MontageSkel = ExecutionMontage->GetSkeleton();
		USkeleton* CharSkel = AnimInst ? AnimInst->CurrentSkeleton : nullptr;
		UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] MontageSkeleton=%s"),
			MontageSkel ? *MontageSkel->GetPathName() : TEXT("NULL"));
		UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] CharSkeleton=%s"),
			CharSkel ? *CharSkel->GetPathName() : TEXT("NULL"));
		UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] Skeleton Match=%s"),
			(MontageSkel == CharSkel) ? TEXT("YES") : TEXT("NO"));

		// 현재 재생 중인 몽타주 확인
		if (AnimInst)
		{
			UAnimMontage* ActiveMontage = AnimInst->GetCurrentActiveMontage();
			UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] 현재 활성 몽타주=%s"),
				ActiveMontage ? *ActiveMontage->GetName() : TEXT("없음"));
		}
	}

	ExecutionMontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, ExecutionMontage, 1.f);

	UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] MontageTask 생성=%s"),
		ExecutionMontageTask ? TEXT("SUCCESS") : TEXT("FAILED"));

	if (!ExecutionMontageTask)
	{
		UE_LOG(LogGunParry, Error, TEXT("[ActivateAbility] MontageTask 생성 실패 → HandleExecutionFinished(true)"));
		HandleExecutionFinished(true);
		return;
	}

	ExecutionMontageTask->OnCompleted.AddDynamic(this, &ThisClass::OnExecutionMontageCompleted);
	ExecutionMontageTask->OnBlendOut.AddDynamic(this, &ThisClass::OnExecutionMontageCompleted);
	ExecutionMontageTask->OnInterrupted.AddDynamic(this, &ThisClass::OnExecutionMontageInterrupted);
	ExecutionMontageTask->OnCancelled.AddDynamic(this, &ThisClass::OnExecutionMontageInterrupted);
	ExecutionMontageTask->ReadyForActivation();
	UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] MontageTask ReadyForActivation 완료"));

	// 7) 적 처형 피격 몽타주 (Multicast)
	Enemy->Multicast_PlayParryVictim();
	UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] Enemy Multicast_PlayParryVictim 호출"));

	// 8) 카메라 연출 (로컬만)
	BeginCameraEffect(Hero);
	UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] BeginCameraEffect 호출"));

	// 9) 플레이어 이동/시점 잠금
	Hero->LockMoveInput();
	Hero->LockLookInput();
	UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] Hero 이동/시점 잠금 완료"));
	UE_LOG(LogGunParry, Warning, TEXT("══════════════════════════════════════════"));
}

// ═══════════════════════════════════════════════════════════
// 몽타주 콜백
// ═══════════════════════════════════════════════════════════

void UHeroGameplayAbility_GunParry::OnExecutionMontageCompleted()
{
	UE_LOG(LogGunParry, Warning, TEXT("[MontageCallback] OnCompleted 호출"));
	HandleExecutionFinished(false);
}

void UHeroGameplayAbility_GunParry::OnExecutionMontageInterrupted()
{
	UE_LOG(LogGunParry, Warning, TEXT("[MontageCallback] OnInterrupted 호출"));
	HandleExecutionFinished(true);
}

// ═══════════════════════════════════════════════════════════
// 처형 종료 — 사망 처리 + 넉백 + 태그 정리
// ═══════════════════════════════════════════════════════════

void UHeroGameplayAbility_GunParry::HandleExecutionFinished(bool bWasCancelled)
{
	UE_LOG(LogGunParry, Warning, TEXT("══════════════════════════════════════════"));
	UE_LOG(LogGunParry, Warning, TEXT("[HandleExecutionFinished] 시작 — bCancelled=%d"), bWasCancelled);

	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	AHellunaEnemyCharacter* Enemy = ParryTarget.IsValid() ? ParryTarget.Get() : nullptr;

	UE_LOG(LogGunParry, Warning, TEXT("[HandleExecutionFinished] Hero=%s, Enemy=%s"),
		Hero ? *Hero->GetName() : TEXT("nullptr"),
		Enemy ? *Enemy->GetName() : TEXT("nullptr"));

	// --- 서버: 적 사망 처리 ---
	if (Hero && Hero->HasAuthority() && Enemy)
	{
		if (UHellunaHealthComponent* HealthComp = Enemy->FindComponentByClass<UHellunaHealthComponent>())
		{
			UE_LOG(LogGunParry, Warning, TEXT("[HandleExecutionFinished] 적 HP=%.1f → 0으로 설정"), HealthComp->GetHealth());
			HealthComp->SetHealth(0.f);
		}

		if (UWorld* World = Hero->GetWorld())
		{
			if (AGameModeBase* GM = World->GetAuthGameMode())
			{
				if (auto* DefenseGM = Cast<AHellunaDefenseGameMode>(GM))
				{
					DefenseGM->NotifyMonsterDied(Enemy);
					UE_LOG(LogGunParry, Warning, TEXT("[HandleExecutionFinished] NotifyMonsterDied 호출"));
				}
			}
		}

		Enemy->DespawnMassEntityOnServer(TEXT("GunParry"));
		Enemy->SetLifeSpan(0.5f);
		UE_LOG(LogGunParry, Warning, TEXT("[HandleExecutionFinished] DespawnMassEntity + SetLifeSpan(0.5)"));
	}

	// --- 적 AnimLocked 해제 ---
	if (Enemy)
	{
		if (UHellunaAbilitySystemComponent* EnemyASC = Enemy->GetHellunaAbilitySystemComponent())
		{
			EnemyASC->RemoveStateTag(HellunaGameplayTags::Enemy_State_AnimLocked);
		}
		Enemy->UnlockMovement();
		UE_LOG(LogGunParry, Warning, TEXT("[HandleExecutionFinished] Enemy AnimLocked 해제 + UnlockMovement"));
	}

	// --- 플레이어 태그 정리 + 넉백 ---
	if (Hero)
	{
		if (UHellunaAbilitySystemComponent* HeroASC = Hero->GetHellunaAbilitySystemComponent())
		{
			HeroASC->RemoveStateTag(HellunaGameplayTags::Player_State_ParryExecution);
			HeroASC->RemoveStateTag(HellunaGameplayTags::Player_State_Invincible);

			HeroASC->AddStateTag(HellunaGameplayTags::Player_State_PostParryInvincible);

			if (UWorld* World = Hero->GetWorld())
			{
				World->GetTimerManager().SetTimer(
					PostInvincibleTimerHandle,
					[WeakASC = TWeakObjectPtr<UHellunaAbilitySystemComponent>(HeroASC)]()
					{
						if (WeakASC.IsValid())
						{
							WeakASC->RemoveStateTag(HellunaGameplayTags::Player_State_PostParryInvincible);
						}
					},
					PostParryInvincibleDuration,
					false
				);
			}
			UE_LOG(LogGunParry, Warning, TEXT("[HandleExecutionFinished] Hero 태그 정리 + PostParryInvincible(%.1f초)"), PostParryInvincibleDuration);
		}

		if (Hero->HasAuthority() && Enemy)
		{
			FVector KnockbackDir = (Hero->GetActorLocation() - Enemy->GetActorLocation()).GetSafeNormal();
			KnockbackDir.Z = 0.2f;
			KnockbackDir.Normalize();
			Hero->LaunchCharacter(KnockbackDir * PostParryKnockbackStrength, true, true);
			UE_LOG(LogGunParry, Warning, TEXT("[HandleExecutionFinished] 넉백 적용 (%.0f)"), PostParryKnockbackStrength);
		}

		Hero->UnlockMoveInput();
		Hero->UnlockLookInput();
		EndCameraEffect(Hero);
		UE_LOG(LogGunParry, Warning, TEXT("[HandleExecutionFinished] Hero 잠금 해제 + 카메라 원복"));
	}

	ParryTarget = nullptr;
	ExecutionMontageTask = nullptr;

	const FGameplayAbilitySpecHandle Handle = GetCurrentAbilitySpecHandle();
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	const FGameplayAbilityActivationInfo ActivationInfo = GetCurrentActivationInfo();
	EndAbility(Handle, ActorInfo, ActivationInfo, true, bWasCancelled);
	UE_LOG(LogGunParry, Warning, TEXT("[HandleExecutionFinished] EndAbility 호출 완료"));
	UE_LOG(LogGunParry, Warning, TEXT("══════════════════════════════════════════"));
}

// ═══════════════════════════════════════════════════════════
// EndAbility
// ═══════════════════════════════════════════════════════════

void UHeroGameplayAbility_GunParry::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	UE_LOG(LogGunParry, Warning, TEXT("[EndAbility] bWasCancelled=%d"), bWasCancelled);
	ExecutionMontageTask = nullptr;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

// ═══════════════════════════════════════════════════════════
// FindParryableEnemy — 범위+전방각+태그 검색
// ═══════════════════════════════════════════════════════════

AHellunaEnemyCharacter* UHeroGameplayAbility_GunParry::FindParryableEnemy(const AHellunaHeroCharacter* Hero) const
{
	if (!Hero) return nullptr;

	UWorld* World = Hero->GetWorld();
	if (!World) return nullptr;

	const FVector HeroLocation = Hero->GetActorLocation();
	const FVector HeroForward = Hero->GetActorForwardVector();
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(ParryDetectionHalfAngle));

	AHellunaEnemyCharacter* BestEnemy = nullptr;
	float BestDistSq = ParryDetectionRange * ParryDetectionRange;

	TArray<FOverlapResult> Overlaps;
	FCollisionShape Sphere = FCollisionShape::MakeSphere(ParryDetectionRange);
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Hero);

	if (!World->OverlapMultiByObjectType(
		Overlaps,
		HeroLocation,
		FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn),
		Sphere,
		Params))
	{
		UE_LOG(LogGunParry, Verbose, TEXT("[FindParryableEnemy] OverlapMulti 결과 없음"));
		return nullptr;
	}

	UE_LOG(LogGunParry, Verbose, TEXT("[FindParryableEnemy] Overlap 결과: %d개"), Overlaps.Num());

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(Overlap.GetActor());
		if (!Enemy) continue;
		if (!Enemy->bCanBeParried) continue;

		const FVector ToEnemy = (Enemy->GetActorLocation() - HeroLocation).GetSafeNormal();
		if (FVector::DotProduct(HeroForward, ToEnemy) < CosHalfAngle) continue;

		if (!UHellunaFunctionLibrary::NativeDoesActorHaveTag(Enemy, HellunaGameplayTags::Enemy_Ability_Parryable))
			continue;

		if (UHellunaFunctionLibrary::NativeDoesActorHaveTag(Enemy, HellunaGameplayTags::Enemy_State_AnimLocked))
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
// FindParryableEnemyStatic — TryParryInstead용 사전 체크
// ═══════════════════════════════════════════════════════════

AHellunaEnemyCharacter* UHeroGameplayAbility_GunParry::FindParryableEnemyStatic(const AHellunaHeroCharacter* Hero)
{
	if (!Hero) return nullptr;

	UWorld* World = Hero->GetWorld();
	if (!World) return nullptr;

	constexpr float DetectionRange = 300.f;
	constexpr float HalfAngleDeg = 60.f;

	const FVector HeroLocation = Hero->GetActorLocation();
	const FVector HeroForward = Hero->GetActorForwardVector();
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(HalfAngleDeg));

	AHellunaEnemyCharacter* BestEnemy = nullptr;
	float BestDistSq = DetectionRange * DetectionRange;

	TArray<FOverlapResult> Overlaps;
	FCollisionShape Sphere = FCollisionShape::MakeSphere(DetectionRange);
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
		if (!Enemy->bCanBeParried) continue;

		const FVector ToEnemy = (Enemy->GetActorLocation() - HeroLocation).GetSafeNormal();
		if (FVector::DotProduct(HeroForward, ToEnemy) < CosHalfAngle) continue;

		if (!UHellunaFunctionLibrary::NativeDoesActorHaveTag(Enemy, HellunaGameplayTags::Enemy_Ability_Parryable))
			continue;

		if (UHellunaFunctionLibrary::NativeDoesActorHaveTag(Enemy, HellunaGameplayTags::Enemy_State_AnimLocked))
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
// 카메라 연출
// ═══════════════════════════════════════════════════════════

void UHeroGameplayAbility_GunParry::BeginCameraEffect(AHellunaHeroCharacter* Hero)
{
	if (!Hero || !Hero->IsLocallyControlled()) return;
	if (bCameraEffectActive) return;

	if (USpringArmComponent* Boom = Hero->GetCameraBoom())
	{
		SavedArmLength = Boom->TargetArmLength;
		Boom->TargetArmLength = SavedArmLength * 0.6f;
	}

	if (UCameraComponent* Camera = Hero->GetFollowCamera())
	{
		SavedFOV = Camera->FieldOfView;
		Camera->SetFieldOfView(SavedFOV * 0.85f);
	}

	bCameraEffectActive = true;
	UE_LOG(LogGunParry, Warning, TEXT("[CameraEffect] BEGIN — ArmLength=%.0f→%.0f, FOV=%.0f→%.0f"),
		SavedArmLength, SavedArmLength * 0.6f, SavedFOV, SavedFOV * 0.85f);
}

void UHeroGameplayAbility_GunParry::EndCameraEffect(AHellunaHeroCharacter* Hero)
{
	if (!Hero || !Hero->IsLocallyControlled()) return;
	if (!bCameraEffectActive) return;

	if (USpringArmComponent* Boom = Hero->GetCameraBoom())
	{
		Boom->TargetArmLength = SavedArmLength;
	}

	if (UCameraComponent* Camera = Hero->GetFollowCamera())
	{
		Camera->SetFieldOfView(SavedFOV);
	}

	bCameraEffectActive = false;
	UE_LOG(LogGunParry, Warning, TEXT("[CameraEffect] END — ArmLength→%.0f, FOV→%.0f"), SavedArmLength, SavedFOV);
}
