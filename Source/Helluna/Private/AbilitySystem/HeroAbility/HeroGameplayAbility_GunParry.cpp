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
	// 적: AnimLocked (처형 중 피격 모션 차단)
	if (UHellunaFunctionLibrary::NativeDoesActorHaveTag(MutableTarget, HellunaGameplayTags::Enemy_State_AnimLocked))
		return true;
	// 플레이어: Invincible (처형 중 무적)
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
		return false;

	if (!ActorInfo || !ActorInfo->AvatarActor.IsValid())
		return false;

	const AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(ActorInfo->AvatarActor.Get());
	if (!Hero) return false;

	const AHeroWeapon_GunBase* Weapon = Cast<AHeroWeapon_GunBase>(Hero->GetCurrentWeapon());
	if (!Weapon) return false;
	if (!Weapon->bCanParry) return false;
	if (!Weapon->CanFire()) return false;

	// 패링 가능한 적이 범위 내에 있는지
	return FindParryableEnemy(Hero) != nullptr;
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
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	if (!Hero)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 서버 Authority에서만 핵심 로직 실행
	if (!Hero->HasAuthority())
	{
		// 클라이언트는 카메라 연출만 시작 (서버에서 몽타주가 복제됨)
		BeginCameraEffect(Hero);
		return;
	}

	// 무기 확인 + 탄약 소모
	AHeroWeapon_GunBase* Weapon = Cast<AHeroWeapon_GunBase>(Hero->GetCurrentWeapon());
	if (!Weapon || !Weapon->CanFire())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 패링 대상 찾기
	AHellunaEnemyCharacter* Enemy = FindParryableEnemy(Hero);
	if (!Enemy)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ParryTarget = Enemy;

	// 1) 탄약 1발 소모
	Weapon->CurrentMag = FMath::Max(Weapon->CurrentMag - 1, 0);
	Weapon->BroadcastAmmoChanged();

	// 2) 적 ASC에 AnimLocked 태그 부여
	if (UHellunaAbilitySystemComponent* EnemyASC = Enemy->GetHellunaAbilitySystemComponent())
	{
		EnemyASC->AddStateTag(HellunaGameplayTags::Enemy_State_AnimLocked);
	}

	// 3) 플레이어 ASC에 무적 + 처형 태그 부여
	if (UHellunaAbilitySystemComponent* HeroASC = Hero->GetHellunaAbilitySystemComponent())
	{
		HeroASC->AddStateTag(HellunaGameplayTags::Player_State_Invincible);
		HeroASC->AddStateTag(HellunaGameplayTags::Player_State_ParryExecution);
	}

	// 4) 적 이동 잠금
	Enemy->LockMovementAndFaceTarget(Hero);

	// 5) Motion Warping — 적 정면 ExecutionDistance 위치로 이동
	if (UMotionWarpingComponent* WarpComp = Hero->FindComponentByClass<UMotionWarpingComponent>())
	{
		const FVector EnemyForward = Enemy->GetActorForwardVector();
		const FVector WarpLocation = Enemy->GetActorLocation() + EnemyForward * ExecutionDistance;
		const FRotator WarpRotation = (-EnemyForward).Rotation(); // 적을 마주보는 방향

		FMotionWarpingTarget WarpTarget;
		WarpTarget.Name = WarpTargetName;
		WarpTarget.Location = WarpLocation;
		WarpTarget.Rotation = WarpRotation;
		WarpComp->AddOrUpdateWarpTarget(WarpTarget);
	}

	// 6) 처형 몽타주 재생 (무기의 ParryExecutionMontage)
	UAnimMontage* ExecutionMontage = Weapon->ParryExecutionMontage;
	if (!ExecutionMontage)
	{
		HandleExecutionFinished(true);
		return;
	}

	ExecutionMontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, ExecutionMontage, 1.f);

	if (!ExecutionMontageTask)
	{
		HandleExecutionFinished(true);
		return;
	}

	ExecutionMontageTask->OnCompleted.AddDynamic(this, &ThisClass::OnExecutionMontageCompleted);
	ExecutionMontageTask->OnBlendOut.AddDynamic(this, &ThisClass::OnExecutionMontageCompleted);
	ExecutionMontageTask->OnInterrupted.AddDynamic(this, &ThisClass::OnExecutionMontageInterrupted);
	ExecutionMontageTask->OnCancelled.AddDynamic(this, &ThisClass::OnExecutionMontageInterrupted);
	ExecutionMontageTask->ReadyForActivation();

	// 7) 적 처형 피격 몽타주 (Multicast)
	Enemy->Multicast_PlayParryVictim();

	// 8) 카메라 연출 (로컬만)
	BeginCameraEffect(Hero);

	// 9) 플레이어 이동/시점 잠금
	Hero->LockMoveInput();
	Hero->LockLookInput();
}

// ═══════════════════════════════════════════════════════════
// 몽타주 콜백
// ═══════════════════════════════════════════════════════════

