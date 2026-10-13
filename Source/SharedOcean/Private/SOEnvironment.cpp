#include "SOEnvironment.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Math/RandomStream.h"
#include "OceanManager.h"
#include "ProceduralMeshComponent.h"
#include "SOMeshBuilder.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	struct FIslandDef { FVector2D C; float R; float H; int32 Seed; float Rocky; };
	const FIslandDef IslandDefs[] = {
		{ FVector2D(-70000.f, -35000.f), 9500.f, 1800.f, 3, 0.15f },	// start island with the jetty
		{ FVector2D(65000.f, 42000.f), 14000.f, 3200.f, 7, 0.35f },		// lighthouse island
		{ FVector2D(15000.f, -90000.f), 4500.f, 1100.f, 11, 0.8f },		// rocky islet
		{ FVector2D(-30000.f, 95000.f), 11000.f, 2400.f, 19, 0.25f },
		{ FVector2D(-28000.f, -64000.f), 6000.f, 1300.f, 23, 0.3f },	// near the start, to the south
		{ FVector2D(-98000.f, 12000.f), 7500.f, 1900.f, 29, 0.2f },
		{ FVector2D(30000.f, -20000.f), 2600.f, 700.f, 31, 0.9f },		// sea stack
	};
	const FLinearColor Sand(0.86f, 0.75f, 0.52f), WetSand(0.58f, 0.49f, 0.34f), Grass(0.30f, 0.50f, 0.17f), Grass2(0.40f, 0.56f, 0.20f);
	const FLinearColor Rock(0.45f, 0.43f, 0.40f), Rock2(0.36f, 0.34f, 0.32f), Bark(0.46f, 0.34f, 0.21f), Leaf(0.22f, 0.48f, 0.13f), Leaf2(0.30f, 0.58f, 0.17f);
	const FLinearColor Plank(0.52f, 0.36f, 0.20f), Plank2(0.46f, 0.31f, 0.17f), PostCol(0.30f, 0.20f, 0.11f), Iron(0.12f, 0.12f, 0.13f);

	float ShoreMod(float Th, int32 Seed)
	{
		FRandomStream R(Seed);
		const float P1 = R.FRandRange(0, 6.28f), P2 = R.FRandRange(0, 6.28f), P3 = R.FRandRange(0, 6.28f);
		return 1.f + 0.12f * FMath::Sin(3 * Th + P1) + 0.07f * FMath::Sin(5 * Th + P2) + 0.04f * FMath::Sin(9 * Th + P3);
	}
	float IslandHeight(const FIslandDef& D, float Rr, float Th)
	{
		const float T = Rr / (D.R * ShoreMod(Th, D.Seed));
		if (T >= 1.f) return -1300.f * FMath::SmoothStep(1.f, 1.5f, T) - 40.f * (T - 1.f);
		float H = D.H * FMath::Pow(1.f - FMath::SmoothStep(0.18f, 1.f, T), 1.35f);
		H += 0.08f * D.H * FMath::Sin(Th * 4.f + D.Seed) * FMath::Sin(T * 7.f) * (1.f - T);
		return FMath::Max(H, 0.f) + (1.f - T) * 30.f;
	}

	void Palm(FSOMesh& M, FVector Base, float Height, FVector2D Lean, int32 Seed)
	{
		FRandomStream R(Seed);
		const int32 Segs = 6;
		FVector P = Base;
		const FVector LeanDir(Lean.X, Lean.Y, 0);
		for (int32 I = 0; I < Segs; ++I)
		{
			const float T0 = (float)I / Segs, T1 = (float)(I + 1) / Segs;
			const FVector Q = Base + FVector(0, 0, Height * T1) + LeanDir * Height * 0.35f * T1 * T1;
			M.Cylinder(P, Q, FMath::Lerp(24.f, 13.f, T0), FMath::Lerp(24.f, 13.f, T1) * 1.08f, 7, (I & 1) ? Bark : FSOMesh::Shade(Bark, 0.85f), I == 0, false);
			P = Q;
		}
		const int32 Fronds = 8;
		for (int32 F = 0; F < Fronds; ++F)
		{
			const float A = 2 * PI * F / Fronds + R.FRandRange(-0.2f, 0.2f);
			const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0);
			const FVector Side = FVector::CrossProduct(FVector::UpVector, Dir);
			const float Len = R.FRandRange(320.f, 420.f);
			FVector Prev = P;
			for (int32 K = 0; K < 5; ++K)
			{
				const float T = (float)(K + 1) / 5;
				const FVector Next = P + Dir * Len * T + FVector(0, 0, 90.f * T - 260.f * T * T);
				const float W0 = 70.f * FMath::Sin(PI * (float)K / 5 + 0.25f), W1 = 70.f * FMath::Sin(PI * T + 0.25f) * (1.f - T * 0.6f);
				const FLinearColor C = (F & 1) ? Leaf : Leaf2;
				M.Quad(Prev - Side * W0, Next - Side * W1, Next + Side * W1, Prev + Side * W0, C);
				M.Quad(Prev + Side * W0, Next + Side * W1, Next - Side * W1, Prev - Side * W0, FSOMesh::Shade(C, 0.7f));
				Prev = Next;
			}
		}
		for (int32 K = 0; K < 3; ++K) M.Blob(P + FVector(FMath::Cos(K * 2.1f) * 22, FMath::Sin(K * 2.1f) * 22, -25), FVector(14), 3, 5, FLinearColor(0.35f, 0.24f, 0.10f), Seed + K, 0.1f);
	}
}

