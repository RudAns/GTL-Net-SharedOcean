#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SOEnvironment.generated.h"

class UDirectionalLightComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UExponentialHeightFogComponent;
class UVolumetricCloudComponent;
class UPostProcessComponent;
class UPointLightComponent;
class UProceduralMeshComponent;

/**
 * Sky, sun, clouds, fog and the islands (built procedurally, identical on every machine). The storm darkens
 * the sky locally around the camera, using the same deterministic storm weight as the waves.
 */
UCLASS()
class SHAREDOCEAN_API ASOEnvironment : public AActor
{
	GENERATED_BODY()
public:
	ASOEnvironment();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** (x, y, radius) in cm. Ships collide with these circles. */
	static const TArray<FVector>& Islands();
	float GetCameraStormWeight() const { return CamStorm; }

private:
	void BuildIsland(int32 Index);
	void BuildDock();
	void BuildLighthouse(FVector Base);
	UProceduralMeshComponent* NewMesh(const TCHAR* Name);

	UPROPERTY() TObjectPtr<UDirectionalLightComponent> Sun;
	UPROPERTY() TObjectPtr<USkyAtmosphereComponent> Atmosphere;
	UPROPERTY() TObjectPtr<USkyLightComponent> SkyLight;
	UPROPERTY() TObjectPtr<UExponentialHeightFogComponent> Fog;
	UPROPERTY() TObjectPtr<UVolumetricCloudComponent> Clouds;
	UPROPERTY() TObjectPtr<UPostProcessComponent> Post;
	UPROPERTY() TObjectPtr<UPointLightComponent> Beacon;
	UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> Meshes;
	float CamStorm = 0.f;
	float Flash = 0.f;
	float NextFlash = 3.f;
};
