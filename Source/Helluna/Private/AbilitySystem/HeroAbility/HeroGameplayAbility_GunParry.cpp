// Capstone Project Helluna — Gun Parry System

#include "AbilitySystem/HeroAbility/HeroGameplayAbility_GunParry.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StateTreeComponent.h"
#include "AIController.h"
#include "MotionWarpingComponent.h"
#include "MassAgentComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraShakeBase.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
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

DEFINE_LOG_CATEGORY(LogGunParry);

namespace
{
	bool TryDespawnMassEntitySafely(AHellunaEnemyCharacter* Enemy, const TCHAR* Where)
	{
		if (!Enemy || !Enemy->HasAuthority())
		{
			return false;
		}

		UMassAgentComponent* MassAgentComp = Enemy->FindComponentByClass<UMassAgentComponent>();
		if (!MassAgentComp || !MassAgentComp->GetEntityHandle().IsValid())
		{
			return false;
		}

		Enemy->DespawnMassEntityOnServer(Where);
		return true;
	}
}

// ═══════════════════════════════════════════════════════════
// [Step 10] GA_GunParry 등록 — BP DataAsset 방식
//
// DataAsset_HeroStartUpData의 ReactiveAbilities 배열에 등록.
// InputTag 바인딩 불필요 (Shoot GA에서 TryActivateAbilityByTag로 호출).
// 에디터: DA_Hero → ReactiveAbilities에 UHeroGameplayAbility_GunParry 추가.
// ═══════════════════════════════════════════════════════════

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
	if (!ASC || !Weapon)
	{
		UE_LOG(LogGunParry, Warning, TEXT("[TryParryInstead] ASC=%s, Weapon=%s → 스킵"),
			ASC ? TEXT("Valid") : TEXT("nullptr"), Weapon ? TEXT("Valid") : TEXT("nullptr"));
		return false;
	}
	if (Weapon->FireMode == EWeaponFireMode::FullAuto)
	{
		UE_LOG(LogGunParry, Verbose, TEXT("[TryParryInstead] FullAuto 무기 → 패링 불가"));
		return false;
	}
	if (!Weapon->bCanParry)
	{
		UE_LOG(LogGunParry, Verbose, TEXT("[TryParryInstead] bCanParry=false → 스킵"));
		return false;
	}

	const AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(ASC->GetAvatarActor());
	if (!Hero)
	{
		UE_LOG(LogGunParry, Warning, TEXT("[TryParryInstead] Hero 캐스트 실패"));
		return false;
	}

	AHellunaEnemyCharacter* Found = FindParryableEnemyStatic(Hero);
	if (!Found)
	{
		UE_LOG(LogGunParry, Verbose, TEXT("[TryParryInstead] FindParryableEnemyStatic → nullptr, 패링 스킵"));
		return false;
	}

	UE_LOG(LogGunParry, Warning, TEXT("[TryParryInstead] Frame=%llu 패링 대상=%s, 거리=%.0f → TryActivateAbilityByTag"),
		GFrameCounter, *Found->GetName(),
		FVector::Dist(Hero->GetActorLocation(), Found->GetActorLocation()));
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

	const bool bIsServerSide = Hero->HasAuthority();
	AHellunaEnemyCharacter* FoundEnemy = FindParryableEnemy(Hero);
	UE_LOG(LogGunParry, Warning, TEXT("[CanActivate] %s: FindParryableEnemy → %s"),
		bIsServerSide ? TEXT("SERVER") : TEXT("CLIENT"),
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
	UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] 시작 — Frame=%llu, Authority=%s, ActivationMode=%d"),
		GFrameCounter,
		(ActorInfo && ActorInfo->AvatarActor.IsValid() && ActorInfo->AvatarActor->HasAuthority()) ? TEXT("SERVER") : TEXT("CLIENT"),
		(int32)ActivationInfo.ActivationMode);

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	bHandleExecutionFinishedCalled = false;
	bKillProcessed = false;
	ParryFireEventTask = nullptr;

	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	if (!Hero)
	{
		UE_LOG(LogGunParry, Error, TEXT("[ActivateAbility] Hero nullptr → EndAbility"));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const bool bIsServer = Hero->HasAuthority();
	UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] %s 실행"), bIsServer ? TEXT("SERVER") : TEXT("CLIENT"));

	// ═══════════════════════════════════════════════════════════
	// 서버: 데이터 로직 (탄약, 태그, 사망 준비)
	// ═══════════════════════════════════════════════════════════
	AHeroWeapon_GunBase* Weapon = Cast<AHeroWeapon_GunBase>(Hero->GetCurrentWeapon());
	AHellunaEnemyCharacter* Enemy = nullptr;

	if (bIsServer)
	{
		if (!Weapon || !Weapon->CanFire())
		{
			UE_LOG(LogGunParry, Error, TEXT("[ActivateAbility] SERVER: Weapon 없음 또는 CanFire=false → EndAbility"));
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}

		Enemy = FindParryableEnemy(Hero);
		if (!Enemy)
		{
			UE_LOG(LogGunParry, Error, TEXT("[ActivateAbility] SERVER: FindParryableEnemy → nullptr → EndAbility"));
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}

		ParryTarget = Enemy;
		UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] SERVER: 타겟=%s"), *Enemy->GetName());

		// 1) 탄약 1발 소모
		Weapon->CurrentMag = FMath::Max(Weapon->CurrentMag - 1, 0);
		Weapon->BroadcastAmmoChanged();
		UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] SERVER: 탄약 소모 → 남은 탄: %d"), Weapon->CurrentMag);

		// 2) 적 ASC에 AnimLocked 태그 부여
		if (UHellunaAbilitySystemComponent* EnemyASC = Enemy->GetHellunaAbilitySystemComponent())
		{
			EnemyASC->AddStateTag(HellunaGameplayTags::Enemy_State_AnimLocked);
			UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] SERVER: Enemy AnimLocked 부여"));
		}

		// 3) 플레이어 ASC에 무적 + 처형 태그 부여
		if (UHellunaAbilitySystemComponent* HeroASC = Hero->GetHellunaAbilitySystemComponent())
		{
			HeroASC->AddStateTag(HellunaGameplayTags::Player_State_Invincible);
			HeroASC->AddStateTag(HellunaGameplayTags::Player_State_ParryExecution);
			UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] SERVER: Hero Invincible+ParryExecution 부여"));
		}

		// 4) 적 이동 잠금 + AI 중단
		Enemy->LockMovementAndFaceTarget(Hero);

		// 적 AI(StateTree) 중단 — AIController에서 찾기
		if (AAIController* AIC = Cast<AAIController>(Enemy->GetController()))
		{
			if (UStateTreeComponent* STComp = AIC->FindComponentByClass<UStateTreeComponent>())
			{
				STComp->StopLogic(TEXT("GunParry"));
				UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] SERVER: Enemy StateTree 중단"));
			}
		}

		// 적 진행 중인 몽타주 전부 중단 (공격 모션 멈추기)
		if (UAnimInstance* EnemyAnim = Enemy->GetMesh()->GetAnimInstance())
		{
			EnemyAnim->StopAllMontages(0.1f);
			UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] SERVER: Enemy 몽타주 전부 중단"));
		}

		// 5) 적 처형 피격 몽타주 (Multicast — 모든 클라에서 재생)
		Enemy->Multicast_PlayParryVictim();
		UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] SERVER: Enemy Multicast_PlayParryVictim 호출"));
	}

	// ═══════════════════════════════════════════════════════════
	// 서버+클라이언트 공통: 워프 + 몽타주 + 카메라 + 잠금
	// ═══════════════════════════════════════════════════════════

	// 클라이언트에서도 Enemy 참조 필요 (워프/몽타주용)
	if (!bIsServer && !Enemy)
	{
		Enemy = FindParryableEnemyStatic(Hero);
		if (Enemy)
		{
			ParryTarget = Enemy;
			UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] CLIENT: ParryTarget 설정=%s"), *Enemy->GetName());
		}
	}

	// ═══════════════════════════════════════════════════════════
	// 무기에서 카메라/워프 설정 캐싱
	// ═══════════════════════════════════════════════════════════
	if (Weapon)
	{
		CachedWarpAngleOffset = Weapon->WarpAngleOffset;
		CachedExecutionDistance = Weapon->ExecutionDistance;
		bCachedFaceEnemyAfterWarp = Weapon->bFaceEnemyAfterWarp;
		CachedArmLengthMul = Weapon->CameraArmLengthMultiplier;
		CachedFOVMul = Weapon->CameraFOVMultiplier;
		CachedYawOffset = Weapon->CameraExecutionYawOffset;
		CachedCameraTargetOffset = Weapon->CameraTargetOffset;
		CachedReturnSpeed = Weapon->CameraReturnSpeed;
		CachedReturnDelay = Weapon->CameraReturnDelay;
		CachedCameraEntrySpeed = Weapon->CameraEntrySpeed;
		CachedParryExecutionCameraShake = Weapon->ParryExecutionCameraShake;
		CachedParryExecutionShakeScale = Weapon->ParryExecutionShakeScale;

		UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] 무기 카메라 설정: Weapon=%s, ArmMul=%.2f, FOVMul=%.2f, YawOffset=%.1f, WarpAngle=%.1f, ExecDist=%.0f, Shake=%s, ShakeScale=%.1f"),
			*Weapon->GetName(), CachedArmLengthMul, CachedFOVMul, CachedYawOffset, CachedWarpAngleOffset, CachedExecutionDistance,
			CachedParryExecutionCameraShake ? *CachedParryExecutionCameraShake->GetName() : TEXT("None"),
			CachedParryExecutionShakeScale);
	}

	// [Fix: bug-004-2] 즉시 워프 — MotionWarping Notify 없이 직접 위치 이동
	if (Enemy)
	{
		const FVector HeroLocBefore = Hero->GetActorLocation();
		const FVector EnemyForward = Enemy->GetActorForwardVector();
		const FVector EnemyLocation = Enemy->GetActorLocation();
		const FVector OffsetDir = EnemyForward.RotateAngleAxis(CachedWarpAngleOffset, FVector::UpVector);
		FVector WarpLocation = EnemyLocation + OffsetDir * CachedExecutionDistance;

		// [Fix: camera-warp-tuning] Z축 처리
		if (bKeepHeroZOnWarp)
		{
			WarpLocation.Z = HeroLocBefore.Z;
		}
		WarpLocation.Z += WarpZOffset;

		FRotator WarpRotation = Hero->GetActorRotation();
		if (bCachedFaceEnemyAfterWarp)
		{
			WarpRotation = (EnemyLocation - WarpLocation).Rotation();
			WarpRotation.Pitch = 0.f;  // 캐릭터 기울어짐 방지
			WarpRotation.Roll = 0.f;
		}

		// ─── 워프 잔상 나이아가라 이펙트 (Step 2b: Multicast RPC로 모든 클라에서 스폰) ───
		if (Weapon && Weapon->ParryWarpEffect)
		{
			if (Hero->HasAuthority())
			{
				// 서버에서 Multicast 호출 → 모든 클라이언트에서 VFX 스폰
				const bool bGhost = Weapon->bParryWarpGhostMesh;
				const float GhostOpacity = Weapon->ParryWarpGhostOpacity;

				Hero->Multicast_PlayParryWarpVFX(
					Weapon->ParryWarpEffect,
					HeroLocBefore,
					Hero->GetActorRotation(),
					Weapon->ParryWarpEffectScale,
					Weapon->ParryWarpEffectColor,
					bGhost, GhostOpacity);

				Hero->Multicast_PlayParryWarpVFX(
					Weapon->ParryWarpEffect,
					WarpLocation,
					WarpRotation,
					Weapon->ParryWarpEffectScale,
					Weapon->ParryWarpEffectColor,
					bGhost, GhostOpacity);

				UE_LOG(LogGunParry, Warning,
					TEXT("[ActivateAbility] SERVER: Multicast_PlayParryWarpVFX 호출 — Effect=%s, 출발=%s, 도착=%s"),
					*Weapon->ParryWarpEffect->GetName(),
					*HeroLocBefore.ToString(),
					*WarpLocation.ToString());
			}
		}
		else
		{
			UE_LOG(LogGunParry, Verbose,
				TEXT("[ActivateAbility] %s: 워프 이펙트 없음 (ParryWarpEffect=nullptr) — Multicast 스킵"),
				bIsServer ? TEXT("SERVER") : TEXT("CLIENT"));
		}

		Hero->SetActorLocationAndRotation(WarpLocation, WarpRotation);

		// [Fix: collision] 처형 중 캐릭터 충돌 비활성화 — 적과 겹침 방지
		if (UCapsuleComponent* Capsule = Hero->GetCapsuleComponent())
		{
			Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}

		// 카메라 정면 배치 — bUseControllerRotationYaw 비활성화 + ControlRotation = WarpYaw + 180 + YawOffset
		if (Hero->IsLocallyControlled())
		{
			// bUseControllerRotationYaw 저장 및 비활성화 (ActorRotation과 ControlRotation 분리)
			bSavedUseControllerRotationYaw = Hero->bUseControllerRotationYaw;
			Hero->bUseControllerRotationYaw = false;

			if (APlayerController* PC = Cast<APlayerController>(Hero->GetController()))
			{
				SavedControlRotationYaw = PC->GetControlRotation().Yaw;

				// 카메라를 캐릭터 정면/옆에 배치: WarpYaw + 180(정면) + YawOffset(미세조정)
				FRotator CameraRotation = WarpRotation;
				CameraRotation.Yaw += 180.f + CachedYawOffset;
				CameraRotation.Pitch = 0.f;  // 카메라 기울어짐 방지

				if (CachedCameraEntrySpeed <= 0.f)
				{
					// Speed=0 → 즉시 세팅 (기존 동작)
					PC->SetControlRotation(CameraRotation);
					UE_LOG(LogGunParry, Warning, TEXT("[CameraEntry] Speed=0 — 즉시 SetControlRotation Yaw=%.1f"),
						CameraRotation.Yaw);
				}
				else
				{
					// InterpTo로 부드럽게 진입
					TargetCameraEntryRotation = CameraRotation;
					CameraEntryTickCount = 0;

					const float CurrentYaw = PC->GetControlRotation().Yaw;
					UE_LOG(LogGunParry, Warning, TEXT("[CameraEntry] 시작 — CurrentYaw=%.1f → TargetYaw=%.1f (Speed=%.1f)"),
						CurrentYaw, CameraRotation.Yaw, CachedCameraEntrySpeed);

					if (UWorld* World = Hero->GetWorld())
					{
						constexpr float EntryTickRate = 0.016f;
						World->GetTimerManager().SetTimer(
							CameraEntryTimerHandle,
							FTimerDelegate::CreateWeakLambda(this, [this, EntryTickRate]()
							{
								++CameraEntryTickCount;

								AHellunaHeroCharacter* LambdaHero = Cast<AHellunaHeroCharacter>(GetAvatarActorFromActorInfo());
								if (!LambdaHero) { return; }

								APlayerController* LambdaPC = Cast<APlayerController>(LambdaHero->GetController());
								if (!LambdaPC) { return; }

								FRotator CurCtrl = LambdaPC->GetControlRotation();
								const float NewYaw = FMath::FInterpTo(CurCtrl.Yaw, TargetCameraEntryRotation.Yaw, EntryTickRate, CachedCameraEntrySpeed);
								const float NewPitch = FMath::FInterpTo(CurCtrl.Pitch, TargetCameraEntryRotation.Pitch, EntryTickRate, CachedCameraEntrySpeed);

								CurCtrl.Yaw = NewYaw;
								CurCtrl.Pitch = NewPitch;
								LambdaPC->SetControlRotation(CurCtrl);

								// 도착 판정
								const bool bYawDone = FMath::IsNearlyEqual(FMath::UnwindDegrees(NewYaw - TargetCameraEntryRotation.Yaw), 0.f, 1.0f);
								const bool bPitchDone = FMath::IsNearlyEqual(FMath::UnwindDegrees(NewPitch - TargetCameraEntryRotation.Pitch), 0.f, 0.5f);

								if (bYawDone && bPitchDone)
								{
									// 정확한 값으로 스냅
									LambdaPC->SetControlRotation(TargetCameraEntryRotation);

									if (UWorld* W = LambdaHero->GetWorld())
									{
										W->GetTimerManager().ClearTimer(CameraEntryTimerHandle);
									}

									UE_LOG(LogGunParry, Warning, TEXT("[CameraEntry] 완료 — Yaw=%.1f (소요 프레임=%d)"),
										TargetCameraEntryRotation.Yaw, CameraEntryTickCount);
								}
							}),
							EntryTickRate, true);
					}
				}
			}
		}
		const float DistToEnemy = FVector::Dist(WarpLocation, EnemyLocation);
		const float WarpDist = FVector::Dist(HeroLocBefore, WarpLocation);
		UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] %s: 워프 — 이동전=%s → 이동후=%s (워프거리=%.0f, 적까지=%.0f, 각도오프셋=%.0f)"),
			bIsServer ? TEXT("SERVER") : TEXT("CLIENT"),
			*HeroLocBefore.ToString(), *WarpLocation.ToString(),
			WarpDist, DistToEnemy, CachedWarpAngleOffset);
	}
	else
	{
		UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] %s: Enemy nullptr → 워프 스킵"),
			bIsServer ? TEXT("SERVER") : TEXT("CLIENT"));
	}

	// 몽타주 캐싱 + 유효성 검사
	CachedExecutionMontage = Weapon ? Weapon->ParryExecutionMontage : nullptr;
	CachedWarpAppearDelay = Weapon ? Weapon->WarpAppearDelay : 0.f;

	UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] %s: ExecutionMontage=%s, WarpAppearDelay=%.3f"),
		bIsServer ? TEXT("SERVER") : TEXT("CLIENT"),
		CachedExecutionMontage ? *CachedExecutionMontage->GetName() : TEXT("NULL"),
		CachedWarpAppearDelay);

	if (!CachedExecutionMontage)
	{
		UE_LOG(LogGunParry, Error, TEXT("[ActivateAbility] ExecutionMontage NULL"));
		if (bIsServer) HandleExecutionFinished(true);
		return;
	}

	// 카메라 연출 + 입력 잠금 (딜레이와 무관하게 즉시 적용)
	BeginCameraEffect(Hero);
	Hero->LockMoveInput();
	Hero->LockLookInput();

	// ─── 워프 등장 딜레이 (메시 숨김 → 딜레이 → 메시 등장 + 몽타주) ───
	if (CachedWarpAppearDelay > 0.f)
	{
		// 로컬에서만 메시 숨김 (다른 플레이어에겐 즉시 텔레포트로 보임)
		if (Hero->IsLocallyControlled())
		{
			if (USkeletalMeshComponent* HeroMesh = Hero->GetMesh())
			{
				HeroMesh->SetVisibility(false);
				bMeshHiddenForWarp = true;
			}
			UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] CLIENT: 캐릭터 메시 숨김 (WarpAppearDelay=%.3f초)"),
				CachedWarpAppearDelay);
		}

		// 딜레이 후 메시 등장 + 몽타주 시작
		if (UWorld* World = Hero->GetWorld())
		{
			World->GetTimerManager().SetTimer(
				WarpAppearTimerHandle, this, &ThisClass::OnWarpAppearTimerComplete,
				CachedWarpAppearDelay, false);
		}
	}
	else
	{
		UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] WarpAppearDelay=0 — 즉시 몽타주 시작"));
		BeginExecutionMontage();
	}

	UE_LOG(LogGunParry, Warning, TEXT("[ActivateAbility] 완료 — 카메라+잠금"));
	UE_LOG(LogGunParry, Warning, TEXT("══════════════════════════════════════════"));
}

