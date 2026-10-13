#include "SOPlayerController.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerInput.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "OceanManager.h"
#include "OceanSurface.h"
#include "SOCharacter.h"
#include "SOGameState.h"
#include "SOHud.h"
#include "UnrealClient.h"

namespace
{
	const int32 ProbeSteps[] = { 1, 2, 4, 8, 16 };
	const float LagSteps[] = { 0.f, 100.f, 200.f, 350.f };
}

ASOPlayerController::ASOPlayerController()
{
	NetClock = CreateDefaultSubobject<USONetClock>(TEXT("NetClock"));
	bShowMouseCursor = false;
}

void ASOPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController()) return;
	Hud = CreateWidget<USOHud>(this, USOHud::StaticClass());
	if (Hud) Hud->AddToViewport();
	SetInputMode(FInputModeGameOnly());
	FParse::Value(FCommandLine::Get(), TEXT("-SOCapture="), CaptureDir);
	FParse::Value(FCommandLine::Get(), TEXT("-SOCaptureRole="), CaptureRole);
	if (!CaptureDir.IsEmpty())
	{
		if (CaptureRole.IsEmpty()) CaptureRole = TEXT("host");
		IFileManager::Get().MakeDirectory(*CaptureDir, true);
		UE_LOG(LogTemp, Display, TEXT("SOTEST start role=%s dir=%s"), *CaptureRole, *CaptureDir);
	}
}

void ASOPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	const FKey Keys[] = { EKeys::E, EKeys::Tab, EKeys::H, EKeys::Escape, EKeys::Enter, EKeys::SpaceBar,
		EKeys::F1, EKeys::F2, EKeys::F3, EKeys::F4, EKeys::F5, EKeys::F6, EKeys::F7, EKeys::F8, EKeys::F9, EKeys::F10,
		EKeys::LeftBracket, EKeys::RightBracket, EKeys::Comma, EKeys::Period, EKeys::Hyphen, EKeys::Equals };
	for (const FKey& K : Keys) InputComponent->BindKey(K, IE_Pressed, this, &ASOPlayerController::OnKey);
	// Mouse look must be read while the input stack is processed; the raw mouse delta is reset afterwards.
	InputComponent->BindAxisKey(EKeys::MouseX, this, &ASOPlayerController::OnMouseX);
	InputComponent->BindAxisKey(EKeys::MouseY, this, &ASOPlayerController::OnMouseY);
}

ASOCharacter* ASOPlayerController::GetSailor() const { return Cast<ASOCharacter>(GetPawn()); }

ASOShip* ASOPlayerController::GetShip() const
{
	if (ASOCharacter* S = GetSailor())
	{
		if (S->HelmShip) return S->HelmShip;
		if (UPrimitiveComponent* Base = S->GetMovementBase())
			if (ASOShip* Sh = Cast<ASOShip>(Base->GetOwner())) return Sh;
	}
	for (TActorIterator<ASOShip> It(GetWorld()); It; ++It) return *It;
	return nullptr;
}

ASOShip* ASOPlayerController::GetNearbyHelmShip() const
{
	ASOCharacter* S = GetSailor();
	if (!S) return nullptr;
	for (TActorIterator<ASOShip> It(GetWorld()); It; ++It)
		if (FVector::Dist(It->GetHelmLocation(), S->GetActorLocation()) < 320.f) return *It;
	return nullptr;
}

void ASOPlayerController::StartGame()
{
	bIntroOpen = false;
	Toast(TEXT("出航"), TEXT("走到船尾的舵轮旁按 E 掌舵。按 Tab 打开原理实验台，F1–F10 逐项切换原理。"));
}

