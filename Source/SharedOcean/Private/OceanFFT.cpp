#include "OceanFFT.h"

#include "Async/ParallelFor.h"
#include "HAL/PlatformTime.h"
#include "Math/RandomStream.h"

namespace
{
	constexpr float G = 9.81f;
}

// ---------------------------------------------------------------------------------------------------------------
// FFT

void FOceanFFT2D::Init(int32 InN)
{
	N = InN;
	LogN = FMath::FloorLog2(N);
	check((1 << LogN) == N);
	BitRev.SetNumUninitialized(N);
	for (int32 I = 0; I < N; ++I)
	{
		int32 R = 0;
		for (int32 B = 0; B < LogN; ++B) if (I & (1 << B)) R |= 1 << (LogN - 1 - B);
		BitRev[I] = R;
	}
	Twiddle.SetNumUninitialized(N / 2);
	for (int32 K = 0; K < N / 2; ++K)
	{
		const double A = 2.0 * UE_DOUBLE_PI * K / N;
		Twiddle[K] = FOceanComplex((float)FMath::Cos(A), (float)FMath::Sin(A));
	}
}

void FOceanFFT2D::Inverse1D(FOceanComplex* A) const
{
	for (int32 I = 0; I < N; ++I)
	{
		const int32 R = BitRev[I];
		if (R > I) Swap(A[I], A[R]);
	}
	for (int32 Size = 2; Size <= N; Size <<= 1)
	{
		const int32 Half = Size >> 1, Step = N / Size;
		for (int32 I = 0; I < N; I += Size)
		{
			for (int32 K = 0; K < Half; ++K)
			{
				const FOceanComplex T = Twiddle[K * Step] * A[I + K + Half];
				A[I + K + Half] = A[I + K] - T;
				A[I + K] = A[I + K] + T;
			}
		}
	}
}

void FOceanFFT2D::Inverse2D(FOceanComplex* Data) const
{
	const int32 LocalN = N;
	ParallelFor(LocalN, [this, Data, LocalN](int32 Row) { Inverse1D(Data + Row * LocalN); });
	ParallelFor(LocalN, [this, Data, LocalN](int32 Col)
	{
		TArray<FOceanComplex, TInlineAllocator<512>> Line;
		Line.SetNumUninitialized(LocalN);
		for (int32 R = 0; R < LocalN; ++R) Line[R] = Data[R * LocalN + Col];
		Inverse1D(Line.GetData());
		for (int32 R = 0; R < LocalN; ++R) Data[R * LocalN + Col] = Line[R];
	});
}

// ---------------------------------------------------------------------------------------------------------------
// Cascade

void FOceanCascade::Init(int32 InN, float InPatchSize, float InLambdaMin, float InLambdaMax, uint32 InSeed, bool bInCarriesSwell)
{
	N = InN;
	PatchSize = InPatchSize;
	LambdaMin = InLambdaMin;
	LambdaMax = InLambdaMax;
	Seed = InSeed;
	bCarriesSwell = bInCarriesSwell;
	FFT.Init(N);
	const int32 NN = N * N;
	// Fixed gaussian draws in a fixed order: identical on every machine that uses the same seed.
	FRandomStream Rng((int32)(Seed * 2654435761u));
	Xi.SetNumUninitialized(NN);
	for (int32 I = 0; I < NN; ++I)
	{
		const float U1 = FMath::Max(Rng.GetFraction(), 1e-7f), U2 = Rng.GetFraction();
		const float R = FMath::Sqrt(-2.f * FMath::Loge(U1));
		Xi[I] = FOceanComplex(R * FMath::Cos(2.f * PI * U2), R * FMath::Sin(2.f * PI * U2));
	}
	Amp.Init(0.f, NN);
	F1.SetNumZeroed(NN); F2.SetNumZeroed(NN); F3.SetNumZeroed(NN); F4.SetNumZeroed(NN);
	Disp.SetNumZeroed(NN);
	Deriv.SetNumZeroed(NN);
	Jacobian.Init(1.f, NN);
	Foam.Init(0.f, NN);
}

bool FOceanCascade::InBand(float KLen) const
{
	if (KLen <= 0.f) return false;
	const float Lambda = 2.f * PI / KLen;
	return Lambda >= LambdaMin && Lambda < LambdaMax;
}

