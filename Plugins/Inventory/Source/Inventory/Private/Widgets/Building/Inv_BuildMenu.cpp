// Gihyeon's Inventory Project

#include "Widgets/Building/Inv_BuildMenu.h"
#include "Inventory.h"
#include "Components/HorizontalBox.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WrapBox.h"
#include "Components/Button.h"
#include "Widgets/Building/Inv_BuildingButton.h"

void UInv_BuildMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// === 탭 버튼 OnClicked 바인딩 ===
	if (IsValid(Button_Support))
	{
		Button_Support->OnClicked.AddUniqueDynamic(this, &ThisClass::ShowSupport);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] Button_Support 바인딩 실패 — WBP에 위젯 추가 필요"));
	}

	if (IsValid(Button_Auxiliary))
	{
		Button_Auxiliary->OnClicked.AddUniqueDynamic(this, &ThisClass::ShowAuxiliary);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] Button_Auxiliary 바인딩 실패 — WBP에 위젯 추가 필요"));
	}

	if (IsValid(Button_Construction))
	{
		Button_Construction->OnClicked.AddUniqueDynamic(this, &ThisClass::ShowConstruction);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] Button_Construction 바인딩 실패 — WBP에 위젯 추가 필요"));
	}

	// === Switcher 검증 ===
	if (!IsValid(Switcher))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] Switcher 바인딩 실패 — WBP에 위젯 추가 필요"));
	}

	// === WrapBox 검증 ===
	if (!IsValid(WrapBox_Support))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] WrapBox_Support 바인딩 실패 — WBP에 위젯 추가 필요"));
	}
	if (!IsValid(WrapBox_Auxiliary))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] WrapBox_Auxiliary 바인딩 실패 — WBP에 위젯 추가 필요"));
	}
	if (!IsValid(WrapBox_Construction))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] WrapBox_Construction 바인딩 실패 — WBP에 위젯 추가 필요"));
	}

	// === 초기 탭 = 지원 ===
	ShowSupport();

	// === 초기화 완료 로그 ===
	UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] 초기화 완료: Switcher=%s, 탭 버튼 3개 바인딩"),
		IsValid(Switcher) ? TEXT("Valid") : TEXT("NULL"));
}

void UInv_BuildMenu::ShowSupport()
{
	if (IsValid(WrapBox_Support) && IsValid(Button_Support))
	{
		SetActiveTab(WrapBox_Support, Button_Support);

		const int32 SupportCount = IsValid(WrapBox_Support) ? WrapBox_Support->GetChildrenCount() : 0;
		const int32 AuxiliaryCount = IsValid(WrapBox_Auxiliary) ? WrapBox_Auxiliary->GetChildrenCount() : 0;
		const int32 ConstructionCount = IsValid(WrapBox_Construction) ? WrapBox_Construction->GetChildrenCount() : 0;

		UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] 탭 전환: Support (Support=%d, Auxiliary=%d, Construction=%d 개)"),
			SupportCount, AuxiliaryCount, ConstructionCount);
	}
}

void UInv_BuildMenu::ShowAuxiliary()
{
	if (IsValid(WrapBox_Auxiliary) && IsValid(Button_Auxiliary))
	{
		SetActiveTab(WrapBox_Auxiliary, Button_Auxiliary);

		const int32 SupportCount = IsValid(WrapBox_Support) ? WrapBox_Support->GetChildrenCount() : 0;
		const int32 AuxiliaryCount = IsValid(WrapBox_Auxiliary) ? WrapBox_Auxiliary->GetChildrenCount() : 0;
		const int32 ConstructionCount = IsValid(WrapBox_Construction) ? WrapBox_Construction->GetChildrenCount() : 0;

		UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] 탭 전환: Auxiliary (Support=%d, Auxiliary=%d, Construction=%d 개)"),
			SupportCount, AuxiliaryCount, ConstructionCount);
	}
}

void UInv_BuildMenu::ShowConstruction()
{
	if (IsValid(WrapBox_Construction) && IsValid(Button_Construction))
	{
		SetActiveTab(WrapBox_Construction, Button_Construction);

		const int32 SupportCount = IsValid(WrapBox_Support) ? WrapBox_Support->GetChildrenCount() : 0;
		const int32 AuxiliaryCount = IsValid(WrapBox_Auxiliary) ? WrapBox_Auxiliary->GetChildrenCount() : 0;
		const int32 ConstructionCount = IsValid(WrapBox_Construction) ? WrapBox_Construction->GetChildrenCount() : 0;

		UE_LOG(LogTemp, Warning, TEXT("[BuildMenu] 탭 전환: Construction (Support=%d, Auxiliary=%d, Construction=%d 개)"),
			SupportCount, AuxiliaryCount, ConstructionCount);
	}
}

void UInv_BuildMenu::SetActiveTab(UWidget* Content, UButton* ActiveButton)
{
	if (!IsValid(Switcher) || !IsValid(Content) || !IsValid(ActiveButton))
	{
		return;
	}

	// 모든 탭 버튼 활성화
	if (IsValid(Button_Support)) { Button_Support->SetIsEnabled(true); }
	if (IsValid(Button_Auxiliary)) { Button_Auxiliary->SetIsEnabled(true); }
	if (IsValid(Button_Construction)) { Button_Construction->SetIsEnabled(true); }

	// 선택된 탭 버튼 비활성화 (강조 표시)
	ActiveButton->SetIsEnabled(false);

	// Switcher로 콘텐츠 전환
	Switcher->SetActiveWidget(Content);
}