void ASOPlayerController::OnKey(FKey Key)
{
	if (bIntroOpen)
	{
		if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::E) StartGame();
		return;
	}
	if (Key == EKeys::Tab)
	{
		bLabOpen = !bLabOpen;
		bShowMouseCursor = bLabOpen;
		if (bLabOpen) { FInputModeGameAndUI M; M.SetHideCursorDuringCapture(false); M.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock); SetInputMode(M); }
		else SetInputMode(FInputModeGameOnly());
		return;
	}
	if (Key == EKeys::H) { bHelpOpen = !bHelpOpen; return; }
	if (Key == EKeys::Escape) { bHelpOpen = false; if (bLabOpen) OnKey(EKeys::Tab); return; }
	if (Key == EKeys::E) { ServerInteract(); return; }
	if (Key == EKeys::SpaceBar) { if (ASOCharacter* S = GetSailor()) if (!S->IsAtHelm()) S->Jump(); return; }
	static const TPair<FKey, ESOLab> Map[] = {
		{ EKeys::F1, ESOLab::LayerView }, { EKeys::F2, ESOLab::Probes }, { EKeys::F3, ESOLab::ClockMode }, { EKeys::F4, ESOLab::Presentation },
		{ EKeys::F5, ESOLab::SeaState }, { EKeys::F6, ESOLab::StormOverlay }, { EKeys::F7, ESOLab::NetLag }, { EKeys::F8, ESOLab::Ghost },
		{ EKeys::F9, ESOLab::Buoyancy }, { EKeys::F10, ESOLab::PhysicsHz } };
	for (const auto& P : Map) if (Key == P.Key) { Lab(P.Value, 1); return; }
	if (Key == EKeys::LeftBracket) Lab(ESOLab::ProbeCount, -1);
	else if (Key == EKeys::RightBracket) Lab(ESOLab::ProbeCount, 1);
	else if (Key == EKeys::Comma) Lab(ESOLab::SwellScale, -1);
	else if (Key == EKeys::Period) Lab(ESOLab::SwellScale, 1);
	else if (Key == EKeys::Hyphen) Lab(ESOLab::InterpDelay, -1);
	else if (Key == EKeys::Equals) Lab(ESOLab::InterpDelay, 1);
}

// ---------------------------------------------------------------------------------------------------------------
// Lab switches

bool ASOPlayerController::LabIsServer(ESOLab W)
{
	switch (W)
	{
	case ESOLab::SeaState: case ESOLab::Buoyancy: case ESOLab::PhysicsHz: case ESOLab::ProbeCount: case ESOLab::SwellScale:
	case ESOLab::Storm: case ESOLab::InverseQuery: case ESOLab::SnapshotHz: return true;
	default: return false;
	}
}

FText ASOPlayerController::LabName(ESOLab W)
{
	static const TCHAR* N[] = { TEXT("海面分层"), TEXT("浮力探针显示"), TEXT("海浪时钟"), TEXT("船的呈现"), TEXT("海况"), TEXT("风暴区显示"),
		TEXT("模拟网络延迟"), TEXT("快照幽灵"), TEXT("浮力模型"), TEXT("服务器物理频率"), TEXT("浮力探针数量"), TEXT("涌浪倍率"),
		TEXT("插值延迟"), TEXT("移动风暴"), TEXT("高度查询"), TEXT("快照频率"), TEXT("注入时钟误差"), TEXT("镜头随船横滚"), TEXT("clipmap 层级着色"), TEXT("泡沫"), TEXT("画面波段计算") };
	return FText::FromString(N[FMath::Clamp((int32)W, 0, (int32)ESOLab::Count - 1)]);
}

FText ASOPlayerController::LabKey(ESOLab W)
{
	static const TCHAR* K[] = { TEXT("F1"), TEXT("F2"), TEXT("F3"), TEXT("F4"), TEXT("F5"), TEXT("F6"), TEXT("F7"), TEXT("F8"), TEXT("F9"), TEXT("F10"),
		TEXT("[ ]"), TEXT(", ."), TEXT("- ="), TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT("") };
	return FText::FromString(K[FMath::Clamp((int32)W, 0, (int32)ESOLab::Count - 1)]);
}

