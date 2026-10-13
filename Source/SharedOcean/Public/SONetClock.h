#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SONetClock.generated.h"

/** Where the ocean takes its time from on this client (report ch.4.2). */
UENUM()
enum class ESOClockMode : uint8
{
	NetworkSynced,	// NTP-style: min-RTT sample, slewed, monotonic
	EngineDefault,	// AGameStateBase::GetServerWorldTimeSeconds(): replicated delta, no latency compensation
	LocalClock,		// this machine's own clock: the sea starts when the client joins
	Count
};

/**
 * Network clock owned by the client's PlayerController. Sends a ping with the local send time a few times
 * per second; the server answers with its clock. Offset = Ts + RTT/2 - t1 from the sample with the smallest
 * round trip in a sliding window, then the local estimate slews towards it at a bounded rate so ocean time
 * never jumps or runs backwards.
 */
UCLASS(ClassGroup = (SharedOcean))
class SHAREDOCEAN_API USONetClock : public UActorComponent
{
	GENERATED_BODY()
public:
	USONetClock();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Called by the controller when a pong arrives. */
	void OnPong(double ClientSendTime, double ServerTime);

	/** Best estimate of the server clock right now (always the synced estimate; used for snapshot interpolation). */
	double GetServerTimeEstimate() const;
	/** Time the ocean is evaluated at, according to the selected mode, plus the injected error. */
	double GetOceanClockTime() const;
	double LocalNow() const;

	ESOClockMode Mode = ESOClockMode::NetworkSynced;
	float InjectedErrorMs = 0.f;	// lab control: add a fixed clock error to see its effect on the sea

	// stats for the HUD
	float LastRttMs = 0.f;
	float MinRttMs = 0.f;
	float OffsetJitterMs = 0.f;		// spread of offsets in the window
	double TargetOffset = 0.0;		// server - local, from the best sample
	double SmoothedOffset = 0.0;
	bool bHasSample = false;
	int32 PingsSent = 0, PongsReceived = 0;
	/** Offset error of the selected mode relative to the synced estimate, in ms (what the sea is off by). */
	float ModeErrorMs() const;

private:
	struct FSample { float Rtt; double Offset; };
	TArray<FSample> Window;
	float PingTimer = 0.f;
};