// ═══════════════════════════════════════════════════════════
// 워프 등장 딜레이 완료 — 메시 보이기 + 몽타주 시작
// ═══════════════════════════════════════════════════════════

void UHeroGameplayAbility_GunParry::OnWarpAppearTimerComplete()
{
	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	if (!Hero) return;

	// 메시 보이기 (로컬만)
	if (bMeshHiddenForWarp && Hero->IsLocallyControlled())
	{
		if (USkeletalMeshComponent* HeroMesh = Hero->GetMesh())
		{
			HeroMesh->SetVisibility(true);
		}
		bMeshHiddenForWarp = false;
		UE_LOG(LogGunParry, Warning, TEXT("[WarpAppearTimer] CLIENT: 캐릭터 메시 등장 — 딜레이 %.3f초 후"),
			CachedWarpAppearDelay);
	}

	BeginExecutionMontage();
}

// ═══════════════════════════════════════════════════════════
// 몽타주 생성 및 시작 (딜레이 후 또는 즉시)
// ═══════════════════════════════════════════════════════════

void UHeroGameplayAbility_GunParry::BeginExecutionMontage()
{
	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	if (!Hero) return;

	const bool bIsServer = Hero->HasAuthority();

	if (!CachedExecutionMontage)
	{
		UE_LOG(LogGunParry, Error, TEXT("[BeginExecutionMontage] ExecutionMontage NULL"));
		if (bIsServer) HandleExecutionFinished(true);
		return;
	}

	if (CachedExecutionMontage)
	{
		USkeleton* MontageSkel = CachedExecutionMontage->GetSkeleton();
		USkeleton* CharSkel = Hero->GetMesh() && Hero->GetMesh()->GetSkeletalMeshAsset()
			? Hero->GetMesh()->GetSkeletalMeshAsset()->GetSkeleton() : nullptr;
		UE_LOG(LogGunParry, Warning, TEXT("[BeginExecutionMontage] MontageSkel=%s, CharSkel=%s, Match=%s"),
			MontageSkel ? *MontageSkel->GetName() : TEXT("NULL"),
			CharSkel ? *CharSkel->GetName() : TEXT("NULL"),
			(MontageSkel == CharSkel) ? TEXT("YES") : TEXT("NO"));
	}

	// [Fix: bug-004-1] 기존 몽타주 강제 중단 (히트 리액션 등)
	if (USkeletalMeshComponent* HeroMesh = Hero->GetMesh())
	{
		if (UAnimInstance* AnimInst = HeroMesh->GetAnimInstance())
		{
			UAnimMontage* ActiveMontage = AnimInst->GetCurrentActiveMontage();
			if (ActiveMontage)
			{
				UE_LOG(LogGunParry, Warning, TEXT("[BeginExecutionMontage] 기존 몽타주 '%s' 강제 중단"), *ActiveMontage->GetName());
				AnimInst->StopAllMontages(0.1f);
			}
		}
	}

	// [Fix: bug-005] Hero.PlayFullBody=true → ABP가 매 틱 읽어서 전신 몽타주 재생
	Hero->PlayFullBody = true;
	UE_LOG(LogGunParry, Warning, TEXT("[BeginExecutionMontage] Hero.PlayFullBody = true"));

	ParryFireEventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this,
		HellunaGameplayTags::Event_Parry_Fire,
		nullptr,
		false,
		true);

	if (ParryFireEventTask)
	{
		ParryFireEventTask->EventReceived.AddDynamic(this, &ThisClass::OnParryExecutionFireEvent);
		ParryFireEventTask->ReadyForActivation();
	}
	else
	{
		UE_LOG(LogGunParry, Warning, TEXT("[BeginExecutionMontage] WaitGameplayEvent 생성 실패 — EventTag=%s"),
			*HellunaGameplayTags::Event_Parry_Fire.GetTag().ToString());
	}

	ExecutionMontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, CachedExecutionMontage, 1.f);

	UE_LOG(LogGunParry, Warning, TEXT("[BeginExecutionMontage] MontageTask 생성=%s"),
		ExecutionMontageTask ? TEXT("SUCCESS") : TEXT("FAILED"));

	if (ExecutionMontageTask)
	{
		ExecutionMontageTask->OnCompleted.AddDynamic(this, &ThisClass::OnExecutionMontageCompleted);
		ExecutionMontageTask->OnBlendOut.AddDynamic(this, &ThisClass::OnExecutionMontageCompleted);
		ExecutionMontageTask->OnInterrupted.AddDynamic(this, &ThisClass::OnExecutionMontageInterrupted);
		ExecutionMontageTask->OnCancelled.AddDynamic(this, &ThisClass::OnExecutionMontageInterrupted);
		ExecutionMontageTask->ReadyForActivation();
		UE_LOG(LogGunParry, Warning, TEXT("[BeginExecutionMontage] MontageTask ReadyForActivation — Frame=%llu"), GFrameCounter);

		// 비파괴 검증: 몽타주 실제 재생 상태 확인
		if (UAnimInstance* VerifyAnim = Hero->GetMesh()->GetAnimInstance())
		{
			UAnimMontage* ActiveMontage = VerifyAnim->GetCurrentActiveMontage();
			const bool bIsPlaying = VerifyAnim->Montage_IsPlaying(CachedExecutionMontage);
			const float MontagePos = VerifyAnim->Montage_GetPosition(CachedExecutionMontage);
			const float PlayRate = VerifyAnim->Montage_GetPlayRate(CachedExecutionMontage);
			UE_LOG(LogGunParry, Warning, TEXT("[BeginExecutionMontage] 몽타주 검증: ActiveMontage=%s, IsPlaying=%s, Position=%.3f, PlayRate=%.2f"),
				ActiveMontage ? *ActiveMontage->GetName() : TEXT("없음"),
				bIsPlaying ? TEXT("YES") : TEXT("NO"),
				MontagePos, PlayRate);
		}
	}
	else
	{
		UE_LOG(LogGunParry, Error, TEXT("[BeginExecutionMontage] MontageTask 생성 실패"));
		if (bIsServer) HandleExecutionFinished(true);
	}
}