const TArray<FVector>& ASOEnvironment::Islands()
{
	static TArray<FVector> List;
	if (List.Num() == 0)
		for (const FIslandDef& D : IslandDefs) List.Add(FVector(D.C.X, D.C.Y, D.R * 1.05f));
	return List;
}

ASOEnvironment::ASOEnvironment()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(RootComponent);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetAtmosphereSunLight(true);
	Sun->SetIntensity(9.f);
	Sun->SetLightColor(FLinearColor(1.f, 0.93f, 0.82f));
	Sun->SetWorldRotation(FRotator(-30.f, 212.f, 0.f));
	Sun->bUseTemperature = false;
	Sun->DynamicShadowDistanceMovableLight = 12000.f;
	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
	Atmosphere->SetupAttachment(RootComponent);
	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(RootComponent);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;
	SkyLight->SetIntensity(1.f);
	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(RootComponent);
	Fog->SetFogDensity(0.00025f);
	Fog->SetFogHeightFalloff(0.0012f);
	Fog->SetStartDistance(20000.f);
	Fog->SetFogMaxOpacity(0.75f);
	Fog->SetFogInscatteringColor(FLinearColor(0.42f, 0.55f, 0.70f));
	Clouds = CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("Clouds"));
	Clouds->SetupAttachment(RootComponent);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> CloudMat(TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst"));
	if (CloudMat.Succeeded()) Clouds->SetMaterial(CloudMat.Object);
	Post = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Post"));
	Post->SetupAttachment(RootComponent);
	Post->bUnbound = true;
	FPostProcessSettings& P = Post->Settings;
	P.bOverride_AutoExposureMethod = true; P.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	P.bOverride_AutoExposureApplyPhysicalCameraExposure = true; P.AutoExposureApplyPhysicalCameraExposure = false;
	P.bOverride_AutoExposureBias = true; P.AutoExposureBias = -0.5f;
	P.bOverride_ColorContrast = true; P.ColorContrast = FVector4(1.06f, 1.06f, 1.06f, 1.f);
	P.bOverride_BloomIntensity = true; P.BloomIntensity = 0.35f;
	P.bOverride_VignetteIntensity = true; P.VignetteIntensity = 0.3f;
	P.bOverride_ColorSaturation = true; P.ColorSaturation = FVector4(1.08f, 1.08f, 1.08f, 1.f);
	Beacon = CreateDefaultSubobject<UPointLightComponent>(TEXT("Beacon"));
	Beacon->SetupAttachment(RootComponent);
	Beacon->SetIntensity(80000.f);
	Beacon->SetAttenuationRadius(9000.f);
	Beacon->SetLightColor(FLinearColor(1.f, 0.75f, 0.4f));
	Beacon->SetCastShadows(false);
}

