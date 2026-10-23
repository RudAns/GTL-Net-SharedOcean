#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SOShip.h"
#include "SONetClock.h"
#include "SOPlayerController.generated.h"

class USOHud;
class ASOCharacter;

/** Every lab switch, so the HUD, hotkeys and the self-test all go through one place. */
UENUM()
enum class ESOLab : uint8
{
	LayerView, Probes, ClockMode, Presentation, SeaState, StormOverlay, NetLag, Ghost, Buoyancy, PhysicsHz,
	ProbeCount, SwellScale, InterpDelay, Storm, InverseQuery, SnapshotHz, ClockError, CameraRoll, LevelTint, Foam,
	VisualGPU,
	Count
};

/**
 * Input, the lab switches, the network clock RPCs, debug drawing and the scripted self-test (-SOCapture).
 * Server-side switches (sea state, buoyancy model, physics rate...) are sent as RPCs and replicated through
 * the game state, so every machine sees the same authority; client-side switches (clock mode, presentation,
 * interpolation delay) only change how this machine evaluates and draws the shared state.
 */
UCLASS()
class SHAREDOCEAN_API ASOPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	ASOPlayerController();
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	USONetClock* GetNetClock() const { return NetClock; }
	ASOCharacter* GetSailor() const;
	ASOShip* GetShip() const;			// the ship the sailor is on (or the player's ship)
	ASOShip* GetNearbyHelmShip() const;

	// ---- client-side settings ----
	ESOShipPresentation Presentation = ESOShipPresentation::Interpolate;
	float InterpDelayMs = 100.f;
	float NetLagMs = 0.f;
	bool bShowProbes = false;
	bool bShowGhost = false;
	bool bLabOpen = false;
	bool bHelpOpen = false;
	bool bIntroOpen = true;

	// ---- lab ----
	void Lab(ESOLab Which, int32 Dir = 1);
	FText LabValue(ESOLab Which) const;
	static FText LabName(ESOLab Which);
	static FText LabKey(ESOLab Which);
	static bool LabIsServer(ESOLab Which);
	void Toast(const FString& Title, const FString& Body);
	FString ToastTitle, ToastBody;
	double ToastTime = -100.0;
	void StartGame();

	// ---- RPCs ----
	UFUNCTION(Server, Unreliable) void ServerClockPing(double ClientTime);
	UFUNCTION(Client, Unreliable) void ClientClockPong(double ClientTime, double ServerTime);
	UFUNCTION(Server, Reliable) void ServerInteract();
	UFUNCTION(Server, Unreliable) void ServerHelmInput(float Rudder, float SailDelta);
	UFUNCTION(Server, Reliable) void ServerLab(ESOLab Which, int32 Dir);
	UFUNCTION(Server, Reliable) void ServerSetLag(float Ms);

	bool IsHost() const { return GetNetMode() != NM_Client; }
	float FrameMs = 0.f;

private:
	void ApplyLag(float Ms);
	void DrawDebugViews();
	void UpdateMovement(float Dt);
	void OnKey(FKey Key);
	void OnMouseX(float V);
	void OnMouseY(float V);
	void TickCapture(float Dt);

	UPROPERTY() TObjectPtr<USONetClock> NetClock;
	UPROPERTY() TObjectPtr<USOHud> Hud;
	float MoveF = 0.f, MoveR = 0.f;
	float HelmSendTimer = 0.f;
	float LastRudder = 0.f, LastSail = 0.f;

	// self-test
	FString CaptureDir;
	FString CaptureRole;
	FString JoinAddress;		// -SOJoin=<addr>: keep trying to reach the host while we are not a client
	FTimerHandle JoinTimer;
	void RetryJoin();
	FString WindowSide;			// -SOSide=L|R: left or right half of the primary monitor (Run2P)
	void PlaceWindow();				// role in the title bar, and side-by-side placement
	double CaptureT = 0.0;
	int32 CaptureStep = 0;
	double StepStart = 0.0;
	TArray<float> Samples;
	TMap<FString, float> Metrics;
	TSet<FKey> HeldKeys;
	void Press(FKey Key);
	void Hold(FKey Key, bool bDown);
	void Shot(const FString& Name);
	void Check(bool bOk, const FString& Name, const FString& Detail);
};