FText ASOPlayerController::LabValue(ESOLab W) const
{
	const AOceanManager* O = AOceanManager::Get(this);
	const ASOGameState* GS = ASOGameState::Get(this);
	const AOceanSurface* Surf = nullptr;
	for (TActorIterator<AOceanSurface> It(GetWorld()); It; ++It) { Surf = *It; break; }
	const ASOCharacter* S = GetSailor();
	FString V;
	switch (W)
	{
	case ESOLab::LayerView:
	{
		static const TCHAR* L[] = { TEXT("全部波段"), TEXT("只看玩法波段"), TEXT("只看画面波段"), TEXT("波段着色") };
		V = O ? L[(int32)O->LayerView] : TEXT("-");
		break;
	}
	case ESOLab::Probes: V = bShowProbes ? TEXT("显示") : TEXT("隐藏"); break;
	case ESOLab::ClockMode:
	{
		static const TCHAR* L[] = { TEXT("网络时钟（NTP 式）"), TEXT("引擎默认服务器时间"), TEXT("各自的本地时钟") };
		V = IsHost() ? TEXT("主机：权威时钟") : L[(int32)NetClock->Mode];
		break;
	}
	case ESOLab::Presentation:
	{
		static const TCHAR* L[] = { TEXT("整体快照插值"), TEXT("海面同步回退"), TEXT("水平插值 + 竖直重建") };
		V = IsHost() ? TEXT("主机：权威状态") : L[(int32)Presentation];
		break;
	}
	case ESOLab::SeaState: V = GS ? AOceanManager::SeaStateName((ESOSeaState)GS->Sea.Current).ToString() : TEXT("-"); break;
	case ESOLab::StormOverlay: V = (O && O->bStormOverlay) ? TEXT("显示") : TEXT("隐藏"); break;
	case ESOLab::NetLag: V = FString::Printf(TEXT("%d ms"), (int32)NetLagMs); break;
	case ESOLab::Ghost: V = bShowGhost ? TEXT("显示") : TEXT("隐藏"); break;
	case ESOLab::Buoyancy: V = (GS && GS->Settings.Buoyancy == (uint8)ESOBuoyancyModel::PlaneFit) ? TEXT("平面拟合 + 弹簧") : TEXT("浮力探针"); break;
	case ESOLab::PhysicsHz: V = GS ? FString::Printf(TEXT("%d Hz"), GS->Settings.PhysicsHz) : TEXT("-"); break;
	case ESOLab::ProbeCount: V = GS ? FString::Printf(TEXT("%d 个"), GS->Settings.ProbeCount) : TEXT("-"); break;
	case ESOLab::SwellScale: V = GS ? FString::Printf(TEXT("× %.2f"), GS->Sea.SwellScale) : TEXT("-"); break;
	case ESOLab::InterpDelay: V = FString::Printf(TEXT("%d ms"), (int32)InterpDelayMs); break;
	case ESOLab::Storm: V = (GS && GS->Sea.bStorm) ? TEXT("开") : TEXT("关"); break;
	case ESOLab::InverseQuery: V = (GS && !GS->Settings.bInverseQuery) ? TEXT("直接按 x 求值") : TEXT("反解水平位移"); break;
	case ESOLab::SnapshotHz: V = GS ? FString::Printf(TEXT("%d Hz"), GS->Settings.SnapshotHz) : TEXT("-"); break;
	case ESOLab::ClockError: V = FString::Printf(TEXT("%+d ms"), (int32)NetClock->InjectedErrorMs); break;
	case ESOLab::CameraRoll: V = (S && S->bCameraFollowsRoll) ? TEXT("跟随") : TEXT("保持水平"); break;
	case ESOLab::LevelTint: V = (Surf && Surf->bTintLevels) ? TEXT("显示") : TEXT("隐藏"); break;
	case ESOLab::Foam: V = (O && O->bFoam) ? TEXT("开") : TEXT("关"); break;
	case ESOLab::VisualGPU: V = (O && O->bVisualOnGPU) ? TEXT("GPU 计算着色器") : TEXT("CPU"); break;
	default: break;
	}
	return FText::FromString(V);
}

void ASOPlayerController::Toast(const FString& Title, const FString& Body)
{
	ToastTitle = Title;
	ToastBody = Body;
	ToastTime = GetWorld()->GetTimeSeconds();
}

