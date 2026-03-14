// Capstone Project Helluna — Gun Parry System

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaHeroGameplayAbility.h"
#include "HeroGameplayAbility_GunParry.generated.h"

class AHellunaEnemyCharacter;
class AHeroWeapon_GunBase;
class UHellunaAbilitySystemComponent;
class UAbilityTask_PlayMontageAndWait;

/**
 * 건패링 GA — 바이오하자드 RE 스타일 카운터 처형
 *
 * 사격 버튼(LMB) → Shoot GA에서 TryParryInstead() 호출
 * → CanActivateAbility 통과 시 이 GA 활성화 → 처형 연출 → 적 사망
 *
 * 팀원 코드 간접 참조용 static 헬퍼 4개 제공:
 *   ShouldBlockDamage()    — 무적 상태 체크
 *   ShouldBlockHitReact()  — 피격 모션 차단 체크
 *   ShouldDeferDeath()     — 지연 사망 체크
 *   TryParryInstead()      — Shoot GA에서 패링 분기
 */
UCLASS()
class HELLUNA_API UHeroGameplayAbility_GunParry : public UHellunaHeroGameplayAbility
{
	GENERATED_BODY()

public:
	UHeroGameplayAbility_GunParry();

	// ═══════════════════════════════════════════════════════════
	// Static 헬퍼 — 팀원 코드에서 1줄로 호출
	// ═══════════════════════════════════════════════════════════

	/** 무적 상태 체크 (Invincible 또는 PostParryInvincible) */
	static bool ShouldBlockDamage(const AActor* Target);

	/** 피격 모션 차단 체크 (적: AnimLocked / 플레이어: Invincible) */
	static bool ShouldBlockHitReact(const AActor* Target);

	/** 지연 사망 체크 (AnimLocked 상태인 적) */
	static bool ShouldDeferDeath(const AActor* Enemy);

	/** Shoot GA에서 호출 — FullAuto 체크 + 패링 GA 발동 시도 */
	static bool TryParryInstead(UHellunaAbilitySystemComponent* ASC, const AHeroWeapon_GunBase* Weapon);

protected:
	// ═══════════════════════════════════════════════════════════
	// 에디터 설정
	// ═══════════════════════════════════════════════════════════

	/** 패링 감지 범위 (cm) */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|Detection",
		meta = (DisplayName = "감지 범위", ClampMin = "100.0", ClampMax = "1000.0"))
	float ParryDetectionRange = 300.f;

	/** 전방 반각 (도) — 이 각도 안에 있는 적만 패링 대상 */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|Detection",
		meta = (DisplayName = "전방 반각(도)", ClampMin = "10.0", ClampMax = "180.0"))
	float ParryDetectionHalfAngle = 60.f;

	/** Motion Warping 타겟 이름 */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|Execution")
	FName WarpTargetName = TEXT("ParryTarget");

	/** 처형 시 플레이어-몬스터 거리 (cm) */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|Execution",
		meta = (DisplayName = "처형 거리", ClampMin = "50.0", ClampMax = "300.0"))
	float ExecutionDistance = 100.f;

	/** 처형 후 넉백 강도 */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|PostExecution",
		meta = (DisplayName = "넉백 강도", ClampMin = "0.0", ClampMax = "2000.0"))
	float PostParryKnockbackStrength = 600.f;

	/** 사후 무적 시간 (초) */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|PostExecution",
		meta = (DisplayName = "사후 무적 시간(초)", ClampMin = "0.0", ClampMax = "5.0"))
	float PostParryInvincibleDuration = 1.0f;

	// ═══════════════════════════════════════════════════════════
	// GAS 오버라이드
	// ═══════════════════════════════════════════════════════════

	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

private:
	// ═══════════════════════════════════════════════════════════
	// 내부 로직
	// ═══════════════════════════════════════════════════════════

	/** 범위+전방각+태그 기반 패링 대상 검색 */
	AHellunaEnemyCharacter* FindParryableEnemy(const AHellunaHeroCharacter* Hero) const;

	/** 몽타주 완료 콜백 */
	UFUNCTION()
	void OnExecutionMontageCompleted();

	/** 몽타주 중단 콜백 */
	UFUNCTION()
	void OnExecutionMontageInterrupted();

	/** 처형 종료 공통 처리 (사망+넉백+태그 정리) */
	void HandleExecutionFinished(bool bWasCancelled);

	/** 카메라 연출 시작 (로컬만) */
	void BeginCameraEffect(AHellunaHeroCharacter* Hero);

	/** 카메라 연출 종료 (로컬만) */
	void EndCameraEffect(AHellunaHeroCharacter* Hero);

	// ═══════════════════════════════════════════════════════════
	// 런타임 상태
	// ═══════════════════════════════════════════════════════════

	UPROPERTY()
	TWeakObjectPtr<AHellunaEnemyCharacter> ParryTarget;

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> ExecutionMontageTask;

	FTimerHandle PostInvincibleTimerHandle;

	/** 카메라 연출 전 원래 값 저장 */
	float SavedArmLength = 0.f;
	float SavedFOV = 0.f;
	bool bCameraEffectActive = false;
};
