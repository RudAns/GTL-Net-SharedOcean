"""Build every asset the demo needs, headless:

  UnrealEditor-Cmd.exe SharedOcean.uproject -run=pythonscript -script="<abs path>/Tools/build_assets.py"

Creates under /Game/Ocean: MPC_Ocean, M_Ocean, M_VertexColor, M_Sail, M_Emissive, M_Beam, and the empty map
/Game/Maps/Ocean. The HLSL lives here so the material logic is reviewable as text.

M_Ocean reads the FFT cascades written by AOceanManager each frame:
  TD*  RGBA16F  (dispX, dispY, height, foam)  metres       TN*  RGBA16F  (dh/dx, dh/dy, dDx/dx, dDy/dy)
  cascades: 0 G0  1 G1  (gameplay band, also on the CPU for physics)   2 V2  3 V3  (visual band)   S0 S1 storm
"""
import unreal

ROOT = "/Game/Ocean"
AT = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary


def log(*a):
    unreal.log("[build_assets] " + " ".join(str(x) for x in a))


def fresh(name, cls, factory, path=ROOT):
    full = f"{path}/{name}"
    asset = unreal.load_asset(full) if EAL.does_asset_exist(full) else None
    if asset is None:
        asset = AT.create_asset(name, path, cls, factory)
    if isinstance(asset, unreal.Material):
        MEL.delete_all_material_expressions(asset)
    return asset


def set_flags(mat, **flags):
    for k, v in flags.items():
        try:
            mat.set_editor_property(k, v)
        except Exception as e:  # noqa
            log("flag", k, e)


class Graph:
    def __init__(self, mat):
        self.mat = mat
        self.y = 0

    def node(self, cls, x=-1400, **props):
        e = MEL.create_material_expression(self.mat, cls, x, self.y)
        self.y += 90
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    def custom(self, code, inputs, out_type, desc, x=-300):
        c = self.node(unreal.MaterialExpressionCustom, x)
        c.set_editor_property("code", code)
        c.set_editor_property("description", desc)
        c.set_editor_property("output_type", out_type)
        ins = []
        for name, _ in inputs:
            ci = unreal.CustomInput()
            ci.set_editor_property("input_name", name)
            ins.append(ci)
        c.set_editor_property("inputs", ins)
        for name, (src, out) in inputs:
            MEL.connect_material_expressions(src, out, c, name)
        return c

    def scalar(self, name, v):
        return self.node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=v)

    def vector(self, name, v):
        return self.node(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=unreal.LinearColor(*v))

    def tex(self, name, t):
        return self.node(unreal.MaterialExpressionTextureObjectParameter, parameter_name=name, texture=t)

    def coll(self, mpc, name):
        e = self.node(unreal.MaterialExpressionCollectionParameter)
        e.set_editor_property("collection", mpc)
        e.set_editor_property("parameter_name", name)
        return e


F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
F4 = unreal.CustomMaterialOutputType.CMOT_FLOAT4

# ----------------------------------------------------------------------------------------------------------------
# Ocean HLSL. Each custom node gets its own copy of COMMON.

COMMON = r"""
struct OCN
{
    float4 Pt; float4 LM; float4 Mk; float4 St;
    // Fade a band out once the mesh cell (or pixel footprint) reaches a quarter of its shortest wavelength.
    float4 W(float s) { float4 w = saturate(2.0 - 4.0 * s / LM); w.xy *= Mk.x; w.zw *= Mk.y; return w; }
    // Deterministic storm weight (centre is a function of the shared clock, computed on the CPU).
    float SW(float2 p) { if (St.z <= 0.0 || Mk.z <= 0.0) return 0.0; return 1.0 - smoothstep(St.z, St.z + St.w, length(p - St.xy)); }
};
OCN o; o.Pt = Patch; o.LM = LambdaMin; o.Mk = Mask; o.St = Storm;
"""