void ASOPlayerController::Lab(ESOLab W, int32 Dir)
{
	AOceanManager* O = AOceanManager::Get(this);
	AOceanSurface* Surf = nullptr;
	for (TActorIterator<AOceanSurface> It(GetWorld()); It; ++It) { Surf = *It; break; }
	ASOCharacter* S = GetSailor();
	FString Body;
	if (LabIsServer(W))
	{
		if (IsHost()) ServerLab_Implementation(W, Dir); else ServerLab(W, Dir);
	}
	switch (W)
	{
	case ESOLab::LayerView:
		if (O) O->LayerView = (ESOLayerView)(((int32)O->LayerView + 1) % (int32)ESOLayerView::Count);
		Body = TEXT("玩法波段（G0、G1，波长 16 米以上）在服务器和每个客户端的 CPU 上用同一份代码求值，船只受它影响；画面波段（V2、V3）只在本机绘制，振幅有上限，不参与任何判定。");
		break;
	case ESOLab::Probes:
		bShowProbes = !bShowProbes;
		Body = TEXT("金色点是船体上的浮力探针，青色点是它查询到的海面高度；淹没越深，向上的箭头越长。力作用在探针位置，所以除了升沉还会产生纵摇和横摇。");
		break;
	case ESOLab::ClockMode:
		if (!IsHost()) NetClock->Mode = (ESOClockMode)(((int32)NetClock->Mode + 1) % (int32)ESOClockMode::Count);
		Body = IsHost() ? TEXT("主机就是时钟源。在客户端窗口里切换这一项，才能看到海浪错相。")
			: TEXT("网络时钟取往返时间最短的样本并限速修正；引擎默认的服务器时间不扣除传输延迟；本地时钟从加入时起算，海面会与服务器完全错相，船看起来沉进水里或悬在空中。");
		break;
	case ESOLab::Presentation:
		if (!IsHost()) Presentation = (ESOShipPresentation)(((int32)Presentation + 1) % (int32)ESOShipPresentation::Count);
		if (O) O->SetPresentationDelay(!IsHost() && Presentation == ESOShipPresentation::RewindSea ? InterpDelayMs * 0.001f : 0.f);
		Body = IsHost() ? TEXT("主机直接显示权威状态。在客户端窗口里切换，比较三种呈现方式下的“吃水偏差”。")
			: TEXT("快照来自过去：整体插值时船落后海面一个插值延迟；海面回退让两者对齐但整片海也晚了；竖直重建只取服务器的水平运动，升沉和摇摆按本机海面计算。");
		break;
	case ESOLab::SeaState:
		Body = TEXT("海况只改变频谱参数：风速、浪高与涌浪振幅。各端用同一组参数和同一个时钟独立求值，网络上只发几个数字，过渡在 8 秒内平滑完成。");
		break;
	case ESOLab::StormOverlay:
		if (O) O->bStormOverlay = !O->bStormOverlay;
		Body = TEXT("风暴中心是共享时钟的函数，沿固定圆周移动，所有机器不通信也能算出同一位置。红色区域内混入风暴频谱，权重随距离平滑衰减。");
		break;
	case ESOLab::NetLag:
	{
		int32 I = 0;
		for (int32 K = 0; K < UE_ARRAY_COUNT(LagSteps); ++K) if (FMath::IsNearlyEqual(LagSteps[K], NetLagMs)) I = K;
		ApplyLag(LagSteps[(I + 1) % UE_ARRAY_COUNT(LagSteps)]);
		Body = IsHost() ? TEXT("延迟加在主机发出的数据包上。到客户端窗口里看往返时间与船的变化。")
			: TEXT("往返时间增加后，网络时钟仍能把海浪对齐（误差只来自上下行不对称）；插值缓冲不足时，船会停在最新快照上。");
		break;
	}
	case ESOLab::Ghost:
		bShowGhost = !bShowGhost;
		Body = TEXT("橙色线框是刚收到的最新快照，实体船是按插值延迟回放的位置。二者的距离就是“为了平滑而付出的延迟”。");
		break;
	case ESOLab::Buoyancy:
		Body = TEXT("平面拟合：用探针处的浪高拟合一个平面，作为船的目标姿态，再用弹簧去追（Atlas, GDC 2019）。服务器物理频率很低时比逐点受力更稳。");
		break;
	case ESOLab::PhysicsHz:
		Body = TEXT("服务器物理频率降到 10 Hz 时，逐点受力的显式积分会变粗糙；平面拟合 + 弹簧仍然平稳。主机端画面在两个物理步之间插值。");
		break;
	case ESOLab::ProbeCount:
		Body = TEXT("1 个探针只有升沉；沿龙骨 2 个才有纵摇；左右舷都有才有横摇。探针越多，短于船长的波相互抵消，船对碎浪越不敏感。");
		break;
	case ESOLab::SwellScale:
		Body = TEXT("单独放大长涌浪的振幅：浪高与波长解耦，这是做“夸张巨浪”最直接的手段（报告 6.2）。改变后各端重建振幅表。");
		break;
	case ESOLab::InterpDelay:
		InterpDelayMs = FMath::Clamp(InterpDelayMs + Dir * 25.f, 0.f, 400.f);
		if (O && !IsHost() && Presentation == ESOShipPresentation::RewindSea) O->SetPresentationDelay(InterpDelayMs * 0.001f);
		Body = TEXT("插值延迟要能容纳至少两个快照间隔加上网络抖动。太小会断档，太大船与本机海面错开更多。");
		break;
	case ESOLab::Storm:
		Body = TEXT("关闭后只剩全局海况；开启后风暴沿固定路径巡游，区域内浪高显著增大。");
		break;
	case ESOLab::InverseQuery:
		Body = TEXT("带水平位移的 FFT 海面不是严格的高度场。直接按 x 求值会在浪峰附近产生误差，船出现穿插或悬空；反解迭代 4 次即可收敛。");
		break;
	case ESOLab::SnapshotHz:
		Body = TEXT("快照频率越低，带宽越省，但需要更长的插值延迟。");
		break;
	case ESOLab::ClockError:
		NetClock->InjectedErrorMs = FMath::Clamp(NetClock->InjectedErrorMs + Dir * 50.f, -1000.f, 1000.f);
		Body = TEXT("两端海面高度差约为 振幅 × 角频率 × 时钟误差。50 ms 的误差在长涌浪上只有几厘米，几百毫秒就肉眼可见。");
		break;
	case ESOLab::CameraRoll:
		if (S) S->bCameraFollowsRoll = !S->bCameraFollowsRoll;
		Body = TEXT("巨浪中镜头完全跟随船体横滚容易晕动。默认让地平线保持水平。");
		break;
	case ESOLab::LevelTint:
		if (Surf) Surf->bTintLevels = !Surf->bTintLevels;
		Body = TEXT("海面网格是以相机为中心的同心环，每环格距加倍，顶点吸附在各自的世界网格上，因此镜头移动时海面不“游泳”。");
		break;
	case ESOLab::VisualGPU:
		if (O) O->bVisualOnGPU = !O->bVisualOnGPU;
		Body = TEXT("画面波段（V2、V3）只影响画面，所以放在 GPU 上：计算着色器完成频谱推进、行列两遍逆 FFT 和雅可比泡沫（RDG）。玩法波段必须留在 CPU，因为服务器和物理都要读它。切换前后画面一致，CPU 耗时下降。");
		break;
	case ESOLab::Foam:
		if (O) O->bFoam = !O->bFoam;
		Body = TEXT("泡沫在雅可比行列式小于阈值（浪峰受挤压）处生成并随时间衰减。它是按时间累积的本地状态，只用于画面，不参与同步。");
		break;
	default: break;
	}
	Toast(LabName(W).ToString(), Body);
}

