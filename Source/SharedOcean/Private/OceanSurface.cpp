#include "OceanSurface.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "OceanManager.h"
#include "ProceduralMeshComponent.h"

AOceanSurface::AOceanSurface()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	bReplicates = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AOceanSurface::BeginPlay()
{
	Super::BeginPlay();
	UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Ocean/M_Ocean.M_Ocean"));
	for (int32 L = 0; L <= Levels; ++L)		// Levels rings + 1 skirt
	{
		UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(this, *FString::Printf(TEXT("OceanRing%d"), L));
		C->SetupAttachment(RootComponent);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->bReceivesDecals = false;
		C->SetAbsolute(true, true, true);
		C->RegisterComponent();
		if (L < Levels) BuildRing(C, L); else BuildSkirt(C);
		UMaterialInstanceDynamic* MID = Mat ? UMaterialInstanceDynamic::Create(Mat, this) : nullptr;
		if (MID) C->SetMaterial(0, MID);
		Rings.Add(C);
		MIDs.Add(MID);
	}
}

void AOceanSurface::BuildRing(UProceduralMeshComponent* Comp, int32 Level) const
{
	const float S = BaseCell * (1 << Level);
	const int32 Gc = GridCells, W = Gc + 1;
	TArray<FVector> V;
	TArray<int32> T;
	TArray<FVector> N;
	TArray<FVector2D> UV;
	TArray<FLinearColor> Col;
	V.Reserve(W * W + 2);
	for (int32 J = 0; J <= Gc; ++J)
		for (int32 I = 0; I <= Gc; ++I)
		{
			V.Add(FVector((I - Gc / 2) * S, (J - Gc / 2) * S, 0.f));
			N.Add(FVector::UpVector);
			UV.Add(FVector2D(I - Gc / 2, J - Gc / 2));
			Col.Add(FLinearColor(Level / 8.f, 0, 0, 1));
		}
	// Hole: two cells smaller than the finer ring on each side; the overlap is discarded in the material.
	const float Hole = Level == 0 ? -1.f : float(Gc / 4 - 1);
	for (int32 J = 0; J < Gc; ++J)
		for (int32 I = 0; I < Gc; ++I)
		{
			const float Ci = I - Gc / 2 + 0.5f, Cj = J - Gc / 2 + 0.5f;
			if (FMath::Abs(Ci) < Hole && FMath::Abs(Cj) < Hole) continue;
			const int32 A = J * W + I, B = A + 1, D = A + W, E = D + 1;
			// upward-facing in UE winding
			T.Add(A); T.Add(E); T.Add(B);
			T.Add(A); T.Add(D); T.Add(E);
		}
	// Two unreferenced vertices stretch the bounds so displaced waves are not culled.
	V.Add(FVector(0, 0, 3000.f)); N.Add(FVector::UpVector); UV.Add(FVector2D::ZeroVector); Col.Add(FLinearColor::Black);
	V.Add(FVector(0, 0, -3000.f)); N.Add(FVector::UpVector); UV.Add(FVector2D::ZeroVector); Col.Add(FLinearColor::Black);
	TArray<FProcMeshTangent> Tan;
	Comp->CreateMeshSection_LinearColor(0, V, T, N, UV, Col, Tan, false);
}