// ═══════════════════════════════════════════════════════════
// 몽타주 콜백
// ═══════════════════════════════════════════════════════════

void UHeroGameplayAbility_GunParry::OnExecutionMontageCompleted()
{
	const AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	const bool bIsServer = Hero && Hero->HasAuthority();

	float MontagePos = 0.f;
	if (Hero)
	{
		if (UAnimInstance* AnimInst = Hero->GetMesh()->GetAnimInstance())
		{
			MontagePos = AnimInst->Montage_GetPosition(AnimInst->GetCurrentActiveMontage());
		}
	}

	UE_LOG(LogGunParry, Warning, TEXT("[MontageCallback] OnCompleted — %s, Frame=%llu, MontagePos=%.3f"),
		bIsServer ? TEXT("SERVER") : TEXT("CLIENT"), GFrameCounter, MontagePos);
	HandleExecutionFinished(false);
}

void UHeroGameplayAbility_GunParry::OnExecutionMontageInterrupted()
{
	const AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	const bool bIsServer = Hero && Hero->HasAuthority();

	FString ActiveMontageName = TEXT("없음");
	float MontagePos = 0.f;
	if (Hero)
	{
		if (UAnimInstance* AnimInst = Hero->GetMesh()->GetAnimInstance())
		{
			UAnimMontage* Active = AnimInst->GetCurrentActiveMontage();
			if (Active) ActiveMontageName = Active->GetName();
			MontagePos = AnimInst->Montage_GetPosition(Active);
		}
	}

	UE_LOG(LogGunParry, Warning, TEXT("[MontageCallback] OnInterrupted — %s, Frame=%llu, ActiveMontage=%s, Pos=%.3f"),
		bIsServer ? TEXT("SERVER") : TEXT("CLIENT"), GFrameCounter, *ActiveMontageName, MontagePos);
	HandleExecutionFinished(true);
}

