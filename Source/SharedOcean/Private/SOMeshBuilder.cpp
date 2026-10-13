#include "SOMeshBuilder.h"

#include "Math/RandomStream.h"
#include "ProceduralMeshComponent.h"

void FSOMesh::Tri(FVector A, FVector B, FVector Cc, FLinearColor Col)
{
	A = Xf.TransformPosition(A); B = Xf.TransformPosition(B); Cc = Xf.TransformPosition(Cc);
	const FVector Nn = FVector::CrossProduct(B - A, Cc - A).GetSafeNormal();
	const int32 I = V.Num();
	V.Add(A); V.Add(B); V.Add(Cc);
	N.Add(Nn); N.Add(Nn); N.Add(Nn);
	UV.Add(FVector2D(A.X, A.Y) * 0.01); UV.Add(FVector2D(B.X, B.Y) * 0.01); UV.Add(FVector2D(Cc.X, Cc.Y) * 0.01);
	C.Add(Col); C.Add(Col); C.Add(Col);
	// UE front faces wind the other way round.
	T.Add(I); T.Add(I + 2); T.Add(I + 1);
}

void FSOMesh::Quad(FVector A, FVector B, FVector Cc, FVector D, FLinearColor Col)
{
	Tri(A, B, Cc, Col);
	Tri(A, Cc, D, Col);
}

void FSOMesh::Box(FVector Ctr, FVector H, FLinearColor Col, FQuat Rot)
{
	auto P = [&](float X, float Y, float Z) { return Ctr + Rot.RotateVector(FVector(X * H.X, Y * H.Y, Z * H.Z)); };
	const FLinearColor Top = Shade(Col, 1.08f), Side = Col, Bot = Shade(Col, 0.7f);
	Quad(P(-1, -1, 1), P(1, -1, 1), P(1, 1, 1), P(-1, 1, 1), Top);
	Quad(P(-1, -1, -1), P(-1, 1, -1), P(1, 1, -1), P(1, -1, -1), Bot);
	Quad(P(1, -1, -1), P(1, 1, -1), P(1, 1, 1), P(1, -1, 1), Side);
	Quad(P(-1, -1, -1), P(-1, -1, 1), P(-1, 1, 1), P(-1, 1, -1), Shade(Side, 0.92f));
	Quad(P(-1, 1, -1), P(-1, 1, 1), P(1, 1, 1), P(1, 1, -1), Shade(Side, 0.96f));
	Quad(P(-1, -1, -1), P(1, -1, -1), P(1, -1, 1), P(-1, -1, 1), Shade(Side, 0.88f));
}

void FSOMesh::Cylinder(FVector A, FVector B, float R0, float R1, int32 Segs, FLinearColor Col, bool bCapA, bool bCapB)
{
	const FVector Axis = (B - A).GetSafeNormal();
	const FVector Ref = FMath::Abs(Axis.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector;
	const FVector U = FVector::CrossProduct(Axis, Ref).GetSafeNormal();
	const FVector W = FVector::CrossProduct(Axis, U);
	TArray<FVector> Ra, Rb;
	for (int32 S = 0; S < Segs; ++S)
	{
		const float Ang = 2.f * PI * S / Segs;
		const FVector Dir = U * FMath::Cos(Ang) + W * FMath::Sin(Ang);
		Ra.Add(A + Dir * R0);
		Rb.Add(B + Dir * R1);
	}
	for (int32 S = 0; S < Segs; ++S)
	{
		const int32 S1 = (S + 1) % Segs;
		const float Lit = 0.82f + 0.18f * FMath::Cos(2.f * PI * (S + 0.5f) / Segs);
		Quad(Ra[S], Ra[S1], Rb[S1], Rb[S], Shade(Col, Lit));
	}
	for (int32 S = 1; S + 1 < Segs; ++S)
	{
		if (bCapA) Tri(Ra[0], Ra[S + 1], Ra[S], Shade(Col, 0.75f));
		if (bCapB) Tri(Rb[0], Rb[S], Rb[S + 1], Shade(Col, 1.05f));
	}
}

void FSOMesh::Blob(FVector Ctr, FVector R, int32 Rings, int32 Segs, FLinearColor Col, int32 Seed, float Jitter)
{
	FRandomStream Rng(Seed);
	TArray<FVector> P;
	P.SetNum((Rings + 1) * Segs);
	for (int32 I = 0; I <= Rings; ++I)
	{
		const float Th = PI * I / Rings;
		for (int32 S = 0; S < Segs; ++S)
		{
			const float Ph = 2.f * PI * S / Segs;
			FVector D(FMath::Sin(Th) * FMath::Cos(Ph), FMath::Sin(Th) * FMath::Sin(Ph), FMath::Cos(Th));
			const float J = (I == 0 || I == Rings) ? 1.f : 1.f + Rng.FRandRange(-Jitter, Jitter);
			P[I * Segs + S] = Ctr + D * R * J;
		}
	}
	for (int32 I = 0; I < Rings; ++I)
		for (int32 S = 0; S < Segs; ++S)
		{
			const int32 S1 = (S + 1) % Segs;
			const FVector A = P[I * Segs + S], B = P[(I + 1) * Segs + S], Cc = P[(I + 1) * Segs + S1], D = P[I * Segs + S1];
			const float Lit = 0.78f + 0.3f * (1.f - (float)I / Rings);
			Quad(A, B, Cc, D, Shade(Col, Lit + Rng.FRandRange(-0.05f, 0.05f)));
		}
}

void FSOMesh::Grid(const TArray<FVector>& Pts, int32 Cols, int32 Rows, FLinearColor Col, bool bTwoSided)
{
	// Pts is (Rows+1) x (Cols+1), row-major; smooth normals.
	const int32 Base = V.Num();
	const int32 W = Cols + 1;
	for (int32 R = 0; R <= Rows; ++R)
		for (int32 Cc = 0; Cc <= Cols; ++Cc)
		{
			const FVector P = Xf.TransformPosition(Pts[R * W + Cc]);
			const FVector Pr = Xf.TransformPosition(Pts[R * W + FMath::Min(Cc + 1, Cols)]) - Xf.TransformPosition(Pts[R * W + FMath::Max(Cc - 1, 0)]);
			const FVector Pu = Xf.TransformPosition(Pts[FMath::Min(R + 1, Rows) * W + Cc]) - Xf.TransformPosition(Pts[FMath::Max(R - 1, 0) * W + Cc]);
			V.Add(P);
			N.Add(FVector::CrossProduct(Pr, Pu).GetSafeNormal());
			UV.Add(FVector2D((float)Cc / Cols, (float)R / Rows));
			C.Add(Col);
		}
	for (int32 R = 0; R < Rows; ++R)
		for (int32 Cc = 0; Cc < Cols; ++Cc)
		{
			const int32 A = Base + R * W + Cc, B = A + 1, D = A + W, E = D + 1;
			T.Add(A); T.Add(E); T.Add(B);
			T.Add(A); T.Add(D); T.Add(E);
			if (bTwoSided)
			{
				T.Add(A); T.Add(B); T.Add(E);
				T.Add(A); T.Add(E); T.Add(D);
			}
		}
}

void FSOMesh::Commit(UProceduralMeshComponent* Comp, int32 Section, UMaterialInterface* Mat, bool bCollision) const
{
	if (!Comp) return;
	TArray<FProcMeshTangent> Tan;
	Comp->CreateMeshSection_LinearColor(Section, V, T, N, UV, C, Tan, bCollision);
	if (Mat) Comp->SetMaterial(Section, Mat);
}
