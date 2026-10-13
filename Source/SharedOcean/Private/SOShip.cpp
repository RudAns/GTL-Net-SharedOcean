#include "SOShip.h"

#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "OceanManager.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"
#include "SOEnvironment.h"
#include "SOGameState.h"
#include "SOMeshBuilder.h"
#include "SONetClock.h"
#include "SOPlayerController.h"
#include "UObject/ConstructorHelpers.h"

int32 FSOShipState::LastSerializedBits = 0;

namespace
{
	constexpr float G = 9.81f;
	constexpr float RestDraft = 1.5f;		// m: probe depth at rest; probes sit 1.5 m below the origin
	constexpr float ProbeZ = -1.5f;
	constexpr float DeckZ = 1.25f;			// m above the origin
	const FVector Inertia(4.0, 35.0, 38.0);	// m^2, mass-normalised: roll, pitch, yaw

	FVector ProbeLocal(int32 N, int32 I)
	{
		switch (N)
		{
		case 1: return FVector(0, 0, ProbeZ);
		case 2: return FVector(I == 0 ? -7.0 : 7.0, 0, ProbeZ);
		case 4: return FVector((I & 1) ? 6.5 : -6.5, (I & 2) ? 1.9 : -1.9, ProbeZ);
		case 8:
		{
			static const float Xs[4] = { -7.5f, -2.5f, 2.5f, 7.0f };
			return FVector(Xs[I / 2], (I & 1) ? 2.0 : -2.0, ProbeZ);
		}
		default:
		{
			const float X = -8.4f + 16.4f * float(I / 2) / 7.f;
			return FVector(X, (I & 1) ? 2.1 : -2.1, ProbeZ);
		}
		}
	}

	FQuat YawOf(const FQuat& Q) { return FRotator(0.f, Q.Rotator().Yaw, 0.f).Quaternion(); }

	// ---- hull shape (metres) ----
	constexpr float XStern = -10.5f, XBow = 11.0f;
	float HullU(float X) { return (X - XStern) / (XBow - XStern); }
	float HalfBeam(float X)
	{
		const float U = HullU(X);
		float B = 3.0f * (1.f - FMath::Pow(FMath::Max(0.f, (U - 0.45f) / 0.55f), 2.2f));
		B *= 0.78f + 0.22f * FMath::SmoothStep(0.f, 0.25f, U);
		return FMath::Max(B, 0.04f);
	}
	float GunwaleZ(float X)
	{
		const float U = HullU(X);
		return 2.05f + 0.65f * FMath::Square(FMath::Max(0.f, (U - 0.6f) / 0.4f)) + 0.3f * FMath::Square(FMath::Max(0.f, (0.15f - U) / 0.15f));
	}
	float KeelZ(float X)
	{
		const float U = HullU(X);
		return -1.3f + 0.85f * FMath::Pow(FMath::Max(0.f, (U - 0.72f) / 0.28f), 1.5f) + 0.45f * FMath::Max(0.f, (0.12f - U) / 0.12f);
	}
	FVector HullPoint(float X, float S, float Side)	// S: 0 keel .. 1 gunwale
	{
		const float T = S * HALF_PI;
		const float Y = HalfBeam(X) * FMath::Pow(FMath::Sin(T), 0.5f);
		const float Z = KeelZ(X) + (GunwaleZ(X) - KeelZ(X)) * (1.f - FMath::Pow(FMath::Cos(T), 0.5f));
		return FVector(X, Side * Y, Z) * 100.f;
	}

	const FLinearColor Bottom(0.40f, 0.09f, 0.06f), Boot(0.86f, 0.82f, 0.72f), WoodA(0.36f, 0.21f, 0.11f), WoodB(0.30f, 0.17f, 0.09f);
	const FLinearColor Trim(0.16f, 0.10f, 0.06f), Gold(0.80f, 0.58f, 0.20f), DeckA(0.62f, 0.44f, 0.26f), DeckB(0.55f, 0.38f, 0.21f);
	const FLinearColor Spar(0.44f, 0.30f, 0.16f), Iron(0.11f, 0.11f, 0.12f), Rope(0.55f, 0.47f, 0.33f), Canvas(0.93f, 0.89f, 0.79f);

	FLinearColor HullColor(float Zm, float Top)
	{
		if (Zm < -0.12f) return Bottom;
		if (Zm < 0.08f) return Boot;
		if (Zm > Top - 0.16f) return Trim;
		if (FMath::Abs(Zm - (Top - 0.55f)) < 0.07f) return Gold;
		return (FMath::FloorToInt(Zm / 0.32f) & 1) ? WoodA : WoodB;
	}
}

// -------------------------------------------------------------------------------------------------------------
// Snapshot serialisation

