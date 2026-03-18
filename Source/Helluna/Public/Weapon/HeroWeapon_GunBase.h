// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "Weapon/HellunaHeroWeapon.h"
#include "NiagaraSystem.h"
// [Phase 7.5] EquipActor 발사 사운드용
#include "Player/Inv_PlayerController.h"
#include "EquipmentManagement/EquipActor/Inv_EquipActor.h"
#include "HeroWeapon_GunBase.generated.h"

/**
 * 
 */

class AHellunaHeroCharacter;
class UCameraShakeBase;

UENUM(BlueprintType)
enum class EWeaponFireMode : uint8
{
	SemiAuto UMETA(DisplayName = "단발"),
	FullAuto UMETA(DisplayName = "연발")
};

USTRUCT(BlueprintType)
struct FGunAnimationSet
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (DisplayName = "장전 애니메이션"))
	UAnimMontage* Reload = nullptr;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAmmoChanged, int32, CurrentAmmo, int32, MaxAmmo);

UCLASS()
class HELLUNA_API AHeroWeapon_GunBase : public AHellunaHeroWeapon
{
	GENERATED_BODY()
	
public:

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats", meta = (DisplayName = "상하 반동"))
	float ReboundUp = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats", meta = (DisplayName = "좌우 반동"))
	float ReboundLeftRight = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats", meta = (DisplayName = "발사 모드"))
	EWeaponFireMode FireMode = EWeaponFireMode::SemiAuto;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats")
	float Range = 20000.f;
	
	UFUNCTION(BlueprintCallable, Category = "Weapon|Fire")
	virtual void Fire(AController* InstigatorController);

	// ===== [ADD] 탄창 최대치
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats", meta = (DisplayName = "탄창"))
	int32 MaxMag = 30;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Recoil", meta = (DisplayName = "반동 복귀 시간"))
	float RecoilReturnDuration = 0.10f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Recoil", meta = (DisplayName = "반동 복귀 강도"))
	float RecoilReturnStrength = 0.3f; // 스나이퍼 0.3 기준

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Recoil", meta = (DisplayName = "반동 틱 간격"))
	float RecoilTickInterval = 0.01f;

	// ===== [ADD] 탄창 현재치 (복제)
	UPROPERTY(ReplicatedUsing = OnRep_CurrentAmmoInMag, BlueprintReadOnly, Category = "Weapon|Stats")
	int32 CurrentMag = 30;

	// ===== [ADD] UI에 뿌릴 이벤트
	UPROPERTY(BlueprintAssignable, Category = "Weapon|Ammo")
	FOnAmmoChanged OnAmmoChanged;

	// 발사 가능?
	UFUNCTION(BlueprintPure, Category = "Weapon|Ammo")
	bool CanFire() const;

	// 장전 가능? 
	UFUNCTION(BlueprintPure, Category = "Weapon|Ammo")
	bool CanReload() const;

	// 장전(클라 호출 가능, 서버에서 최종 처리)
	UFUNCTION(BlueprintCallable, Category = "Weapon|Ammo")
	void Reload();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Anim", meta = (DisplayName = "총기 전용 애니메이션"))
	FGunAnimationSet GunAnimSet;

	const FGunAnimationSet& GetAnimSet() const { return GunAnimSet; }

	// ═══════════════════════════════════════════════════════════
	// 건패링 관련 — bCanParry=true일 때만 하위 옵션 표시
	// ═══════════════════════════════════════════════════════════