UProceduralMeshComponent* ASOEnvironment::NewMesh(const TCHAR* Name)
{
	UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(this, Name);
	C->SetupAttachment(RootComponent);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->RegisterComponent();
	Meshes.Add(C);
	return C;
}

void ASOEnvironment::BeginPlay()
{
	Super::BeginPlay();
	for (int32 I = 0; I < UE_ARRAY_COUNT(IslandDefs); ++I) BuildIsland(I);
	BuildDock();
}

void ASOEnvironment::BuildIsland(int32 Index)
{
	const FIslandDef& D = IslandDefs[Index];
	UMaterialInterface* VC = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Ocean/M_VertexColor.M_VertexColor"));
	FSOMesh M;
	M.Xf = FTransform(FVector(D.C.X, D.C.Y, 0.f));
	const int32 Rings = 18, Segs = 56;
	auto Pt = [&](int32 K, int32 S)
	{
		const float Th = 2 * PI * S / Segs;
		const float Rr = D.R * 1.5f * FMath::Pow((float)(K + 1) / Rings, 1.2f);
		return FVector(FMath::Cos(Th) * Rr, FMath::Sin(Th) * Rr, IslandHeight(D, Rr, Th));
	};
	FRandomStream Rng(D.Seed * 77);
	auto Colour = [&](const FVector& A, const FVector& B, const FVector& C)
	{
		const float Z = (A.Z + B.Z + C.Z) / 3.f;
		const FVector N = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		const float Steep = 1.f - FMath::Abs((float)N.Z);
		if (Z < -40.f) return WetSand;
		if (Steep > 0.45f - D.Rocky * 0.2f && Z > 120.f) return (Rng.FRand() > 0.5f) ? Rock : Rock2;
		if (Z < 260.f) return FSOMesh::Shade(Sand, 0.95f + Rng.FRand() * 0.1f);
		if (D.Rocky > 0.6f) return Rock;
		return (Rng.FRand() > 0.5f) ? Grass : Grass2;
	};
	const FVector Top(0, 0, IslandHeight(D, 0.f, 0.f));
	for (int32 S = 0; S < Segs; ++S)
	{
		const FVector A = Pt(0, S), B = Pt(0, (S + 1) % Segs);
		M.Tri(Top, A, B, Colour(Top, A, B));
	}
	for (int32 K = 0; K + 1 < Rings; ++K)
		for (int32 S = 0; S < Segs; ++S)
		{
			const FVector A = Pt(K, S), B = Pt(K + 1, S), C = Pt(K + 1, (S + 1) % Segs), Dd = Pt(K, (S + 1) % Segs);
			M.Tri(A, B, C, Colour(A, B, C));
			M.Tri(A, C, Dd, Colour(A, C, Dd));
		}
	// shoreline rocks
	for (int32 I = 0; I < 14 + int32(D.Rocky * 20); ++I)
	{
		const float Th = Rng.FRandRange(0, 2 * PI);
		const float Rr = D.R * ShoreMod(Th, D.Seed) * Rng.FRandRange(0.92f, 1.12f);
		const float S = Rng.FRandRange(120.f, 420.f) * (1.f + D.Rocky);
		M.Blob(FVector(FMath::Cos(Th) * Rr, FMath::Sin(Th) * Rr, IslandHeight(D, Rr, Th) - S * 0.3f), FVector(S, S * 0.8f, S * 0.6f), 4, 7, Rock, D.Seed * 100 + I, 0.25f);
	}
	// palms on the low ground behind the beach
	if (D.Rocky < 0.6f)
	{
		const int32 N = int32(D.R / 700.f);
		for (int32 I = 0; I < N; ++I)
		{
			const float Th = Rng.FRandRange(0, 2 * PI);
			const float Rr = D.R * ShoreMod(Th, D.Seed) * Rng.FRandRange(0.45f, 0.82f);
			const FVector2D Out(FMath::Cos(Th), FMath::Sin(Th));
			Palm(M, FVector(Out.X * Rr, Out.Y * Rr, IslandHeight(D, Rr, Th) - 20.f), Rng.FRandRange(650.f, 1050.f), Out * Rng.FRandRange(0.3f, 0.8f), D.Seed * 1000 + I);
		}
	}
	// a hut on the start island
	if (Index == 0)
	{
		const FVector Hp(-1800.f, -1200.f, IslandHeight(D, 2163.f, FMath::Atan2(-1200.f, -1800.f)));
		M.Box(Hp + FVector(0, 0, 130), FVector(220, 200, 130), FLinearColor(0.62f, 0.47f, 0.30f));
		M.Box(Hp + FVector(221, 0, 90), FVector(2, 55, 90), FLinearColor(0.20f, 0.13f, 0.07f));
		const FVector R0 = Hp + FVector(-260, -240, 260), R1 = Hp + FVector(260, -240, 260), R2 = Hp + FVector(260, 240, 260), R3 = Hp + FVector(-260, 240, 260), Apex = Hp + FVector(0, 0, 450);
		const FLinearColor Thatch(0.64f, 0.50f, 0.24f);
		M.Tri(R0, R1, Apex, Thatch); M.Tri(R1, R2, Apex, FSOMesh::Shade(Thatch, 0.9f)); M.Tri(R2, R3, Apex, Thatch); M.Tri(R3, R0, Apex, FSOMesh::Shade(Thatch, 0.85f));
	}
	M.Commit(NewMesh(*FString::Printf(TEXT("Island%d"), Index)), 0, VC);
	if (Index == 1) BuildLighthouse(FVector(D.C.X + 1500.f, D.C.Y - 900.f, IslandHeight(D, 1750.f, FMath::Atan2(-900.f, 1500.f)) - 30.f));
}

