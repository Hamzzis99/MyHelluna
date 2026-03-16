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
		meta = (DisplayName = "감지 범위", ClampMin = "100.0", ClampMax = "1000.0",
			ToolTip = "패링 가능한 적을 탐지하는 최대 거리 (cm). 이 범위 밖의 적은 패링 대상이 아님."))
	float ParryDetectionRange = 300.f;

	/** 전방 반각 (도) — 이 각도 안에 있는 적만 패링 대상 */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|Detection",
		meta = (DisplayName = "전방 반각(도)", ClampMin = "10.0", ClampMax = "180.0",
			ToolTip = "플레이어 정면 기준 좌우 감지 각도. 60이면 정면 120도 범위. 180이면 전방위."))
	float ParryDetectionHalfAngle = 60.f;

	/** Motion Warping 타겟 이름 */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|Execution",
		meta = (ToolTip = "MotionWarping에서 사용하는 타겟 이름. 몽타주의 MotionWarping Notify와 일치해야 함."))
	FName WarpTargetName = TEXT("ParryTarget");

	/** 처형 시 플레이어-몬스터 거리 (cm) */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|Execution",
		meta = (DisplayName = "처형 거리", ClampMin = "50.0", ClampMax = "300.0",
			ToolTip = "워프 후 플레이어와 적 사이의 거리 (cm). 값이 작을수록 적에게 더 가까이 붙음."))
	float ExecutionDistance = 100.f;

	/** 워프 위치를 적 기준 어디로 할지 (0=정면, 90=옆, 180=뒤) */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|Execution",
		meta = (DisplayName = "워프 각도 오프셋(도)", ClampMin = "0.0", ClampMax = "360.0",
			ToolTip = "적 기준 워프 방향. 0=적 정면(샷건), 90=적 옆, 180=적 뒤(권총 뒤통수 사격). 무기별로 다르게 세팅."))
	float WarpAngleOffset = 180.f;

	/** 워프 후 플레이어가 적을 바라보도록 회전할지 */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|Execution",
		meta = (DisplayName = "워프 후 적 방향 회전",
			ToolTip = "true면 워프 후 자동으로 적을 바라봄. false면 워프 시 방향 그대로 유지."))
	bool bFaceEnemyAfterWarp = true;

	/** 워프 시 히어로 원래 Z 높이 유지 */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|Execution",
		meta = (DisplayName = "워프 시 원래 높이 유지",
			ToolTip = "true면 워프 후 히어로의 원래 Z 높이를 유지. false면 적의 Z 높이로 이동. 적이 높은 곳에 있으면 false가 자연스러움."))
	bool bKeepHeroZOnWarp = true;

	/** 워프 위치 Z 오프셋 (cm) */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|Execution",
		meta = (DisplayName = "워프 Z 오프셋", ClampMin = "-200.0", ClampMax = "200.0",
			ToolTip = "워프 위치의 Z축 추가 오프셋 (cm). 캐릭터가 지면에 정확히 닿도록 미세 조정. 양수=위, 음수=아래."))
	float WarpZOffset = 0.f;

	/** 처형 후 넉백 강도 */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|PostExecution",
		meta = (DisplayName = "넉백 강도", ClampMin = "0.0", ClampMax = "2000.0",
			ToolTip = "처형 완료 후 적을 밀어내는 힘. 0이면 넉백 없음. 사망 연출용."))
	float PostParryKnockbackStrength = 600.f;

	/** 사후 무적 시간 (초) */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|PostExecution",
		meta = (DisplayName = "사후 무적 시간(초)", ClampMin = "0.0", ClampMax = "5.0",
			ToolTip = "처형 완료 후 플레이어가 무적 상태로 유지되는 시간 (초). 처형 후 안전하게 빠져나올 여유."))
	float PostParryInvincibleDuration = 1.0f;

	/** 카메라 줌인 — 스프링암 길이 배율 */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|Camera",
		meta = (DisplayName = "카메라 암 배율", ClampMin = "0.1", ClampMax = "1.0",
			ToolTip = "처형 중 스프링암 길이를 원래 값의 몇 배로 줄일지. 0.6이면 40% 줌인. 값이 작을수록 캐릭터에 더 가까워짐."))
	float CameraArmLengthMultiplier = 0.6f;

	/** 카메라 줌인 — FOV 배율 */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|Camera",
		meta = (DisplayName = "카메라 FOV 배율", ClampMin = "0.5", ClampMax = "1.0",
			ToolTip = "처형 중 FOV를 원래 값의 몇 배로 줄일지. 0.85면 15% 좁아짐. 값이 작을수록 더 좁고 집중적인 화면."))
	float CameraFOVMultiplier = 0.85f;

	/** 처형 중 카메라 타겟 오프셋 — SpringArm SocketOffset에 더해짐 */
	UPROPERTY(EditDefaultsOnly, Category = "GunParry|Camera",
		meta = (DisplayName = "카메라 타겟 오프셋",
			ToolTip = "처형 중 카메라가 바라보는 지점의 오프셋. X=전후, Y=좌우, Z=상하. (0,0,50)이면 캐릭터 머리 위를 바라봄."))
	FVector CameraTargetOffset = FVector::ZeroVector;

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

	/** 범위+전방각+태그 기반 패링 대상 검색 (인스턴스 메서드) */
	AHellunaEnemyCharacter* FindParryableEnemy(const AHellunaHeroCharacter* Hero) const; 

	/** 패링 가능한 적이 근처에 있는지 경량 체크 (static — TryParryInstead용) */
	static AHellunaEnemyCharacter* FindParryableEnemyStatic(const AHellunaHeroCharacter* Hero);

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

	/** 처형 대상 적 — TObjectPtr로 GC 방지 (처형 중 소멸 방지) */
	UPROPERTY()
	TObjectPtr<AHellunaEnemyCharacter> ParryTarget;

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> ExecutionMontageTask;

	FTimerHandle PostInvincibleTimerHandle;

	/** HandleExecutionFinished가 이미 호출되었는지 (EndAbility에서 서버 킬 로직 보장용) */
	bool bHandleExecutionFinishedCalled = false;

	/** 카메라 연출 전 원래 값 저장 */
	float SavedArmLength = 0.f;
	float SavedFOV = 0.f;
	FVector SavedSocketOffset = FVector::ZeroVector;
	bool bCameraEffectActive = false;
};
