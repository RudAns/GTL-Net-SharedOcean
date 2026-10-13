#include "SOGameMode.h"

#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "SOCharacter.h"
#include "SOChest.h"
#include "SOGameState.h"
#include "SOPlayerController.h"
#include "SOShip.h"

ASOGameMode::ASOGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = ASOCharacter::StaticClass();
	PlayerControllerClass = ASOPlayerController::StaticClass();
	GameStateClass = ASOGameState::StaticClass();
	bUseSeamlessTravel = false;
}

void ASOGameMode::EnsureWorld()
{
	if (CrewShip) return;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// The crew's sloop waits off the start island's jetty; the rival circles further out.
	CrewShip = GetWorld()->SpawnActor<ASOShip>(ASOShip::StaticClass(), FTransform(FRotator(0, 25.f, 0), FVector(-52000.f, -26000.f, 110.f)), P);
	RivalShip = GetWorld()->SpawnActor<ASOShip>(ASOShip::StaticClass(), FTransform(FRotator(0, 200.f, 0), FVector(-20000.f, 9000.f, 110.f)), P);
	if (RivalShip) { RivalShip->bAutopilot = true; RivalShip->AutoRudder = 0.22f; RivalShip->Sail = 0.55f; }
	for (int32 I = 0; I < 3; ++I)
	{
		ASOChest* C = GetWorld()->SpawnActor<ASOChest>(ASOChest::StaticClass(), FTransform(FVector::ZeroVector), P);
		if (C) C->Respawn(I);
	}
}

void ASOGameMode::StartPlay()
{
	EnsureWorld();
	Super::StartPlay();
}

void ASOGameMode::RestartPlayer(AController* NewPlayer)
{
	EnsureWorld();
	if (!NewPlayer || !CrewShip) { Super::RestartPlayer(NewPlayer); return; }
	// Everyone joins the same crew: stand on the deck amidships, a little apart.
	const float Side = (Joined++ % 2) ? -110.f : 110.f;
	const FVector Loc = CrewShip->GetActorTransform().TransformPosition(FVector(330.f, Side, 15.f + 100.f));
	const FRotator Rot(0, CrewShip->GetActorRotation().Yaw, 0);
	RestartPlayerAtTransform(NewPlayer, FTransform(Rot, Loc));
	if (NewPlayer->GetPawn()) NewPlayer->SetControlRotation(FRotator(-12.f, Rot.Yaw, 0));
}

void ASOGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
}
