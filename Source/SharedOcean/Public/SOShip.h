#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SOShip.generated.h"

class UBoxComponent;
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class AOceanManager;

/**
 * One ship snapshot as it goes over the wire. Custom NetSerialize: positions in whole centimetres
 * (packed ints), rotation as "smallest three" quaternion (2 + 3 x 10 bits), velocities as 16-bit
 * fixed point (report ch.8.2). ServerTime stamps the physics step the state belongs to.
 */
USTRUCT()
struct FSOShipState
{
	GENERATED_BODY()
	double ServerTime = 0.0;
	FVector Pos = FVector::ZeroVector;		// cm
	FQuat Rot = FQuat::Identity;
	FVector Vel = FVector::ZeroVector;		// cm/s
	FVector AngVel = FVector::ZeroVector;	// rad/s, world
	float Rudder = 0.f;						// -1..1
	float Sail = 0.f;						// 0..1
	float ServerDraft = 0.f;				// cm, the server's own draft error at this step (4 cm steps)
	UPROPERTY() uint32 Seq = 0;

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);
	bool Identical(const FSOShipState* Other, uint32 PortFlags) const { return Other && Other->Seq == Seq; }
	static int32 LastSerializedBits;		// for the HUD
};

template<> struct TStructOpsTypeTraits<FSOShipState> : public TStructOpsTypeTraitsBase2<FSOShipState>
{
	enum { WithNetSerializer = true, WithIdentical = true };
};

/** How a client draws a ship that the server simulates (report ch.4.4). */
UENUM()
enum class ESOShipPresentation : uint8
{
	Interpolate,	// whole pose from snapshot interpolation; sea at "now"
	RewindSea,		// whole pose interpolated; sea drawn at the same delayed time
	Reconstruct,	// x, y, yaw interpolated; heave, pitch, roll rebuilt from the local sea
	Count
};

/** Rigid-body state of the simulation (SI units internally except positions in cm). */
struct FSOBody
{
	FVector Pos = FVector::ZeroVector;		// cm
	FQuat Rot = FQuat::Identity;
	FVector Vel = FVector::ZeroVector;		// cm/s
	FVector AngVel = FVector::ZeroVector;	// rad/s
};

UCLASS()
class SHAREDOCEAN_API ASOShip : public AActor
{
	GENERATED_BODY()
public:
	ASOShip();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// ---- control (server) ----
	void SetHelmInput(float InRudder, float InSailDelta);
	UPROPERTY(Replicated) TObjectPtr<APawn> Helmsman;
	FVector GetHelmStandLocation() const;	// world
	FVector GetHelmLocation() const;

	// ---- read-outs ----
	const FSOShipState& GetRepState() const { return RepState; }
	double GetLastSnapshotArrival() const { return LastArrival; }
	float GetSnapshotAgeMs() const;
	float GetDraftErrorCm() const { return DraftErrorCm; }		// mean probe submersion relative to rest draft, on the local sea
	/** Client: local draft error minus the server's draft error for the displayed instant. Near 0 = both machines see the hull sit on the sea the same way. */
	float GetDraftDeltaCm() const { return HasAuthority() ? 0.f : DraftErrorCm - ShownServerDraft; }
	float GetShownServerDraft() const { return HasAuthority() ? DraftErrorCm : ShownServerDraft; }
	float GetSpeedKnots() const;
	FRotator GetDisplayRotation() const { return GetActorRotation(); }
	int32 GetProbeCount() const;
	void GetProbeWorld(TArray<FVector>& Out) const;
	float GetSnapshotKbps() const { return SnapshotKbps; }
	const FSOShipState* GetNewestSnapshot() const { return Buffer.Num() ? &Buffer.Last() : nullptr; }
	float Rudder = 0.f, Sail = 0.f;			// current control values (server: authoritative; client: replicated)
	bool bAutopilot = false;
	float AutoRudder = 0.2f;

private:
	UFUNCTION() void OnRep_State();
	void BuildMesh();
	void ServerStep(float Dt);
	void StepBody(FSOBody& B, float Dt, const AOceanManager* Ocean, bool bVerticalOnly) const;
	void BuoyancyProbes(const FSOBody& B, const AOceanManager* Ocean, FVector& F, FVector& Tau, int32& Wet) const;
	void BuoyancyPlaneFit(const FSOBody& B, const AOceanManager* Ocean, FVector& F, FVector& Tau) const;
	float QueryHeight(const AOceanManager* Ocean, FVector2D P) const;
	void ApplyPose(const FVector& Pos, const FQuat& Rot);
	bool SampleBuffer(double T, FSOShipState& Out) const;
	void UpdateClient(float Dt);
	void UpdateVisualBits(float Dt);
	void CollideIslands(FSOBody& B) const;
	FVector BodyOrigin() const;

	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Deck;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> HullMesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> SailMesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> WheelMesh;
	UPROPERTY() TArray<TObjectPtr<UBoxComponent>> Walls;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SailMID;

	UPROPERTY(ReplicatedUsing = OnRep_State) FSOShipState RepState;

	FSOBody Body;			// server: authoritative state; client: reconstruction state
	FSOBody PrevBody;		// server: previous step for render interpolation
	float Accumulator = 0.f;
	double SimTime = 0.0;
	float RudderInput = 0.f, SailInput = 0.f;
	uint32 Seq = 0;
	TArray<FSOShipState> Buffer;
	double LastArrival = 0.0;
	float DraftErrorCm = 0.f;
	float ShownServerDraft = 0.f;
	float ComputeDraft(const FVector& Origin, const FQuat& Rot) const;
	float WheelAngle = 0.f;
	float SnapshotKbps = 0.f;
	double KbpsWindowStart = 0.0;
	int32 KbpsBits = 0;
	bool bBodyInit = false;
};
