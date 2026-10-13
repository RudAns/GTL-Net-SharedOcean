#include "SOHud.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "EngineUtils.h"
#include "OceanManager.h"
#include "SOCharacter.h"
#include "SOChest.h"
#include "SOEnvironment.h"
#include "SOGameState.h"
#include "Styling/CoreStyle.h"

namespace
{
	// Night-sea panels with a brass rim and parchment ink: reads over bright sky, dark storm water and sails.
	const FLinearColor Fill(0.010f, 0.022f, 0.035f, 0.86f);
	const FLinearColor FillSoft(0.012f, 0.028f, 0.045f, 0.74f);
	const FLinearColor Rim(0.62f, 0.45f, 0.18f, 0.85f);
	const FLinearColor RowFill(0.030f, 0.055f, 0.080f, 0.95f);
	const FLinearColor Ink(0.97f, 0.93f, 0.84f);
	const FLinearColor Dim(0.66f, 0.70f, 0.72f);
	const FLinearColor Gold(1.00f, 0.76f, 0.30f);
	const FLinearColor Sea(0.36f, 0.86f, 0.82f);
	const FLinearColor Good(0.45f, 0.88f, 0.48f);
	const FLinearColor Warn(1.00f, 0.72f, 0.28f);
	const FLinearColor Bad(1.00f, 0.42f, 0.32f);
	const FLinearColor CapFill(0.90f, 0.84f, 0.70f);
	const FLinearColor CapInk(0.05f, 0.05f, 0.06f);

	FSlateBrush Rounded(FLinearColor F, float R, FLinearColor O = FLinearColor::Transparent, float W = 0.f)
	{
		FSlateRoundedBoxBrush B(F, R);
		B.OutlineSettings = FSlateBrushOutlineSettings(FVector4(R, R, R, R), FSlateColor(O), W);
		return B;
	}
	FText T(const FString& S) { return FText::FromString(S); }
}

void USOClick::OnClicked()
{
	if (ASOPlayerController* P = Owner.Get()) P->Lab(Which, Dir);
}

ASOPlayerController* USOHud::PC() const { return Cast<ASOPlayerController>(GetOwningPlayer()); }

UTextBlock* USOHud::Text(int32 Size, FLinearColor Color, bool bBold, float WrapAt)
{
	UTextBlock* B = WidgetTree->ConstructWidget<UTextBlock>();
	B->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size));
	B->SetColorAndOpacity(FSlateColor(Color));
	B->SetShadowOffset(FVector2D(1.f, 1.f));
	B->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f));
	if (WrapAt > 0.f) { B->SetAutoWrapText(false); B->SetWrapTextAt(WrapAt); }
	return B;
}

UBorder* USOHud::Panel(FLinearColor F, FLinearColor R, float Radius, FMargin Pad)
{
	UBorder* B = WidgetTree->ConstructWidget<UBorder>();
	B->SetBrush(Rounded(F, Radius, R, R.A > 0.f ? 1.5f : 0.f));
	B->SetPadding(Pad);
	return B;
}

UBorder* USOHud::KeyCap(const FString& Key, int32 Size)
{
	UBorder* Cap = Panel(CapFill, FLinearColor(0.35f, 0.28f, 0.16f, 1.f), 4.f, FMargin(7, 1, 7, 2));
	UTextBlock* Tx = Text(Size, CapInk, true);
	Tx->SetShadowColorAndOpacity(FLinearColor::Transparent);
	Tx->SetText(T(Key));
	Tx->SetJustification(ETextJustify::Center);
	Cap->SetContent(Tx);
	Cap->SetHorizontalAlignment(HAlign_Center);
	Cap->SetVerticalAlignment(VAlign_Center);
	return Cap;
}

UButton* USOHud::Button(ESOLab Which, int32 Dir, UWidget* Content)
{
	UButton* B = WidgetTree->ConstructWidget<UButton>();
	FButtonStyle St;
	St.SetNormal(Rounded(FLinearColor(0, 0, 0, 0), 6.f));
	St.SetHovered(Rounded(FLinearColor(1.f, 0.85f, 0.6f, 0.10f), 6.f, Gold, 1.f));
	St.SetPressed(Rounded(FLinearColor(1.f, 0.85f, 0.6f, 0.20f), 6.f, Gold, 1.f));
	St.SetNormalPadding(FMargin(0)); St.SetPressedPadding(FMargin(0));
	B->SetStyle(St);
	USOClick* H = NewObject<USOClick>(this);
	H->Which = Which; H->Dir = Dir; H->Owner = PC();
	B->OnClicked.AddDynamic(H, &USOClick::OnClicked);
	Clicks.Add(H);
	if (Content)
	{
		B->SetContent(Content);
		// buttons centre their content by default; lab rows must span the full width so their columns line up
		if (UButtonSlot* BS = Cast<UButtonSlot>(Content->Slot))
		{
			BS->SetHorizontalAlignment(HAlign_Fill);
			BS->SetPadding(FMargin(0));
		}
	}
	return B;
}

