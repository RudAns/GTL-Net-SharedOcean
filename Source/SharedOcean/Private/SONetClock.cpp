#include "SONetClock.h"

#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "SOPlayerController.h"

USONetClock::USONetClock()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
}

double USONetClock::LocalNow() const
{
	// Real time, not world time: monotonic and not quantised to frames, so round trips are measured properly.
	return FPlatformTime::Seconds();
}

void USONetClock::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ASOPlayerController* PC = Cast<ASOPlayerController>(GetOwner());
	if (!PC || !PC->IsLocalController() || GetNetMode() != NM_Client) return;

	PingTimer -= DeltaTime;
	if (PingTimer <= 0.f)
	{
		PingTimer = bHasSample ? 0.25f : 0.05f;
		PC->ServerClockPing(LocalNow());
		++PingsSent;
	}
	if (bHasSample)
	{
		// Slew: at most 5% of real time, so the sea speeds up or slows down imperceptibly instead of jumping.
		const double Diff = TargetOffset - SmoothedOffset;
		const double MaxStep = 0.05 * DeltaTime;
		if (FMath::Abs(Diff) > 1.0) SmoothedOffset = TargetOffset;		// first lock or a long stall
		else SmoothedOffset += FMath::Clamp(Diff, -MaxStep, MaxStep);
	}
}

void USONetClock::OnPong(double ClientSendTime, double ServerTime)
{
	const double Now = LocalNow();
	const float Rtt = (float)FMath::Max(0.0, Now - ClientSendTime);
	if (Rtt <= 0.0005f) return;		// a reply inside the same instant cannot be a real round trip
	const double Offset = ServerTime + Rtt * 0.5 - Now;
	++PongsReceived;
	LastRttMs = Rtt * 1000.f;
	Window.Add({ Rtt, Offset });
	if (Window.Num() > 24) Window.RemoveAt(0);
	// The sample with the smallest round trip has the least room for asymmetric delay.
	int32 Best = 0;
	double Lo = 1e9, Hi = -1e9;
	for (int32 I = 0; I < Window.Num(); ++I)
	{
		if (Window[I].Rtt < Window[Best].Rtt) Best = I;
		Lo = FMath::Min(Lo, Window[I].Offset);
		Hi = FMath::Max(Hi, Window[I].Offset);
	}
	MinRttMs = Window[Best].Rtt * 1000.f;
	OffsetJitterMs = float((Hi - Lo) * 1000.0);
	TargetOffset = Window[Best].Offset;
	if (!bHasSample) SmoothedOffset = TargetOffset;
	bHasSample = true;
}

double USONetClock::GetServerTimeEstimate() const
{
	if (GetNetMode() != NM_Client) return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	return LocalNow() + SmoothedOffset;
}

double USONetClock::GetOceanClockTime() const
{
	double T = GetServerTimeEstimate();
	if (GetNetMode() == NM_Client)
	{
		switch (Mode)
		{
		case ESOClockMode::EngineDefault:
			if (const AGameStateBase* GS = GetWorld()->GetGameState()) T = GS->GetServerWorldTimeSeconds();
			break;
		case ESOClockMode::LocalClock:
			T = GetWorld()->GetTimeSeconds();	// this client's own clock, which started when it loaded the map
			break;
		default: break;
		}
	}
	return T + InjectedErrorMs * 0.001;
}

float USONetClock::ModeErrorMs() const
{
	return float((GetOceanClockTime() - GetServerTimeEstimate()) * 1000.0);
}