void UHeroGameplayAbility_GunParry::OnExecutionMontageCompleted()
{
	HandleExecutionFinished(false);
}

void UHeroGameplayAbility_GunParry::OnExecutionMontageInterrupted()
{
	HandleExecutionFinished(true);
}

// ═══════════════════════════════════════════════════════════
// 처형 종료 — 사망 처리 + 넉백 + 태그 정리
// ═══════════════════════════════════════════════════════════

void UHeroGameplayAbility_GunParry::HandleExecutionFinished(bool bWasCancelled)
{
	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	AHellunaEnemyCharacter* Enemy = ParryTarget.IsValid() ? ParryTarget.Get() : nullptr;

	// --- 서버: 적 사망 처리 ---
	if (Hero && Hero->HasAuthority() && Enemy)
	{
		// GA_Death 중복 발동 방지
		if (UHellunaHealthComponent* HealthComp = Enemy->FindComponentByClass<UHellunaHealthComponent>())
		{
			HealthComp->SetHealth(0.f);
		}

		// 적 사망 통보 (GameMode)
		if (UWorld* World = Hero->GetWorld())
		{
			if (AGameModeBase* GM = World->GetAuthGameMode())
			{
				// NotifyMonsterDied는 AHellunaDefenseGameMode에 있음
				// OnMonsterDeath 경로 대신 직접 처리하므로 중복 카운트 없음
				if (auto* DefenseGM = Cast<AHellunaDefenseGameMode>(GM))
				{
					DefenseGM->NotifyMonsterDied(Enemy);
				}
			}
		}

		// ECS Entity 제거 (재생성 방지)
		Enemy->DespawnMassEntityOnServer(TEXT("GunParry"));

		// Actor 제거 예약
		Enemy->SetLifeSpan(0.5f);
	}

	// --- 서버: 적 AnimLocked 해제 ---
	if (Enemy)
	{
		if (UHellunaAbilitySystemComponent* EnemyASC = Enemy->GetHellunaAbilitySystemComponent())
		{
			EnemyASC->RemoveStateTag(HellunaGameplayTags::Enemy_State_AnimLocked);
		}
		Enemy->UnlockMovement();
	}

	// --- 서버: 플레이어 태그 정리 + 넉백 ---
	if (Hero)
	{
		if (UHellunaAbilitySystemComponent* HeroASC = Hero->GetHellunaAbilitySystemComponent())
		{
			HeroASC->RemoveStateTag(HellunaGameplayTags::Player_State_ParryExecution);
			HeroASC->RemoveStateTag(HellunaGameplayTags::Player_State_Invincible);

			// 사후 무적 부여 (타이머로 해제)
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
		}

		// 넉백 (적과 반대 방향)
		if (Hero->HasAuthority() && Enemy)
		{
			FVector KnockbackDir = (Hero->GetActorLocation() - Enemy->GetActorLocation()).GetSafeNormal();
			KnockbackDir.Z = 0.2f;
			KnockbackDir.Normalize();
			Hero->LaunchCharacter(KnockbackDir * PostParryKnockbackStrength, true, true);
		}

		// 이동/시점 잠금 해제
		Hero->UnlockMoveInput();
		Hero->UnlockLookInput();

		// 카메라 원복
		EndCameraEffect(Hero);
	}

	// 상태 초기화
	ParryTarget = nullptr;
	ExecutionMontageTask = nullptr;

	const FGameplayAbilitySpecHandle Handle = GetCurrentAbilitySpecHandle();
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	const FGameplayAbilityActivationInfo ActivationInfo = GetCurrentActivationInfo();
	EndAbility(Handle, ActorInfo, ActivationInfo, true, bWasCancelled);
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

	// SphereOverlap으로 주변 Pawn 수집
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
		return nullptr;
	}

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(Overlap.GetActor());
		if (!Enemy) continue;
		if (!Enemy->bCanBeParried) continue;

		// 전방 반각 체크
		const FVector ToEnemy = (Enemy->GetActorLocation() - HeroLocation).GetSafeNormal();
		if (FVector::DotProduct(HeroForward, ToEnemy) < CosHalfAngle) continue;

		// Parryable 태그 체크 (패링 윈도우 열려있는지)
		if (!UHellunaFunctionLibrary::NativeDoesActorHaveTag(Enemy, HellunaGameplayTags::Enemy_Ability_Parryable))
			continue;

		// AnimLocked 체크 (다른 플레이어가 이미 패링 중인지)
		if (UHellunaFunctionLibrary::NativeDoesActorHaveTag(Enemy, HellunaGameplayTags::Enemy_State_AnimLocked))
			continue;

		// 가장 가까운 적 선택
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
		Boom->TargetArmLength = SavedArmLength * 0.6f; // 줌인
	}

	if (UCameraComponent* Camera = Hero->GetFollowCamera())
	{
		SavedFOV = Camera->FieldOfView;
		Camera->SetFieldOfView(SavedFOV * 0.85f); // 약간 좁히기
	}

	bCameraEffectActive = true;
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
}
