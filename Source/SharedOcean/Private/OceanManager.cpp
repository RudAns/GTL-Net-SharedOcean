#include "OceanManager.h"

#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "OceanGPU.h"
#include "RenderGraphBuilder.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialParameterCollection.h"
#include "SOPlayerController.h"
#include "SONetClock.h"

namespace
{
	struct FCascadeDef { int32 N; float Patch; float LMin; float LMax; uint32 Seed; bool bSwell; };
	// G0 G1 V2 V3 S0 S1. Patch sizes are not integer ratios to hide tiling.
	const FCascadeDef Defs[AOceanManager::NumCascades] = {
		{ 64,  768.f, 60.f, 1e6f, 11, true  },
		{ 128, 192.f, 16.f, 60.f, 23, false },
		{ 128, 48.f,  3.2f, 16.f, 37, false },
		{ 64,  11.7f, 0.f,  3.2f, 41, false },
		{ 64,  768.f, 60.f, 1e6f, 11, true  },		// storm versions share the seeds of G0/G1: the storm
		{ 128, 192.f, 16.f, 60.f, 23, false },		// grows the same waves rather than inventing new ones
	};
	constexpr float StormFalloffM = 260.f;
	constexpr float StormPathRadiusM = 900.f;
	constexpr float TransitionSeconds = 8.f;

	FOceanSeaState StateDef(ESOSeaState S)
	{
		FOceanSeaState R;
		switch (S)
		{
		case ESOSeaState::Calm:   R.WindSpeed = 6.f;  R.SignificantHeight = 0.7f;  R.SwellAmplitude = 0.6f;  R.SwellWavelength = 90.f;  R.Choppiness = 0.9f;  break;
		case ESOSeaState::Normal: R.WindSpeed = 11.f; R.SignificantHeight = 2.0f;  R.SwellAmplitude = 1.8f;  R.SwellWavelength = 110.f; R.Choppiness = 1.15f; break;
		default:                  R.WindSpeed = 18.f; R.SignificantHeight = 4.8f;  R.SwellAmplitude = 5.0f;  R.SwellWavelength = 135.f; R.Choppiness = 1.3f;  break;
		}
		R.WindDirDeg = 30.f;
		R.SwellDirDeg = 12.f;
		return R;
	}

	void MakeTexture(TObjectPtr<UTexture2D>& Out, int32 N, const TCHAR* Name)
	{
		Out = UTexture2D::CreateTransient(N, N, PF_FloatRGBA, FName(Name));
		Out->SRGB = false;
		Out->Filter = TF_Bilinear;
		Out->AddressX = TA_Wrap;
		Out->AddressY = TA_Wrap;
		Out->CompressionSettings = TC_HDR;
		Out->NeverStream = true;
		Out->UpdateResource();
	}

	void Upload(UTexture2D* Tex, int32 N, TArray<FFloat16Color>&& Pixels)
	{
		if (!Tex) return;
		FFloat16Color* Data = new FFloat16Color[N * N];
		FMemory::Memcpy(Data, Pixels.GetData(), N * N * sizeof(FFloat16Color));
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, N, N);
		Tex->UpdateTextureRegions(0, 1, Region, N * sizeof(FFloat16Color), sizeof(FFloat16Color), (uint8*)Data,
			[](uint8* SrcData, const FUpdateTextureRegion2D* Regions) { delete[] (FFloat16Color*)SrcData; delete Regions; });
	}
}

AOceanManager::AOceanManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	bReplicates = false;
	static ConstructorHelpers::FObjectFinder<UMaterialParameterCollection> MPCFinder(TEXT("/Game/Ocean/MPC_Ocean.MPC_Ocean"));
	if (MPCFinder.Succeeded()) MPC = MPCFinder.Object;
}

AOceanManager* AOceanManager::Get(const UObject* WorldContext)
{
	UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!W) return nullptr;
	for (TActorIterator<AOceanManager> It(W); It; ++It) return *It;
	return nullptr;
}