UCanvasPanelSlot* USOHud::Put(UWidget* W, FVector2D Anchor, FVector2D Align, FVector2D Pos)
{
	UCanvasPanelSlot* S = Canvas->AddChildToCanvas(W);
	S->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
	S->SetAlignment(Align);
	S->SetPosition(Pos);
	S->SetAutoSize(true);
	return S;
}

void USOHud::Row(UVerticalBox* Box, ESOLab Which, bool bStepper)
{
	UHorizontalBox* H = WidgetTree->ConstructWidget<UHorizontalBox>();
	USizeBox* KeyBox = WidgetTree->ConstructWidget<USizeBox>();
	KeyBox->SetWidthOverride(44.f);
	const FString Key = ASOPlayerController::LabKey(Which).ToString();
	if (!Key.IsEmpty()) KeyBox->SetContent(KeyCap(Key, 11));
	H->AddChildToHorizontalBox(KeyBox)->SetVerticalAlignment(VAlign_Center);
	USpacer* Sp = WidgetTree->ConstructWidget<USpacer>(); Sp->SetSize(FVector2D(8, 1));
	H->AddChildToHorizontalBox(Sp);
	UTextBlock* Name = Text(14, Ink);
	Name->SetText(ASOPlayerController::LabName(Which));
	H->AddChildToHorizontalBox(Name)->SetVerticalAlignment(VAlign_Center);
	UTextBlock* Scope = Text(10, ASOPlayerController::LabIsServer(Which) ? Gold : Sea);
	const bool bClientOnly = Which == ESOLab::ClockMode || Which == ESOLab::Presentation || Which == ESOLab::InterpDelay || Which == ESOLab::Ghost || Which == ESOLab::ClockError;
	Scope->SetText(T(ASOPlayerController::LabIsServer(Which) ? TEXT("  服务器") : (bClientOnly ? TEXT("  客户端") : TEXT(""))));
	H->AddChildToHorizontalBox(Scope)->SetVerticalAlignment(VAlign_Center);
	USpacer* Fill1 = WidgetTree->ConstructWidget<USpacer>();
	H->AddChildToHorizontalBox(Fill1)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UBorder* Pill = Panel(FLinearColor(0.10f, 0.20f, 0.24f, 1.f), FLinearColor::Transparent, 9.f, FMargin(9, 1, 9, 2));
	UTextBlock* Val = Text(13, Sea, true);
	Pill->SetContent(Val);
	LabValues.Add(Val);
	LabOrder.Add(Which);
	if (bStepper)
	{
		UBorder* M = Panel(RowFill, Rim, 4.f, FMargin(8, 0, 8, 1)); M->SetContent(Text(14, Ink, true)); Cast<UTextBlock>(M->GetContent())->SetText(T(TEXT("−")));
		UBorder* P = Panel(RowFill, Rim, 4.f, FMargin(8, 0, 8, 1)); P->SetContent(Text(14, Ink, true)); Cast<UTextBlock>(P->GetContent())->SetText(T(TEXT("+")));
		H->AddChildToHorizontalBox(Button(Which, -1, M))->SetPadding(FMargin(0, 0, 4, 0));
		H->AddChildToHorizontalBox(Pill)->SetVerticalAlignment(VAlign_Center);
		H->AddChildToHorizontalBox(Button(Which, 1, P))->SetPadding(FMargin(4, 0, 0, 0));
		UBorder* Back = Panel(FLinearColor::Transparent, FLinearColor::Transparent, 6.f, FMargin(4, 3, 6, 3));
		Back->SetContent(H);
		Box->AddChildToVerticalBox(Back);
	}
	else
	{
		H->AddChildToHorizontalBox(Pill)->SetVerticalAlignment(VAlign_Center);
		UBorder* Back = Panel(FLinearColor::Transparent, FLinearColor::Transparent, 6.f, FMargin(4, 3, 6, 3));
		Back->SetContent(H);
		Box->AddChildToVerticalBox(Button(Which, 1, Back));
	}
}