bool FSOShipState::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	const int64 StartBits = Ar.IsSaving() ? static_cast<FBitWriter&>(Ar).GetNumBits() : static_cast<FBitReader&>(Ar).GetPosBits();
	Ar << ServerTime;
	for (int32 A = 0; A < 3; ++A)
	{
		int32 Cm = Ar.IsSaving() ? FMath::RoundToInt(Pos[A]) : 0;
		uint32 Zig = (uint32)((Cm << 1) ^ (Cm >> 31));
		Ar.SerializeIntPacked(Zig);
		if (Ar.IsLoading()) { Cm = (int32)(Zig >> 1) ^ -(int32)(Zig & 1); Pos[A] = Cm; }
	}
	// Smallest-three quaternion: drop the largest component, 10 bits for each of the others.
	{
		uint32 Largest = 0, Q[3] = { 0, 0, 0 };
		if (Ar.IsSaving())
		{
			FQuat R = Rot.GetNormalized();
			const float C[4] = { (float)R.X, (float)R.Y, (float)R.Z, (float)R.W };
			for (uint32 I = 1; I < 4; ++I) if (FMath::Abs(C[I]) > FMath::Abs(C[Largest])) Largest = I;
			const float Sign = C[Largest] < 0.f ? -1.f : 1.f;
			int32 K = 0;
			for (uint32 I = 0; I < 4; ++I)
				if (I != Largest) Q[K++] = (uint32)FMath::Clamp(FMath::RoundToInt((C[I] * Sign + 0.70710678f) / 1.41421356f * 1023.f), 0, 1023);
		}
		Ar.SerializeInt(Largest, 4);
		for (int32 I = 0; I < 3; ++I) Ar.SerializeInt(Q[I], 1024);
		if (Ar.IsLoading())
		{
			float C[4];
			float Sum = 0.f;
			int32 K = 0;
			for (uint32 I = 0; I < 4; ++I)
				if (I != Largest) { C[I] = Q[K++] / 1023.f * 1.41421356f - 0.70710678f; Sum += C[I] * C[I]; }
			C[Largest] = FMath::Sqrt(FMath::Max(0.f, 1.f - Sum));
			Rot = FQuat(C[0], C[1], C[2], C[3]).GetNormalized();
		}
	}
	for (int32 A = 0; A < 3; ++A)
	{
		int16 V = Ar.IsSaving() ? (int16)FMath::Clamp(FMath::RoundToInt(Vel[A]), -32767, 32767) : 0;
		int16 W = Ar.IsSaving() ? (int16)FMath::Clamp(FMath::RoundToInt(AngVel[A] * 1000.f), -32767, 32767) : 0;
		Ar << V << W;
		if (Ar.IsLoading()) { Vel[A] = V; AngVel[A] = W / 1000.f; }
	}
	int8 Rd = Ar.IsSaving() ? (int8)FMath::RoundToInt(Rudder * 127.f) : 0;
	uint8 Sl = Ar.IsSaving() ? (uint8)FMath::RoundToInt(Sail * 255.f) : 0;
	Ar << Rd << Sl;
	if (Ar.IsLoading()) { Rudder = Rd / 127.f; Sail = Sl / 255.f; }
	int8 Dr = Ar.IsSaving() ? (int8)FMath::Clamp(FMath::RoundToInt(ServerDraft * 0.25f), -127, 127) : 0;
	Ar << Dr;
	if (Ar.IsLoading()) ServerDraft = Dr * 4.f;
	Ar.SerializeIntPacked(Seq);
	const int64 EndBits = Ar.IsSaving() ? static_cast<FBitWriter&>(Ar).GetNumBits() : static_cast<FBitReader&>(Ar).GetPosBits();
	LastSerializedBits = (int32)(EndBits - StartBits);
	bOutSuccess = true;
	return true;
}

// -------------------------------------------------------------------------------------------------------------

ASOShip::ASOShip()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	bReplicates = true;
	SetReplicateMovement(false);
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(30.f);
	SetMinNetUpdateFrequency(10.f);
	NetPriority = 3.f;

	Deck = CreateDefaultSubobject<UBoxComponent>(TEXT("Deck"));
	Deck->SetBoxExtent(FVector(815.f, 245.f, 15.f));
	Deck->SetMobility(EComponentMobility::Movable);
	Deck->SetCollisionProfileName(TEXT("BlockAll"));
	Deck->SetCanEverAffectNavigation(false);
	RootComponent = Deck;

	HullMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Hull"));
	HullMesh->SetupAttachment(Deck);
	HullMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HullMesh->bUseAsyncCooking = true;
	SailMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Sails"));
	SailMesh->SetupAttachment(Deck);
	SailMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WheelMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Wheel"));
	WheelMesh->SetupAttachment(Deck);
	WheelMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ASOShip::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASOShip, RepState);
	DOREPLIFETIME(ASOShip, Helmsman);
}

void ASOShip::BeginPlay()
{
	Super::BeginPlay();
	// The deck box is the root; offset it so the actor origin stays at the hull's waterline reference.
	Deck->SetBoxExtent(FVector(815.f, 245.f, 15.f));
	BuildMesh();
	RepState.ServerTime = GetWorld()->GetTimeSeconds();
	RepState.Pos = BodyOrigin();
	RepState.Rot = GetActorQuat();
	KbpsWindowStart = GetWorld()->GetTimeSeconds();
}

// -------------------------------------------------------------------------------------------------------------
// Geometry

