#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SOPlayerController.h"
#include "SOHud.generated.h"

class UTextBlock;
class UBorder;
class UButton;
class UVerticalBox;
class UCanvasPanel;
class UCanvasPanelSlot;

/** Click target for a lab row; forwards to the controller. */
UCLASS()
class USOClick : public UObject
{
	GENERATED_BODY()
public:
	ESOLab Which = ESOLab::LayerView;
	int32 Dir = 1;
	TWeakObjectPtr<ASOPlayerController> Owner;
	UFUNCTION() void OnClicked();
};

/**
 * The whole HUD, built in code: title and crew card, compass with wind / storm / treasure markers, ship
 * gauges, network gauges, context key prompts, the principle lab (every switch with its key and value),
 * toasts that explain each switch, the intro card and the key reference.
 */
UCLASS()
class SHAREDOCEAN_API USOHud : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	ASOPlayerController* PC() const;
	UTextBlock* Text(int32 Size, FLinearColor Color, bool bBold = false, float WrapAt = 0.f);
	UBorder* Panel(FLinearColor Fill, FLinearColor Rim, float Radius, FMargin Pad);
	UBorder* KeyCap(const FString& Key, int32 Size = 12);
	UButton* Button(ESOLab Which, int32 Dir, UWidget* Content);
	UCanvasPanelSlot* Put(UWidget* W, FVector2D Anchor, FVector2D Align, FVector2D Pos);
	void Row(UVerticalBox* Box, ESOLab Which, bool bStepper);
	UVerticalBox* Gauges(UVerticalBox* Box, const TArray<FString>& Names, TArray<TObjectPtr<UTextBlock>>& Out);

	UPROPERTY() TArray<TObjectPtr<USOClick>> Clicks;
	UPROPERTY() TObjectPtr<UCanvasPanel> Canvas;
	UPROPERTY() TObjectPtr<UTextBlock> RoleText;
	UPROPERTY() TObjectPtr<UTextBlock> SeaText;
	UPROPERTY() TObjectPtr<UTextBlock> TreasureText;
	UPROPERTY() TObjectPtr<UBorder> StormChip;
	UPROPERTY() TObjectPtr<UCanvasPanel> Compass;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> CompassTicks;
	UPROPERTY() TArray<TObjectPtr<UBorder>> CompassMarks;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> ShipValues;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> NetValues;
	UPROPERTY() TObjectPtr<UTextBlock> NetTitle;
	UPROPERTY() TObjectPtr<UBorder> PromptPanel;
	UPROPERTY() TObjectPtr<UTextBlock> PromptText;
	UPROPERTY() TObjectPtr<UBorder> LabPanel;
	UPROPERTY() TObjectPtr<UBorder> LabHint;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> LabValues;
	TArray<ESOLab> LabOrder;
	UPROPERTY() TObjectPtr<UBorder> ToastPanel;
	UPROPERTY() TObjectPtr<UTextBlock> ToastTitle;
	UPROPERTY() TObjectPtr<UTextBlock> ToastBody;
	UPROPERTY() TObjectPtr<UBorder> Intro;
	UPROPERTY() TObjectPtr<UBorder> Help;
	UPROPERTY() TArray<TObjectPtr<UWidget>> GameplayWidgets;
};
