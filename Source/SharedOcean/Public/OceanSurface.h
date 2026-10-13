#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OceanSurface.generated.h"

class UProceduralMeshComponent;
class UMaterialInstanceDynamic;

/**
 * Camera-centred geometry clipmap (report ch.5.1 / ch.9): nested square rings whose vertices stay snapped to
 * their own world grid, so the surface does not swim as the camera moves. Each ring discards the pixels the
 * finer ring already covers, and the finer ring morphs its outer band onto the coarser grid, so there are no
 * cracks. Displacement and shading happen in M_Ocean from the cascade textures.
 */
UCLASS()
class SHAREDOCEAN_API AOceanSurface : public AActor
{
	GENERATED_BODY()
public:
	AOceanSurface();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	static constexpr int32 Levels = 9;
	static constexpr int32 GridCells = 128;
	static constexpr float BaseCell = 50.f;	// cm
	bool bTintLevels = false;

private:
	void BuildRing(UProceduralMeshComponent* Comp, int32 Level) const;
	void BuildSkirt(UProceduralMeshComponent* Comp) const;
	void BindTextures(UMaterialInstanceDynamic* MID) const;

	UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> Rings;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> MIDs;
	int32 BoundVersion = -1;
};