# clipmap morph shared by WPO and the rest-position interpolator
MORPH = r"""
float3 lp = WP - OBJ;
float2 g = round(lp.xy / CellSize);
float dinf = max(abs(g.x), abs(g.y));
float hc = LevelHalf / CellSize;
float m = saturate((dinf - (hc - 16.0)) / 14.0);
float2 odd = frac(g * 0.5) * 2.0;
float2 mp = (g - odd * m) * CellSize;
float2 rest = (OBJ.xy + mp) * 0.01;
float sEff = CellSize * 0.01 * (1.0 + m);
"""

OCEAN_WPO = COMMON + MORPH + r"""
float4 w = o.W(sEff);
float3 d0 = TD0.SampleLevel(TD0Sampler, rest / Patch.x, 0).xyz * w.x;
float3 d1 = TD1.SampleLevel(TD0Sampler, rest / Patch.y, 0).xyz * w.y;
float3 gp = d0 + d1;
float ws = o.SW(rest);
if (ws > 0.001)
{
    float3 s0 = TDS0.SampleLevel(TD0Sampler, rest / Patch.x, 0).xyz * w.x;
    float3 s1 = TDS1.SampleLevel(TD0Sampler, rest / Patch.y, 0).xyz * w.y;
    gp = lerp(gp, s0 + s1, ws);
}
float3 d = gp + TD2.SampleLevel(TD0Sampler, rest / Patch.z, 0).xyz * w.z + TD3.SampleLevel(TD0Sampler, rest / Patch.w, 0).xyz * w.w;
return float3(mp - lp.xy, 0.0) + d * 100.0;
"""

OCEAN_REST = COMMON + MORPH + r"""
return float4(rest, o.SW(rest), sEff);
"""

# pixel side: sample everything once into a small struct
PIX = COMMON + r"""
float2 rest = R.xy;
float ws = R.z;
float fp = max(Depth * 0.01 * 0.0012, R.w * 0.35);
float4 w = o.W(fp);
float4 n0 = TN0.SampleLevel(TN0Sampler, rest / Patch.x, 0), n1 = TN1.SampleLevel(TN0Sampler, rest / Patch.y, 0);
float4 n2 = TN2.SampleLevel(TN0Sampler, rest / Patch.z, 0), n3 = TN3.SampleLevel(TN0Sampler, rest / Patch.w, 0);
float4 a0 = TD0.SampleLevel(TN0Sampler, rest / Patch.x, 0), a1 = TD1.SampleLevel(TN0Sampler, rest / Patch.y, 0);
float4 a2 = TD2.SampleLevel(TN0Sampler, rest / Patch.z, 0), a3 = TD3.SampleLevel(TN0Sampler, rest / Patch.w, 0);
float2 sl = n0.xy * w.x + n1.xy * w.y;
float2 dd = n0.zw * w.x + n1.zw * w.y;
float h = a0.z * w.x + a1.z * w.y;
float foam = max(a0.w * w.x, a1.w * w.y);
if (ws > 0.001)
{
    float4 ns0 = TNS0.SampleLevel(TN0Sampler, rest / Patch.x, 0), ns1 = TNS1.SampleLevel(TN0Sampler, rest / Patch.y, 0);
    float4 as0 = TDS0.SampleLevel(TN0Sampler, rest / Patch.x, 0), as1 = TDS1.SampleLevel(TN0Sampler, rest / Patch.y, 0);
    sl = lerp(sl, ns0.xy * w.x + ns1.xy * w.y, ws);
    dd = lerp(dd, ns0.zw * w.x + ns1.zw * w.y, ws);
    h = lerp(h, as0.z * w.x + as1.z * w.y, ws);
    foam = lerp(foam, max(as0.w * w.x, as1.w * w.y), ws);
}
sl += n2.xy * w.z + n3.xy * w.w;
dd += n2.zw * w.z + n3.zw * w.w;
h += (a2.z * w.z + a3.z * w.w) * 0.5;
// Combined compression of all bands (each band alone rarely folds): whitecaps where the surface is squeezed.
float Jc = (1.0 + dd.x) * (1.0 + dd.y);
foam = max(foam, saturate((0.80 - Jc) * 2.6));
foam = saturate(max(foam, max(a2.w * w.z, a3.w * w.w) * 0.8));
// break the foam up with the finest band so it reads as patches, not a flat mask
foam *= saturate(0.55 + a3.z * 2.5 + a2.z * 0.6);
float3 N = normalize(float3(-sl.x, -sl.y, 1.0));
float3 V = normalize(Cv);
float3 L = normalize(Sun);
float Hs = 0.6 + SeaIntensity * 3.0;
float peak = saturate(h / (0.55 * Hs) * 0.5 + 0.45);
peak = peak * peak;
"""