void ASOEnvironment::BuildDock()
{
	UMaterialInterface* VC = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Ocean/M_VertexColor.M_VertexColor"));
	const FIslandDef& D = IslandDefs[0];
	const FVector2D Dir = FVector2D(0.894f, 0.447f);
	const FVector2D Start = D.C + Dir * D.R * 0.86f;
	const float Len = 5200.f, Deck = 150.f;
	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
	FSOMesh M;
	M.Xf = FTransform(FRotator(0, Yaw, 0), FVector(Start.X, Start.Y, 0));
	for (float X = 0.f; X < Len; X += 32.f)
		M.Box(FVector(X, 0, Deck), FVector(14.f, 160.f, 6.f), (int32(X / 32.f) & 1) ? Plank : Plank2, FQuat(FVector::ForwardVector, FMath::DegreesToRadians(FMath::Sin(X * 0.13f) * 1.5f)));
	for (float X = 0.f; X <= Len; X += 300.f)
		for (float S : { -1.f, 1.f })
		{
			M.Cylinder(FVector(X, S * 150.f, -600.f), FVector(X, S * 150.f, Deck + 90.f), 16, 14, 7, PostCol);
			if (X + 300.f <= Len) M.Cylinder(FVector(X, S * 150.f, Deck + 80.f), FVector(X + 300.f, S * 150.f, Deck + 80.f), 3, 3, 4, FLinearColor(0.55f, 0.47f, 0.33f), false, false);
		}
	for (float S : { -1.f, 1.f })
	{
		M.Cylinder(FVector(Len, S * 150.f, Deck), FVector(Len, S * 150.f, Deck + 260.f), 6, 6, 6, Iron);
		M.Box(FVector(Len, S * 150.f, Deck + 285.f), FVector(16, 16, 24), FLinearColor(1.0f, 0.78f, 0.35f));
	}
	for (int32 K = 0; K < 4; ++K)
	{
		const FVector P(Len - 600.f - K * 70.f, (K & 1) ? 90.f : -95.f, Deck + 6.f);
		M.Cylinder(P, P + FVector(0, 0, 95), 33, 33, 10, FLinearColor(0.47f, 0.29f, 0.14f));
		M.Cylinder(P + FVector(0, 0, 20), P + FVector(0, 0, 27), 35, 35, 10, Iron, false, false);
	}
	M.Box(FVector(Len - 950.f, 60.f, Deck + 40.f), FVector(40, 40, 34), FLinearColor(0.58f, 0.42f, 0.22f));
	M.Commit(NewMesh(TEXT("Dock")), 0, VC);
}

