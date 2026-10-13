#pragma once

#include "CoreMinimal.h"

class UProceduralMeshComponent;
class UMaterialInterface;

/**
 * Small flat-shaded mesh builder for the procedural scenery (ship, islands, props). Faces are given
 * counter-clockwise as seen from outside (right-hand rule outward normal); the builder emits them in UE's
 * winding. Colours are vertex colours read by one shared material. Units: centimetres.
 */
struct SHAREDOCEAN_API FSOMesh
{
	TArray<FVector> V;
	TArray<int32> T;
	TArray<FVector> N;
	TArray<FVector2D> UV;
	TArray<FLinearColor> C;
	FTransform Xf = FTransform::Identity;	// applied to everything added

	void Reset() { V.Reset(); T.Reset(); N.Reset(); UV.Reset(); C.Reset(); Xf = FTransform::Identity; }
	int32 Num() const { return V.Num(); }

	void Tri(FVector A, FVector B, FVector Cc, FLinearColor Col);
	void Quad(FVector A, FVector B, FVector Cc, FVector D, FLinearColor Col);
	void Box(FVector Center, FVector Half, FLinearColor Col, FQuat Rot = FQuat::Identity);
	/** Tapered cylinder from A to B. Caps optional. */
	void Cylinder(FVector A, FVector B, float R0, float R1, int32 Segs, FLinearColor Col, bool bCapA = true, bool bCapB = true);
	/** Low-poly lumpy sphere (rocks, foliage clumps). */
	void Blob(FVector Center, FVector Radii, int32 Rings, int32 Segs, FLinearColor Col, int32 Seed, float Jitter);
	/** Smooth-shaded strip between two polylines (sails, banners); per-vertex colours. */
	void Grid(const TArray<FVector>& Pts, int32 Cols, int32 Rows, FLinearColor Col, bool bTwoSided);

	void Commit(UProceduralMeshComponent* Comp, int32 Section, UMaterialInterface* Mat, bool bCollision = false) const;

	static FLinearColor Shade(FLinearColor Col, float K) { return FLinearColor(Col.R * K, Col.G * K, Col.B * K, Col.A); }
};