void ASOShip::BuildMesh()
{
	UMaterialInterface* VC = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Ocean/M_VertexColor.M_VertexColor"));
	UMaterialInterface* SailMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Ocean/M_Sail.M_Sail"));
	const FVector RootOffset(-165.f, 0.f, DeckZ * 100.f - 15.f);	// where the deck box sits in hull space
	const FTransform HullToRoot(-RootOffset);

	FSOMesh M;
	M.Xf = HullToRoot;
	const int32 St = 26, Rg = 9;
	auto Xs = [&](int32 I) { return FMath::Lerp(XStern, XBow, (float)I / St); };
	// outer skin
	for (int32 I = 0; I < St; ++I)
	{
		const float X0 = Xs(I), X1 = Xs(I + 1);
		for (int32 J = 0; J < Rg; ++J)
		{
			const float S0 = (float)J / Rg, S1 = (float)(J + 1) / Rg;
			for (float Side : { -1.f, 1.f })
			{
				const FVector A = HullPoint(X0, S0, Side), B = HullPoint(X1, S0, Side), Cc = HullPoint(X1, S1, Side), D = HullPoint(X0, S1, Side);
				const FLinearColor Col = HullColor((A.Z + Cc.Z) * 0.005f, GunwaleZ((X0 + X1) * 0.5f));
				if (Side > 0) M.Quad(A, D, Cc, B, Col); else M.Quad(A, B, Cc, D, FSOMesh::Shade(Col, 0.93f));
			}
		}
	}
	// transom
	{
		const float X = XStern;
		const FVector Ctr(X * 100.f, 0.f, (KeelZ(X) + GunwaleZ(X)) * 50.f);
		for (int32 J = 0; J < Rg; ++J)
			for (float Side : { -1.f, 1.f })
			{
				const FVector A = HullPoint(X, (float)J / Rg, Side), B = HullPoint(X, (float)(J + 1) / Rg, Side);
				const FLinearColor Col = FSOMesh::Shade(HullColor((A.Z + B.Z) * 0.005f, GunwaleZ(X)), 0.85f);
				if (Side > 0) M.Tri(Ctr, B, A, Col); else M.Tri(Ctr, A, B, Col);
			}
		// stern windows: a gold band and three dark panes
		for (int32 K = -1; K <= 1; ++K)
			M.Box(FVector(X * 100.f - 2.f, K * 55.f, 150.f), FVector(3.f, 18.f, 16.f), FLinearColor(0.10f, 0.08f, 0.05f));
		M.Box(FVector(X * 100.f - 3.f, 0.f, 180.f), FVector(3.f, 170.f, 5.f), Gold);
	}
	// deck planks (flat, matches the collision boxes)
	const float DeckH = DeckZ * 100.f;
	for (int32 I = 0; I < St; ++I)
	{
		const float X0 = FMath::Max(Xs(I), XStern + 0.1f), X1 = Xs(I + 1);
		if (X0 > 10.2f) continue;
		const float B0 = HalfBeam(X0) * 0.95f - 0.12f, B1 = HalfBeam(FMath::Min(X1, 10.2f)) * 0.95f - 0.12f;
		const int32 Strips = 8;
		for (int32 K = 0; K < Strips; ++K)
		{
			const float T0 = -1.f + 2.f * K / Strips, T1 = -1.f + 2.f * (K + 1) / Strips;
			const FVector A(X0 * 100, T0 * B0 * 100, DeckH), B(FMath::Min(X1, 10.2f) * 100, T0 * B1 * 100, DeckH);
			const FVector Cc(FMath::Min(X1, 10.2f) * 100, T1 * B1 * 100, DeckH), D(X0 * 100, T1 * B0 * 100, DeckH);
			M.Quad(A, B, Cc, D, FSOMesh::Shade((K & 1) ? DeckA : DeckB, 0.97f + 0.06f * ((I * 7 + K * 3) % 5) / 4.f));
		}
		// inner bulwark and cap rail
		for (float Side : { -1.f, 1.f })
		{
			const float X1c = FMath::Min(X1, 10.2f);
			const FVector A(X0 * 100, Side * B0 * 100, DeckH), B(X1c * 100, Side * B1 * 100, DeckH);
			const FVector Cc(X1c * 100, Side * B1 * 100, GunwaleZ(X1c) * 100), D(X0 * 100, Side * B0 * 100, GunwaleZ(X0) * 100);
			if (Side > 0) M.Quad(A, B, Cc, D, FLinearColor(0.42f, 0.27f, 0.14f)); else M.Quad(A, D, Cc, B, FLinearColor(0.40f, 0.25f, 0.13f));
			const FVector O0 = HullPoint(X0, 1.f, Side), O1 = HullPoint(X1c, 1.f, Side);
			if (Side > 0) M.Quad(D, Cc, O1, O0, Trim); else M.Quad(D, O0, O1, Cc, Trim);
		}
	}
	// spars and rigging
	const FVector MastBase(150, 0, DeckH), MastTop(150, 0, 1650);
	M.Cylinder(MastBase, MastTop, 24, 14, 10, Spar);
	M.Box(FVector(150, 0, 1180), FVector(55, 55, 6), Trim);				// crow's nest floor
	M.Cylinder(FVector(150, 0, 340), FVector(-760, 0, 300), 12, 10, 8, Spar);	// boom
	M.Cylinder(FVector(150, 0, 1150), FVector(-560, 0, 1300), 10, 8, 8, Spar);	// gaff
	M.Cylinder(FVector(1020, 0, 245), FVector(1560, 0, 370), 14, 8, 8, Spar);	// bowsprit
	auto Line = [&](FVector A, FVector B) { M.Cylinder(A, B, 2.2f, 2.2f, 4, Rope, false, false); };
	Line(MastTop, FVector(1555, 0, 372));
	Line(MastTop, FVector(-1030, 0, 230));
	for (float Side : { -1.f, 1.f })
	{
		for (float Dx : { -70.f, 0.f, 70.f }) Line(FVector(150, 0, 1500), FVector(150 + Dx, Side * HalfBeam(1.5f + Dx * 0.01f) * 100.f, GunwaleZ(1.5f) * 100.f));
	}
	// helm pedestal
	M.Box(FVector(-760, 0, DeckH + 55), FVector(16, 14, 55), Trim);
	// cannons and carriages
	for (float Cx : { -250.f, 350.f })
		for (float Side : { -1.f, 1.f })
		{
			const float Y = Side * (HalfBeam(Cx * 0.01f) * 100.f - 75.f);
			M.Box(FVector(Cx, Y, DeckH + 18), FVector(45, 30, 18), FLinearColor(0.30f, 0.18f, 0.09f));
			M.Cylinder(FVector(Cx, Y - Side * 55.f, DeckH + 45), FVector(Cx, Y + Side * 95.f, DeckH + 48), 15, 11, 10, Iron);
			M.Cylinder(FVector(Cx - 25, Y, DeckH + 12), FVector(Cx + 25, Y, DeckH + 12), 9, 9, 6, Trim);
		}
	// cargo: barrels and crates around the mast
	for (int32 K = 0; K < 3; ++K)
	{
		const FVector P(40.f + K * 55.f, (K == 1) ? -130.f : -150.f, DeckH);
		M.Cylinder(P, P + FVector(0, 0, 90), 32, 32, 10, FLinearColor(0.47f, 0.29f, 0.14f));
		M.Cylinder(P + FVector(0, 0, 18), P + FVector(0, 0, 24), 34, 34, 10, Iron, false, false);
		M.Cylinder(P + FVector(0, 0, 66), P + FVector(0, 0, 72), 34, 34, 10, Iron, false, false);
	}
	M.Box(FVector(260, 140, DeckH + 32), FVector(32, 32, 32), FLinearColor(0.58f, 0.42f, 0.22f));
	M.Box(FVector(260, 140, DeckH + 88), FVector(24, 24, 24), FLinearColor(0.52f, 0.37f, 0.19f), FQuat(FVector::UpVector, 0.4f));
	// stern lantern
	M.Cylinder(FVector(-1010, 0, GunwaleZ(XStern) * 100.f), FVector(-1010, 0, GunwaleZ(XStern) * 100.f + 70.f), 4, 4, 6, Iron);
	M.Box(FVector(-1010, 0, GunwaleZ(XStern) * 100.f + 90.f), FVector(13, 13, 18), FLinearColor(1.0f, 0.78f, 0.32f));
	// pennant at the masthead
	M.Tri(FVector(150, 0, 1650), FVector(150, 0, 1610), FVector(10, 0, 1635), FLinearColor(0.70f, 0.08f, 0.06f));
	M.Tri(FVector(150, 0, 1650), FVector(10, 0, 1635), FVector(150, 0, 1610), FLinearColor(0.60f, 0.07f, 0.05f));
	M.Commit(HullMesh, 0, VC);

	// sails, relative to the boom so scaling the component in Z furls them onto it
	FSOMesh S;
	S.Xf = FTransform(FVector(0, 0, -310));	// sail space: hull space with the origin on the boom
	auto Bilinear = [](FVector A, FVector B, FVector Cc, FVector D, float U, float V) { return FMath::Lerp(FMath::Lerp(A, B, U), FMath::Lerp(D, Cc, U), V); };
	{
		const int32 Cols = 12, Rows = 10;
		const FVector Tack(135, 0, 350), Clew(-735, 0, 312), Peak(-545, 0, 1290), Throat(135, 0, 1140);
		TArray<FVector> P;
		for (int32 R = 0; R <= Rows; ++R)
			for (int32 Cc = 0; Cc <= Cols; ++Cc)
			{
				const float U = (float)Cc / Cols, V = (float)R / Rows;
				FVector Q = Bilinear(Tack, Clew, Peak, Throat, U, V);
				Q.Y += 90.f * FMath::Sin(PI * U) * FMath::Sin(PI * FMath::Min(V * 1.1f, 1.f));
				P.Add(Q);
			}
		S.Grid(P, Cols, Rows, Canvas, true);
	}
	{
		const int32 Cols = 8, Rows = 8;
		const FVector Tack(1450, 0, 360), Clew(520, 0, 300), Head(175, 0, 1480);
		TArray<FVector> P;
		for (int32 R = 0; R <= Rows; ++R)
			for (int32 Cc = 0; Cc <= Cols; ++Cc)
			{
				const float U = (float)Cc / Cols, V = (float)R / Rows;
				FVector Q = Bilinear(Tack, Clew, Head, Head, U, V);
				Q.Y += 70.f * FMath::Sin(PI * U) * FMath::Sin(PI * V) * (1.f - V * 0.5f);
				P.Add(Q);
			}
		S.Grid(P, Cols, Rows, FSOMesh::Shade(Canvas, 0.96f), true);
	}
	S.Commit(SailMesh, 0, SailMat ? SailMat : VC);
	SailMesh->SetRelativeLocation(FVector(0, 0, 310) - RootOffset);
	if (SailMat) SailMID = SailMesh->CreateDynamicMaterialInstance(0, SailMat);

	// wheel, built around its own axle (X) so it can spin
	FSOMesh W;
	const int32 Seg = 14;
	for (int32 K = 0; K < Seg; ++K)
	{
		const float A0 = 2 * PI * K / Seg, A1 = 2 * PI * (K + 1) / Seg;
		W.Cylinder(FVector(0, FMath::Cos(A0) * 55, FMath::Sin(A0) * 55), FVector(0, FMath::Cos(A1) * 55, FMath::Sin(A1) * 55), 4, 4, 5, Spar, false, false);
	}
	for (int32 K = 0; K < 8; ++K)
	{
		const float A = 2 * PI * K / 8;
		W.Cylinder(FVector::ZeroVector, FVector(0, FMath::Cos(A) * 74, FMath::Sin(A) * 74), 3, 2.5f, 5, Spar, false, true);
	}
	W.Cylinder(FVector(-12, 0, 0), FVector(12, 0, 0), 9, 9, 8, Gold);
	W.Commit(WheelMesh, 0, VC);
	WheelMesh->SetRelativeLocation(FVector(-735.f, 0.f, DeckH + 125.f) + HullToRoot.GetLocation());

	// collision: deck boxes and bulwark walls so a walking player stays aboard
	auto AddBox = [this, &HullToRoot](FName Name, FVector Ctr, FVector Half, float Yaw)
	{
		UBoxComponent* B = NewObject<UBoxComponent>(this, Name);
		B->SetupAttachment(Deck);
		B->SetBoxExtent(Half);
		B->SetRelativeLocationAndRotation(HullToRoot.TransformPosition(Ctr), FRotator(0, Yaw, 0));
		B->SetCollisionProfileName(TEXT("BlockAll"));
		B->SetCanEverAffectNavigation(false);
		B->RegisterComponent();
		Walls.Add(B);
	};
	AddBox(TEXT("BowDeck"), FVector(800, 0, DeckH - 15), FVector(180, 150, 15), 0);
	for (float Side : { -1.f, 1.f })
	{
		const int32 Segs = 7;
		for (int32 K = 0; K < Segs; ++K)
		{
			const float X0 = FMath::Lerp(XStern + 0.1f, 10.0f, (float)K / Segs), X1 = FMath::Lerp(XStern + 0.1f, 10.0f, (float)(K + 1) / Segs);
			const FVector P0(X0 * 100, Side * (HalfBeam(X0) * 95.f - 12.f), 0), P1(X1 * 100, Side * (HalfBeam(X1) * 95.f - 12.f), 0);
			const FVector Mid = (P0 + P1) * 0.5f;
			const float Len = FVector::Dist2D(P0, P1);
			const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(P1.Y - P0.Y, P1.X - P0.X));
			const float Top = GunwaleZ((X0 + X1) * 0.5f) * 100.f + 20.f;
			AddBox(*FString::Printf(TEXT("Wall_%d_%d"), Side > 0 ? 1 : 0, K), FVector(Mid.X, Mid.Y, (DeckH + Top) * 0.5f), FVector(Len * 0.5f + 8.f, 9.f, (Top - DeckH) * 0.5f), Yaw);
		}
	}
	AddBox(TEXT("Transom"), FVector(XStern * 100 + 15, 0, DeckH + 60), FVector(10, 230, 60), 0);
	AddBox(TEXT("Mast"), FVector(150, 0, DeckH + 150), FVector(26, 26, 150), 0);
	AddBox(TEXT("Helm"), FVector(-760, 0, DeckH + 55), FVector(18, 16, 55), 0);
	HullMesh->SetRelativeLocation(FVector::ZeroVector);
}

