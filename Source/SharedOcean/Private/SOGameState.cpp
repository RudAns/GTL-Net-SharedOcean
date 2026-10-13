#include "SOGameState.h"

#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "OceanSurface.h"
#include "SOEnvironment.h"

ASOGameState::ASOGameState()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ASOGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASOGameState, Sea);
	DOREPLIFETIME(ASOGameState, Settings);
	DOREPLIFETIME(ASOGameState, Treasure);
}

ASOGameState* ASOGameState::Get(const UObject* WorldContext)
{
	UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	return W ? W->GetGameState<ASOGameState>() : nullptr;
}

void ASOGameState::BeginPlay()
{
	Super::BeginPlay();
	// Every machine builds its own ocean, surface and scenery; only their inputs are shared.
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (!AOceanManager::Get(this)) GetWorld()->SpawnActor<AOceanManager>(AOceanManager::StaticClass(), FTransform::Identity, P);
	if (!IsRunningDedicatedServer())
	{
		GetWorld()->SpawnActor<AOceanSurface>(AOceanSurface::StaticClass(), FTransform::Identity, P);
	}
	GetWorld()->SpawnActor<ASOEnvironment>(ASOEnvironment::StaticClass(), FTransform::Identity, P);
	PushToOcean();
}

void ASOGameState::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (AOceanManager* M = AOceanManager::Get(this))
		if (!(M->GetSeaState() == Sea)) M->ApplySeaState(Sea);
}

void ASOGameState::OnRep_Sea() { PushToOcean(); }

void ASOGameState::PushToOcean()
{
	if (AOceanManager* M = AOceanManager::Get(this)) M->ApplySeaState(Sea);
}

void ASOGameState::SetSeaState(ESOSeaState S)
{
	if (!HasAuthority() || Sea.Current == (uint8)S) return;
	Sea.Previous = Sea.Current;
	Sea.Current = (uint8)S;
	Sea.ChangeTime = GetWorld()->GetTimeSeconds();
	PushToOcean();
}

void ASOGameState::SetSwellScale(float Scale)
{
	if (!HasAuthority()) return;
	Sea.SwellScale = FMath::Clamp(FMath::RoundToFloat(Scale * 4.f) / 4.f, 0.f, 3.f);
	PushToOcean();
}

void ASOGameState::SetStorm(bool bOn)
{
	if (!HasAuthority()) return;
	Sea.bStorm = bOn;
	PushToOcean();
}