void ASOEnvironment::BuildLighthouse(FVector Base)
{
	UMaterialInterface* VC = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Ocean/M_VertexColor.M_VertexColor"));
	UMaterialInterface* Emi = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Ocean/M_Emissive.M_Emissive"));
	FSOMesh M, L;
	const float H = 1900.f;
	const int32 Bands = 6;
	for (int32 I = 0; I < Bands; ++I)
	{
		const float T0 = (float)I / Bands, T1 = (float)(I + 1) / Bands;
		M.Cylinder(Base + FVector(0, 0, H * T0), Base + FVector(0, 0, H * T1), FMath::Lerp(320.f, 200.f, T0), FMath::Lerp(320.f, 200.f, T1), 16,
			(I & 1) ? FLinearColor(0.75f, 0.12f, 0.08f) : FLinearColor(0.93f, 0.91f, 0.86f), I == 0, false);
	}
	M.Cylinder(Base + FVector(0, 0, H), Base + FVector(0, 0, H + 30), 290, 290, 16, FLinearColor(0.15f, 0.15f, 0.16f));
	for (int32 K = 0; K < 12; ++K)
	{
		const float A = 2 * PI * K / 12;
		M.Cylinder(Base + FVector(FMath::Cos(A) * 280, FMath::Sin(A) * 280, H + 30), Base + FVector(FMath::Cos(A) * 280, FMath::Sin(A) * 280, H + 130), 5, 5, 4, Iron, false, false);
	}
	M.Cylinder(Base + FVector(0, 0, H + 330), Base + FVector(0, 0, H + 520), 210, 20, 16, FLinearColor(0.75f, 0.12f, 0.08f));
	L.Cylinder(Base + FVector(0, 0, H + 30), Base + FVector(0, 0, H + 330), 170, 170, 12, FLinearColor(1.0f, 0.82f, 0.45f));
	M.Commit(NewMesh(TEXT("Lighthouse")), 0, VC);
	L.Commit(NewMesh(TEXT("LighthouseLamp")), 0, Emi ? Emi : VC);
	Beacon->SetWorldLocation(Base + FVector(0, 0, H + 180));
}

void ASOEnvironment::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const AOceanManager* O = AOceanManager::Get(this);
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!O || !PC || !PC->PlayerCameraManager) return;
	const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
	CamStorm = FMath::FInterpTo(CamStorm, O->StormWeight(FVector2D(Cam)), DeltaSeconds, 1.5f);
	// Lightning: purely local flashes inside the storm.
	NextFlash -= DeltaSeconds;
	if (CamStorm > 0.5f && NextFlash <= 0.f) { Flash = 1.f; NextFlash = FMath::FRandRange(2.5f, 7.f); }
	Flash = FMath::Max(0.f, Flash - DeltaSeconds * 6.f);
	Sun->SetIntensity(FMath::Lerp(9.f, 1.6f, CamStorm));
	Sun->SetLightColor(FMath::Lerp(FLinearColor(1.f, 0.93f, 0.82f), FLinearColor(0.70f, 0.75f, 0.85f), CamStorm));
	SkyLight->SetIntensity(FMath::Lerp(1.f, 0.45f, CamStorm) + Flash * 3.f);
	Fog->SetFogDensity(FMath::Lerp(0.00025f, 0.010f, CamStorm));
	Fog->SetFogInscatteringColor(FMath::Lerp(FLinearColor(0.42f, 0.55f, 0.70f), FLinearColor(0.22f, 0.26f, 0.30f), CamStorm));
}