FVector ASOShip::GetHelmLocation() const
{
	return GetActorTransform().TransformPosition(FVector(-735.f + 165.f, 0.f, 250.f - 110.f));
}

FVector ASOShip::GetHelmStandLocation() const
{
	// Behind the wheel on the deck, capsule centre height above the deck box top.
	return GetActorTransform().TransformPosition(FVector(-845.f + 165.f, 0.f, 15.f + 95.f));
}

// -------------------------------------------------------------------------------------------------------------
// Simulation

int32 ASOShip::GetProbeCount() const
{
	const ASOGameState* GS = ASOGameState::Get(this);
	const int32 N = GS ? GS->Settings.ProbeCount : 8;
	return (N == 1 || N == 2 || N == 4 || N == 8 || N == 16) ? N : 8;
}

void ASOShip::GetProbeWorld(TArray<FVector>& Out) const
{
	Out.Reset();
	const int32 N = GetProbeCount();
	const FTransform Xf(GetActorQuat(), BodyOrigin());
	for (int32 I = 0; I < N; ++I) Out.Add(Xf.TransformPosition(ProbeLocal(N, I) * 100.f));
}

FVector ASOShip::BodyOrigin() const
{
	// The actor root is the deck box; the simulation origin is the hull's waterline reference below it.
	return GetActorTransform().TransformPosition(FVector(165.f, 0.f, -(DeckZ * 100.f - 15.f)));
}