// ═══════════════════════════════════════════════════════════
// 총 발사 프레임 Notify 이벤트
// ═══════════════════════════════════════════════════════════

void UHeroGameplayAbility_GunParry::OnParryExecutionFireEvent(FGameplayEventData Payload)
{
	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	AHellunaEnemyCharacter* Enemy = ParryTarget;

	UE_LOG(LogGunParry, Warning, TEXT("[ParryNotify] Event.Parry.Fire 수신 — %s, Enemy=%s, bKillProcessed=%s"),
		Hero ? *Hero->GetName() : TEXT("nullptr"),
		Enemy ? *Enemy->GetName() : TEXT("nullptr"),
		bKillProcessed ? TEXT("Y") : TEXT("N"));

	if (!Hero || bKillProcessed)
	{
		return;
	}

	// VFX 중단 — 총 발사 타이밍에 잔상 소멸 시작
	if (Hero->HasAuthority())
	{
		Hero->Multicast_StopParryWarpVFX();
	}

	PlayParryExecutionCameraShake(Hero);

	if (Hero->HasAuthority())
	{
		ProcessExecutionKill(false);
	}
	else
	{
		bKillProcessed = true;
	}
}

// ═══════════════════════════════════════════════════════════
// 총 발사 프레임 킬 처리
// ═══════════════════════════════════════════════════════════

