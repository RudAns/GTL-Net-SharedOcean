#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "OceanManager.h"
#include "SOGameState.generated.h"

UENUM()
enum class ESOBuoyancyModel : uint8 { Probes, PlaneFit, Count };

/** Server-side lab settings everyone sees (they change what the authority simulates). */
USTRUCT()
struct FSOServerSettings
{
	GENERATED_BODY()
	UPROPERTY() uint8 Buoyancy = (uint8)ESOBuoyancyModel::Probes;
	UPROPERTY() uint8 ProbeCount = 8;		// 1, 2, 4, 8, 16
	UPROPERTY() uint8 PhysicsHz = 60;		// 60, 30, 10
	UPROPERTY() uint8 SnapshotHz = 30;		// 30, 20, 10
	UPROPERTY() bool bInverseQuery = true;	// height query with displacement inversion (report ch.3.3)
};

/**
 * Replicates the shared state that is not a moving object: the sea state (seed, state, change time, swell
 * scale, storm on/off), the server lab settings and the treasure score. The ocean itself is never sent.
 */
UCLASS()
class SHAREDOCEAN_API ASOGameState : public AGameStateBase
{
	GENERATED_BODY()
public:
	ASOGameState();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(ReplicatedUsing = OnRep_Sea) FSOSeaStateRep Sea;
	UPROPERTY(Replicated) FSOServerSettings Settings;
	UPROPERTY(Replicated) int32 Treasure = 0;

	UFUNCTION() void OnRep_Sea();

	// server
	void SetSeaState(ESOSeaState S);
	void SetSwellScale(float Scale);
	void SetStorm(bool bOn);

	static ASOGameState* Get(const UObject* WorldContext);

private:
	void PushToOcean();
};