const TCHAR* AOceanManager::CascadeName(int32 I)
{
	static const TCHAR* Names[] = { TEXT("G0"), TEXT("G1"), TEXT("V2"), TEXT("V3"), TEXT("S0"), TEXT("S1") };
	return Names[FMath::Clamp(I, 0, NumCascades - 1)];
}

FText AOceanManager::SeaStateName(ESOSeaState S)
{
	switch (S)
	{
	case ESOSeaState::Calm: return FText::FromString(TEXT("平静"));
	case ESOSeaState::Normal: return FText::FromString(TEXT("普通"));
	default: return FText::FromString(TEXT("风暴"));
	}
}

void AOceanManager::BeginPlay()
{
	Super::BeginPlay();
	InitCascades();
}

void AOceanManager::InitCascades()
{
	const bool bRender = !IsRunningDedicatedServer();
	for (int32 C = 0; C < NumCascades; ++C)
	{
		const FCascadeDef& D = Defs[C];
		Cascades[C].Init(D.N, D.Patch, D.LMin, D.LMax, (uint32)(SeaRep.Seed * 31 + D.Seed), D.bSwell);
		if (bRender)
		{
			MakeTexture(DispTex[C], D.N, *FString::Printf(TEXT("T_OceanDisp_%s"), CascadeName(C)));
			MakeTexture(DerivTex[C], D.N, *FString::Printf(TEXT("T_OceanDeriv_%s"), CascadeName(C)));
		}
	}
	BuiltRep = SeaRep;
	RebuildTables();
	if (bRender) InitGPU();
	++TextureVersion;
}

void AOceanManager::InitGPU()
{
	for (int32 G = 0; G < 2; ++G)
	{
		const FCascadeDef& D = Defs[2 + G];
		H0Tex[G] = UTexture2D::CreateTransient(D.N, D.N, PF_A32B32G32R32F, *FString::Printf(TEXT("T_OceanH0_%s"), CascadeName(2 + G)));
		H0Tex[G]->SRGB = false;
		H0Tex[G]->Filter = TF_Nearest;
		H0Tex[G]->CompressionSettings = TC_HDR;
		H0Tex[G]->NeverStream = true;
		H0Tex[G]->UpdateResource();
		auto MakeRT = [&](const TCHAR* Name, EPixelFormat Fmt)
		{
			UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(this, *FString::Printf(TEXT("%s_%s"), Name, CascadeName(2 + G)));
			RT->bCanCreateUAV = true;
			RT->AddressX = TA_Wrap;
			RT->AddressY = TA_Wrap;
			RT->ClearColor = FLinearColor::Black;
			RT->bAutoGenerateMips = false;
			RT->InitCustomFormat(D.N, D.N, Fmt, true);
			RT->UpdateResourceImmediate(true);
			return RT;
		};
		GDisp[G] = MakeRT(TEXT("RT_OceanDisp"), PF_FloatRGBA);
		GDeriv[G] = MakeRT(TEXT("RT_OceanDeriv"), PF_FloatRGBA);
		GFoam[G] = MakeRT(TEXT("RT_OceanFoam"), PF_R32_FLOAT);
		bH0Dirty[G] = true;
	}
}

UTexture* AOceanManager::GetDispTexture(int32 C) const
{
	if (IsCascadeOnGPU(C)) return GDisp[C - 2];
	return DispTex[C];
}

UTexture* AOceanManager::GetDerivTexture(int32 C) const
{
	if (IsCascadeOnGPU(C)) return GDeriv[C - 2];
	return DerivTex[C];
}