	/** 이 무기로 건패링이 가능한지 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Parry",
		meta = (DisplayName = "건패링 가능",
			ToolTip = "true면 이 무기로 건패링 가능. false면 아래 패링 옵션이 전부 숨겨짐."))
	bool bCanParry = false;

	/** 무기 타입별 처형 몽타주 (플레이어 측) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Parry",
		meta = (DisplayName = "패링 처형 몽타주 (플레이어)",
			EditCondition = "bCanParry", EditConditionHides,
			ToolTip = "건패링 처형 시 플레이어가 재생할 몽타주. 무기마다 다른 연출."))
	TObjectPtr<UAnimMontage> ParryExecutionMontage = nullptr;

	/** 처형 총 발사 순간 카메라 셰이크 클래스 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Parry",
		meta = (DisplayName = "처형 카메라 셰이크",
			EditCondition = "bCanParry", EditConditionHides,
			ToolTip = "처형 총 발사 순간에 재생할 카메라 셰이크 클래스. BP에서 카메라 셰이크 에셋을 지정. nullptr이면 셰이크 없음."))
	TSubclassOf<UCameraShakeBase> ParryExecutionCameraShake = nullptr;

	/** 처형 총 발사 순간 카메라 셰이크 강도 배율 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Parry",
		meta = (DisplayName = "처형 셰이크 강도", ClampMin = "0.0", ClampMax = "5.0",
			EditCondition = "bCanParry", EditConditionHides,
			ToolTip = "카메라 셰이크 강도 배율. 1.0=기본, 2.0=두 배 강하게. 권총=1.0, 샷건=2.0 추천."))
	float ParryExecutionShakeScale = 1.0f;

	/** 적 기준 워프 방향 (도) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Parry",
		meta = (DisplayName = "워프 각도 오프셋(도)", ClampMin = "0.0", ClampMax = "360.0",
			EditCondition = "bCanParry", EditConditionHides,
			ToolTip = "적 기준 워프 방향. 0=적 정면(샷건: 얼굴에 총 대기), 90=옆, 180=적 뒤(권총: 뒤통수 사격)."))
	float WarpAngleOffset = 180.f;

	/** 워프 후 적과의 거리 (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Parry",
		meta = (DisplayName = "처형 거리", ClampMin = "30.0", ClampMax = "300.0",
			EditCondition = "bCanParry", EditConditionHides,
			ToolTip = "워프 후 적과의 거리(cm). 권총=100, 샷건=60(밀착) 추천."))
	float ExecutionDistance = 100.f;

	/** 워프 후 적 방향 회전 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Parry",
		meta = (DisplayName = "워프 후 적 방향 회전",
			EditCondition = "bCanParry", EditConditionHides,
			ToolTip = "true면 워프 후 캐릭터가 적을 바라보도록 회전."))
	bool bFaceEnemyAfterWarp = true;

	/** 처형 중 스프링암 길이 배율 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Parry|Camera",
		meta = (DisplayName = "카메라 암 배율", ClampMin = "0.1", ClampMax = "1.0",
			EditCondition = "bCanParry", EditConditionHides,
			ToolTip = "처형 중 스프링암 길이 배율. 0.35면 65% 줌인. 권총=0.35, 샷건=0.2 추천."))
	float CameraArmLengthMultiplier = 0.6f;

	/** 처형 중 FOV 배율 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Parry|Camera",
		meta = (DisplayName = "카메라 FOV 배율", ClampMin = "0.3", ClampMax = "1.0",
			EditCondition = "bCanParry", EditConditionHides,
			ToolTip = "처형 중 FOV 배율. 0.7이면 30% 좁아짐. 권총=0.7, 샷건=0.5 추천."))
	float CameraFOVMultiplier = 0.85f;

	/** 처형 중 카메라 Yaw 오프셋 (도) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Parry|Camera",
		meta = (DisplayName = "카메라 Yaw 오프셋(도)", ClampMin = "-180.0", ClampMax = "180.0",
			EditCondition = "bCanParry", EditConditionHides,
			ToolTip = "처형 중 카메라 Yaw 오프셋. 정면(180도) 기준 추가 회전. 권총=30(RE9 옆), 샷건=0(정면 클로즈업) 추천."))
	float CameraExecutionYawOffset = 0.0f;

	/** 처형 중 카메라 위치 오프셋 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Parry|Camera",
		meta = (DisplayName = "카메라 타겟 오프셋",
			EditCondition = "bCanParry", EditConditionHides,
			ToolTip = "처형 중 카메라 위치 오프셋(SpringArm SocketOffset에 더함). X=전후, Y=좌우, Z=상하."))
	FVector CameraTargetOffset = FVector::ZeroVector;

	/** 카메라 복귀 보간 속도 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Parry|Camera",
		meta = (DisplayName = "카메라 복귀 속도", ClampMin = "0.5", ClampMax = "20.0",
			EditCondition = "bCanParry", EditConditionHides,
			ToolTip = "처형 후 카메라 복귀 속도(InterpSpeed). 3=~0.5초(RE느낌), 5=~0.3초(빠름). 샷건=5.0 추천."))
	float CameraReturnSpeed = 3.0f;

	/** 카메라 복귀 시작 전 대기 시간 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Parry|Camera",
		meta = (DisplayName = "카메라 복귀 딜레이(초)", ClampMin = "0.0", ClampMax = "2.0",
			EditCondition = "bCanParry", EditConditionHides,
			ToolTip = "처형 후 카메라 복귀 시작까지 대기(초). 권총=0.3, 샷건=0.1 추천."))
	float CameraReturnDelay = 0.0f;

	// ═══════════════════════════════════════════════════════════
	// 건패링 워프 이펙트 (나이아가라)
	// ═══════════════════════════════════════════════════════════

	/** 패링 워프 시 스폰할 나이아가라 이펙트. 캐릭터 원래 위치에서 잔상이 남는 연출. nullptr이면 이펙트 없음. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Parry",
		meta = (DisplayName = "워프 잔상 이펙트",
			EditCondition = "bCanParry", EditConditionHides,
			ToolTip = "패링 워프 시 스폰할 나이아가라 이펙트. 캐릭터 원래 위치에서 잔상이 남는 연출. nullptr이면 이펙트 없음."))
	TObjectPtr<UNiagaraSystem> ParryWarpEffect = nullptr;

	/** 워프 이펙트 크기 배율. 1.0=기본, 2.0=두 배. 무기별로 다르게 세팅. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Parry",
		meta = (DisplayName = "워프 이펙트 크기", ClampMin = "0.1", ClampMax = "5.0",
			EditCondition = "bCanParry", EditConditionHides,
			ToolTip = "워프 이펙트 크기 배율. 1.0=기본, 2.0=두 배. 무기별로 다르게 세팅."))
	float ParryWarpEffectScale = 1.0f;

	/** 워프 잔상 이펙트 색상. 무기별로 다른 색 가능. 기본=SF 파란색. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Parry",
		meta = (DisplayName = "워프 이펙트 색상",
			EditCondition = "bCanParry", EditConditionHides,
			ToolTip = "워프 잔상 이펙트 색상. 무기별로 다른 색 가능. 기본=SF 파란색."))
	FLinearColor ParryWarpEffectColor = FLinearColor(0.2f, 0.5f, 1.0f, 1.0f);

	/** 워프 후 캐릭터가 도착지에 나타나기까지 딜레이(초). 0이면 즉시 나타남. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Parry",
		meta = (DisplayName = "워프 등장 딜레이(초)", ClampMin = "0.0", ClampMax = "0.5",
			EditCondition = "bCanParry", EditConditionHides,
			ToolTip = "워프 후 캐릭터가 도착지에 나타나기까지 딜레이(초). 0이면 즉시 나타남. 0.05=순간이동 잔상 느낌, 0.1=확실한 순간이동 느낌. 권총=0.05 추천."))
	float WarpAppearDelay = 0.05f;

protected:

	virtual void BeginPlay() override;

	UFUNCTION(Server, Reliable)
	void ServerReload();

	void Reload_Internal();

	UFUNCTION()
	void OnRep_CurrentAmmoInMag();

public:
	void BroadcastAmmoChanged();

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	// 서버에서 실제 히트판정/데미지 수행

	// (선택) 이펙트/사운드 동기화용
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastFireFX(FVector_NetQuantize TraceStart, FVector_NetQuantize TraceEnd, bool bHit, FVector_NetQuantize HitLocation);

	// ════════════════════════════════════════════════════════════════
	// [Phase 7.5] 발사 사운드/FX 헬퍼 (자식 클래스에서도 사용)
	// ════════════════════════════════════════════════════════════════
	// [2026-02-18] 작업자: 김기현
	// ────────────────────────────────────────────────────────────────
	// 사운드와 FX를 분리하여 Shotgun 등 자식 클래스가
	// 사운드는 1회, FX는 펠릿 수만큼 호출 가능하게 함.
	//
	// PlayEquipActorFireSound():
	//   EquipActor의 GetFireSound() → 소음기 여부 자동 분기
	//   소음기 장착 시 → SuppressedFireSound
	//   소음기 미장착 시 → DefaultFireSound
	//   BP에서 변경 불가 — EquipActor의 UPROPERTY는 private
	//   (EquipActor BP 디테일 패널에서 무기별 사운드 에셋 설정)
	//
	// SpawnImpactFX(Location):
	//   기존 Niagara 임팩트 이펙트 로직 분리
	// ════════════════════════════════════════════════════════════════

	// 발사 사운드 1회 재생 (EquipActor 기반, 소음기 자동 분기)
	void PlayEquipActorFireSound();

	// 임팩트 FX 1회 스폰 (Niagara)
	void SpawnImpactFX(const FVector& SpawnLocation);

	// 실제 라인트레이스 + 데미지 적용
	void DoLineTraceAndDamage(AController* InstigatorController, const FVector& TraceStart, const FVector& TraceEnd);



	// 트레이스 채널
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Trace")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|FX")
	TObjectPtr<UNiagaraSystem> ImpactFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|FX")
	FVector ImpactFXScale = FVector(1.f, 1.f, 1.f);

// 연사속도 관련 함수
public:
	//다음 발사 시간
	UPROPERTY(Transient)  
	float NextAllowedFireTime = 0.f;

	// 다음 발사시간에 도달했는지 확인
	bool CanFireByRate(float Now, float Interval) const
	{
		return Now >= NextAllowedFireTime;
	}

	// 발사 후 다음 발사시간 갱신
	void ConsumeFireByRate(float Now, float Interval)
	{
		NextAllowedFireTime = Now + FMath::Max(Interval, 0.01f);
	}

	//반동
	UFUNCTION(BlueprintCallable, Category = "Weapon|Recoil")
	void ApplyRecoil(AHellunaHeroCharacter* TargetCharacter);

private:
	// ✅ [ADD] 반동 내부 구현(로컬 전용)
	FTimerHandle RecoilTimerHandle;

	TWeakObjectPtr<AHellunaHeroCharacter> RecoilTarget;

	// 타겟(누적되는 목표 반동 오프셋)
	float RecoilTargetPitch = 0.f;
	float RecoilTargetYaw = 0.f;

	// 현재 적용 중인 오프셋(카메라에 이미 반영된 값)
	float RecoilCurrentPitch = 0.f;
	float RecoilCurrentYaw = 0.f;

	// 이전 프레임 오프셋(Delta 계산용)
	float RecoilPrevPitch = 0.f;
	float RecoilPrevYaw = 0.f;

	// 시작 딜레이(고정 0.03) - 첫 발에만 적용하고 연사 중엔 재딜레이 안 걸리게
	float RecoilStartDelayRemaining = 0.f;
	

	void TickRecoil();
};