OCEAN_NORMAL = PIX + "return N;"

OCEAN_COLOR = PIX + r"""
float3 deep = float3(0.003, 0.026, 0.050);
float3 lift = float3(0.008, 0.090, 0.100);
float3 c = lerp(deep, lift, peak * 0.75);
c = lerp(c, float3(0.78, 0.83, 0.85), foam);
if (Mask.w > 0.5)
{
    // band tint: gameplay band (warm) vs visual band (cool)
    float gpw = length(n0.xy * w.x + n1.xy * w.y), vbw = length(n2.xy * w.z + n3.xy * w.w);
    c = lerp(float3(0.85, 0.45, 0.10), float3(0.10, 0.45, 0.95), saturate(vbw / (gpw + vbw + 1e-4))) * (0.35 + 0.65 * peak);
}
if (StormOverlay > 0.5) c = lerp(c, float3(0.55, 0.05, 0.03), ws * 0.55);
if (LevelTint >= 0.0)
{
    float k = frac(LevelTint * 0.37);
    c = lerp(c, saturate(abs(frac(k + float3(0.0, 0.33, 0.67)) * 6.0 - 3.0) - 1.0), 0.45);
}
return c;
"""

OCEAN_EMISSIVE = PIX + r"""
// Sub-surface approximation (Rare, SIGGRAPH 2018): wave peaks show more sub-surface colour, strongest when
// looking towards the sun through the crest and at grazing angles.
float back = pow(saturate(dot(-V, L) * 0.5 + 0.5), 4.0);
float graze = 1.0 - saturate(V.z * 1.2);
float sunUp = saturate(L.z * 4.0 + 0.15);
float sss = peak * (0.18 + 1.5 * back) * (0.35 + 0.65 * graze) * sunUp * (1.0 - foam) * (1.0 - 0.6 * ws);
if (Mask.w > 0.5) sss = 0.0;
return float3(0.03, 0.42, 0.36) * sss * 0.45;
"""

OCEAN_ROUGH = PIX + "return Mask.w > 0.5 ? 0.9 : lerp(0.08, 0.55, foam);"

OCEAN_MASK = r"""
float2 p = R.xy * 100.0;
bool inside = p.x > Inner.x && p.y > Inner.y && p.x < Inner.z && p.y < Inner.w;
return inside ? 0.0 : 1.0;
"""