void ASOPlayerController::ServerLab_Implementation(ESOLab W, int32 Dir)
{
	ASOGameState* GS = ASOGameState::Get(this);
	if (!GS) return;
	FSOServerSettings& S = GS->Settings;
	switch (W)
	{
	case ESOLab::SeaState: GS->SetSeaState((ESOSeaState)((GS->Sea.Current + (Dir >= 0 ? 1 : 2)) % 3)); break;
	case ESOLab::Buoyancy: S.Buoyancy = S.Buoyancy == (uint8)ESOBuoyancyModel::Probes ? (uint8)ESOBuoyancyModel::PlaneFit : (uint8)ESOBuoyancyModel::Probes; break;
	case ESOLab::PhysicsHz: S.PhysicsHz = S.PhysicsHz == 60 ? 30 : (S.PhysicsHz == 30 ? 10 : 60); break;
	case ESOLab::ProbeCount:
	{
		int32 I = 3;
		for (int32 K = 0; K < 5; ++K) if (ProbeSteps[K] == S.ProbeCount) I = K;
		S.ProbeCount = ProbeSteps[FMath::Clamp(I + Dir, 0, 4)];
		break;
	}
	case ESOLab::SwellScale: GS->SetSwellScale(GS->Sea.SwellScale + Dir * 0.25f); break;
	case ESOLab::Storm: GS->SetStorm(!GS->Sea.bStorm); break;
	case ESOLab::InverseQuery: S.bInverseQuery = !S.bInverseQuery; break;
	case ESOLab::SnapshotHz: S.SnapshotHz = S.SnapshotHz == 30 ? 20 : (S.SnapshotHz == 20 ? 10 : 30); break;
	default: break;
	}
}