void UHeroGameplayAbility_GunParry::ProcessExecutionKill(bool bIsFallback)
{
	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	AHellunaEnemyCharacter* Enemy = ParryTarget;

	if (!Hero || !Hero->HasAuthority() || bKillProcessed || !Enemy)
	{
		return;
	}

	UHellunaHealthComponent* HealthComp = Enemy->FindComponentByClass<UHellunaHealthComponent>();
	if (!HealthComp)
	{
		UE_LOG(LogGunParry, Warning, TEXT("[ParryNotify] 킬 처리 스킵 — HealthComponent 없음, Enemy=%s"),
			*Enemy->GetName());
		return;
	}

	const float OldHealth = HealthComp->GetHealth();
	if (!HealthComp->IsDead())
	{
		HealthComp->SetHealth(0.f);
	}

	UE_LOG(LogGunParry, Warning, TEXT("[ParryNotify] 킬 처리 — Enemy HP=%.1f → 0, Frame=%llu"),
		OldHealth, GFrameCounter);

	if (UWorld* World = Hero->GetWorld())
	{
		if (AHellunaDefenseGameMode* DefenseGM = Cast<AHellunaDefenseGameMode>(World->GetAuthGameMode()))
		{
			DefenseGM->NotifyMonsterDied(Enemy);
		}
	}

	const TCHAR* DespawnWhere = bIsFallback ? TEXT("GunParryFallback") : TEXT("GunParryNotify");
	TryDespawnMassEntitySafely(Enemy, DespawnWhere);

	bKillProcessed = true;
}

// ═══════════════════════════════════════════════════════════
// 총 발사 프레임 카메라 셰이크
// ═══════════════════════════════════════════════════════════

void UHeroGameplayAbility_GunParry::PlayParryExecutionCameraShake(AHellunaHeroCharacter* Hero) const
{
	if (!Hero || !Hero->IsLocallyControlled() || !CachedParryExecutionCameraShake)
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(Hero->GetController());
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}

	PC->PlayerCameraManager->StartCameraShake(CachedParryExecutionCameraShake, CachedParryExecutionShakeScale);
	UE_LOG(LogGunParry, Warning, TEXT("[ParryNotify] 카메라 셰이크 재생 — ShakeClass=%s, Scale=%.1f"),
		*CachedParryExecutionCameraShake->GetName(),
		CachedParryExecutionShakeScale);
}

// ═══════════════════════════════════════════════════════════
// 처형 종료 — 사망 처리 + 넉백 + 태그 정리
// ═══════════════════════════════════════════════════════════