float FOceanCascade::Phillips(float Kx, float Ky, const FOceanSeaState& S) const
{
	const float K2 = Kx * Kx + Ky * Ky;
	if (K2 < 1e-12f) return 0.f;
	const float K = FMath::Sqrt(K2);
	const float L = S.WindSpeed * S.WindSpeed / G;
	const float Wx = FMath::Cos(FMath::DegreesToRadians(S.WindDirDeg)), Wy = FMath::Sin(FMath::DegreesToRadians(S.WindDirDeg));
	float Dir = (Kx * Wx + Ky * Wy) / K;
	const float Against = Dir < 0.f ? 0.07f : 1.f;		// waves running against the wind are weak
	const float SmallL = 0.05f;							// suppress capillary waves
	const float P = FMath::Exp(-1.f / (K2 * L * L)) / (K2 * K2) * Dir * Dir * Against * FMath::Exp(-K2 * SmallL * SmallL);
	const float Dk = 2.f * PI / PatchSize;
	return P * Dk * Dk;
}

float FOceanCascade::Swell(float Kx, float Ky, const FOceanSeaState& S) const
{
	const float K = FMath::Sqrt(Kx * Kx + Ky * Ky);
	if (K < 1e-6f) return 0.f;
	const float Ks = 2.f * PI / FMath::Max(S.SwellWavelength, 1.f);
	const float Dx = FMath::Cos(FMath::DegreesToRadians(S.SwellDirDeg)), Dy = FMath::Sin(FMath::DegreesToRadians(S.SwellDirDeg));
	const float Dir = FMath::Max((Kx * Dx + Ky * Dy) / K, 0.f);
	const float R = (K - Ks) / (0.12f * Ks);
	return FMath::Exp(-R * R) * FMath::Pow(Dir, 24.f);
}

double FOceanCascade::BandVariance(const FOceanSeaState& State) const
{
	double Sum = 0.0;
	for (int32 J = 0; J < N; ++J)
		for (int32 I = 0; I < N; ++I)
		{
			const float Kx = KAt(I), Ky = KAt(J);
			if (InBand(FMath::Sqrt(Kx * Kx + Ky * Ky))) Sum += Phillips(Kx, Ky, State);
		}
	return Sum;
}

void FOceanCascade::BuildAmplitudes(const FOceanSeaState& State, float VarianceScale, TArray<float>& OutAmp) const
{
	const int32 NN = N * N;
	OutAmp.Init(0.f, NN);
	TArray<float> SwellW;
	double SwellSum = 0.0;
	if (bCarriesSwell && State.SwellAmplitude > 0.f)
	{
		SwellW.Init(0.f, NN);
		for (int32 J = 0; J < N; ++J)
			for (int32 I = 0; I < N; ++I)
			{
				const float W = Swell(KAt(I), KAt(J), State);
				SwellW[J * N + I] = W;
				SwellSum += W;
			}
	}
	const double SwellVar = 0.5 * State.SwellAmplitude * State.SwellAmplitude;
	for (int32 J = 0; J < N; ++J)
		for (int32 I = 0; I < N; ++I)
		{
			const float Kx = KAt(I), Ky = KAt(J);
			double Var = 0.0;
			if (InBand(FMath::Sqrt(Kx * Kx + Ky * Ky))) Var += VarianceScale * Phillips(Kx, Ky, State);
			if (SwellSum > 0.0) Var += SwellVar * SwellW[J * N + I] / SwellSum;
			// h~ uses both h0(k) and conj(h0(-k)), so each draw carries half of the mode's variance.
			OutAmp[J * N + I] = (float)FMath::Sqrt(Var * 0.25);
		}
}

void FOceanCascade::SetAmplitudes(const TArray<float>& A, const TArray<float>& B, float Blend, float InChoppiness)
{
	Choppiness = InChoppiness;
	const int32 NN = N * N;
	for (int32 I = 0; I < NN; ++I) Amp[I] = FMath::Lerp(A[I], B[I], Blend);
}