UVerticalBox* USOHud::Gauges(UVerticalBox* Box, const TArray<FString>& Names, TArray<TObjectPtr<UTextBlock>>& Out)
{
	for (const FString& N : Names)
	{
		UHorizontalBox* H = WidgetTree->ConstructWidget<UHorizontalBox>();
		USizeBox* NB = WidgetTree->ConstructWidget<USizeBox>();
		NB->SetWidthOverride(118.f);
		UTextBlock* L = Text(13, Dim); L->SetText(T(N));
		NB->SetContent(L);
		H->AddChildToHorizontalBox(NB);
		UTextBlock* V = Text(14, Ink, true);
		H->AddChildToHorizontalBox(V);
		Out.Add(V);
		Box->AddChildToVerticalBox(H)->SetPadding(FMargin(0, 1, 0, 1));
	}
	return Box;
}

TSharedRef<SWidget> USOHud::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;
		auto VBox = [this]() { return WidgetTree->ConstructWidget<UVerticalBox>(); };
		auto HBox = [this]() { return WidgetTree->ConstructWidget<UHorizontalBox>(); };
		auto Label = [this](const FString& S, int32 Size, FLinearColor C, bool bBold = false, float Wrap = 0.f) { UTextBlock* Tx = Text(Size, C, bBold, Wrap); Tx->SetText(T(S)); return Tx; };
		auto Gap = [this](float W, float H) { USpacer* S = WidgetTree->ConstructWidget<USpacer>(); S->SetSize(FVector2D(W, H)); return S; };
		auto Sized = [this](UWidget* W, float Wd, float Ht) { USizeBox* B = WidgetTree->ConstructWidget<USizeBox>(); if (Wd > 0) B->SetWidthOverride(Wd); if (Ht > 0) B->SetHeightOverride(Ht); B->SetContent(W); return B; };

		// ---------------- title / crew card (top-left) ----------------
		{
			UBorder* Card = Panel(Fill, Rim, 10.f, FMargin(16, 12, 18, 12));
			UVerticalBox* V = VBox();
			V->AddChildToVerticalBox(Label(TEXT("共享之海"), 24, Gold, true));
			V->AddChildToVerticalBox(Label(TEXT("FFT 海面 · 船体浮力 · 多人一致性"), 12, Dim));
			V->AddChildToVerticalBox(Gap(1, 8));
			RoleText = Text(14, Sea, true); SeaText = Text(14, Ink); TreasureText = Text(14, Gold, true);
			V->AddChildToVerticalBox(RoleText);
			V->AddChildToVerticalBox(SeaText);
			V->AddChildToVerticalBox(TreasureText);
			StormChip = Panel(FLinearColor(0.45f, 0.08f, 0.05f, 0.95f), FLinearColor(1.f, 0.5f, 0.35f, 0.9f), 6.f, FMargin(8, 2, 8, 3));
			StormChip->SetContent(Label(TEXT("风暴区：浪高与浪长显著增大"), 13, Ink, true));
			V->AddChildToVerticalBox(StormChip)->SetPadding(FMargin(0, 6, 0, 0));
			Card->SetContent(V);
			Put(Card, FVector2D(0, 0), FVector2D(0, 0), FVector2D(18, 16));
			GameplayWidgets.Add(Card);
		}
		// ---------------- compass (top-center) ----------------
		{
			UBorder* Card = Panel(FillSoft, Rim, 8.f, FMargin(0));
			Compass = WidgetTree->ConstructWidget<UCanvasPanel>();
			Card->SetContent(Sized(Compass, 620.f, 46.f));
			const TCHAR* Names[] = { TEXT("北"), TEXT("东北"), TEXT("东"), TEXT("东南"), TEXT("南"), TEXT("西南"), TEXT("西"), TEXT("西北") };
			for (int32 I = 0; I < 8; ++I)
			{
				UTextBlock* Tk = Label(Names[I], I % 2 ? 12 : 16, I % 2 ? Dim : Ink, I % 2 == 0);
				UCanvasPanelSlot* S = Compass->AddChildToCanvas(Tk);
				S->SetAutoSize(true);
				S->SetAlignment(FVector2D(0.5f, 0.5f));
				CompassTicks.Add(Tk);
			}
			const TCHAR* Marks[] = { TEXT("风"), TEXT("风暴"), TEXT("宝箱"), TEXT("灯塔"), TEXT("敌船") };
			const FLinearColor MarkC[] = { FLinearColor(0.20f, 0.45f, 0.55f, 1.f), FLinearColor(0.55f, 0.10f, 0.07f, 1.f), FLinearColor(0.60f, 0.42f, 0.08f, 1.f), FLinearColor(0.35f, 0.30f, 0.30f, 1.f), FLinearColor(0.25f, 0.25f, 0.35f, 1.f) };
			for (int32 I = 0; I < 5; ++I)
			{
				UBorder* Mk = Panel(MarkC[I], FLinearColor(1, 1, 1, 0.5f), 5.f, FMargin(6, 0, 6, 1));
				Mk->SetContent(Label(Marks[I], 12, Ink, true));
				UCanvasPanelSlot* S = Compass->AddChildToCanvas(Mk);
				S->SetAutoSize(true);
				S->SetAlignment(FVector2D(0.5f, 0.5f));
				CompassMarks.Add(Mk);
			}
			Put(Card, FVector2D(0.5f, 0), FVector2D(0.5f, 0), FVector2D(0, 14));
			GameplayWidgets.Add(Card);
		}
		// ---------------- ship gauges (bottom-left) ----------------
		{
			UBorder* Card = Panel(Fill, Rim, 10.f, FMargin(14, 10, 16, 12));
			UVerticalBox* V = VBox();
			V->AddChildToVerticalBox(Label(TEXT("航海仪表"), 15, Gold, true));
			V->AddChildToVerticalBox(Gap(1, 4));
			Gauges(V, { TEXT("航速"), TEXT("帆 / 舵"), TEXT("升沉"), TEXT("纵摇 / 横摇"), TEXT("吃水偏差"), TEXT("两端吃水差"), TEXT("浮力"), TEXT("海况 Hs") }, ShipValues);
			Card->SetContent(Sized(V, 330.f, 0));
			Put(Card, FVector2D(0, 1), FVector2D(0, 1), FVector2D(18, -16));
			GameplayWidgets.Add(Card);
		}
		// ---------------- network gauges (bottom-left, right of the ship card) ----------------
		{
			UBorder* Card = Panel(Fill, Rim, 10.f, FMargin(14, 10, 16, 12));
			UVerticalBox* V = VBox();
			NetTitle = Label(TEXT("网络与时钟"), 15, Gold, true);
			V->AddChildToVerticalBox(NetTitle);
			V->AddChildToVerticalBox(Gap(1, 4));
			Gauges(V, { TEXT("往返时间"), TEXT("服务器时钟"), TEXT("海浪时钟误差"), TEXT("两端浪高差≈"), TEXT("快照"), TEXT("快照年龄"), TEXT("插值延迟") }, NetValues);
			Card->SetContent(Sized(V, 380.f, 0));
			Put(Card, FVector2D(0, 1), FVector2D(0, 1), FVector2D(372, -16));
			GameplayWidgets.Add(Card);
		}
		// ---------------- context prompt (bottom-center) ----------------
		{
			PromptPanel = Panel(FillSoft, FLinearColor::Transparent, 8.f, FMargin(14, 6, 14, 7));
			PromptText = Text(15, Ink, true);
			PromptPanel->SetContent(PromptText);
			Put(PromptPanel, FVector2D(0.5f, 1), FVector2D(0.5f, 1), FVector2D(110, -24));
			GameplayWidgets.Add(PromptPanel);
		}
		// ---------------- lab (right) ----------------
		{
			LabPanel = Panel(Fill, Rim, 10.f, FMargin(14, 12, 14, 12));
			UVerticalBox* V = VBox();
			UHorizontalBox* Hd = HBox();
			Hd->AddChildToHorizontalBox(Label(TEXT("原理实验台"), 19, Gold, true));
			Hd->AddChildToHorizontalBox(Gap(10, 1));
			Hd->AddChildToHorizontalBox(Label(TEXT("点击切换 · Tab 关闭"), 12, Dim))->SetVerticalAlignment(VAlign_Center);
			V->AddChildToVerticalBox(Hd);
			auto Section = [&](const FString& S) { V->AddChildToVerticalBox(Label(S, 13, Sea, true))->SetPadding(FMargin(2, 9, 0, 2)); };
			Section(TEXT("海面与巨浪"));
			Row(V, ESOLab::LayerView, false); Row(V, ESOLab::SeaState, false); Row(V, ESOLab::SwellScale, true);
			Row(V, ESOLab::Storm, false); Row(V, ESOLab::StormOverlay, false); Row(V, ESOLab::Foam, false); Row(V, ESOLab::LevelTint, false); Row(V, ESOLab::VisualGPU, false);
			Section(TEXT("船体浮力"));
			Row(V, ESOLab::Buoyancy, false); Row(V, ESOLab::ProbeCount, true); Row(V, ESOLab::PhysicsHz, false); Row(V, ESOLab::InverseQuery, false); Row(V, ESOLab::Probes, false);
			Section(TEXT("多人一致性"));
			Row(V, ESOLab::ClockMode, false); Row(V, ESOLab::Presentation, false); Row(V, ESOLab::InterpDelay, true); Row(V, ESOLab::NetLag, false);
			Row(V, ESOLab::SnapshotHz, false); Row(V, ESOLab::ClockError, true); Row(V, ESOLab::Ghost, false); Row(V, ESOLab::CameraRoll, false);
			LabPanel->SetContent(Sized(V, 430.f, 0));
			Put(LabPanel, FVector2D(1, 0.5f), FVector2D(1, 0.5f), FVector2D(-16, 10));
			LabHint = Panel(FillSoft, Rim, 8.f, FMargin(10, 6, 12, 7));
			UHorizontalBox* H = HBox();
			H->AddChildToHorizontalBox(KeyCap(TEXT("Tab"), 12));
			H->AddChildToHorizontalBox(Gap(8, 1));
			H->AddChildToHorizontalBox(Label(TEXT("原理实验台"), 14, Ink, true))->SetVerticalAlignment(VAlign_Center);
			H->AddChildToHorizontalBox(Gap(12, 1));
			H->AddChildToHorizontalBox(KeyCap(TEXT("H"), 12));
			H->AddChildToHorizontalBox(Gap(8, 1));
			H->AddChildToHorizontalBox(Label(TEXT("按键说明"), 14, Ink, true))->SetVerticalAlignment(VAlign_Center);
			LabHint->SetContent(H);
			Put(LabHint, FVector2D(1, 0), FVector2D(1, 0), FVector2D(-18, 16));
			GameplayWidgets.Add(LabHint);
		}
		// ---------------- toast (top-center, under the compass) ----------------
		{
			ToastPanel = Panel(Fill, Rim, 10.f, FMargin(16, 10, 16, 12));
			UVerticalBox* V = VBox();
			ToastTitle = Text(16, Gold, true);
			ToastBody = Text(14, Ink);
			ToastBody->SetAutoWrapText(true);
			V->AddChildToVerticalBox(ToastTitle);
			V->AddChildToVerticalBox(ToastBody)->SetPadding(FMargin(0, 4, 0, 0));
			ToastPanel->SetContent(Sized(V, 620.f, 0));
			Put(ToastPanel, FVector2D(0.5f, 0), FVector2D(0.5f, 0), FVector2D(0, 78));
		}
		// ---------------- intro card ----------------
		{
			Intro = Panel(FLinearColor(0.008f, 0.018f, 0.03f, 0.93f), Rim, 14.f, FMargin(34, 26, 34, 26));
			UVerticalBox* V = VBox();
			V->AddChildToVerticalBox(Label(TEXT("共享之海"), 40, Gold, true));
			V->AddChildToVerticalBox(Label(TEXT("《盗贼之海》式海面：FFT 巨浪、船体浮力与多人一致性 · GameTech-Lab 复现"), 15, Dim));
			V->AddChildToVerticalBox(Gap(1, 14));
			V->AddChildToVerticalBox(Label(TEXT("海面是“种子、海况参数、时间”的纯函数：每台机器用同步后的时钟各自计算同一片海，网络上只传时钟和几个参数。船由服务器模拟并以快照复制；甲板上的水手相对船体移动。"), 15, Ink, false, 700.f));
			V->AddChildToVerticalBox(Gap(1, 10));
			V->AddChildToVerticalBox(Label(TEXT("目标：掌舵出航，收集海上漂流的宝箱。打开原理实验台，逐项切换各个机制，观察海浪、船体和两端画面的变化。双开窗口（Run2P.bat）可以对比主机和客户端。"), 15, Ink, false, 700.f));
			V->AddChildToVerticalBox(Gap(1, 16));
			const TPair<const TCHAR*, const TCHAR*> Keys[] = {
				{ TEXT("W A S D"), TEXT("移动；掌舵时 W/S 升降帆，A/D 转舵") }, { TEXT("鼠标"), TEXT("环视") }, { TEXT("E"), TEXT("在舵轮旁掌舵 / 离开") },
				{ TEXT("空格"), TEXT("跳跃") }, { TEXT("Tab"), TEXT("原理实验台（可点击）") }, { TEXT("F1–F10"), TEXT("逐项切换原理，见实验台") }, { TEXT("H"), TEXT("按键说明") } };
			for (const auto& K : Keys)
			{
				UHorizontalBox* H = HBox();
				H->AddChildToHorizontalBox(Sized(KeyCap(K.Key, 13), 96.f, 0));
				H->AddChildToHorizontalBox(Gap(12, 1));
				H->AddChildToHorizontalBox(Label(K.Value, 15, Ink))->SetVerticalAlignment(VAlign_Center);
				V->AddChildToVerticalBox(H)->SetPadding(FMargin(0, 3, 0, 3));
			}
			V->AddChildToVerticalBox(Gap(1, 18));
			V->AddChildToVerticalBox(Label(TEXT("按 Enter 起航"), 20, Gold, true))->SetHorizontalAlignment(HAlign_Center);
			Intro->SetContent(V);
			Put(Intro, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f), FVector2D(0, 0));
		}
		// ---------------- key reference ----------------
		{
			Help = Panel(FLinearColor(0.008f, 0.018f, 0.03f, 0.94f), Rim, 12.f, FMargin(26, 20, 26, 20));
			UVerticalBox* V = VBox();
			V->AddChildToVerticalBox(Label(TEXT("按键说明"), 24, Gold, true));
			V->AddChildToVerticalBox(Gap(1, 10));
			const TPair<const TCHAR*, const TCHAR*> Keys[] = {
				{ TEXT("W A S D / 鼠标 / 空格"), TEXT("移动、环视、跳跃") }, { TEXT("E"), TEXT("掌舵 / 离开舵轮；掌舵时 W/S 升降帆，A/D 转舵") },
				{ TEXT("F1"), TEXT("海面分层：全部 / 玩法波段 / 画面波段 / 波段着色") }, { TEXT("F2"), TEXT("浮力探针与查询点") },
				{ TEXT("F3"), TEXT("海浪时钟（客户端）：网络时钟 / 引擎默认 / 本地时钟") }, { TEXT("F4"), TEXT("船的呈现（客户端）：整体插值 / 海面回退 / 竖直重建") },
				{ TEXT("F5"), TEXT("海况：平静 / 普通 / 风暴（服务器）") }, { TEXT("F6"), TEXT("风暴区显示") },
				{ TEXT("F7"), TEXT("模拟网络延迟 0 / 100 / 200 / 350 ms") }, { TEXT("F8"), TEXT("快照幽灵（客户端）") },
				{ TEXT("F9"), TEXT("浮力模型：探针受力 / 平面拟合 + 弹簧（服务器）") }, { TEXT("F10"), TEXT("服务器物理频率 60 / 30 / 10 Hz") },
				{ TEXT("[ ]"), TEXT("浮力探针数量 1 / 2 / 4 / 8 / 16") }, { TEXT(", ."), TEXT("涌浪倍率") }, { TEXT("- ="), TEXT("插值延迟") },
				{ TEXT("Tab / H / Esc"), TEXT("实验台 / 本说明 / 关闭") } };
			for (const auto& K : Keys)
			{
				UHorizontalBox* H = HBox();
				H->AddChildToHorizontalBox(Sized(KeyCap(K.Key, 13), 170.f, 0));
				H->AddChildToHorizontalBox(Gap(12, 1));
				H->AddChildToHorizontalBox(Label(K.Value, 15, Ink))->SetVerticalAlignment(VAlign_Center);
				V->AddChildToVerticalBox(H)->SetPadding(FMargin(0, 3, 0, 3));
			}
			Help->SetContent(V);
			Put(Help, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f), FVector2D(0, 0));
		}
	}
	return Super::RebuildWidget();
}