void AOceanManager::DispatchGPU(int32 C, float Dt)
{
	const int32 G = C - 2;
	const FOceanCascade& Cs = Cascades[C];
	const int32 N = Cs.GetN();
	if (bH0Dirty[G])
	{
		TArray<FLinearColor> Packed;
		Cs.BuildH0(Packed);
		FLinearColor* Data = new FLinearColor[N * N];
		FMemory::Memcpy(Data, Packed.GetData(), N * N * sizeof(FLinearColor));
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, N, N);
		H0Tex[G]->UpdateTextureRegions(0, 1, Region, N * sizeof(FLinearColor), sizeof(FLinearColor), (uint8*)Data,
			[](uint8* Src, const FUpdateTextureRegion2D* R) { delete[] (FLinearColor*)Src; delete R; });
		bH0Dirty[G] = false;
	}
	FOceanGPUParams P;
	P.N = N;
	P.Patch = Cs.GetPatchSize();
	P.Time = GetOceanTime();
	P.Omega0 = 2.f * PI / LoopPeriod;
	P.Choppiness = Cs.GetChoppiness();
	P.Dt = Dt;
	P.FoamThreshold = 0.72f;
	P.FoamGain = 3.0f;
	P.FoamDecay = C == 3 ? 0.8f : 2.6f;
	P.bFoam = bFoam;
	FTextureResource* H0R = H0Tex[G]->GetResource();
	FTextureRenderTargetResource* DR = GDisp[G]->GameThread_GetRenderTargetResource();
	FTextureRenderTargetResource* NR = GDeriv[G]->GameThread_GetRenderTargetResource();
	FTextureRenderTargetResource* FR = GFoam[G]->GameThread_GetRenderTargetResource();
	if (!H0R || !DR || !NR || !FR) return;
	ENQUEUE_RENDER_COMMAND(SharedOceanGPU)([P, H0R, DR, NR, FR](FRHICommandListImmediate& RHICmdList)
	{
		if (!H0R->TextureRHI) return;
		FRDGBuilder GraphBuilder(RHICmdList);
		OceanGPU_AddPasses(GraphBuilder, P, H0R->TextureRHI, DR->GetRenderTargetTexture(), NR->GetRenderTargetTexture(), FR->GetRenderTargetTexture());
		GraphBuilder.Execute();
	});
}

void AOceanManager::RebuildTables()
{
	for (int32 S = 0; S < (int32)ESOSeaState::Count; ++S)
	{
		FOceanSeaState State = StateDef((ESOSeaState)S);
		State.SwellAmplitude *= SeaRep.SwellScale;
		double Var = 0.0;
		for (int32 C = 0; C < 4; ++C) Var += Cascades[C].BandVariance(State);
		const float H = State.SignificantHeight * 0.25f;
		const float Scale = Var > 0.0 ? float(H * H / Var) : 0.f;
		for (int32 C = 0; C < NumCascades; ++C) Cascades[C].BuildAmplitudes(State, Scale, Tables[C][S]);
	}
	bTablesBuilt = true;
	bH0Dirty[0] = bH0Dirty[1] = true;
}

void AOceanManager::ApplySeaState(const FSOSeaStateRep& Rep)
{
	const bool bReseed = Rep.Seed != SeaRep.Seed;
	const bool bRetable = Rep.SwellScale != SeaRep.SwellScale;
	SeaRep = Rep;
	if (!HasActorBegunPlay()) return;
	if (bReseed) InitCascades();
	else if (bRetable) RebuildTables();
}

void AOceanManager::UpdateTime(float Dt)
{
	double T = GetWorld()->GetTimeSeconds();
	if (GetNetMode() == NM_Client)
	{
		if (ASOPlayerController* PC = Cast<ASOPlayerController>(GetWorld()->GetFirstPlayerController()))
			if (USONetClock* Clock = PC->GetNetClock()) T = Clock->GetOceanClockTime();
	}
	SharedTime = T - PresentationDelay;
}

void AOceanManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateTime(DeltaSeconds);
	const double Start = FPlatformTime::Seconds();

	StateBlend = FMath::SmoothStep(0.f, 1.f, float((SharedTime - SeaRep.ChangeTime) / TransitionSeconds));
	const int32 Prev = FMath::Clamp<int32>(SeaRep.Previous, 0, 2), Cur = FMath::Clamp<int32>(SeaRep.Current, 0, 2);
	const FOceanSeaState SPrev = StateDef((ESOSeaState)Prev), SCur = StateDef((ESOSeaState)Cur), SStorm = StateDef(ESOSeaState::Storm);
	const float Chop = FMath::Lerp(SPrev.Choppiness, SCur.Choppiness, StateBlend);
	const float T = GetOceanTime();
	const bool bRender = !IsRunningDedicatedServer();
	if (bVisualOnGPU != bLastGPU) { bLastGPU = bVisualOnGPU; ++TextureVersion; bH0Dirty[0] = bH0Dirty[1] = true; }

	for (int32 C = 0; C < NumCascades; ++C)
	{
		const bool bStormCascade = C >= 4;
		if (bStormCascade && !SeaRep.bStorm) continue;
		if (!bRender && (C == 2 || C == 3)) continue;		// the visual band never exists on a dedicated server
		const int32 Src = bStormCascade ? (int32)ESOSeaState::Storm : Prev;
		const int32 Dst = bStormCascade ? (int32)ESOSeaState::Storm : Cur;
		Cascades[C].SetAmplitudes(Tables[C][Src], Tables[C][Dst], bStormCascade ? 1.f : StateBlend, bStormCascade ? SStorm.Choppiness : Chop);
		if (bRender && IsCascadeOnGPU(C))
		{
			if (StateBlend < 1.f) bH0Dirty[C - 2] = true;
			DispatchGPU(C, DeltaSeconds);
			continue;
		}
		Cascades[C].Evaluate(T, LoopPeriod, bRender);
		if (bRender) Cascades[C].UpdateFoam(DeltaSeconds, 0.72f, 3.0f, C == 3 ? 0.8f : 2.6f);
	}
	EvalMs = (FPlatformTime::Seconds() - Start) * 1000.0;
	if (bRender)
	{
		UploadTextures();
		PushMaterialParams();
	}
}

void AOceanManager::UploadTextures()
{
	for (int32 C = 0; C < NumCascades; ++C)
	{
		if (C >= 4 && !SeaRep.bStorm) continue;
		if (IsCascadeOnGPU(C)) continue;
		const FOceanCascade& Cs = Cascades[C];
		const int32 N = Cs.GetN(), NN = N * N;
		TArray<FFloat16Color> D, V;
		D.SetNumUninitialized(NN);
		V.SetNumUninitialized(NN);
		for (int32 I = 0; I < NN; ++I)
		{
			const FVector3f& P = Cs.Disp[I];
			D[I] = FFloat16Color(FLinearColor(P.X, P.Y, P.Z, bFoam ? Cs.Foam[I] : 0.f));
			const FVector4f& Q = Cs.Deriv[I];
			V[I] = FFloat16Color(FLinearColor(Q.X, Q.Y, Q.Z, Q.W));
		}
		Upload(DispTex[C], N, MoveTemp(D));
		Upload(DerivTex[C], N, MoveTemp(V));
	}
}