void UHeroGameplayAbility_GunParry::HandleExecutionFinished(bool bWasCancelled)
{
	bHandleExecutionFinishedCalled = true;

	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	AHellunaEnemyCharacter* Enemy = ParryTarget;
	const bool bIsServer = Hero && Hero->HasAuthority();

	UE_LOG(LogGunParry, Warning, TEXT("══════════════════════════════════════════"));
	UE_LOG(LogGunParry, Warning, TEXT("[HandleExecutionFinished] %s — Frame=%llu, bCancelled=%d, Hero=%s, Enemy=%s"),
		bIsServer ? TEXT("SERVER") : TEXT("CLIENT"), GFrameCounter, bWasCancelled,
		Hero ? *Hero->GetName() : TEXT("nullptr"),
		Enemy ? *Enemy->GetName() : TEXT("nullptr"));

	// 진입 시 상태 스냅샷
	if (Enemy)
	{
		float EnemyHP = -1.f;
		if (UHellunaHealthComponent* HC = Enemy->FindComponentByClass<UHellunaHealthComponent>())
			EnemyHP = HC->GetHealth();

		const bool bAnimLocked = UHellunaFunctionLibrary::NativeDoesActorHaveTag(Enemy, HellunaGameplayTags::Enemy_State_AnimLocked);
		const bool bParryable = UHellunaFunctionLibrary::NativeDoesActorHaveTag(Enemy, HellunaGameplayTags::Enemy_Ability_Parryable);
		const float DistToHero = Hero ? FVector::Dist(Hero->GetActorLocation(), Enemy->GetActorLocation()) : -1.f;

		UE_LOG(LogGunParry, Warning, TEXT("[HandleExecutionFinished] Enemy 상태: HP=%.1f, AnimLocked=%s, Parryable=%s, 거리=%.0f, bCanBeParried=%s, bEnraged=%s"),
			EnemyHP,
			bAnimLocked ? TEXT("Y") : TEXT("N"),
			bParryable ? TEXT("Y") : TEXT("N"),
			DistToHero,
			Enemy->bCanBeParried ? TEXT("Y") : TEXT("N"),
			Enemy->bEnraged ? TEXT("Y") : TEXT("N"));
	}
	if (Hero)
	{
		const bool bInvincible = UHellunaFunctionLibrary::NativeDoesActorHaveTag(Hero, HellunaGameplayTags::Player_State_Invincible);
		const bool bExecution = UHellunaFunctionLibrary::NativeDoesActorHaveTag(Hero, HellunaGameplayTags::Player_State_ParryExecution);
		const bool bPostInvincible = UHellunaFunctionLibrary::NativeDoesActorHaveTag(Hero, HellunaGameplayTags::Player_State_PostParryInvincible);
		UE_LOG(LogGunParry, Warning, TEXT("[HandleExecutionFinished] Hero 태그: Invincible=%s, ParryExecution=%s, PostParryInvincible=%s"),
			bInvincible ? TEXT("Y") : TEXT("N"),
			bExecution ? TEXT("Y") : TEXT("N"),
			bPostInvincible ? TEXT("Y") : TEXT("N"));
	}

	// ═══════════════════════════════════════════════════════════
	// 서버 전용: 적 사망 + 태그 정리 + 넉백
	// (클라이언트에서는 절대 실행하지 않음)
	// ═══════════════════════════════════════════════════════════
	if (bIsServer)
	{
		if (!bWasCancelled && !bKillProcessed)
		{
			UE_LOG(LogGunParry, Warning, TEXT("[HandleExecutionFinished] 킬 폴백 — Notify 미발동, 여기서 킬 처리"));
			ProcessExecutionKill(true);
		}
		else if (bWasCancelled && !bKillProcessed)
		{
			UE_LOG(LogGunParry, Warning, TEXT("[HandleExecutionFinished] SERVER: bCancelled=true → 킬 미처리, 정리만 수행"));
		}

		if (Enemy && bKillProcessed)
		{
			Enemy->SetLifeSpan(0.5f);
			UE_LOG(LogGunParry, Warning, TEXT("[HandleExecutionFinished] SERVER: SetLifeSpan(0.5)"));
		}

		// 적 AnimLocked 해제 (서버 태그이므로 서버에서만)
		if (Enemy)
		{
			if (UHellunaAbilitySystemComponent* EnemyASC = Enemy->GetHellunaAbilitySystemComponent())
			{
				EnemyASC->RemoveStateTag(HellunaGameplayTags::Enemy_State_AnimLocked);
			}
			Enemy->UnlockMovement();
			UE_LOG(LogGunParry, Warning, TEXT("[HandleExecutionFinished] SERVER: Enemy AnimLocked 해제"));
		}

		// 플레이어 서버 태그 정리
		if (UHellunaAbilitySystemComponent* HeroASC = Hero->GetHellunaAbilitySystemComponent())
		{
			HeroASC->RemoveStateTag(HellunaGameplayTags::Player_State_ParryExecution);
			HeroASC->RemoveStateTag(HellunaGameplayTags::Player_State_Invincible);

			if (!bWasCancelled && bKillProcessed)
			{
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
						PostParryInvincibleDuration, false
					);
				}
				UE_LOG(LogGunParry, Warning, TEXT("[HandleExecutionFinished] SERVER: 태그 정리 + PostParryInvincible(%.1f초)"), PostParryInvincibleDuration);
			}
		}

		// 넉백
		if (!bWasCancelled && Enemy && bKillProcessed)
		{
			FVector KnockbackDir = (Hero->GetActorLocation() - Enemy->GetActorLocation()).GetSafeNormal();
			KnockbackDir.Z = 0.f;
			KnockbackDir.Normalize();
			Hero->LaunchCharacter(KnockbackDir * PostParryKnockbackStrength, true, false);
			UE_LOG(LogGunParry, Warning, TEXT("[HandleExecutionFinished] SERVER: 넉백 적용 (%.0f)"), PostParryKnockbackStrength);
		}
	}

	// ═══════════════════════════════════════════════════════════
	// 클라이언트+서버 공통: 이동 잠금 해제 + 카메라 원복
	// ═══════════════════════════════════════════════════════════
	if (Hero)
	{
		// [Fix: bug-005] Hero.PlayFullBody 원복
		Hero->PlayFullBody = false;
		UE_LOG(LogGunParry, Warning, TEXT("[HandleExecutionFinished] Hero.PlayFullBody = false"));

		// [Fix: collision] 처형 종료 — 충돌 복원
		if (UCapsuleComponent* Capsule = Hero->GetCapsuleComponent())
		{
			Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		}

		Hero->UnlockMoveInput();
		// LookInput은 EndCameraEffect의 복귀 타이머 완료 시 해제 (ControlRotation InterpTo 중 충돌 방지)
		EndCameraEffect(Hero);
		UE_LOG(LogGunParry, Warning, TEXT("[HandleExecutionFinished] %s: 이동 잠금 해제 + 카메라 복귀 시작 (시점 잠금은 복귀 완료 시 해제)"),
			bIsServer ? TEXT("SERVER") : TEXT("CLIENT"));
	}

	ParryTarget = nullptr;
	ExecutionMontageTask = nullptr;
	ParryFireEventTask = nullptr;

	const FGameplayAbilitySpecHandle Handle = GetCurrentAbilitySpecHandle();
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	const FGameplayAbilityActivationInfo ActivationInfo = GetCurrentActivationInfo();
	EndAbility(Handle, ActorInfo, ActivationInfo, true, bWasCancelled);
	UE_LOG(LogGunParry, Warning, TEXT("[HandleExecutionFinished] %s: EndAbility 완료"),
		bIsServer ? TEXT("SERVER") : TEXT("CLIENT"));
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
	UE_LOG(LogGunParry, Warning, TEXT("[EndAbility] Frame=%llu, bWasCancelled=%d, bHandleFinishedCalled=%s"),
		GFrameCounter, bWasCancelled, bHandleExecutionFinishedCalled ? TEXT("Y") : TEXT("N"));

	// [Fix: bug-006] CLIENT EndAbility가 SERVER로 리플리케이트되면
	// SERVER의 MontageTask 콜백 없이 여기로 직행함.
	// 서버이고 HandleExecutionFinished가 아직 미호출이면 여기서 실행.
	if (ActorInfo && ActorInfo->AvatarActor.IsValid() && ActorInfo->AvatarActor->HasAuthority()
		&& !bHandleExecutionFinishedCalled && !bWasCancelled)
	{
		UE_LOG(LogGunParry, Warning, TEXT("[EndAbility] SERVER: HandleExecutionFinished 미호출 → 여기서 실행"));
		HandleExecutionFinished(false);
	}

	// 카메라 진입 InterpTo 타이머 안전 해제
	if (CameraEntryTimerHandle.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(CameraEntryTimerHandle);
		}
	}

	// 워프 등장 딜레이 안전 원복 — GA 취소/종료 시 메시 반드시 복원
	if (WarpAppearTimerHandle.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(WarpAppearTimerHandle);
		}
	}
	if (bMeshHiddenForWarp)
	{
		if (ActorInfo && ActorInfo->AvatarActor.IsValid())
		{
			if (AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(ActorInfo->AvatarActor.Get()))
			{
				if (USkeletalMeshComponent* HeroMesh = Hero->GetMesh())
				{
					HeroMesh->SetVisibility(true);
				}
			}
		}
		bMeshHiddenForWarp = false;
		UE_LOG(LogGunParry, Warning, TEXT("[EndAbility] 캐릭터 메시 Visibility 안전 원복"));
	}

	bHandleExecutionFinishedCalled = false;
	bKillProcessed = false;
	ExecutionMontageTask = nullptr;
	ParryFireEventTask = nullptr;
	CachedExecutionMontage = nullptr;

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

	UE_LOG(LogGunParry, Warning, TEXT("[FindParryableEnemy] Frame=%llu, Hero=%s, Loc=%s, Forward=%s, Range=%.0f, HalfAngle=%.0f, Overlaps=%d"),
		GFrameCounter, *Hero->GetName(), *HeroLocation.ToString(), *HeroForward.ToString(),
		ParryDetectionRange, ParryDetectionHalfAngle, Overlaps.Num());

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(Overlap.GetActor());
		if (!Enemy) continue;

		// 각 조건별 실패 원인 디버그
		if (!Enemy->bCanBeParried)
		{
			UE_LOG(LogGunParry, Warning, TEXT("[FindParryableEnemy] %s: bCanBeParried=false → 스킵"), *Enemy->GetName());
			continue;
		}

		const FVector ToEnemy = (Enemy->GetActorLocation() - HeroLocation).GetSafeNormal();
		const float DotResult = FVector::DotProduct(HeroForward, ToEnemy);
		if (DotResult < CosHalfAngle)
		{
			UE_LOG(LogGunParry, Warning, TEXT("[FindParryableEnemy] %s: 전방각 밖 (Dot=%.2f < Cos=%.2f) → 스킵"),
				*Enemy->GetName(), DotResult, CosHalfAngle);
			continue;
		}

		const bool bHasParryable = UHellunaFunctionLibrary::NativeDoesActorHaveTag(Enemy, HellunaGameplayTags::Enemy_Ability_Parryable);
		if (!bHasParryable)
		{
			UE_LOG(LogGunParry, Warning, TEXT("[FindParryableEnemy] %s: Parryable 태그 없음 (bEnraged=%d) → 스킵"),
				*Enemy->GetName(), Enemy->bEnraged);
			continue;
		}

		if (UHellunaFunctionLibrary::NativeDoesActorHaveTag(Enemy, HellunaGameplayTags::Enemy_State_AnimLocked))
		{
			UE_LOG(LogGunParry, Warning, TEXT("[FindParryableEnemy] %s: AnimLocked → 스킵"), *Enemy->GetName());
			continue;
		}

		const float DistSq = FVector::DistSquared(HeroLocation, Enemy->GetActorLocation());
		UE_LOG(LogGunParry, Warning, TEXT("[FindParryableEnemy] %s: 조건 통과 — Dot=%.2f, 거리=%.0f"),
			*Enemy->GetName(), DotResult, FMath::Sqrt(DistSq));
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestEnemy = Enemy;
		}
	}

	UE_LOG(LogGunParry, Warning, TEXT("[FindParryableEnemy] 결과: %s (거리=%.0f)"),
		BestEnemy ? *BestEnemy->GetName() : TEXT("nullptr"),
		BestEnemy ? FMath::Sqrt(BestDistSq) : 0.f);
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

	// 이전 복귀 보간이 진행 중이면 즉시 스냅하고 타이머 해제
	if (CameraReturnTimerHandle.IsValid())
	{
		UE_LOG(LogGunParry, Warning, TEXT("[CameraEffect] 복귀 진행 중 새 패링 → 즉시 스냅"));
		if (UWorld* World = Hero->GetWorld())
		{
			World->GetTimerManager().ClearTimer(*CameraReturnTimerHandle);
		}
		CameraReturnTimerHandle.Reset();

		// 아직 목표값에 도달하지 못했을 수 있으므로 즉시 스냅
		if (USpringArmComponent* Boom = Hero->GetCameraBoom())
		{
			Boom->TargetArmLength = SavedArmLength;
			if (!CachedCameraTargetOffset.IsZero())
				Boom->SocketOffset = SavedSocketOffset;
			Boom->bDoCollisionTest = bSavedDoCollisionTest;
		}
		if (UCameraComponent* Camera = Hero->GetFollowCamera())
			Camera->SetFieldOfView(SavedFOV);
	}

	if (bCameraEffectActive) return;

	if (USpringArmComponent* Boom = Hero->GetCameraBoom())
	{
		SavedArmLength = Boom->TargetArmLength;
		Boom->TargetArmLength = SavedArmLength * CachedArmLengthMul;

		// CameraTargetOffset → SocketOffset에 더하기
		if (!CachedCameraTargetOffset.IsZero())
		{
			SavedSocketOffset = Boom->SocketOffset;
			Boom->SocketOffset += CachedCameraTargetOffset;
		}

		// [Fix: collision-probe] 카메라 180도 공전 시 캐릭터 메시 관통 방지
		bSavedDoCollisionTest = Boom->bDoCollisionTest;
		Boom->bDoCollisionTest = false;
	}

	if (UCameraComponent* Camera = Hero->GetFollowCamera())
	{
		SavedFOV = Camera->FieldOfView;
		Camera->SetFieldOfView(SavedFOV * CachedFOVMul);
	}

	bCameraEffectActive = true;
	UE_LOG(LogGunParry, Warning, TEXT("[CameraEffect] BEGIN — ArmLength=%.0f→%.0f, FOV=%.0f→%.0f, SocketOffset=%s"),
		SavedArmLength, SavedArmLength * CachedArmLengthMul, SavedFOV, SavedFOV * CachedFOVMul,
		*CachedCameraTargetOffset.ToString());
}

