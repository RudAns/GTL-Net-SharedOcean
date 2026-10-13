#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SOChest.generated.h"

class UProceduralMeshComponent;

/**
 * Floating treasure. Only its horizontal drift position is replicated; every machine floats it on its own
 * copy of the sea (height and tilt from the gameplay band), which is exactly why a deterministic ocean
 * lets small floating props cost no bandwidth at all.
 */
UCLASS()
class SHAREDOCEAN_API ASOChest : public AActor
{
	GENERATED_BODY()
public:
	ASOChest();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	void Respawn(int32 Salt);

	UPROPERTY(Replicated) FVector2D Drift = FVector2D::ZeroVector;	// cm

private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Mesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Beam;
	int32 Spawns = 0;
};