float ASOShip::QueryHeight(const AOceanManager* Ocean, FVector2D P) const
{
	const ASOGameState* GS = ASOGameState::Get(this);
	const bool bInv = GS ? GS->Settings.bInverseQuery : true;
	return Ocean ? Ocean->HeightCm(P, bInv ? 4 : 0) : 0.f;
}

void ASOShip::BuoyancyProbes(const FSOBody& B, const AOceanManager* Ocean, FVector& F, FVector& Tau, int32& Wet) const
{
	const int32 N = GetProbeCount();
	const float K = G / (N * RestDraft);
	const float C = 2.f * 0.55f * FMath::Sqrt(G / RestDraft) / N;
	const FVector Vm = B.Vel * 0.01f;
	for (int32 I = 0; I < N; ++I)
	{
		const FVector Rl = B.Rot.RotateVector(ProbeLocal(N, I));		// m, from the origin
		const FVector Pw = B.Pos + Rl * 100.f;
		const float Depth = (QueryHeight(Ocean, FVector2D(Pw)) - (float)Pw.Z) * 0.01f;
		if (Depth <= 0.f) continue;
		++Wet;
		const FVector Vp = Vm + FVector::CrossProduct(B.AngVel, Rl);
		const float Fz = FMath::Max(K * FMath::Min(Depth, 2.6f) - C * (float)Vp.Z, -0.3f * K);
		F.Z += Fz;
		// The depth is measured at the probe, but the force acts above the centre of mass (metacentre), so a
		// tilted hull is pushed back upright; a single deep probe would otherwise act like an inverted pendulum.
		const FVector Ra = B.Rot.RotateVector(FVector(ProbeLocal(N, I).X, ProbeLocal(N, I).Y, 0.6f));
		Tau += FVector::CrossProduct(Ra, FVector(0, 0, Fz));
	}
}

