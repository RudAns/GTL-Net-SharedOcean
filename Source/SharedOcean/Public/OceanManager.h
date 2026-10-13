#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OceanFFT.h"
#include "OceanManager.generated.h"

class UTexture;
class UTexture2D;
class UTextureRenderTarget2D;
class UMaterialParameterCollection;

UENUM()
enum class ESOSeaState : uint8 { Calm, Normal, Storm, Count };

/** Which bands the surface draws (report ch.6.1: gameplay band vs. visual band). */
UENUM()
enum class ESOLayerView : uint8 { All, GameplayOnly, VisualOnly, TintBands, Count };

/** The sea-state part of the shared state. Replicated by the game state, a few dozen bytes. */
USTRUCT()
struct FSOSeaStateRep
{
	GENERATED_BODY()
	UPROPERTY() uint8 Current = (uint8)ESOSeaState::Normal;
	UPROPERTY() uint8 Previous = (uint8)ESOSeaState::Normal;
	UPROPERTY() double ChangeTime = -100.0;	// server seconds; transitions blend over a fixed time
	UPROPERTY() float SwellScale = 1.f;		// art control: exaggerate the long swell
	UPROPERTY() bool bStorm = true;			// the travelling local storm
	UPROPERTY() int32 Seed = 1337;
	bool operator==(const FSOSeaStateRep& O) const
	{
		return Current == O.Current && Previous == O.Previous && ChangeTime == O.ChangeTime && SwellScale == O.SwellScale && bStorm == O.bStorm && Seed == O.Seed;
	}
};

/**
 * Owns the FFT cascades on this machine, evaluates them once per frame at this machine's estimate of the
 * shared clock, uploads the visual cascades to textures for the ocean material and answers height queries
 * for physics with the gameplay band only. Spawned locally on every machine (it is not replicated: its
 * inputs are the replicated sea state and the network clock).
 *
 * Cascades (metres):  G0  768 m patch, 64^2,  lambda >= 60   gameplay (CPU everywhere, carries the swell)
 *                     G1  192 m patch, 128^2, 16..60         gameplay
 *                     V2   48 m patch, 128^2, 3.2..16        visual only
 *                     V3   12 m patch, 64^2,  < 3.2          visual only
 *                     S0, S1: storm versions of G0, G1 blended in by a deterministic storm weight field.
 */
UCLASS()
class SHAREDOCEAN_API AOceanManager : public AActor
{
	GENERATED_BODY()
public:
	AOceanManager();
	static AOceanManager* Get(const UObject* WorldContext);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	static constexpr int32 NumCascades = 6;		// G0 G1 V2 V3 S0 S1
	static constexpr float LoopPeriod = 512.f;	// seconds; the whole sea repeats exactly
	static const TCHAR* CascadeName(int32 I);

	// ---- time ----
	/** This machine's estimate of the server clock (unwrapped seconds) that the ocean is evaluated at. */
	double GetSharedTime() const { return SharedTime; }
	float GetOceanTime() const { return (float)FMath::Fmod(SharedTime, (double)LoopPeriod); }
	/** Presentation delay (client "rewind" mode): the ocean is drawn and queried this many seconds in the past. */
	void SetPresentationDelay(float Seconds) { PresentationDelay = Seconds; }
	float GetPresentationDelay() const { return PresentationDelay; }

	// ---- queries, centimetres in UE space ----
	/** Gameplay-band displacement of the surface particle whose rest position is RestXY. */
	FVector GameplayDisplacementCm(FVector2D RestXY) const;
	/** Gameplay-band height at world XY. Iterations = 0 ignores the horizontal displacement (report ch.3.3). */
	float HeightCm(FVector2D WorldXY, int32 Iterations = 4) const;
	/** Normal of the gameplay band by central differences. */
	FVector NormalAt(FVector2D WorldXY) const;
	float StormWeight(FVector2D WorldXY) const;
	FVector2D StormCenterCm() const;
	float StormRadiusCm() const { return StormRadiusM * 100.f; }

	// ---- shared state ----
	void ApplySeaState(const FSOSeaStateRep& Rep);
	const FSOSeaStateRep& GetSeaState() const { return SeaRep; }
	float GetStateBlend() const { return StateBlend; }
	static FText SeaStateName(ESOSeaState S);

	// ---- visuals / debug (local only) ----
	ESOLayerView LayerView = ESOLayerView::All;
	bool bStormOverlay = false;
	bool bFoam = true;
	/** Visual band (V2, V3) on the GPU: RDG compute passes instead of the CPU FFT (report ch.9). */
	bool bVisualOnGPU = true;
	bool IsCascadeOnGPU(int32 C) const { return bVisualOnGPU && (C == 2 || C == 3) && GDisp[C - 2] != nullptr; }
	UTexture* GetDispTexture(int32 C) const;
	UTexture* GetDerivTexture(int32 C) const;
	int32 GetTextureVersion() const { return TextureVersion; }
	const FOceanCascade& GetCascade(int32 C) const { return Cascades[C]; }
	double GetEvalMs() const { return EvalMs; }
	float GetSignificantHeight() const;

private:
	void InitCascades();
	void RebuildTables();
	void UpdateTime(float Dt);
	void UploadTextures();
	void PushMaterialParams();

	FOceanCascade Cascades[NumCascades];
	TArray<float> Tables[NumCascades][(int32)ESOSeaState::Count];
	FSOSeaStateRep SeaRep;
	FSOSeaStateRep BuiltRep;
	bool bTablesBuilt = false;
	float StateBlend = 1.f;
	double SharedTime = 0.0;
	float PresentationDelay = 0.f;
	double EvalMs = 0.0;
	float StormRadiusM = 380.f;

	UPROPERTY() TObjectPtr<UTexture2D> DispTex[NumCascades];
	UPROPERTY() TObjectPtr<UTexture2D> DerivTex[NumCascades];
	UPROPERTY() TObjectPtr<UMaterialParameterCollection> MPC;
	UPROPERTY() TObjectPtr<UTexture2D> H0Tex[2];
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> GDisp[2];
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> GDeriv[2];
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> GFoam[2];
	bool bH0Dirty[2] = { true, true };
	bool bLastGPU = true;
	int32 TextureVersion = 0;
	void InitGPU();
	void DispatchGPU(int32 C, float Dt);
};
