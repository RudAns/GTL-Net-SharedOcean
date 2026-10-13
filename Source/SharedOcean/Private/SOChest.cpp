#include "SOChest.h"

#include "EngineUtils.h"
#include "Math/RandomStream.h"
#include "Net/UnrealNetwork.h"
#include "OceanManager.h"
#include "ProceduralMeshComponent.h"
#include "SOEnvironment.h"
#include "SOGameState.h"
#include "SOMeshBuilder.h"
#include "SOShip.h"

ASOChest::ASOChest()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false);
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(2.f);
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RootComponent = Mesh;
	Beam = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Beam"));
	Beam->SetupAttachment(Mesh);
	Beam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Beam->SetCastShadow(false);
	Beam->SetAbsolute(false, true, false);	// stays vertical while the chest tilts
}

void ASOChest::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASOChest, Drift);
}

void ASOChest::BeginPlay()
{
	Super::BeginPlay();
	UMaterialInterface* VC = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Ocean/M_VertexColor.M_VertexColor"));
	UMaterialInterface* BeamMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Ocean/M_Beam.M_Beam"));
	const FLinearColor Wood(0.42f, 0.24f, 0.11f), Band(0.85f, 0.62f, 0.18f), Dark(0.14f, 0.09f, 0.05f);
	FSOMesh M;
	M.Box(FVector(0, 0, 10), FVector(55, 38, 30), Wood);			// body, mostly under water
	M.Box(FVector(0, 0, 46), FVector(57, 40, 9), FSOMesh::Shade(Wood, 1.1f));	// lid
	for (float X : { -38.f, 0.f, 38.f }) M.Box(FVector(X, 0, 28), FVector(5, 41, 32), Band);
	M.Box(FVector(56, 0, 26), FVector(4, 9, 10), Band);			// lock
	M.Box(FVector(0, 0, 57), FVector(40, 26, 3), Dark);
	M.Commit(Mesh, 0, VC);
	FSOMesh B;
	B.Cylinder(FVector(0, 0, 0), FVector(0, 0, 9000), 90, 40, 12, FLinearColor(1.0f, 0.8f, 0.3f), false, false);
	B.Commit(Beam, 0, BeamMat ? BeamMat : VC);
}

void ASOChest::Respawn(int32 Salt)
{
	if (!HasAuthority()) return;
	FRandomStream Rng(9173 + Salt * 131 + (Spawns++) * 7919);
	for (int32 Try = 0; Try < 64; ++Try)
	{
		const float A = Rng.FRandRange(0.f, 2.f * PI), R = Rng.FRandRange(25000.f, 115000.f);
		const FVector2D P(FMath::Cos(A) * R, FMath::Sin(A) * R);
		bool bOk = true;
		for (const FVector& Is : ASOEnvironment::Islands()) if (FVector2D::Distance(P, FVector2D(Is.X, Is.Y)) < Is.Z + 6000.f) bOk = false;
		if (bOk) { Drift = P; break; }
	}
}

void ASOChest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		Drift += FVector2D(0.87f, 0.5f) * 25.f * DeltaSeconds;		// slow drift with the wind
		for (TActorIterator<ASOShip> It(GetWorld()); It; ++It)
		{
			if (FVector2D::Distance(FVector2D(It->GetActorLocation()), Drift) < 1400.f && !It->bAutopilot)
			{
				if (ASOGameState* GS = ASOGameState::Get(this)) ++GS->Treasure;
				Respawn(Spawns);
				break;
			}
		}
	}
	// Float on this machine's sea: nothing about the bobbing is sent.
	if (AOceanManager* O = AOceanManager::Get(this))
	{
		const float H = O->HeightCm(Drift, 3);
		const FVector N = O->NormalAt(Drift);
		const FQuat Tilt = FQuat::FindBetweenNormals(FVector::UpVector, N);
		const float Spin = (float)FMath::Fmod(GetWorld()->GetTimeSeconds() * 4.0, 360.0);
		SetActorLocationAndRotation(FVector(Drift.X, Drift.Y, H - 18.f), Tilt * FRotator(0, Spin, 0).Quaternion());
	}
}
