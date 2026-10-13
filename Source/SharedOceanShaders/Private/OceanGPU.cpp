#include "OceanGPU.h"

#include "GlobalShader.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"

class FOceanSpectrumCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FOceanSpectrumCS);
	SHADER_USE_PARAMETER_STRUCT(FOceanSpectrumCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, N)
		SHADER_PARAMETER(float, Patch)
		SHADER_PARAMETER(float, Time)
		SHADER_PARAMETER(float, Omega0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, H0)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutA)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutB)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P) { return IsFeatureLevelSupported(P.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FOceanSpectrumCS, "/SharedOcean/OceanFFT.usf", "SpectrumCS", SF_Compute);

class FOceanFFTCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FOceanFFTCS);
	SHADER_USE_PARAMETER_STRUCT(FOceanFFTCS, FGlobalShader);
	class FSize : SHADER_PERMUTATION_SPARSE_INT("FFT_N", 64, 128);
	using FPermutationDomain = TShaderPermutationDomain<FSize>;
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, Vertical)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SrcA)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SrcB)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DstA)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DstB)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P) { return IsFeatureLevelSupported(P.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FOceanFFTCS, "/SharedOcean/OceanFFT.usf", "FFTCS", SF_Compute);

class FOceanFinalizeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FOceanFinalizeCS);
	SHADER_USE_PARAMETER_STRUCT(FOceanFinalizeCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, N)
		SHADER_PARAMETER(float, Choppiness)
		SHADER_PARAMETER(float, Dt)
		SHADER_PARAMETER(float, FoamThreshold)
		SHADER_PARAMETER(float, FoamGain)
		SHADER_PARAMETER(float, FoamDecay)
		SHADER_PARAMETER(uint32, FoamOn)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, InA)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, InB)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutDisp)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutDeriv)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, Foam)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P) { return IsFeatureLevelSupported(P.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FOceanFinalizeCS, "/SharedOcean/OceanFFT.usf", "FinalizeCS", SF_Compute);

void OceanGPU_AddPasses(FRDGBuilder& GraphBuilder, const FOceanGPUParams& P, FRHITexture* H0, FRHITexture* Disp, FRHITexture* Deriv, FRHITexture* FoamTex)
{
	if (!H0 || !Disp || !Deriv || !FoamTex || (P.N != 64 && P.N != 128)) return;
	FGlobalShaderMap* Map = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	FRDGTextureRef RH0 = RegisterExternalTexture(GraphBuilder, H0, TEXT("OceanH0"));
	FRDGTextureRef RDisp = RegisterExternalTexture(GraphBuilder, Disp, TEXT("OceanDisp"));
	FRDGTextureRef RDeriv = RegisterExternalTexture(GraphBuilder, Deriv, TEXT("OceanDeriv"));
	FRDGTextureRef RFoam = RegisterExternalTexture(GraphBuilder, FoamTex, TEXT("OceanFoam"));
	const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(FIntPoint(P.N, P.N), PF_A32B32G32R32F, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureRef A = GraphBuilder.CreateTexture(Desc, TEXT("OceanSpecA"));
	FRDGTextureRef B = GraphBuilder.CreateTexture(Desc, TEXT("OceanSpecB"));
	FRDGTextureRef C = GraphBuilder.CreateTexture(Desc, TEXT("OceanSpecC"));
	FRDGTextureRef D = GraphBuilder.CreateTexture(Desc, TEXT("OceanSpecD"));
	const FIntVector Groups8(FMath::DivideAndRoundUp(P.N, 8), FMath::DivideAndRoundUp(P.N, 8), 1);
	{
		FOceanSpectrumCS::FParameters* Ps = GraphBuilder.AllocParameters<FOceanSpectrumCS::FParameters>();
		Ps->N = P.N; Ps->Patch = P.Patch; Ps->Time = P.Time; Ps->Omega0 = P.Omega0;
		Ps->H0 = RH0;
		Ps->OutA = GraphBuilder.CreateUAV(A);
		Ps->OutB = GraphBuilder.CreateUAV(B);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("OceanSpectrum"), TShaderMapRef<FOceanSpectrumCS>(Map), Ps, Groups8);
	}
	FOceanFFTCS::FPermutationDomain Perm;
	Perm.Set<FOceanFFTCS::FSize>(P.N);
	TShaderMapRef<FOceanFFTCS> FFT(Map, Perm);
	for (uint32 Vertical = 0; Vertical < 2; ++Vertical)
	{
		FOceanFFTCS::FParameters* Ps = GraphBuilder.AllocParameters<FOceanFFTCS::FParameters>();
		// ping-pong: rows A,B -> C,D; columns C,D -> A,B (no typed UAV loads needed)
		Ps->Vertical = Vertical;
		Ps->SrcA = Vertical ? C : A;
		Ps->SrcB = Vertical ? D : B;
		Ps->DstA = GraphBuilder.CreateUAV(Vertical ? A : C);
		Ps->DstB = GraphBuilder.CreateUAV(Vertical ? B : D);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("OceanIFFT%s", Vertical ? TEXT("Columns") : TEXT("Rows")), FFT, Ps, FIntVector(P.N, 1, 1));
	}
	{
		FOceanFinalizeCS::FParameters* Ps = GraphBuilder.AllocParameters<FOceanFinalizeCS::FParameters>();
		Ps->N = P.N; Ps->Choppiness = P.Choppiness; Ps->Dt = P.Dt;
		Ps->FoamThreshold = P.FoamThreshold; Ps->FoamGain = P.FoamGain; Ps->FoamDecay = P.FoamDecay; Ps->FoamOn = P.bFoam ? 1u : 0u;
		Ps->InA = A; Ps->InB = B;
		Ps->OutDisp = GraphBuilder.CreateUAV(RDisp);
		Ps->OutDeriv = GraphBuilder.CreateUAV(RDeriv);
		Ps->Foam = GraphBuilder.CreateUAV(RFoam);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("OceanFinalize"), TShaderMapRef<FOceanFinalizeCS>(Map), Ps, Groups8);
	}
	// The material samples these next: leave them readable.
	GraphBuilder.SetTextureAccessFinal(RDisp, ERHIAccess::SRVMask);
	GraphBuilder.SetTextureAccessFinal(RDeriv, ERHIAccess::SRVMask);
}

class FSharedOceanShadersModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		AddShaderSourceDirectoryMapping(TEXT("/SharedOcean"), FPaths::Combine(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), TEXT("Shaders")));
	}
};
IMPLEMENT_MODULE(FSharedOceanShadersModule, SharedOceanShaders);
