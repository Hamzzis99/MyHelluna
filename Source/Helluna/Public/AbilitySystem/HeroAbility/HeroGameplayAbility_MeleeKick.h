// Capstone Project Helluna — Melee Kick System (Resident Evil Style)

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaHeroGameplayAbility.h"
#include "HeroGameplayAbility_MeleeKick.generated.h"

class AHellunaEnemyCharacter;
class UCameraShakeBase;

DECLARE_LOG_CATEGORY_EXTERN(LogMeleeKick, Log, All);

/**
 * 발차기 GA — 바이오하자드 스타일 스태거→킥 시스템
 *
 * Staggered 상태의 적에게 접근 후 발차기.
 * 타겟 적에게 큰 데미지 + 주변 AOE 넉백 + Staggered 연쇄.
 */
UCLASS()
class HELLUNA_API UHeroGameplayAbility_MeleeKick : public UHellunaHeroGameplayAbility
{
	GENERATED_BODY()

public:
	UHeroGameplayAbility_MeleeKick();

	// ═══════════════════════════════════════════════════════════
	// 에디터 설정
	// ═══════════════════════════════════════════════════════════

	/** 발차기 몽타주 */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick",
		meta = (ToolTip = "발차기 몽타주."))
	TObjectPtr<UAnimMontage> KickMontage = nullptr;

	/** Staggered 적 감지 범위(cm) */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick",
		meta = (ToolTip = "Staggered 적 감지 범위(cm)."))
	float KickDetectionRange = 200.f;

	/** Staggered 적 감지 전방 반각(도) */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick",
		meta = (ToolTip = "Staggered 적 감지 전방 반각(도)."))
	float KickDetectionHalfAngle = 60.f;

	/** 발차기 워프 거리(cm) */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick",
		meta = (ToolTip = "발차기 워프 거리(cm). 적과 이 거리만큼 떨어짐."))
	float KickWarpDistance = 80.f;

	/** 발차기 타겟 데미지 */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick",
		meta = (ToolTip = "발차기 타겟 데미지."))
	float KickDamage = 50.f;

	/** 발차기 AOE 넉백 범위(cm) */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick",
		meta = (ToolTip = "발차기 AOE 넉백 범위(cm)."))
	float KickAOERadius = 250.f;

	/** 발차기 AOE 넉백 강도 */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick",
		meta = (ToolTip = "발차기 AOE 넉백 강도."))
	float KickAOEKnockback = 1200.f;

	/** 발차기 AOE 데미지 */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick",
		meta = (ToolTip = "발차기 AOE 데미지."))
	float KickAOEDamage = 20.f;

	/** 발차기 카메라 셰이크 */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick",
		meta = (ToolTip = "발차기 카메라 셰이크."))
	TSubclassOf<UCameraShakeBase> KickCameraShake = nullptr;

	/** 발차기 셰이크 강도 */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick",
		meta = (ToolTip = "발차기 셰이크 강도."))
	float KickShakeScale = 1.5f;

	/** 발차기 AOE로 맞은 적에게도 Staggered 부여 시간(초). 0이면 연쇄 없음. */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeKick",
		meta = (ToolTip = "발차기 AOE Staggered 연쇄 지속 시간(초). 0이면 연쇄 없음."))
	float KickAOEStaggerDuration = 4.0f;

protected:
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
	/** Staggered 적 찾기 */
	AHellunaEnemyCharacter* FindStaggeredEnemy() const;

	/** 킥 임팩트 (노티파이에서 호출) */
	UFUNCTION()
	void OnKickImpactEvent(FGameplayEventData Payload);

	/** 몽타주 완료 */
	UFUNCTION()
	void OnKickMontageCompleted();

	/** 몽타주 중단 */
	UFUNCTION()
	void OnKickMontageInterrupted();

	UPROPERTY()
	TObjectPtr<AHellunaEnemyCharacter> KickTarget = nullptr;

	bool bKickImpactProcessed = false;
};