void AOceanManager::PushMaterialParams()
{
	if (!MPC) return;
	UWorld* W = GetWorld();
	const FVector2D SC = StormCenterCm() / 100.0;
	UKismetMaterialLibrary::SetVectorParameterValue(W, MPC, TEXT("Storm"), FLinearColor(SC.X, SC.Y, SeaRep.bStorm ? StormRadiusM : -1.f, StormFalloffM));
	FLinearColor Mask(1, 1, 1, 0);	// x: gameplay, y: visual, z: storm cascades, w: tint bands
	if (LayerView == ESOLayerView::GameplayOnly) Mask = FLinearColor(1, 0, 1, 0);
	else if (LayerView == ESOLayerView::VisualOnly) Mask = FLinearColor(0, 1, 0, 0);
	else if (LayerView == ESOLayerView::TintBands) Mask = FLinearColor(1, 1, 1, 1);
	if (!SeaRep.bStorm) Mask.B = 0.f;
	UKismetMaterialLibrary::SetVectorParameterValue(W, MPC, TEXT("LayerMask"), Mask);
	UKismetMaterialLibrary::SetVectorParameterValue(W, MPC, TEXT("Patch"), FLinearColor(Defs[0].Patch, Defs[1].Patch, Defs[2].Patch, Defs[3].Patch));
	UKismetMaterialLibrary::SetVectorParameterValue(W, MPC, TEXT("LambdaMin"), FLinearColor(60.f, 16.f, 3.2f, 0.4f));
	UKismetMaterialLibrary::SetScalarParameterValue(W, MPC, TEXT("OceanTime"), GetOceanTime());
	UKismetMaterialLibrary::SetScalarParameterValue(W, MPC, TEXT("StormOverlay"), bStormOverlay ? 1.f : 0.f);
	const float Intensity = FMath::Lerp((float)SeaRep.Previous, (float)SeaRep.Current, StateBlend) * 0.5f;
	UKismetMaterialLibrary::SetScalarParameterValue(W, MPC, TEXT("SeaIntensity"), Intensity);
}

FVector AOceanManager::GameplayDisplacementCm(FVector2D RestXY) const
{
	const float X = RestXY.X * 0.01f, Y = RestXY.Y * 0.01f;
	FVector3f D = Cascades[0].SampleDisplacement(X, Y) + Cascades[1].SampleDisplacement(X, Y);
	if (SeaRep.bStorm)
	{
		const float W = StormWeight(RestXY);
		if (W > 0.f) D = FMath::Lerp(D, Cascades[4].SampleDisplacement(X, Y) + Cascades[5].SampleDisplacement(X, Y), W);
	}
	return FVector(D.X, D.Y, D.Z) * 100.0;
}

float AOceanManager::HeightCm(FVector2D WorldXY, int32 Iterations) const
{
	FVector2D Rest = WorldXY;
	for (int32 I = 0; I < Iterations; ++I)
	{
		const FVector D = GameplayDisplacementCm(Rest);
		Rest = WorldXY - FVector2D(D.X, D.Y);
	}
	return (float)GameplayDisplacementCm(Rest).Z;
}

FVector AOceanManager::NormalAt(FVector2D P) const
{
	const float E = 150.f;
	const float Hx0 = HeightCm(P - FVector2D(E, 0), 2), Hx1 = HeightCm(P + FVector2D(E, 0), 2);
	const float Hy0 = HeightCm(P - FVector2D(0, E), 2), Hy1 = HeightCm(P + FVector2D(0, E), 2);
	return FVector(-(Hx1 - Hx0) / (2 * E), -(Hy1 - Hy0) / (2 * E), 1.0).GetSafeNormal();
}

FVector2D AOceanManager::StormCenterCm() const
{
	// The storm's position is a function of the shared clock: every machine (and every server) agrees
	// without sending it, like the looping storm path observed in Sea of Thieves (report ch.2.3).
	const double A = 2.0 * UE_DOUBLE_PI * FMath::Fmod(SharedTime, (double)LoopPeriod) / LoopPeriod + 0.6;
	return FVector2D(FMath::Cos(A), FMath::Sin(A)) * StormPathRadiusM * 100.0;
}

float AOceanManager::StormWeight(FVector2D P) const
{
	if (!SeaRep.bStorm) return 0.f;
	const float D = (float)FVector2D::Distance(P, StormCenterCm()) * 0.01f;
	return 1.f - FMath::SmoothStep(StormRadiusM, StormRadiusM + StormFalloffM, D);
}

float AOceanManager::GetSignificantHeight() const
{
	const float A = StateDef((ESOSeaState)FMath::Clamp<int32>(SeaRep.Previous, 0, 2)).SignificantHeight;
	const float B = StateDef((ESOSeaState)FMath::Clamp<int32>(SeaRep.Current, 0, 2)).SignificantHeight;
	return FMath::Lerp(A, B, StateBlend);
}