void UHeroGameplayAbility_GunParry::EndCameraEffect(AHellunaHeroCharacter* Hero)
{
	if (!Hero || !Hero->IsLocallyControlled()) return;
	if (!bCameraEffectActive) return;

	bCameraEffectActive = false;

	UWorld* World = Hero->GetWorld();
	if (!World)
	{
		// 월드 없으면 즉시 스냅 폴백
		if (USpringArmComponent* Boom = Hero->GetCameraBoom())
		{
			Boom->TargetArmLength = SavedArmLength;
			if (!CachedCameraTargetOffset.IsZero()) Boom->SocketOffset = SavedSocketOffset;
			Boom->bDoCollisionTest = bSavedDoCollisionTest;
		}
		if (UCameraComponent* Camera = Hero->GetFollowCamera())
			Camera->SetFieldOfView(SavedFOV);
		Hero->bUseControllerRotationYaw = bSavedUseControllerRotationYaw;
		Hero->UnlockLookInput();
		return;
	}

	// GA GC 후에도 안전하게 동작하도록 WeakPtr + 값 캡처
	TWeakObjectPtr<USpringArmComponent> WeakBoom = Hero->GetCameraBoom();
	TWeakObjectPtr<UCameraComponent> WeakCamera = Hero->GetFollowCamera();
	TWeakObjectPtr<UWorld> WeakWorld = World;
	TWeakObjectPtr<APlayerController> WeakPC = Cast<APlayerController>(Hero->GetController());
	TWeakObjectPtr<AHellunaHeroCharacter> WeakHero = Hero;

	const float TargetArmLength = SavedArmLength;
	const float TargetFOV = SavedFOV;
	const FVector TargetSocketOffset = SavedSocketOffset;
	const float InterpSpeed = CachedReturnSpeed;
	const bool bHasOffset = !CachedCameraTargetOffset.IsZero();

	// ControlRotation 복귀 목표 = 처형 전 원래 보던 방향
	const float TargetControlYaw = SavedControlRotationYaw;
	const bool bSavedUseCtrlYaw = bSavedUseControllerRotationYaw;
	const bool bSavedCollisionTest = bSavedDoCollisionTest;

	// TSharedPtr로 람다 내부에서 self-clear 가능
	TSharedPtr<FTimerHandle> TimerHandle = MakeShared<FTimerHandle>();
	CameraReturnTimerHandle = TimerHandle;

	constexpr float TickRate = 0.016f;
	TSharedPtr<int32> TickCount = MakeShared<int32>(0);

	auto InterpLambda = [WeakBoom, WeakCamera, WeakWorld, WeakPC, WeakHero, TimerHandle, TickCount,
		TargetArmLength, TargetFOV, TargetSocketOffset, TargetControlYaw,
		InterpSpeed, bHasOffset, bSavedUseCtrlYaw, bSavedCollisionTest, TickRate]()
	{
		++(*TickCount);

		// 컴포넌트 모두 소멸 → 타이머 해제 + 안전 복원
		if (!WeakBoom.IsValid() && !WeakCamera.IsValid())
		{
			if (WeakHero.IsValid())
			{
				WeakHero->bUseControllerRotationYaw = bSavedUseCtrlYaw;
				WeakHero->UnlockLookInput();
			}
			if (WeakWorld.IsValid() && TimerHandle.IsValid())
				WeakWorld->GetTimerManager().ClearTimer(*TimerHandle);
			return;
		}

		// 첫 틱: 시작 로그
		if (*TickCount == 1)
		{
			const float CurArm = WeakBoom.IsValid() ? WeakBoom->TargetArmLength : 0.f;
			const float CurFOV = WeakCamera.IsValid() ? WeakCamera->FieldOfView : 0.f;
			const float CurYaw = WeakPC.IsValid() ? WeakPC->GetControlRotation().Yaw : 0.f;
			UE_LOG(LogGunParry, Warning, TEXT("[CameraReturn] 시작 — ArmLength=%.0f→%.0f, FOV=%.0f→%.0f, ControlYaw=%.1f→%.1f"),
				CurArm, TargetArmLength, CurFOV, TargetFOV, CurYaw, TargetControlYaw);
		}

		bool bDone = true;

		if (WeakBoom.IsValid())
		{
			WeakBoom->TargetArmLength = FMath::FInterpTo(
				WeakBoom->TargetArmLength, TargetArmLength, TickRate, InterpSpeed);
			if (!FMath::IsNearlyEqual(WeakBoom->TargetArmLength, TargetArmLength, 0.5f))
				bDone = false;

			if (bHasOffset)
			{
				WeakBoom->SocketOffset = FMath::VInterpTo(
					WeakBoom->SocketOffset, TargetSocketOffset, TickRate, InterpSpeed);
				if (!WeakBoom->SocketOffset.Equals(TargetSocketOffset, 0.5f))
					bDone = false;
			}
		}

		if (WeakCamera.IsValid())
		{
			const float NewFOV = FMath::FInterpTo(
				WeakCamera->FieldOfView, TargetFOV, TickRate, InterpSpeed);
			WeakCamera->SetFieldOfView(NewFOV);
			if (!FMath::IsNearlyEqual(NewFOV, TargetFOV, 0.1f))
				bDone = false;
		}

		// ControlRotation Yaw InterpTo (LookInput 잠금 중이므로 플레이어 입력과 충돌 없음)
		if (WeakPC.IsValid())
		{
			FRotator CurCtrl = WeakPC->GetControlRotation();
			const float NewYaw = FMath::FInterpTo(CurCtrl.Yaw, TargetControlYaw, TickRate, InterpSpeed);
			if (!FMath::IsNearlyEqual(FMath::UnwindDegrees(NewYaw - TargetControlYaw), 0.f, 1.0f))
			{
				bDone = false;
			}
			CurCtrl.Yaw = NewYaw;
			WeakPC->SetControlRotation(CurCtrl);
		}

		if (bDone)
		{
			// 정확한 값으로 스냅
			if (WeakBoom.IsValid())
			{
				WeakBoom->TargetArmLength = TargetArmLength;
				if (bHasOffset) WeakBoom->SocketOffset = TargetSocketOffset;
			}
			if (WeakCamera.IsValid())
				WeakCamera->SetFieldOfView(TargetFOV);
			if (WeakPC.IsValid())
			{
				FRotator FinalCtrl = WeakPC->GetControlRotation();
				FinalCtrl.Yaw = TargetControlYaw;
				WeakPC->SetControlRotation(FinalCtrl);
			}

			// bUseControllerRotationYaw 복원 + LookInput 해제
			if (WeakHero.IsValid())
			{
				WeakHero->bUseControllerRotationYaw = bSavedUseCtrlYaw;
				WeakHero->UnlockLookInput();
			}

			// [Fix: collision-probe] 카메라 충돌 프로브 원복
			if (WeakBoom.IsValid())
			{
				WeakBoom->bDoCollisionTest = bSavedCollisionTest;
			}

			const float FinalYaw = WeakPC.IsValid() ? WeakPC->GetControlRotation().Yaw : TargetControlYaw;
			UE_LOG(LogGunParry, Warning, TEXT("[CameraReturn] 완료 — ArmLength=%.0f, FOV=%.0f, ControlYaw=%.1f (소요 프레임=%d)"),
				TargetArmLength, TargetFOV, FinalYaw, *TickCount);

			if (WeakWorld.IsValid() && TimerHandle.IsValid())
				WeakWorld->GetTimerManager().ClearTimer(*TimerHandle);
		}
	};

	World->GetTimerManager().SetTimer(
		*TimerHandle,
		FTimerDelegate::CreateLambda(InterpLambda),
		TickRate, true, CachedReturnDelay);

	UE_LOG(LogGunParry, Warning, TEXT("[CameraEffect] END — 부드러운 복귀 시작 (Speed=%.1f, Delay=%.1f)"),
		CachedReturnSpeed, CachedReturnDelay);
}