void AOceanSurface::BuildSkirt(UProceduralMeshComponent* Comp) const
{
	// Flat far ring from the coarsest level out to the horizon; the waves have faded to zero there.
	const float Inner = GridCells * BaseCell * (1 << (Levels - 1)) * 0.5f - BaseCell * (1 << (Levels - 1)) * 2.f;
	const float Outer = 6000000.f;
	TArray<FVector> V = {
		{ -Outer, -Outer, 0 }, { Outer, -Outer, 0 }, { Outer, Outer, 0 }, { -Outer, Outer, 0 },
		{ -Inner, -Inner, 0 }, { Inner, -Inner, 0 }, { Inner, Inner, 0 }, { -Inner, Inner, 0 } };
	TArray<int32> T;
	auto Q = [&T](int32 A, int32 B, int32 C, int32 D) { T.Append({ A, C, B, A, D, C }); };	// upward in UE winding
	Q(0, 1, 5, 4); Q(1, 2, 6, 5); Q(2, 3, 7, 6); Q(3, 0, 4, 7);
	TArray<FVector> N; N.Init(FVector::UpVector, V.Num());
	TArray<FVector2D> UV; UV.Init(FVector2D::ZeroVector, V.Num());
	TArray<FLinearColor> Col; Col.Init(FLinearColor(1, 0, 0, 1), V.Num());
	V.Add(FVector(0, 0, 3000.f)); N.Add(FVector::UpVector); UV.Add(FVector2D::ZeroVector); Col.Add(FLinearColor::Black);
	TArray<FProcMeshTangent> Tan;
	Comp->CreateMeshSection_LinearColor(0, V, T, N, UV, Col, Tan, false);
}

void AOceanSurface::BindTextures(UMaterialInstanceDynamic* MID) const
{
	const AOceanManager* M = AOceanManager::Get(this);
	if (!M || !MID) return;
	static const TCHAR* DNames[] = { TEXT("TD0"), TEXT("TD1"), TEXT("TD2"), TEXT("TD3"), TEXT("TDS0"), TEXT("TDS1") };
	static const TCHAR* NNames[] = { TEXT("TN0"), TEXT("TN1"), TEXT("TN2"), TEXT("TN3"), TEXT("TNS0"), TEXT("TNS1") };
	for (int32 C = 0; C < AOceanManager::NumCascades; ++C)
	{
		if (UTexture* D = M->GetDispTexture(C)) MID->SetTextureParameterValue(DNames[C], D);
		if (UTexture* Nn = M->GetDerivTexture(C)) MID->SetTextureParameterValue(NNames[C], Nn);
	}
}

void AOceanSurface::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (const AOceanManager* M = AOceanManager::Get(this))
		if (M->GetDispTexture(0) && BoundVersion != M->GetTextureVersion())
		{
			for (UMaterialInstanceDynamic* MID : MIDs) BindTextures(MID);
			BoundVersion = M->GetTextureVersion();
		}
	FVector Cam = FVector::ZeroVector;
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		if (PC->PlayerCameraManager) Cam = PC->PlayerCameraManager->GetCameraLocation();

	FVector2D Center[Levels];
	for (int32 L = 0; L < Levels; ++L)
	{
		const double S2 = 2.0 * BaseCell * (1 << L);
		Center[L] = FVector2D(FMath::RoundToDouble(Cam.X / S2) * S2, FMath::RoundToDouble(Cam.Y / S2) * S2);
	}
	for (int32 L = 0; L <= Levels; ++L)
	{
		UProceduralMeshComponent* C = Rings[L];
		UMaterialInstanceDynamic* MID = MIDs[L];
		const int32 Lc = FMath::Min(L, Levels - 1);
		const float S = BaseCell * (1 << Lc);
		const FVector2D Ctr = L < Levels ? Center[L] : Center[Levels - 1];
		C->SetWorldLocation(FVector(Ctr.X, Ctr.Y, 0.0));
		if (!MID) continue;
		MID->SetScalarParameterValue(TEXT("CellSize"), L < Levels ? S : 1e7f);
		MID->SetScalarParameterValue(TEXT("LevelHalf"), L < Levels ? GridCells * S * 0.5f : 1e9f);
		MID->SetScalarParameterValue(TEXT("LevelTint"), bTintLevels ? (float)L : -1.f);
		FVector2D Min(1e9, 1e9), Max(-1e9, -1e9);
		if (L > 0)
		{
			const int32 Inner = L - 1;
			const float Hs = GridCells * BaseCell * (1 << Inner) * 0.5f;
			Min = Center[Inner] - FVector2D(Hs, Hs);
			Max = Center[Inner] + FVector2D(Hs, Hs);
		}
		MID->SetVectorParameterValue(TEXT("InnerRect"), FLinearColor(Min.X, Min.Y, Max.X, Max.Y));
	}
}