void ASOShip::BuoyancyPlaneFit(const FSOBody& B, const AOceanManager* Ocean, FVector& F, FVector& Tau) const
{
	// Atlas (GDC 2019): fit a plane to the wave samples, use it as the target pose, follow it with springs.
	int32 N = GetProbeCount();
	if (N < 4) N = 4;
	const FQuat Yaw = YawOf(B.Rot);
	double Sxx = 0, Sxy = 0, Sx = 0, Syy = 0, Sy = 0, Sxh = 0, Syh = 0, Sh = 0;
	for (int32 I = 0; I < N; ++I)
	{
		const FVector L = ProbeLocal(N, I);
		const FVector Pw = B.Pos + Yaw.RotateVector(FVector(L.X, L.Y, 0)) * 100.f;
		const double H = QueryHeight(Ocean, FVector2D(Pw)) * 0.01;
		Sxx += L.X * L.X; Sxy += L.X * L.Y; Sx += L.X; Syy += L.Y * L.Y; Sy += L.Y; Sxh += L.X * H; Syh += L.Y * H; Sh += H;
	}
	// Solve [Sxx Sxy Sx; Sxy Syy Sy; Sx Sy N] [a b c] = [Sxh Syh Sh] by Cramer's rule.
	const double M[3][3] = { { Sxx, Sxy, Sx }, { Sxy, Syy, Sy }, { Sx, Sy, (double)N } };
	const double R[3] = { Sxh, Syh, Sh };
	auto Det = [](const double A[3][3]) { return A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]); };
	const double D = Det(M);
	double Sol[3] = { 0, 0, Sh / N };
	if (FMath::Abs(D) > 1e-9)
		for (int32 Col = 0; Col < 3; ++Col)
		{
			double Mc[3][3];
			for (int32 Rr = 0; Rr < 3; ++Rr) for (int32 Cc = 0; Cc < 3; ++Cc) Mc[Rr][Cc] = (Cc == Col) ? R[Rr] : M[Rr][Cc];
			Sol[Col] = Det(Mc) / D;
		}
	const float Zt = (float)Sol[2] + ProbeZ + RestDraft;	// origin height for the rest draft on that plane
	const FVector Vm = B.Vel * 0.01f;
	F.Z += 6.5f * (Zt - (float)B.Pos.Z * 0.01f) - 2.f * 0.6f * 2.55f * (float)Vm.Z;
	const FVector TargetUp = Yaw.RotateVector(FVector(-Sol[0], -Sol[1], 1.0).GetSafeNormal());
	const FVector Up = B.Rot.GetUpVector();
	FVector AngAcc = 6.0f * FVector::CrossProduct(Up, TargetUp);
	FVector W = B.AngVel; W.Z = 0;
	AngAcc -= 3.2f * W;
	// Returned as torque already divided by inertia: StepBody adds it directly.
	Tau += AngAcc;
}

void ASOShip::StepBody(FSOBody& B, float Dt, const AOceanManager* Ocean, bool bVerticalOnly) const
{
	const ASOGameState* GS = ASOGameState::Get(this);
	const ESOBuoyancyModel Model = GS ? (ESOBuoyancyModel)GS->Settings.Buoyancy : ESOBuoyancyModel::Probes;
	FVector F(0, 0, 0), Tau(0, 0, 0), AngAcc(0, 0, 0);
	int32 Wet = 0;
	const FVector Vm = B.Vel * 0.01f;
	const FQuat R = B.Rot;
	if (Model == ESOBuoyancyModel::Probes)
	{
		F.Z -= G;
		BuoyancyProbes(B, Ocean, F, Tau, Wet);
		const FVector TauB = R.UnrotateVector(Tau);
		AngAcc = R.RotateVector(FVector(TauB.X / Inertia.X, TauB.Y / Inertia.Y, TauB.Z / Inertia.Z));
	}
	else
	{
		BuoyancyPlaneFit(B, Ocean, F, AngAcc);
		Wet = GetProbeCount();
	}
	const float WetFrac = FMath::Clamp((float)Wet / GetProbeCount(), 0.f, 1.f);
	// ballast keel: a small righting moment (keeps a 1- or 2-probe hull from capsizing)
	AngAcc += 1.6f * FVector::CrossProduct(R.GetUpVector(), FVector::UpVector);
	FVector Wn = B.AngVel; Wn.Z = 0;
	AngAcc -= 0.45f * Wn;

	if (!bVerticalOnly)
	{
		const FVector Vb = R.UnrotateVector(Vm);
		const FVector DragB(-0.02f * Vb.X * FMath::Abs(Vb.X) - 0.06f * Vb.X, -0.9f * Vb.Y * FMath::Abs(Vb.Y) - 0.5f * Vb.Y, 0.f);
		F += R.RotateVector(DragB) * WetFrac;
		FVector Fwd = R.GetForwardVector(); Fwd.Z = 0; Fwd = Fwd.GetSafeNormal();
		const float WindDeg = 30.f;
		const FVector Wind(FMath::Cos(FMath::DegreesToRadians(WindDeg)), FMath::Sin(FMath::DegreesToRadians(WindDeg)), 0.f);
		const float Eff = 0.15f + 0.85f * FMath::Clamp(((float)FVector::DotProduct(Fwd, Wind) + 0.3f) / 1.3f, 0.f, 1.f);
		F += Fwd * (Sail * 1.5f * Eff * WetFrac);
		const float Lateral = (float)FVector::DotProduct(Wind, R.GetRightVector());
		AngAcc += R.GetForwardVector() * (Sail * 0.35f * Lateral);
		AngAcc.Z += Rudder * 0.03f * (float)Vb.X * WetFrac - 0.9f * (float)B.AngVel.Z * FMath::Max(WetFrac, 0.2f);
	}

	B.Vel += F * Dt * 100.f;
	B.AngVel += AngAcc * Dt;
	if (bVerticalOnly) { B.Vel.X = 0; B.Vel.Y = 0; B.AngVel.Z = 0; }
	B.Pos += B.Vel * Dt;
	const FVector W = B.AngVel;
	const float Ang = (float)W.Size() * Dt;
	if (Ang > 1e-6f) B.Rot = (FQuat(W.GetSafeNormal(), Ang) * B.Rot).GetNormalized();
}