void ASOPlayerController::ApplyLag(float Ms)
{
	NetLagMs = Ms;
	ConsoleCommand(FString::Printf(TEXT("NetEmulation.PktLag %d"), FMath::RoundToInt(Ms * 0.5f)));
	if (!IsHost()) ServerSetLag(Ms);
}

void ASOPlayerController::ServerSetLag_Implementation(float Ms)
{
	if (UWorld* W = GetWorld()) GEngine->Exec(W, *FString::Printf(TEXT("NetEmulation.PktLag %d"), FMath::RoundToInt(Ms * 0.5f)));
}

// ---------------------------------------------------------------------------------------------------------------
// RPCs

void ASOPlayerController::ServerClockPing_Implementation(double ClientTime)
{
	ClientClockPong(ClientTime, GetWorld()->GetTimeSeconds());
}

void ASOPlayerController::ClientClockPong_Implementation(double ClientTime, double ServerTime)
{
	if (NetClock) NetClock->OnPong(ClientTime, ServerTime);
}

void ASOPlayerController::ServerInteract_Implementation()
{
	ASOCharacter* S = GetSailor();
	if (!S) return;
	if (S->IsAtHelm()) { S->LeaveHelm(); return; }
	if (ASOShip* Ship = GetNearbyHelmShip()) S->TakeHelm(Ship);
}

void ASOPlayerController::ServerHelmInput_Implementation(float Rudder, float SailDelta)
{
	if (ASOCharacter* S = GetSailor())
		if (S->HelmShip && S->HelmShip->Helmsman == S) S->HelmShip->SetHelmInput(Rudder, SailDelta);
}

// ---------------------------------------------------------------------------------------------------------------
// Tick

void ASOPlayerController::OnMouseX(float V)
{
	if (!bLabOpen && !bIntroOpen && V != 0.f) AddYawInput(V);
}

void ASOPlayerController::OnMouseY(float V)
{
	if (!bLabOpen && !bIntroOpen && V != 0.f) AddPitchInput(-V);
}

void ASOPlayerController::UpdateMovement(float Dt)
{
	ASOCharacter* S = GetSailor();
	if (!S || bIntroOpen) return;
	const float F = (IsInputKeyDown(EKeys::W) ? 1.f : 0.f) - (IsInputKeyDown(EKeys::S) ? 1.f : 0.f);
	const float R = (IsInputKeyDown(EKeys::D) ? 1.f : 0.f) - (IsInputKeyDown(EKeys::A) ? 1.f : 0.f);
	if (S->IsAtHelm())
	{
		HelmSendTimer -= Dt;
		if (HelmSendTimer <= 0.f || R != LastRudder || F != LastSail)
		{
			ServerHelmInput(R, F);
			LastRudder = R; LastSail = F;
			HelmSendTimer = 0.1f;
		}
		return;
	}
	const FRotator Yaw(0, GetControlRotation().Yaw, 0);
	if (F != 0.f) S->AddMovementInput(Yaw.RotateVector(FVector::ForwardVector), F);
	if (R != 0.f) S->AddMovementInput(Yaw.RotateVector(FVector::RightVector), R);
}

void ASOPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (!IsLocalController()) return;
	FrameMs = FMath::Lerp(FrameMs, DeltaTime * 1000.f, 0.05f);
	UpdateMovement(DeltaTime);
	DrawDebugViews();
	if (!CaptureDir.IsEmpty()) TickCapture(DeltaTime);
}

