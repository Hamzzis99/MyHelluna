// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Inv_BuildMenu.generated.h"

class UHorizontalBox;
class UInv_BuildingButton;
class UWidgetSwitcher;
class UWrapBox;
class UButton;

/**
 * 빌드 메뉴 메인 위젯
 * 지원 / 보조 / 건설 3개 탭으로 분류
 * 블루프린트에서 UI 디자인 후 이 클래스를 상속받아 사용
 */
UCLASS()
class INVENTORY_API UInv_BuildMenu : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;

private:
	// === 탭 전환 함수 ===

	UFUNCTION()
	void ShowSupport();

	UFUNCTION()
	void ShowAuxiliary();

	UFUNCTION()
	void ShowConstruction();

	/** 선택된 탭 활성화 — SpatialInventory::SetActiveGrid 패턴 참고 */
	void SetActiveTab(UWidget* Content, UButton* ActiveButton);

	// === 블루프린트에서 바인딩할 위젯들 ===

	// 탭 버튼
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Support;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Auxiliary;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Construction;

	// 탭 콘텐츠 전환기
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> Switcher;

	// 각 탭의 건물 버튼 컨테이너 (WrapBox = 줄바꿈 지원)
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWrapBox> WrapBox_Support;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWrapBox> WrapBox_Auxiliary;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWrapBox> WrapBox_Construction;

	// 기존 컨테이너 — 하위 호환을 위해 BindWidgetOptional로 유지
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> HorizontalBox_Buildings;
};