void USOHud::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	ASOPlayerController* P = PC();
	if (!P) return;
	const AOceanManager* O = AOceanManager::Get(P);
	const ASOGameState* GS = ASOGameState::Get(P);
	ASOShip* Ship = P->GetShip();
	ASOCharacter* Sailor = P->GetSailor();
	const UWorld* W = P->GetWorld();
	const double Now = W->GetTimeSeconds();

	Intro->SetVisibility(P->bIntroOpen ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	Help->SetVisibility(P->bHelpOpen && !P->bIntroOpen ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	for (UWidget* G : GameplayWidgets) G->SetVisibility(P->bIntroOpen ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	LabPanel->SetVisibility(P->bLabOpen && !P->bIntroOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	LabHint->SetVisibility(!P->bLabOpen && !P->bIntroOpen ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	const float ToastAge = (float)(Now - P->ToastTime);
	ToastPanel->SetVisibility(ToastAge < 7.f && !P->bIntroOpen ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	ToastPanel->SetRenderOpacity(FMath::Clamp(7.f - ToastAge, 0.f, 1.f));
	ToastTitle->SetText(T(P->ToastTitle));
	ToastBody->SetText(T(P->ToastBody));

	// crew card
	const USONetClock* C = P->GetNetClock();
	RoleText->SetText(T(P->IsHost() ? TEXT("主机（权威服务器）") : FString::Printf(TEXT("客户端 · 往返 %.0f ms"), C->LastRttMs)));
	if (O && GS)
		SeaText->SetText(T(FString::Printf(TEXT("海况 %s · 涌浪 × %.2f%s"), *AOceanManager::SeaStateName((ESOSeaState)GS->Sea.Current).ToString(), GS->Sea.SwellScale, O->GetStateBlend() < 0.99f ? TEXT(" · 过渡中") : TEXT(""))));
	TreasureText->SetText(T(FString::Printf(TEXT("宝箱 %d"), GS ? GS->Treasure : 0)));
	float CamStorm = 0.f;
	for (TActorIterator<ASOEnvironment> It(W); It; ++It) { CamStorm = It->GetCameraStormWeight(); break; }
	StormChip->SetVisibility(CamStorm > 0.35f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	// compass: bearings relative to the camera yaw, +-100 degrees across 620 px
	float CamYaw = 0.f;
	FVector CamPos = FVector::ZeroVector;
	if (P->PlayerCameraManager) { CamYaw = P->PlayerCameraManager->GetCameraRotation().Yaw; CamPos = P->PlayerCameraManager->GetCameraLocation(); }
	auto Place = [&](UWidget* Wd, float Bearing, float Y)
	{
		const float D = FMath::FindDeltaAngleDegrees(CamYaw, Bearing);
		UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Wd->Slot);
		const bool bShow = FMath::Abs(D) < 100.f;
		Wd->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (S) S->SetPosition(FVector2D(310.f + D / 100.f * 290.f, Y));
	};
	// UE yaw: 0 = +X ("north" here), 90 = +Y ("east")
	for (int32 I = 0; I < CompassTicks.Num(); ++I) Place(CompassTicks[I], I * 45.f, 15.f);
	auto BearingTo = [&](FVector2D Target) { const FVector2D D = Target - FVector2D(CamPos); return FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)); };
	Place(CompassMarks[0], 30.f, 34.f);
	if (O && GS && GS->Sea.bStorm) Place(CompassMarks[1], BearingTo(O->StormCenterCm()), 34.f); else CompassMarks[1]->SetVisibility(ESlateVisibility::Collapsed);
	{
		float Best = 1e12f; FVector2D BestP(0, 0);
		for (TActorIterator<ASOChest> It(W); It; ++It) { const float D = FVector2D::Distance(It->Drift, FVector2D(CamPos)); if (D < Best) { Best = D; BestP = It->Drift; } }
		if (Best < 1e11f) Place(CompassMarks[2], BearingTo(BestP), 34.f);
	}
	Place(CompassMarks[3], BearingTo(FVector2D(65000.f + 1500.f, 42000.f - 900.f)), 34.f);
	for (TActorIterator<ASOShip> It(W); It; ++It) if (It->bAutopilot || (Ship && *It != Ship)) { Place(CompassMarks[4], BearingTo(FVector2D(It->GetActorLocation())), 34.f); break; }

	// ship gauges
	if (Ship)
	{
		const FRotator R = Ship->GetActorRotation();
		const float Heave = (float)Ship->GetActorLocation().Z - 110.f;
		ShipValues[0]->SetText(T(FString::Printf(TEXT("%.1f 节 · 航向 %03.0f°"), Ship->GetSpeedKnots(), FMath::Fmod(R.Yaw + 360.f, 360.f))));
		ShipValues[1]->SetText(T(FString::Printf(TEXT("%.0f%% · %+.0f%%"), Ship->Sail * 100.f, Ship->Rudder * 100.f)));
		ShipValues[2]->SetText(T(FString::Printf(TEXT("%+.2f m"), Heave * 0.01f)));
		ShipValues[3]->SetText(T(FString::Printf(TEXT("%+.1f° / %+.1f°"), R.Pitch, R.Roll)));
		if (P->IsHost())
		{
			ShipValues[4]->SetText(T(FString::Printf(TEXT("%+.0f cm（权威）"), Ship->GetDraftErrorCm())));
			ShipValues[5]->SetText(T(TEXT("—（本机即服务器）")));
			ShipValues[5]->SetColorAndOpacity(FSlateColor(Dim));
		}
		else
		{
			// What matters across machines is whether the hull sits on this sea the way it sits on the server's sea.
			const float Dd = Ship->GetDraftDeltaCm();
			ShipValues[4]->SetText(T(FString::Printf(TEXT("本机 %+.0f / 服务器 %+.0f cm"), Ship->GetDraftErrorCm(), Ship->GetShownServerDraft())));
			ShipValues[5]->SetText(T(FString::Printf(TEXT("%+.0f cm  %s"), Dd, FMath::Abs(Dd) < 25.f ? TEXT("一致") : TEXT("不一致"))));
			ShipValues[5]->SetColorAndOpacity(FSlateColor(FMath::Abs(Dd) < 25.f ? Good : (FMath::Abs(Dd) < 60.f ? Warn : Bad)));
		}
		ShipValues[6]->SetText(T(GS ? FString::Printf(TEXT("%s · %d 探针 · %d Hz"), GS->Settings.Buoyancy ? TEXT("平面拟合") : TEXT("探针受力"), GS->Settings.ProbeCount, GS->Settings.PhysicsHz) : TEXT("-")));
	}
	if (O) ShipValues[7]->SetText(T(FString::Printf(TEXT("%.1f m · 海面计算 %.2f ms"), O->GetSignificantHeight(), O->GetEvalMs())));

	// network gauges
	if (P->IsHost())
	{
		NetTitle->SetText(T(TEXT("网络与时钟 · 主机")));
		NetValues[0]->SetText(T(TEXT("—（本机即服务器）")));
		NetValues[1]->SetText(T(FString::Printf(TEXT("%.2f s（权威）"), C->GetServerTimeEstimate())));
		NetValues[2]->SetText(T(FString::Printf(TEXT("%+.0f ms"), C->InjectedErrorMs)));
		NetValues[3]->SetText(T(TEXT("—")));
		NetValues[4]->SetText(T(GS ? (FSOShipState::LastSerializedBits > 0 ? FString::Printf(TEXT("%d Hz 发出 · %d bit/艘"), GS->Settings.SnapshotHz, FSOShipState::LastSerializedBits) : FString::Printf(TEXT("%d Hz · 暂无客户端"), GS->Settings.SnapshotHz)) : TEXT("-")));
		NetValues[5]->SetText(T(TEXT("0 ms")));
		NetValues[6]->SetText(T(TEXT("主机不插值")));
	}
	else
	{
		NetTitle->SetText(T(TEXT("网络与时钟 · 客户端")));
		NetValues[0]->SetText(T(FString::Printf(TEXT("%.0f ms（最小 %.0f）· 模拟 %d"), C->LastRttMs, C->MinRttMs, (int32)P->NetLagMs)));
		NetValues[1]->SetText(T(FString::Printf(TEXT("%.2f s · 抖动 %.1f ms"), C->GetServerTimeEstimate(), C->OffsetJitterMs)));
		const float Err = C->ModeErrorMs();
		NetValues[2]->SetText(T(FString::Printf(TEXT("%+.0f ms"), Err)));
		NetValues[2]->SetColorAndOpacity(FSlateColor(FMath::Abs(Err) < 60.f ? Good : (FMath::Abs(Err) < 300.f ? Warn : Bad)));
		// Upper bound of the height mismatch between two machines: Hs/2 * omega_peak * |error|
		const float Hs = O ? O->GetSignificantHeight() : 1.f;
		const float Omega = FMath::Sqrt(9.81f * 2.f * PI / 140.f);
		const float Dh = FMath::Min(Hs * 0.5f * Omega * FMath::Abs(Err) * 0.001f, Hs);
		NetValues[3]->SetText(T(FString::Printf(TEXT("%.0f cm"), Dh * 100.f)));
		NetValues[3]->SetColorAndOpacity(FSlateColor(Dh < 0.15f ? Good : (Dh < 0.5f ? Warn : Bad)));
		NetValues[4]->SetText(T(Ship ? FString::Printf(TEXT("%.1f kbit/s · %d bit/个"), Ship->GetSnapshotKbps(), FSOShipState::LastSerializedBits) : TEXT("-")));
		const float Age = Ship ? FMath::Max(Ship->GetSnapshotAgeMs(), 0.f) : 0.f;
		NetValues[5]->SetText(T(FString::Printf(TEXT("%.0f ms"), Age)));
		NetValues[5]->SetColorAndOpacity(FSlateColor(Age < P->InterpDelayMs ? Ink : Warn));
		NetValues[6]->SetText(T(FString::Printf(TEXT("%.0f ms%s"), P->InterpDelayMs, Age > P->InterpDelayMs ? TEXT(" · 缓冲不足") : TEXT(""))));
	}

	// prompt
	FString Prompt;
	if (Sailor && Sailor->IsAtHelm()) Prompt = TEXT("W / S 升降帆　A / D 转舵　E 离开舵轮　鼠标 环视");
	else if (P->GetNearbyHelmShip()) Prompt = TEXT("E 掌舵");
	else Prompt = TEXT("W A S D 移动　空格 跳跃　走到船尾舵轮旁按 E 掌舵");
	PromptText->SetText(T(Prompt));

	// lab values
	if (P->bLabOpen)
		for (int32 I = 0; I < LabValues.Num(); ++I) LabValues[I]->SetText(P->LabValue(LabOrder[I]));
}