void ASOShip::CollideIslands(FSOBody& B) const
{
	for (const FVector& Is : ASOEnvironment::Islands())
	{
		const FVector2D D = FVector2D(B.Pos) - FVector2D(Is.X, Is.Y);
		const double Dist = D.Size();
		const double MinD = Is.Z + 850.0;
		if (Dist < MinD && Dist > 1.0)
		{
			const FVector2D Nn = D / Dist;
			B.Pos.X = Is.X + Nn.X * MinD;
			B.Pos.Y = Is.Y + Nn.Y * MinD;
			const double Vn = B.Vel.X * Nn.X + B.Vel.Y * Nn.Y;
			if (Vn < 0) { B.Vel.X -= Nn.X * Vn * 1.4; B.Vel.Y -= Nn.Y * Vn * 1.4; }
			B.Vel.X *= 0.96; B.Vel.Y *= 0.96;
		}
	}
}

void ASOShip::SetHelmInput(float InRudder, float InSailDelta)
{
	RudderInput = FMath::Clamp(InRudder, -1.f, 1.f);
	SailInput = FMath::Clamp(InSailDelta, -1.f, 1.f);
}

void ASOShip::ServerStep(float Dt)
{
	if (!Helmsman) { RudderInput = bAutopilot ? AutoRudder : 0.f; SailInput = 0.f; }
	Rudder = FMath::FInterpConstantTo(Rudder, RudderInput, Dt, 1.6f);
	Sail = FMath::Clamp(Sail + SailInput * 0.45f * Dt, 0.f, 1.f);
	StepBody(Body, Dt, AOceanManager::Get(this), false);
	CollideIslands(Body);
	SimTime += Dt;
	RepState.ServerTime = SimTime;
	RepState.Pos = Body.Pos;
	RepState.Rot = Body.Rot;
	RepState.Vel = Body.Vel;
	RepState.AngVel = Body.AngVel;
	RepState.Rudder = Rudder;
	RepState.Sail = Sail;
	RepState.ServerDraft = ComputeDraft(Body.Pos, Body.Rot);
	RepState.Seq = ++Seq;
}

void ASOShip::ApplyPose(const FVector& Pos, const FQuat& Rot)
{
	// Pos/Rot describe the simulation origin; place the deck-box root accordingly.
	const FVector RootPos = Pos + Rot.RotateVector(FVector(-165.f, 0.f, DeckZ * 100.f - 15.f));
	SetActorLocationAndRotation(RootPos, Rot, false, nullptr, ETeleportType::None);
}

void ASOShip::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AOceanManager* Ocean = AOceanManager::Get(this);
	if (HasAuthority())
	{
		if (!bBodyInit)
		{
			Body.Pos = BodyOrigin();
			Body.Rot = GetActorQuat();
			PrevBody = Body;
			SimTime = GetWorld()->GetTimeSeconds();
			bBodyInit = true;
		}
		const ASOGameState* GS = ASOGameState::Get(this);
		const float Hz = GS ? FMath::Max<float>(GS->Settings.PhysicsHz, 5.f) : 60.f;
		const float Step = 1.f / Hz;
		Accumulator += DeltaSeconds;
		int32 Guard = 0;
		while (Accumulator >= Step && Guard++ < 12)
		{
			PrevBody = Body;
			ServerStep(Step);
			Accumulator -= Step;
		}
		const float Alpha = FMath::Clamp(Accumulator / Step, 0.f, 1.f);
		ApplyPose(FMath::Lerp(PrevBody.Pos, Body.Pos, Alpha), FQuat::Slerp(PrevBody.Rot, Body.Rot, Alpha));
		if (GS && GetNetUpdateFrequency() != GS->Settings.SnapshotHz) SetNetUpdateFrequency(GS->Settings.SnapshotHz);
	}
	else
	{
		UpdateClient(DeltaSeconds);
	}
	UpdateVisualBits(DeltaSeconds);
}

bool ASOShip::SampleBuffer(double T, FSOShipState& Out) const
{
	if (Buffer.Num() == 0) return false;
	if (T <= Buffer[0].ServerTime) { Out = Buffer[0]; return true; }
	const FSOShipState& Last = Buffer.Last();
	if (T >= Last.ServerTime)
	{
		// Extrapolate briefly, then hold: running out of buffer shows as a stall, not a teleport.
		const float Ex = (float)FMath::Min(T - Last.ServerTime, 0.2);
		Out = Last;
		Out.Pos = Last.Pos + Last.Vel * Ex;
		return true;
	}
	for (int32 I = Buffer.Num() - 2; I >= 0; --I)
	{
		const FSOShipState& A = Buffer[I];
		if (T < A.ServerTime) continue;
		const FSOShipState& B = Buffer[I + 1];
		const double Span = FMath::Max(B.ServerTime - A.ServerTime, 1e-4);
		const float S = (float)((T - A.ServerTime) / Span);
		// Hermite on position with the snapshot velocities (Fiedler, "Snapshot Interpolation").
		const float S2 = S * S, S3 = S2 * S;
		const float H00 = 2 * S3 - 3 * S2 + 1, H10 = S3 - 2 * S2 + S, H01 = -2 * S3 + 3 * S2, H11 = S3 - S2;
		Out = B;
		Out.Pos = A.Pos * H00 + A.Vel * (H10 * Span) + B.Pos * H01 + B.Vel * (H11 * Span);
		Out.Rot = FQuat::Slerp(A.Rot, B.Rot, S);
		Out.Vel = FMath::Lerp(A.Vel, B.Vel, S);
		Out.Rudder = FMath::Lerp(A.Rudder, B.Rudder, S);
		Out.Sail = FMath::Lerp(A.Sail, B.Sail, S);
		Out.ServerDraft = FMath::Lerp(A.ServerDraft, B.ServerDraft, S);
		Out.ServerTime = T;
		return true;
	}
	Out = Buffer[0];
	return true;
}

