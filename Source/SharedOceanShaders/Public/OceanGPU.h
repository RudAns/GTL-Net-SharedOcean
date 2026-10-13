#pragma once

#include "CoreMinimal.h"

class FRDGBuilder;
class FRHITexture;

/** Per-cascade inputs for the GPU visual band. */
struct FOceanGPUParams
{
	int32 N = 128;
	float Patch = 48.f;
	float Time = 0.f;			// wrapped ocean time
	float Omega0 = 0.f;			// 2 pi / loop period
	float Choppiness = 1.f;
	float Dt = 0.016f;
	float FoamThreshold = 0.55f, FoamGain = 2.2f, FoamDecay = 2.4f;
	bool bFoam = true;
};

/** Spectrum -> row IFFT -> column IFFT -> finalise, written into the cascade's displacement/derivative/foam targets. */
SHAREDOCEANSHADERS_API void OceanGPU_AddPasses(FRDGBuilder& GraphBuilder, const FOceanGPUParams& P, FRHITexture* H0, FRHITexture* Disp, FRHITexture* Deriv, FRHITexture* Foam);