def build_mpc():
    full = f"{ROOT}/MPC_Ocean"
    mpc = unreal.load_asset(full) if EAL.does_asset_exist(full) else AT.create_asset("MPC_Ocean", ROOT, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    scalars = []
    for n, v in (("OceanTime", 0.0), ("StormOverlay", 0.0), ("SeaIntensity", 0.5)):
        p = unreal.CollectionScalarParameter()
        p.set_editor_property("parameter_name", n)
        p.set_editor_property("default_value", v)
        scalars.append(p)
    vectors = []
    for n, v in (("Storm", (0, 0, -1, 260)), ("LayerMask", (1, 1, 1, 0)), ("Patch", (768, 192, 48, 11.7)), ("LambdaMin", (60, 16, 3.2, 0.4))):
        p = unreal.CollectionVectorParameter()
        p.set_editor_property("parameter_name", n)
        p.set_editor_property("default_value", unreal.LinearColor(*v))
        vectors.append(p)
    mpc.set_editor_property("scalar_parameters", scalars)
    mpc.set_editor_property("vector_parameters", vectors)
    EAL.save_loaded_asset(mpc)
    return mpc


def build_ocean(mpc):
    mat = fresh("M_Ocean", unreal.Material, unreal.MaterialFactoryNew())
    set_flags(mat, blend_mode=unreal.BlendMode.BLEND_MASKED, tangent_space_normal=False, two_sided=False, opacity_mask_clip_value=0.5)
    g = Graph(mat)
    black = unreal.load_asset("/Engine/EngineResources/Black")
    td = {n: g.tex(n, black) for n in ("TD0", "TD1", "TD2", "TD3", "TDS0", "TDS1")}
    tn = {n: g.tex(n, black) for n in ("TN0", "TN1", "TN2", "TN3", "TNS0", "TNS1")}
    cell = g.scalar("CellSize", 50.0)
    lhalf = g.scalar("LevelHalf", 3200.0)
    tint = g.scalar("LevelTint", -1.0)
    inner = g.vector("InnerRect", (1e9, 1e9, -1e9, -1e9))
    c_storm, c_mask, c_patch, c_lmin = (g.coll(mpc, n) for n in ("Storm", "LayerMask", "Patch", "LambdaMin"))
    c_ovl, c_int = g.coll(mpc, "StormOverlay"), g.coll(mpc, "SeaIntensity")
    wp = g.node(unreal.MaterialExpressionWorldPosition, world_position_shader_offset=unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    obj = g.node(unreal.MaterialExpressionObjectPositionWS)
    common = [("Patch", (c_patch, "")), ("LambdaMin", (c_lmin, "")), ("Mask", (c_mask, "")), ("Storm", (c_storm, ""))]
    vs_in = [("WP", (wp, "")), ("OBJ", (obj, "")), ("CellSize", (cell, "")), ("LevelHalf", (lhalf, ""))] + common
    wpo = g.custom(OCEAN_WPO, vs_in + [(n, (td[n], "")) for n in td], F3, "OceanWPO")
    rest = g.custom(OCEAN_REST, vs_in, F4, "OceanRest")
    interp = g.node(unreal.MaterialExpressionVertexInterpolator)
    MEL.connect_material_expressions(rest, "", interp, "")
    cv = g.node(unreal.MaterialExpressionCameraVectorWS)
    sun = g.node(unreal.MaterialExpressionSkyAtmosphereLightDirection)
    depth = g.node(unreal.MaterialExpressionPixelDepth)
    ps_in = [("R", (interp, "")), ("Cv", (cv, "")), ("Sun", (sun, "")), ("Depth", (depth, "")), ("SeaIntensity", (c_int, "")),
             ("StormOverlay", (c_ovl, "")), ("LevelTint", (tint, ""))] + common + [(n, (td[n], "")) for n in td] + [(n, (tn[n], "")) for n in tn]
    nrm = g.custom(OCEAN_NORMAL, ps_in, F3, "OceanNormal")
    col = g.custom(OCEAN_COLOR, ps_in, F3, "OceanColor")
    emi = g.custom(OCEAN_EMISSIVE, ps_in, F3, "OceanSSS")
    rough = g.custom(OCEAN_ROUGH, ps_in, F1, "OceanRough")
    mask = g.custom(OCEAN_MASK, [("R", (interp, "")), ("Inner", (inner, "RGBA"))], F1, "OceanClipmapMask")
    spec = g.node(unreal.MaterialExpressionConstant, r=0.38)
    MEL.connect_material_property(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    MEL.connect_material_property(nrm, "", unreal.MaterialProperty.MP_NORMAL)
    MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(emi, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    MEL.connect_material_property(mask, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)


VC_COLOR = r"""
float3 c = pow(saturate(VC.rgb), Gamma);
float n = frac(sin(dot(floor(P.xy / 37.0), float2(12.9898, 78.233))) * 43758.5453);
return c * (0.94 + 0.10 * n);
"""


def build_vertex_color():
    mat = fresh("M_VertexColor", unreal.Material, unreal.MaterialFactoryNew())
    g = Graph(mat)
    vc = g.node(unreal.MaterialExpressionVertexColor)
    p = g.node(unreal.MaterialExpressionWorldPosition)
    gamma = g.scalar("Gamma", 2.2)
    col = g.custom(VC_COLOR, [("VC", (vc, "")), ("P", (p, "")), ("Gamma", (gamma, ""))], F3, "VertexColour")
    rough = g.scalar("Rough", 0.82)
    spec = g.node(unreal.MaterialExpressionConstant, r=0.35)
    MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)


SAIL_WPO = r"""
float flap = sin(T * 3.1 + UV.x * 7.0 + UV.y * 3.0) * 0.6 + sin(T * 5.3 + UV.y * 9.0) * 0.4;
float amp = lerp(10.0, 4.0, Fill) * UV.x;
return N * flap * amp;
"""


def build_sail():
    mat = fresh("M_Sail", unreal.Material, unreal.MaterialFactoryNew())
    set_flags(mat, two_sided=True, shading_model=unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    g = Graph(mat)
    vc = g.node(unreal.MaterialExpressionVertexColor)
    uv = g.node(unreal.MaterialExpressionTextureCoordinate, coordinate_index=0)
    t = g.node(unreal.MaterialExpressionTime)
    n = g.node(unreal.MaterialExpressionVertexNormalWS)
    fill = g.scalar("Fill", 1.0)
    col = g.custom("float3 c = pow(saturate(VC.rgb), 2.2); float seam = step(0.93, frac(UV.y * 8.0)); return c * (1.0 - 0.10 * seam);",
                   [("VC", (vc, "")), ("UV", (uv, ""))], F3, "SailColour")
    sss = g.custom("return pow(saturate(VC.rgb), 2.2) * float3(0.85, 0.70, 0.45);", [("VC", (vc, ""))], F3, "SailTransmission")
    wpo = g.custom(SAIL_WPO, [("UV", (uv, "")), ("T", (t, "")), ("N", (n, "")), ("Fill", (fill, ""))], F3, "SailFlutter")
    rough = g.node(unreal.MaterialExpressionConstant, r=0.9)
    MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(sss, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    MEL.connect_material_property(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)


def build_emissive():
    mat = fresh("M_Emissive", unreal.Material, unreal.MaterialFactoryNew())
    g = Graph(mat)
    vc = g.node(unreal.MaterialExpressionVertexColor)
    col = g.custom("return pow(saturate(VC.rgb), 2.2);", [("VC", (vc, ""))], F3, "Lamp")
    emi = g.custom("return pow(saturate(VC.rgb), 2.2) * 25.0;", [("VC", (vc, ""))], F3, "LampGlow")
    MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(emi, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)


def build_beam():
    mat = fresh("M_Beam", unreal.Material, unreal.MaterialFactoryNew())
    set_flags(mat, blend_mode=unreal.BlendMode.BLEND_ADDITIVE, shading_model=unreal.MaterialShadingModel.MSM_UNLIT, two_sided=True)
    g = Graph(mat)
    p = g.node(unreal.MaterialExpressionWorldPosition)
    o = g.node(unreal.MaterialExpressionObjectPositionWS)
    cv = g.node(unreal.MaterialExpressionCameraVectorWS)
    n = g.node(unreal.MaterialExpressionVertexNormalWS)
    emi = g.custom("float z = P.z - O.z; float edge = pow(saturate(abs(dot(normalize(Cv), N))), 1.5); return float3(1.0, 0.72, 0.25) * 1.4 * (1.0 - saturate(z / 9000.0)) * edge;",
                   [("P", (p, "")), ("O", (o, "")), ("Cv", (cv, "")), ("N", (n, ""))], F3, "BeamGlow")
    MEL.connect_material_property(emi, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)


def build_map():
    path = "/Game/Maps/Ocean"
    if EAL.does_asset_exist(path):
        log("map exists")
        return
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    log("map saved", unreal.EditorLoadingAndSavingUtils.save_map(world, path))


def main():
    mpc = build_mpc()
    build_ocean(mpc)
    build_vertex_color()
    build_sail()
    build_emissive()
    build_beam()
    build_map()
    EAL.save_directory(ROOT, only_if_is_dirty=False, recursive=True)
    log("done")


main()