void ASOShip::OnRep_State()
{
	if (Buffer.Num() && RepState.Seq <= Buffer.Last().Seq) return;
	Buffer.Add(RepState);
	LastArrival = GetWorld()->GetTimeSeconds();
	KbpsBits += FSOShipState::LastSerializedBits;
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - KbpsWindowStart > 1.0)
	{
		SnapshotKbps = (float)(KbpsBits / (Now - KbpsWindowStart) / 1000.0);
		KbpsBits = 0;
		KbpsWindowStart = Now;
	}
	while (Buffer.Num() > 2 && Buffer[0].ServerTime < RepState.ServerTime - 3.0) Buffer.RemoveAt(0);
}

float ASOShip::GetSnapshotAgeMs() const
{
	if (HasAuthority() || !Buffer.Num()) return 0.f;
	const ASOPlayerController* PC = Cast<ASOPlayerController>(GetWorld()->GetFirstPlayerController());
	const USONetClock* Clock = PC ? PC->GetNetClock() : nullptr;
	return Clock ? float((Clock->GetServerTimeEstimate() - Buffer.Last().ServerTime) * 1000.0) : 0.f;
}

void ASOShip::UpdateClient(float Dt)
{
	ASOPlayerController* PC = Cast<ASOPlayerController>(GetWorld()->GetFirstPlayerController());
	USONetClock* Clock = PC ? PC->GetNetClock() : nullptr;
	if (!Clock) return;
	const double RenderT = Clock->GetServerTimeEstimate() - PC->InterpDelayMs * 0.001;
	FSOShipState S;
	if (!SampleBuffer(RenderT, S)) return;
	Rudder = S.Rudder;
	Sail = S.Sail;
	ShownServerDraft = S.ServerDraft;
	if (PC->Presentation != ESOShipPresentation::Reconstruct)
	{
		bBodyInit = false;
		Body.Pos = S.Pos; Body.Rot = S.Rot;
		ApplyPose(S.Pos, S.Rot);
		return;
	}
	// Reconstruct: the horizontal motion is the server's; heave, pitch and roll come from this machine's sea.
	if (!bBodyInit)
	{
		Body.Pos = S.Pos; Body.Rot = S.Rot; Body.Vel = S.Vel; Body.AngVel = S.AngVel;
		bBodyInit = true;
	}
	Body.Pos.X = S.Pos.X; Body.Pos.Y = S.Pos.Y;
	const int32 Sub = FMath::Clamp(FMath::CeilToInt(Dt / (1.f / 60.f)), 1, 4);
	for (int32 I = 0; I < Sub; ++I) StepBody(Body, Dt / Sub, AOceanManager::Get(this), true);
	const double Err = S.Pos.Z - Body.Pos.Z;
	if (FMath::Abs(Err) > 300.0) Body.Pos.Z = S.Pos.Z;
	else Body.Pos.Z += Err * FMath::Min(1.0, 0.35 * Dt);
	const FQuat Tilt = YawOf(Body.Rot).Inverse() * Body.Rot;
	Body.Rot = (YawOf(S.Rot) * Tilt).GetNormalized();
	ApplyPose(Body.Pos, Body.Rot);
}

void ASOShip::UpdateVisualBits(float Dt)
{
	WheelAngle = FMath::FInterpTo(WheelAngle, Rudder * 360.f, Dt, 6.f);
	WheelMesh->SetRelativeRotation(FRotator(0.f, 0.f, WheelAngle));
	const float Furl = FMath::Lerp(0.22f, 1.f, Sail);
	SailMesh->SetRelativeScale3D(FVector(1.f, 1.f, Furl));
	if (SailMID) SailMID->SetScalarParameterValue(TEXT("Fill"), Sail);
	DraftErrorCm = ComputeDraft(BodyOrigin(), GetActorQuat());
}

float ASOShip::ComputeDraft(const FVector& Origin, const FQuat& Rot) const
{
	// Mean probe submersion on this machine's sea relative to the rest draft (0 = sits right on the water).
	const AOceanManager* Ocean = AOceanManager::Get(this);
	if (!Ocean) return 0.f;
	const int32 N = GetProbeCount();
	float Sum = 0.f;
	for (int32 I = 0; I < N; ++I)
	{
		const FVector Q = Origin + Rot.RotateVector(ProbeLocal(N, I) * 100.f);
		Sum += Ocean->HeightCm(FVector2D(Q), 3) - (float)Q.Z;
	}
	return Sum / N - RestDraft * 100.f;
}

float ASOShip::GetSpeedKnots() const
{
	const FVector V = HasAuthority() ? Body.Vel : (Buffer.Num() ? Buffer.Last().Vel : FVector::ZeroVector);
	return (float)FVector2D(V).Size() * 0.01f * 1.94384f;
}
