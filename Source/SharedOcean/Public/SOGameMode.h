#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SOGameMode.generated.h"

class ASOShip;

/** Spawns the crew's sloop, a second ship sailing on autopilot and the floating treasure, all server-side. */
UCLASS()
class SHAREDOCEAN_API ASOGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ASOGameMode();
	virtual void StartPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void EnsureWorld();
	UPROPERTY() TObjectPtr<ASOShip> CrewShip;
	UPROPERTY() TObjectPtr<ASOShip> RivalShip;
	int32 Joined = 0;
};