void ASOPlayerController::DrawDebugViews()
{
	UWorld* W = GetWorld();
	AOceanManager* O = AOceanManager::Get(this);
	if (!O) return;
	const ASOGameState* GS = ASOGameState::Get(this);
	if (bShowProbes)
	{
		const bool bInv = GS ? GS->Settings.bInverseQuery : true;
		for (TActorIterator<ASOShip> It(W); It; ++It)
		{
			TArray<FVector> P;
			It->GetProbeWorld(P);
			for (const FVector& Q : P)
			{
				const float H = O->HeightCm(FVector2D(Q), bInv ? 4 : 0);
				const FVector Wp(Q.X, Q.Y, H);
				DrawDebugSphere(W, Q, 16.f, 6, FColor(255, 190, 40), false, -1.f, SDPG_Foreground, 2.f);
				DrawDebugSphere(W, Wp, 12.f, 6, FColor(40, 230, 230), false, -1.f, SDPG_Foreground, 2.f);
				DrawDebugLine(W, Q, Wp, FColor(200, 200, 200), false, -1.f, SDPG_Foreground, 1.f);
				const float Depth = H - (float)Q.Z;
				if (Depth > 0.f) DrawDebugDirectionalArrow(W, Q, Q + FVector(0, 0, FMath::Min(Depth * 1.2f, 600.f)), 40.f, FColor(255, 170, 30), false, -1.f, SDPG_Foreground, 4.f);
			}
		}
	}
	if (bShowGhost && !IsHost())
	{
		for (TActorIterator<ASOShip> It(W); It; ++It)
		{
			const FSOShipState* S = It->GetNewestSnapshot();
			if (!S) continue;
			const FVector Ctr = S->Pos + S->Rot.RotateVector(FVector(30.f, 0.f, 50.f));
			DrawDebugBox(W, Ctr, FVector(1080.f, 300.f, 170.f), S->Rot, FColor(255, 130, 30), false, -1.f, SDPG_Foreground, 5.f);
			DrawDebugLine(W, S->Pos + S->Rot.RotateVector(FVector(150, 0, 125)), S->Pos + S->Rot.RotateVector(FVector(150, 0, 1650)), FColor(255, 130, 30), false, -1.f, SDPG_Foreground, 5.f);
		}
	}
	if (O->bStormOverlay && GS && GS->Sea.bStorm)
	{
		const FVector2D C = O->StormCenterCm();
		DrawDebugCircle(W, FVector(C.X, C.Y, 300.0), O->StormRadiusCm(), 96, FColor(255, 70, 50), false, -1.f, SDPG_World, 60.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
		DrawDebugCircle(W, FVector(C.X, C.Y, 300.0), O->StormRadiusCm() + 26000.f, 96, FColor(255, 150, 60), false, -1.f, SDPG_World, 30.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Self-test: injected input, PASS/FAIL lines, screenshots.

void ASOPlayerController::Press(FKey Key)
{
	if (PlayerInput)
	{
		InputKey(FInputKeyParams(Key, IE_Pressed, 1.0, false));
		InputKey(FInputKeyParams(Key, IE_Released, 0.0, false));
	}
}

void ASOPlayerController::Hold(FKey Key, bool bDown)
{
	if (!PlayerInput) return;
	InputKey(FInputKeyParams(Key, bDown ? IE_Pressed : IE_Released, bDown ? 1.0 : 0.0, false));
	if (bDown) HeldKeys.Add(Key); else HeldKeys.Remove(Key);
}

void ASOPlayerController::Shot(const FString& Name)
{
	FScreenshotRequest::RequestScreenshot(FPaths::Combine(CaptureDir, Name + TEXT(".png")), true, false);
	UE_LOG(LogTemp, Display, TEXT("SOTEST SHOT %s"), *Name);
}

void ASOPlayerController::Check(bool bOk, const FString& Name, const FString& Detail)
{
	const FString Line = FString::Printf(TEXT("SOTEST %s %s  %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *Name, *Detail);
	UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
	FFileHelper::SaveStringToFile(Line + LINE_TERMINATOR, *FPaths::Combine(CaptureDir, CaptureRole + TEXT("_results.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
}
