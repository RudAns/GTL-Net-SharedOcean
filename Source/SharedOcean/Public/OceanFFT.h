#pragma once

#include "CoreMinimal.h"

/**
 * Tessendorf FFT ocean on the CPU (report ch.2, ch.9 "GTLOceanCPU").
 *
 * The surface is a pure function of (seed, spectrum parameters, time): nothing is integrated, so every
 * machine that evaluates the same parameters at the same time gets the same sea (report ch.4.1).
 * Angular frequencies are quantised to multiples of 2*pi/LoopPeriod (Tessendorf, "Animating Waves"),
 * so the sea repeats exactly every LoopPeriod seconds and time can be wrapped before it reaches float
 * precision trouble (report ch.4.2).
 *
 * Units: metres. Grid axis X = UE +X, grid axis Z = UE +Y. Output height is UE +Z.
 */

/** Spectrum of one sea state: a Phillips wind sea scaled to a target significant height, plus a narrow swell. */
struct FOceanSeaState
{
	float WindSpeed = 10.f;			// m/s, sets the Phillips length scale L = V^2/g
	float WindDirDeg = 30.f;		// direction the wind blows towards, degrees from +X
	float SignificantHeight = 2.f;	// Hs = 4 sigma of the wind sea over all cascades, metres
	float SwellAmplitude = 1.f;		// crest amplitude of the swell component, metres (art control, report ch.6.2)
	float SwellWavelength = 140.f;	// metres
	float SwellDirDeg = 10.f;
	float Choppiness = 1.f;			// lambda for the horizontal displacement
};

struct FOceanComplex
{
	float Re = 0.f, Im = 0.f;
	FOceanComplex() = default;
	FOceanComplex(float R, float I) : Re(R), Im(I) {}
	FORCEINLINE FOceanComplex operator+(const FOceanComplex& B) const { return { Re + B.Re, Im + B.Im }; }
	FORCEINLINE FOceanComplex operator-(const FOceanComplex& B) const { return { Re - B.Re, Im - B.Im }; }
	FORCEINLINE FOceanComplex operator*(const FOceanComplex& B) const { return { Re * B.Re - Im * B.Im, Re * B.Im + Im * B.Re }; }
	FORCEINLINE FOceanComplex operator*(float S) const { return { Re * S, Im * S }; }
	FORCEINLINE FOceanComplex Conj() const { return { Re, -Im }; }
	FORCEINLINE FOceanComplex MulI() const { return { -Im, Re }; }	// * i
};

/** In-place radix-2 FFT of size N along rows and columns (inverse transform, e^{+i}). */
class FOceanFFT2D
{
public:
	void Init(int32 InN);
	void Inverse2D(FOceanComplex* Data) const;	// N*N, row-major
	int32 GetN() const { return N; }
private:
	void Inverse1D(FOceanComplex* Line) const;
	int32 N = 0, LogN = 0;
	TArray<int32> BitRev;
	TArray<FOceanComplex> Twiddle;	// e^{+2 pi i k / N}, k < N/2
};

/** One square periodic patch that carries a band [LambdaMin, LambdaMax) of the spectrum. */
class SHAREDOCEAN_API FOceanCascade
{
public:
	void Init(int32 InN, float InPatchSize, float InLambdaMin, float InLambdaMax, uint32 InSeed, bool bInCarriesSwell);

	/** Fills the per-state amplitude table. VarianceScale converts the Phillips shape to metres^2 (shared by all cascades). */
	void BuildAmplitudes(const FOceanSeaState& State, float VarianceScale, TArray<float>& OutAmp) const;
	/** Unscaled wind-sea variance of this band for a state, used to calibrate VarianceScale. */
	double BandVariance(const FOceanSeaState& State) const;

	/** Blend between two prepared amplitude tables (smooth sea-state transitions, report ch.6.3). */
	void SetAmplitudes(const TArray<float>& A, const TArray<float>& B, float Blend, float InChoppiness);

	/** Evaluates the surface at time T (already wrapped to the loop). bFull also produces slopes/Jacobian for rendering. */
	void Evaluate(double T, float LoopPeriod, bool bFull);
	/** Visual-only foam accumulation (time integrated, therefore not part of the shared state). */
	void UpdateFoam(float Dt, float Threshold, float Gain, float DecaySeconds);

	/** Packs (h0(k), conj(h0(-k))) for the GPU path, from the current amplitudes. */
	void BuildH0(TArray<FLinearColor>& Out) const;

	/** Bilinear, wrapped. Returns (dispX, dispY, height) in metres at rest position (X, Y) in metres. */
	FVector3f SampleDisplacement(float X, float Y) const;

	int32 GetN() const { return N; }
	float GetPatchSize() const { return PatchSize; }
	float GetLambdaMin() const { return LambdaMin; }
	float GetLambdaMax() const { return LambdaMax; }
	float GetChoppiness() const { return Choppiness; }
	double GetLastEvalMs() const { return LastEvalMs; }

	// Outputs, N*N row-major (row = grid Z = UE Y).
	TArray<FVector3f> Disp;		// (lambda*Dx, lambda*Dz, h)  -> UE (X, Y, Z)
	TArray<FVector4f> Deriv;	// (dh/dx, dh/dy, lambda*dDx/dx, lambda*dDy/dy)
	TArray<float> Jacobian;
	TArray<float> Foam;

private:
	float Phillips(float Kx, float Ky, const FOceanSeaState& S) const;
	float Swell(float Kx, float Ky, const FOceanSeaState& S) const;
	bool InBand(float KLen) const;
	FORCEINLINE float KAt(int32 I) const { return 2.f * PI * float(I < N / 2 ? I : I - N) / PatchSize; }

	int32 N = 0;
	float PatchSize = 100.f, LambdaMin = 0.f, LambdaMax = 1e9f, Choppiness = 1.f;
	bool bCarriesSwell = false;
	uint32 Seed = 1;
	FOceanFFT2D FFT;
	TArray<FOceanComplex> Xi;		// fixed gaussian draws per mode (the "seed" part of the shared state)
	TArray<float> Amp;				// sqrt(variance/2) per mode for the current blend
	TArray<FOceanComplex> F1, F2, F3, F4;
	double LastEvalMs = 0.0;
};