void FOceanCascade::Evaluate(double T, float LoopPeriod, bool bFull)
{
	const double Start = FPlatformTime::Seconds();
	const double W0 = 2.0 * UE_DOUBLE_PI / LoopPeriod;
	const int32 LocalN = N;
	ParallelFor(LocalN, [&, LocalN](int32 J)
	{
		const float Ky = KAt(J);
		const int32 Jm = (LocalN - J) % LocalN;
		for (int32 I = 0; I < LocalN; ++I)
		{
			const int32 Idx = J * LocalN + I;
			const int32 Im = (LocalN - I) % LocalN;
			const int32 IdxM = Jm * LocalN + Im;
			const float Kx = KAt(I);
			const float K = FMath::Sqrt(Kx * Kx + Ky * Ky);
			const float A0 = Amp[Idx], A1 = Amp[IdxM];
			if (K < 1e-6f || (A0 == 0.f && A1 == 0.f))
			{
				F1[Idx] = F2[Idx] = FOceanComplex();
				if (bFull) F3[Idx] = F4[Idx] = FOceanComplex();
				continue;
			}
			// Quantised dispersion: every component repeats with period LoopPeriod.
			const double Omega = FMath::Max(1.0, FMath::FloorToDouble(FMath::Sqrt(G * K) / W0)) * W0;
			const double Ph = FMath::Fmod(Omega * T, 2.0 * UE_DOUBLE_PI);
			const FOceanComplex E((float)FMath::Cos(Ph), (float)FMath::Sin(Ph));
			const FOceanComplex H0 = Xi[Idx] * A0;
			const FOceanComplex H0m = (Xi[IdxM] * A1).Conj();
			const FOceanComplex H = H0 * E + H0m * E.Conj();
			const float Nx = Kx / K, Ny = Ky / K;
			const FOceanComplex Dx = H.MulI() * Nx;			// i kx/k h
			const FOceanComplex Dy = H.MulI() * Ny;
			F1[Idx] = H + Dx.MulI();							// real: h,   imag: Dx
			F2[Idx] = Dy + (H.MulI() * Kx).MulI();				// real: Dy,  imag: dh/dx
			if (bFull)
			{
				F3[Idx] = H.MulI() * Ky + (H * (-Kx * Nx)).MulI();	// real: dh/dy, imag: dDx/dx
				F4[Idx] = H * (-Ky * Ny) + (H * (-Kx * Ny)).MulI();	// real: dDy/dy, imag: dDx/dy
			}
		}
	});
	FFT.Inverse2D(F1.GetData());
	FFT.Inverse2D(F2.GetData());
	if (bFull)
	{
		FFT.Inverse2D(F3.GetData());
		FFT.Inverse2D(F4.GetData());
	}
	const float L = Choppiness;
	ParallelFor(LocalN, [&, LocalN](int32 J)
	{
		for (int32 I = 0; I < LocalN; ++I)
		{
			const int32 Idx = J * LocalN + I;
			Disp[Idx] = FVector3f(L * F1[Idx].Im, L * F2[Idx].Re, F1[Idx].Re);
			if (bFull)
			{
				const float Dxx = L * F3[Idx].Im, Dyy = L * F4[Idx].Re, Dxy = L * F4[Idx].Im;
				Deriv[Idx] = FVector4f(F2[Idx].Im, F3[Idx].Re, Dxx, Dyy);
				Jacobian[Idx] = (1.f + Dxx) * (1.f + Dyy) - Dxy * Dxy;
			}
		}
	});
	LastEvalMs = (FPlatformTime::Seconds() - Start) * 1000.0;
}

void FOceanCascade::BuildH0(TArray<FLinearColor>& Out) const
{
	Out.SetNumUninitialized(N * N);
	for (int32 J = 0; J < N; ++J)
		for (int32 I = 0; I < N; ++I)
		{
			const int32 Idx = J * N + I, IdxM = ((N - J) % N) * N + (N - I) % N;
			const FOceanComplex A = Xi[Idx] * Amp[Idx], B = (Xi[IdxM] * Amp[IdxM]).Conj();
			Out[Idx] = FLinearColor(A.Re, A.Im, B.Re, B.Im);
		}
}

void FOceanCascade::UpdateFoam(float Dt, float Threshold, float Gain, float DecaySeconds)
{
	const float Decay = FMath::Exp(-Dt / FMath::Max(DecaySeconds, 0.01f));
	const int32 NN = N * N;
	for (int32 I = 0; I < NN; ++I)
		Foam[I] = FMath::Max(Foam[I] * Decay, FMath::Clamp((Threshold - Jacobian[I]) * Gain, 0.f, 1.f));
}

FVector3f FOceanCascade::SampleDisplacement(float X, float Y) const
{
	const float U = X / PatchSize * N, V = Y / PatchSize * N;
	const float Fu = FMath::FloorToFloat(U), Fv = FMath::FloorToFloat(V);
	const float Tu = U - Fu, Tv = V - Fv;
	auto W = [this](int32 A) { return ((A % N) + N) % N; };
	const int32 I0 = W((int32)Fu), J0 = W((int32)Fv), I1 = W(I0 + 1), J1 = W(J0 + 1);
	const FVector3f A = FMath::Lerp(Disp[J0 * N + I0], Disp[J0 * N + I1], Tu);
	const FVector3f B = FMath::Lerp(Disp[J1 * N + I0], Disp[J1 * N + I1], Tu);
	return FMath::Lerp(A, B, Tv);
}
