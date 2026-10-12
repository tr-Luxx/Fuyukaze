"""
Generates the binary assets Fuyukaze needs: its materials, the samurai and enemy blueprints
(re-parented copies of the combat template's), and the level.

Everything else in the game is authored in C++, so this only has to be run once, or again after
changing a material below. Run it from the project folder with:

    Tools\\BuildContent.ps1
"""

import json
import os

import unreal

MAT_DIR = "/Game/Fuyukaze/Materials"
BP_DIR = "/Game/Fuyukaze/Blueprints"
MAP_PATH = "/Game/Fuyukaze/Maps/Fuyukaze"

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
MP = unreal.MaterialProperty


def log(message):
    unreal.log("[build_content] " + str(message))


def new_material(name):
    path = MAT_DIR + "/" + name
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    material = asset_tools.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("used_with_instanced_static_meshes", True)
    return material


def node(material, cls, x, y, **props):
    expression = mel.create_material_expression(material, cls, x, y)
    for key, value in props.items():
        expression.set_editor_property(key, value)
    return expression


def link(a, a_out, b, b_in):
    if not mel.connect_material_expressions(a, a_out, b, b_in):
        unreal.log_warning("[build_content] could not connect %s.%s -> %s.%s" % (a.get_name(), a_out, b.get_name(), b_in))


def out(material, expression, pin, prop):
    if not mel.connect_material_property(expression, pin, prop):
        unreal.log_warning("[build_content] could not connect %s to %s" % (expression.get_name(), prop))


def const(material, value, x, y):
    return node(material, unreal.MaterialExpressionConstant, x, y, r=value)


def custom(material, x, y, code, output_type, input_names):
    """An HLSL node. Keeps the wind and colour maths in one readable place instead of a web of nodes."""
    expression = mel.create_material_expression(material, unreal.MaterialExpressionCustom, x, y)
    inputs = []
    for name in input_names:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        inputs.append(custom_input)
    expression.set_editor_property("inputs", inputs)
    expression.set_editor_property("code", code)
    expression.set_editor_property("output_type", output_type)
    return expression


def vertex_albedo(material, x, y):
    """Vertex colours are stored sRGB-encoded for precision in the darks; decode them back to linear."""
    vertex_color = node(material, unreal.MaterialExpressionVertexColor, x, y)
    decode = custom(material, x + 250, y, "return pow(max(C, 0.0), 2.2);", unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["C"])
    link(vertex_color, "", decode, "C")
    return vertex_color, decode


WIND_CODE = """
// A broad gust front rolls across the land in the wind direction, with a quicker flutter on top.
// W is the vertex sway weight (0 at roots and trunks, 1 at tips), A the amplitude in cm.
float2 dir = normalize(float2(0.906, 0.423));
float along = dot(P.xy, dir);
float gust = sin(T * 1.35 - along * 0.0032) * 0.5 + 0.5;
gust *= 0.6 + 0.4 * sin(T * 0.37 + P.x * 0.0007 - P.y * 0.0011);
float flutter = sin(T * 5.1 + P.x * 0.031 + P.y * 0.027) * 0.22 + sin(T * 8.3 + P.y * 0.045) * 0.08;
float bend = (0.25 + gust + flutter) * W * A;
return float3(dir * bend, -abs(bend) * 0.22);
"""


PART_CODE = """
// Grass and flowers lean away from the samurai's feet and are pressed down a little, most strongly right
// beside her and fading to nothing at the edge of the radius. P is the vertex, O the clump's root,
// F the point between her feet, R the radius and S an overall strength.
float2 away = P.xy - F.xy;
float dist = length(away);
float reach = saturate(1.0 - dist / max(R, 1.0));
float level = saturate(1.0 - abs(O.z - F.z) / 170.0);
float height = saturate((P.z - O.z) / 45.0);
float push = reach * reach * level * height * S;
float2 dir = dist > 0.01 ? away / dist : float2(1.0, 0.0);
return float3(dir * push * 64.0, -push * 27.0);
"""


def shared_parameters():
    """The collection the game writes the samurai's position into each frame. Made once and then kept,
    because materials refer to its parameters by id."""
    path = MAT_DIR + "/MPC_Fuyu"
    if eal.does_asset_exist(path):
        return unreal.load_asset(path)

    collection = asset_tools.create_asset("MPC_Fuyu", MAT_DIR, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    feet = unreal.CollectionVectorParameter()
    feet.set_editor_property("parameter_name", "PlayerFeet")
    feet.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, -1000000.0, 0.0))
    radius = unreal.CollectionScalarParameter()
    radius.set_editor_property("parameter_name", "GrassPartRadius")
    radius.set_editor_property("default_value", 135.0)
    collection.set_editor_property("vector_parameters", [feet])
    collection.set_editor_property("scalar_parameters", [radius])
    eal.save_loaded_asset(collection)
    log("built MPC_Fuyu")
    return collection


def part_offset(material, x, y, strength):
    """World-position offset that parts foliage around the samurai. `strength` is a node giving its scale."""
    collection = shared_parameters()
    position = node(material, unreal.MaterialExpressionWorldPosition, x, y)
    root = node(material, unreal.MaterialExpressionObjectPositionWS, x, y + 150)
    feet = mel.create_material_expression(material, unreal.MaterialExpressionCollectionParameter, x, y + 300)
    feet.set_editor_property("collection", collection)
    feet.set_editor_property("parameter_name", "PlayerFeet")
    radius = mel.create_material_expression(material, unreal.MaterialExpressionCollectionParameter, x, y + 450)
    radius.set_editor_property("collection", collection)
    radius.set_editor_property("parameter_name", "GrassPartRadius")

    part = custom(material, x + 350, y, PART_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["P", "O", "F", "R", "S"])
    link(position, "", part, "P")
    link(root, "", part, "O")
    link(feet, "", part, "F")
    link(radius, "", part, "R")
    link(strength, "", part, "S")
    return part


def wind_offset(material, vertex_color, amplitude, x, y, part=False):
    position = node(material, unreal.MaterialExpressionWorldPosition, x, y)
    time = node(material, unreal.MaterialExpressionTime, x, y + 150)
    amp = const(material, amplitude, x, y + 250)
    wind = custom(material, x + 300, y, WIND_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["P", "T", "W", "A"])
    link(position, "", wind, "P")
    link(time, "", wind, "T")
    link(vertex_color, "A", wind, "W")
    link(amp, "", wind, "A")
    if not part:
        return wind

    both = node(material, unreal.MaterialExpressionAdd, x + 700, y)
    link(wind, "", both, "A")
    link(part_offset(material, x, y + 500, const(material, 1.0, x, y + 1100)), "", both, "B")
    return both


def finish(material):
    mel.layout_material_expressions(material)
    mel.recompile_material(material)
    eal.save_loaded_asset(material)
    log("built " + material.get_name())


SURFACES = "/Game/Fab/Megascans/Surfaces/"
SOIL = SURFACES + "Forest_Floor_sfjmafua/High/sfjmafua_tier_1/Textures/T_sfjmafua_4K_"
ROCK = SURFACES + "Rocky_Forest_Floor_wjvpaff/High/wjvpaff_tier_1/Textures/T_wjvpaff_4K_"


def sample(material, texture, uv, x, y):
    """A texture sample whose sampler type matches how the texture was imported, which the material compiler insists on."""
    virtual = texture.get_editor_property("virtual_texture_streaming")
    compression = texture.get_editor_property("compression_settings")
    ST = unreal.MaterialSamplerType
    if compression == unreal.TextureCompressionSettings.TC_NORMALMAP:
        kind = ST.SAMPLERTYPE_VIRTUAL_NORMAL if virtual else ST.SAMPLERTYPE_NORMAL
    elif compression == unreal.TextureCompressionSettings.TC_MASKS:
        kind = ST.SAMPLERTYPE_VIRTUAL_MASKS if virtual else ST.SAMPLERTYPE_MASKS
    elif texture.get_editor_property("srgb"):
        kind = ST.SAMPLERTYPE_VIRTUAL_COLOR if virtual else ST.SAMPLERTYPE_COLOR
    else:
        kind = ST.SAMPLERTYPE_VIRTUAL_LINEAR_COLOR if virtual else ST.SAMPLERTYPE_LINEAR_COLOR
    expression = node(material, unreal.MaterialExpressionTextureSample, x, y, texture=texture, sampler_type=kind)
    link(uv, "", expression, "UVs")
    return expression


def build_scanned_terrain():
    """Terrain using scanned Megascans ground from Fab. Returns False if those textures have not been imported."""
    textures = {}
    for name, base in (("soil", SOIL), ("rock", ROCK)):
        for suffix in ("B", "N", "ORM"):
            texture = unreal.load_asset(base + suffix)
            if texture is None:
                return False
            textures[name + suffix] = texture

    m = new_material("M_FuyuTerrain")
    vertex_color, albedo = vertex_albedo(m, -1500, -300)

    # terrain UVs are world position in units of 10 m. One close tiling for detail underfoot and one
    # much larger, blended in with distance from the camera so the repeat does not show across a hillside
    uv_near = node(m, unreal.MaterialExpressionTextureCoordinate, -1500, 100, u_tiling=2.6, v_tiling=2.6)
    uv_far = node(m, unreal.MaterialExpressionTextureCoordinate, -1500, 300, u_tiling=0.31, v_tiling=0.31)

    s = {}
    y = 0
    for name in ("soil", "rock"):
        for suffix in ("B", "N", "ORM"):
            s[name + suffix + "n"] = sample(m, textures[name + suffix], uv_near, -1100, y)
            y += 220
        s[name + "Bf"] = sample(m, textures[name + "B"], uv_far, -1100, y)
        y += 220

    # camera distance drives the near/far blend
    position = node(m, unreal.MaterialExpressionWorldPosition, -1500, 600)
    camera = node(m, unreal.MaterialExpressionCameraPositionWS, -1500, 800)
    far = custom(m, -1100, 1900, "return saturate((distance(P, C) - 1500.0) / 9000.0);", unreal.CustomMaterialOutputType.CMOT_FLOAT1, ["P", "C"])
    link(position, "", far, "P")
    link(camera, "", far, "C")

    # the scans supply texture and the vertex colours supply the biome's hue: the scan is normalised to
    # mid-grey and multiplied in, keeping a little of its own colour. Snow (bright vertex colour) stays clean
    color = custom(m, -500, 0, """
float3 soil = lerp(SN, SF, F * 0.75);
float3 rock = lerp(RN, RF, F * 0.75);
float3 scan = lerp(soil, rock, saturate(K * 1.4));
float lum = max(dot(scan, float3(0.3, 0.5, 0.2)), 0.02);
float3 detail = lerp(float3(1, 1, 1) * lum, scan, 0.45) / 0.14;
float snow = saturate(dot(V, float3(0.33, 0.33, 0.33)) * 2.2 - 0.9);
return V * lerp(detail, float3(1, 1, 1), snow * 0.8);
""", unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["V", "SN", "SF", "RN", "RF", "K", "F"])
    link(albedo, "", color, "V")
    link(s["soilBn"], "RGB", color, "SN")
    link(s["soilBf"], "RGB", color, "SF")
    link(s["rockBn"], "RGB", color, "RN")
    link(s["rockBf"], "RGB", color, "RF")
    link(vertex_color, "A", color, "K")
    link(far, "", color, "F")
    out(m, color, "", MP.MP_BASE_COLOR)

    normal = custom(m, -500, 400, "float3 n = lerp(SN, RN, saturate(K * 1.4)); n.xy *= lerp(1.0, 0.35, F); return normalize(n);", unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["SN", "RN", "K", "F"])
    link(s["soilNn"], "RGB", normal, "SN")
    link(s["rockNn"], "RGB", normal, "RN")
    link(vertex_color, "A", normal, "K")
    link(far, "", normal, "F")
    out(m, normal, "", MP.MP_NORMAL)

    # Megascans packs occlusion, roughness and metalness into R, G and B
    orm = custom(m, -500, 700, "return lerp(S, R, saturate(K * 1.4));", unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["S", "R", "K"])
    link(s["soilORMn"], "RGB", orm, "S")
    link(s["rockORMn"], "RGB", orm, "R")
    link(vertex_color, "A", orm, "K")
    rough = node(m, unreal.MaterialExpressionComponentMask, -200, 700, r=False, g=True, b=False, a=False)
    occlusion = node(m, unreal.MaterialExpressionComponentMask, -200, 850, r=True, g=False, b=False, a=False)
    link(orm, "", rough, "")
    link(orm, "", occlusion, "")
    out(m, rough, "", MP.MP_ROUGHNESS)
    out(m, occlusion, "", MP.MP_AMBIENT_OCCLUSION)
    out(m, const(m, 0.3, -200, 1000), "", MP.MP_SPECULAR)
    finish(m)
    return True


def build_terrain():
    if build_scanned_terrain():
        return
    m = new_material("M_FuyuTerrain")
    _, albedo = vertex_albedo(m, -900, 0)

    # two scales of tiling noise break up the smooth vertex-colour gradients: fine grain underfoot, broad mottling from afar
    noise = unreal.load_asset("/Engine/EngineMaterials/Good64x64TilingNoiseHighFreq")
    uv_fine = node(m, unreal.MaterialExpressionTextureCoordinate, -900, 300, u_tiling=5.0, v_tiling=5.0)
    uv_broad = node(m, unreal.MaterialExpressionTextureCoordinate, -900, 500, u_tiling=0.37, v_tiling=0.37)
    fine = node(m, unreal.MaterialExpressionTextureSample, -650, 300, texture=noise)
    broad = node(m, unreal.MaterialExpressionTextureSample, -650, 500, texture=noise)
    link(uv_fine, "", fine, "UVs")
    link(uv_broad, "", broad, "UVs")

    shade = custom(m, -350, 200, "return C * lerp(0.84, 1.14, F) * lerp(0.86, 1.1, B);", unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["C", "F", "B"])
    link(albedo, "", shade, "C")
    link(fine, "R", shade, "F")
    link(broad, "R", shade, "B")
    out(m, shade, "", MP.MP_BASE_COLOR)

    # snow is smoother and brighter than earth, so let brightness drive a little sheen
    rough = custom(m, -350, 450, "return lerp(0.96, 0.62, saturate(dot(C, float3(0.33, 0.33, 0.33)) * 1.6 - 0.7));", unreal.CustomMaterialOutputType.CMOT_FLOAT1, ["C"])
    link(albedo, "", rough, "C")
    out(m, rough, "", MP.MP_ROUGHNESS)
    out(m, const(m, 0.25, -350, 600), "", MP.MP_SPECULAR)
    finish(m)


def build_prop():
    m = new_material("M_FuyuProp")
    m.set_editor_property("two_sided", True)
    _, albedo = vertex_albedo(m, -700, 0)
    out(m, albedo, "", MP.MP_BASE_COLOR)
    out(m, const(m, 0.82, -350, 250), "", MP.MP_ROUGHNESS)
    out(m, const(m, 0.3, -350, 350), "", MP.MP_SPECULAR)
    finish(m)


def build_foliage(name, amplitude, roughness):
    m = new_material(name)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    vertex_color, albedo = vertex_albedo(m, -900, 0)

    # per-instance tint lets grass take the colour of the ground it grows from; untinted instances read white
    tint = node(m, unreal.MaterialExpressionPerInstanceCustomData3Vector, -900, 250, data_index=0, const_default_value=unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    color = node(m, unreal.MaterialExpressionMultiply, -350, 0)
    link(albedo, "", color, "A")
    link(tint, "", color, "B")
    out(m, color, "", MP.MP_BASE_COLOR)

    # light passing through thin leaves and blades picks up their colour
    through = node(m, unreal.MaterialExpressionMultiply, -350, 200, const_b=0.3)
    link(color, "", through, "A")
    out(m, through, "", MP.MP_SUBSURFACE_COLOR)

    out(m, const(m, roughness, -350, 350), "", MP.MP_ROUGHNESS)
    out(m, const(m, 0.15, -350, 450), "", MP.MP_SPECULAR)
    out(m, wind_offset(m, vertex_color, amplitude, -900, 600, part=(name == "M_FuyuGrass")), "", MP.MP_WORLD_POSITION_OFFSET)
    finish(m)


def build_cloth():
    m = new_material("M_FuyuCloth")
    m.set_editor_property("two_sided", True)
    vertex_color, albedo = vertex_albedo(m, -900, 0)
    out(m, albedo, "", MP.MP_BASE_COLOR)
    out(m, const(m, 0.9, -350, 250), "", MP.MP_ROUGHNESS)
    out(m, wind_offset(m, vertex_color, 34.0, -900, 500), "", MP.MP_WORLD_POSITION_OFFSET)
    finish(m)


def build_metal():
    m = new_material("M_FuyuMetal")
    m.set_editor_property("two_sided", True)
    _, albedo = vertex_albedo(m, -700, 0)
    out(m, albedo, "", MP.MP_BASE_COLOR)
    out(m, const(m, 1.0, -350, 200), "", MP.MP_METALLIC)
    out(m, const(m, 0.22, -350, 300), "", MP.MP_ROUGHNESS)
    finish(m)


def build_glow():
    m = new_material("M_FuyuGlow")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    _, albedo = vertex_albedo(m, -900, 0)
    tint = node(m, unreal.MaterialExpressionVectorParameter, -900, 250, parameter_name="Tint", default_value=unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    strength = node(m, unreal.MaterialExpressionScalarParameter, -900, 450, parameter_name="Strength", default_value=24.0)
    # instanced sparks colour themselves through per-instance data; everything else reads white here
    instance = node(m, unreal.MaterialExpressionPerInstanceCustomData3Vector, -900, 600, data_index=0, const_default_value=unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    glow = custom(m, -350, 100, "return C * T * I * S;", unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["C", "T", "I", "S"])
    link(albedo, "", glow, "C")
    link(tint, "", glow, "T")
    link(instance, "", glow, "I")
    link(strength, "", glow, "S")
    out(m, glow, "", MP.MP_EMISSIVE_COLOR)
    finish(m)


def build_water():
    m = new_material("M_FuyuWater")
    noise = unreal.load_asset("/Engine/EngineMaterials/Good64x64TilingNoiseHighFreq")

    # the plane is one huge quad, so pattern it from world position rather than its 0-1 UVs
    position = node(m, unreal.MaterialExpressionWorldPosition, -1200, 0)
    time = node(m, unreal.MaterialExpressionTime, -1200, 200)
    uv_a = custom(m, -900, 0, "return P.xy * 0.00042 + float2(T * 0.006, T * 0.004);", unreal.CustomMaterialOutputType.CMOT_FLOAT2, ["P", "T"])
    uv_b = custom(m, -900, 200, "return P.yx * 0.0011 - float2(T * 0.011, T * 0.007);", unreal.CustomMaterialOutputType.CMOT_FLOAT2, ["P", "T"])
    for uv in (uv_a, uv_b):
        link(position, "", uv, "P")
        link(time, "", uv, "T")

    sample_a = node(m, unreal.MaterialExpressionTextureSample, -650, 0, texture=noise)
    sample_b = node(m, unreal.MaterialExpressionTextureSample, -650, 200, texture=noise)
    link(uv_a, "", sample_a, "UVs")
    link(uv_b, "", sample_b, "UVs")

    color = custom(m, -350, 0, "float n = A * 0.6 + B * 0.4; return lerp(float3(0.004, 0.022, 0.034), float3(0.012, 0.052, 0.066), n);", unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["A", "B"])
    rough = custom(m, -350, 200, "return lerp(0.03, 0.16, A * 0.5 + B * 0.5);", unreal.CustomMaterialOutputType.CMOT_FLOAT1, ["A", "B"])
    ripple = custom(m, -350, 400, "return normalize(float3((A - 0.5) * 0.09, (B - 0.5) * 0.09, 1.0));", unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["A", "B"])
    for target in (color, rough, ripple):
        link(sample_a, "R", target, "A")
        link(sample_b, "G", target, "B")

    out(m, color, "", MP.MP_BASE_COLOR)
    out(m, rough, "", MP.MP_ROUGHNESS)
    out(m, ripple, "", MP.MP_NORMAL)
    out(m, const(m, 1.0, -350, 600), "", MP.MP_SPECULAR)
    out(m, const(m, 0.0, -350, 700), "", MP.MP_METALLIC)
    finish(m)


def build_streak():
    m = new_material("M_FuyuStreak")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    uv = node(m, unreal.MaterialExpressionTextureCoordinate, -900, 0)

    # soft at both ends and both edges, so a ribbon reads as a wisp of moving air
    fade = custom(m, -500, 0, "float ends = sin(saturate(UV.x) * 3.14159); float edge = 1.0 - abs(UV.y * 2.0 - 1.0); return ends * ends * edge * 0.6;", unreal.CustomMaterialOutputType.CMOT_FLOAT1, ["UV"])
    link(uv, "", fade, "UV")
    out(m, node(m, unreal.MaterialExpressionConstant3Vector, -500, 250, constant=unreal.LinearColor(1.6, 1.6, 1.5, 1.0)), "", MP.MP_EMISSIVE_COLOR)
    out(m, fade, "", MP.MP_OPACITY)
    finish(m)


def build_slash():
    """The crescent a sword cut leaves hanging in the air for a moment."""
    m = new_material("M_FuyuSlash")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    uv = node(m, unreal.MaterialExpressionTextureCoordinate, -900, 0)
    tint = node(m, unreal.MaterialExpressionVectorParameter, -900, 200, parameter_name="Tint", default_value=unreal.LinearColor(1.0, 0.9, 0.7, 1.0))
    fade = node(m, unreal.MaterialExpressionScalarParameter, -900, 400, parameter_name="Fade", default_value=1.0)

    # bright along the leading edge, tapering to nothing at both tips and towards the trailing edge
    shape = custom(m, -500, 0, "float tips = pow(sin(saturate(UV.x) * 3.14159), 1.3); float edge = pow(saturate(UV.y), 5.0); return tips * edge * F;", unreal.CustomMaterialOutputType.CMOT_FLOAT1, ["UV", "F"])
    link(uv, "", shape, "UV")
    link(fade, "", shape, "F")
    color = custom(m, -200, 100, "return T * A * 4.5;", unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["T", "A"])
    link(tint, "", color, "T")
    link(shape, "", color, "A")
    out(m, color, "", MP.MP_EMISSIVE_COLOR)
    finish(m)


def build_flower():
    """Flower carpets: white petals take the per-instance colour, so one mesh makes fields of any hue."""
    m = new_material("M_FuyuFlower")
    m.set_editor_property("two_sided", True)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    vertex_color, albedo = vertex_albedo(m, -900, 0)
    tint = node(m, unreal.MaterialExpressionPerInstanceCustomData3Vector, -900, 250, data_index=0, const_default_value=unreal.LinearColor(1.0, 1.0, 1.0, 1.0))

    # only near-white vertices (the petals) are recoloured; leaves and stems keep their own green
    color = custom(m, -350, 0, "float petal = saturate((min(C.r, min(C.g, C.b)) - 0.55) * 5.0); return lerp(C, T * 1.1, petal);", unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["C", "T"])
    link(albedo, "", color, "C")
    link(tint, "", color, "T")
    out(m, color, "", MP.MP_BASE_COLOR)

    through = node(m, unreal.MaterialExpressionMultiply, -100, 200, const_b=0.45)
    link(color, "", through, "A")
    out(m, through, "", MP.MP_SUBSURFACE_COLOR)
    out(m, const(m, 0.6, -350, 350), "", MP.MP_ROUGHNESS)
    out(m, const(m, 0.2, -350, 450), "", MP.MP_SPECULAR)
    out(m, wind_offset(m, vertex_color, 9.0, -900, 600, part=True), "", MP.MP_WORLD_POSITION_OFFSET)
    finish(m)


def build_bird():
    """Distant birds: flat silhouettes whose wingtips beat, each on its own rhythm."""
    m = new_material("M_FuyuBird")
    m.set_editor_property("two_sided", True)
    tint = node(m, unreal.MaterialExpressionPerInstanceCustomData3Vector, -700, 0, data_index=0, const_default_value=unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    out(m, tint, "", MP.MP_BASE_COLOR)
    out(m, const(m, 0.85, -350, 200), "", MP.MP_ROUGHNESS)

    vertex_color = node(m, unreal.MaterialExpressionVertexColor, -900, 400)
    time = node(m, unreal.MaterialExpressionTime, -900, 600)
    random = node(m, unreal.MaterialExpressionPerInstanceRandom, -900, 750)
    flap = custom(m, -450, 500, "float beat = sin(T * 9.0 + R * 6.2832); return float3(0.0, 0.0, beat * W * 34.0 - W * W * 6.0);", unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["T", "R", "W"])
    link(time, "", flap, "T")
    link(random, "", flap, "R")
    link(vertex_color, "A", flap, "W")
    out(m, flap, "", MP.MP_WORLD_POSITION_OFFSET)
    finish(m)


def build_mist():
    """Soft sheets of mist that lie in the valleys and against the foot of the mountain."""
    m = new_material("M_FuyuMist")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_NON_DIRECTIONAL)

    cloud = unreal.load_asset("/Engine/EngineVolumetrics/Fogsheet/Materials/T_EV_FogSheet_CloudTex_01")
    uv = node(m, unreal.MaterialExpressionTextureCoordinate, -1400, 0)
    time = node(m, unreal.MaterialExpressionTime, -1400, 200)
    uv_a = custom(m, -1100, 0, "return UV * 1.6 + float2(T * 0.004, T * 0.0025);", unreal.CustomMaterialOutputType.CMOT_FLOAT2, ["UV", "T"])
    uv_b = custom(m, -1100, 200, "return UV.yx * 3.1 - float2(T * 0.006, T * 0.003);", unreal.CustomMaterialOutputType.CMOT_FLOAT2, ["UV", "T"])
    for target in (uv_a, uv_b):
        link(uv, "", target, "UV")
        link(time, "", target, "T")
    cloud_a = sample(m, cloud, uv_a, -800, 0)
    cloud_b = sample(m, cloud, uv_b, -800, 200)

    # thick in the middle of the sheet and feathered to nothing at its rim, broken up by two drifting layers of cloud.
    # It also thins out close to the camera and where it meets the ground, so it never shows a hard line
    depth = node(m, unreal.MaterialExpressionPixelDepth, -800, 450)
    scene = node(m, unreal.MaterialExpressionSceneDepth, -800, 600)
    shape = custom(m, -450, 100, """
float rim = saturate(1.0 - length(UV - 0.5) * 2.0);
float body = smoothstep(0.0, 0.55, rim);
float wisps = saturate(A * 1.5 + B * 0.7 - 0.25);
float near = saturate((D - 500.0) / 3500.0);
float ground = saturate((S - D) / 1600.0);
return body * lerp(0.4, 1.0, wisps) * near * ground * 0.8;
""", unreal.CustomMaterialOutputType.CMOT_FLOAT1, ["UV", "A", "B", "D", "S"])
    link(scene, "", shape, "S")
    link(uv, "", shape, "UV")
    link(cloud_a, "R", shape, "A")
    link(cloud_b, "R", shape, "B")
    link(depth, "", shape, "D")


    out(m, node(m, unreal.MaterialExpressionConstant3Vector, -450, -150, constant=unreal.LinearColor(0.93, 0.96, 1.0, 1.0)), "", MP.MP_BASE_COLOR)
    out(m, shape, "", MP.MP_OPACITY)
    out(m, const(m, 1.0, -450, 500), "", MP.MP_ROUGHNESS)
    finish(m)


def build_sky():
    """The sky dome. It draws the atmosphere itself, as a sky material must, then adds the stars, Milky Way and moon after dark."""
    m = new_material("M_FuyuSky")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("is_sky", True)

    view = node(m, unreal.MaterialExpressionCameraVectorWS, -1700, 0)
    spin = node(m, unreal.MaterialExpressionScalarParameter, -1700, 200, parameter_name="Spin", default_value=0.0)
    night = node(m, unreal.MaterialExpressionScalarParameter, -900, 400, parameter_name="Night", default_value=0.0)
    gain = node(m, unreal.MaterialExpressionScalarParameter, -900, 550, parameter_name="StarGain", default_value=1.0)
    moon = node(m, unreal.MaterialExpressionVectorParameter, -900, 700, parameter_name="MoonDir", default_value=unreal.LinearColor(0.0, 0.0, 1.0, 0.0))

    # the star map is a panorama painted by the game when it starts: longitude across, straight up at its top edge.
    # Turning the view direction about the celestial pole before looking it up makes the stars wheel overhead
    uv = custom(m, -1300, 0, """
float3 d = -V;
float3 k = normalize(float3(0.42, 0.26, 0.87));
float s = sin(A), c = cos(A);
d = d * c + cross(k, d) * s + k * dot(k, d) * (1.0 - c);
return float2(atan2(d.y, d.x) / 6.2831853 + 0.5, acos(clamp(d.z, -1.0, 1.0)) / 3.14159265);
""", unreal.CustomMaterialOutputType.CMOT_FLOAT2, ["V", "A"])
    link(view, "", uv, "V")
    link(spin, "", uv, "A")

    black = unreal.load_asset("/Engine/EngineResources/Black")
    ST = unreal.MaterialSamplerType
    stars = node(m, unreal.MaterialExpressionTextureSampleParameter2D, -900, 0, parameter_name="Stars", texture=black,
                 sampler_type=ST.SAMPLERTYPE_COLOR if black.get_editor_property("srgb") else ST.SAMPLERTYPE_LINEAR_COLOR)
    log("star map sampler is sRGB: %s" % black.get_editor_property("srgb"))
    link(uv, "", stars, "UVs")

    air = node(m, unreal.MaterialExpressionSkyAtmosphereViewLuminance, -900, 900)
    disk = node(m, unreal.MaterialExpressionSkyAtmosphereLightDiskLuminance, -900, 1050)

    color = custom(m, -400, 300, """
float3 d = -V;
float up = saturate(d.z);

// stars sink into the haze towards the horizon, and a faint glow of the air itself rises to meet them
float3 stars = S * G * N * N * smoothstep(-0.02, 0.24, d.z);
float3 glow = float3(0.006, 0.011, 0.028) * N * (0.3 + 0.7 * pow(1.0 - up, 3.0));

// the moon: a soft-edged disc with a wide halo. W fades it in as it rises
float md = dot(d, normalize(M.xyz));
float disc = smoothstep(0.99982, 0.99990, md);
float halo = pow(saturate(md), 900.0) * 0.5 + pow(saturate(md), 60.0) * 0.045;
float3 moon = float3(0.93, 0.96, 1.0) * (disc * 1.8 + halo * 0.7) * W;

return A + D + stars * (1.0 - disc * W) + glow + moon;
""", unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["V", "S", "N", "G", "M", "W", "A", "D"])
    link(view, "", color, "V")
    link(stars, "RGB", color, "S")
    link(night, "", color, "N")
    link(gain, "", color, "G")
    link(moon, "", color, "M")
    link(moon, "A", color, "W")
    link(air, "", color, "A")
    link(disk, "", color, "D")
    out(m, color, "", MP.MP_EMISSIVE_COLOR)
    finish(m)


def import_texture(path, kind):
    """Brings an image file in as a texture under the materials folder. kind is "color", "normal" or "mask"."""
    name = os.path.splitext(os.path.basename(path))[0]
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", path)
    task.set_editor_property("destination_path", MAT_DIR)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    asset_tools.import_asset_tasks([task])

    texture = unreal.load_asset(MAT_DIR + "/" + name)
    if kind == "normal":
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        texture.set_editor_property("srgb", False)
    elif kind == "mask":
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        texture.set_editor_property("srgb", False)
    eal.save_loaded_asset(texture)
    return texture


def build_katana():
    """
    Materials for the modelled katana ("Katana" by nejenabinupa on Sketchfab, CC-BY-NC-4.0).
    Tools/convert_katana.py must have been run first: it leaves the sword's textures in Saved/Katana.
    """
    folder = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "Katana")
    files = {name: os.path.join(folder, name) for name in os.listdir(folder)} if os.path.isdir(folder) else {}
    needed = ("T_KatanaWrap_Color.jpg", "T_KatanaWrap_Normal.png", "T_KatanaBlade_Color.jpg", "T_KatanaBlade_Mask.png")
    if any(name not in files for name in needed):
        log("katana textures not found in Saved/Katana, run Tools/convert_katana.py first. Skipping the katana")
        return

    uv = lambda m: node(m, unreal.MaterialExpressionTextureCoordinate, -1000, 0)

    # the silk wrap over ray skin on the grip
    m = new_material("M_KatanaWrap")
    m.set_editor_property("two_sided", True)
    coords = uv(m)
    out(m, sample(m, import_texture(files["T_KatanaWrap_Color.jpg"], "color"), coords, -600, 0), "RGB", MP.MP_BASE_COLOR)
    out(m, sample(m, import_texture(files["T_KatanaWrap_Normal.png"], "normal"), coords, -600, 300), "RGB", MP.MP_NORMAL)
    out(m, const(m, 0.8, -350, 600), "", MP.MP_ROUGHNESS)
    finish(m)

    # polished steel, with the wavy temper line painted into its colour and the tang left dull
    m = new_material("M_KatanaBlade")
    m.set_editor_property("two_sided", True)
    coords = uv(m)
    colour = sample(m, import_texture(files["T_KatanaBlade_Color.jpg"], "color"), coords, -600, 0)
    mask = sample(m, import_texture(files["T_KatanaBlade_Mask.png"], "mask"), coords, -600, 300)
    bright = custom(m, -250, 0, "return saturate(C * 1.9 + 0.08);", unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["C"])
    link(colour, "RGB", bright, "C")
    out(m, bright, "", MP.MP_BASE_COLOR)
    metal = node(m, unreal.MaterialExpressionMultiply, -250, 250, const_b=0.93)
    link(mask, "B", metal, "A")
    out(m, metal, "", MP.MP_METALLIC)
    rough = custom(m, -250, 400, "return 0.14 + 0.3 * G;", unreal.CustomMaterialOutputType.CMOT_FLOAT1, ["G"])
    link(mask, "G", rough, "G")
    out(m, rough, "", MP.MP_ROUGHNESS)
    finish(m)

    # plain fittings: black lacquer, gilt, and the dark collar at the blade's root
    for name, colour, metallic, roughness in (
            ("M_KatanaLacquer", (0.012, 0.012, 0.014), 0.14, 0.16),
            ("M_KatanaGilt", (0.72, 0.5, 0.22), 1.0, 0.24),
            ("M_KatanaCollar", (0.015, 0.014, 0.013), 0.0, 0.55)):
        m = new_material(name)
        m.set_editor_property("two_sided", True)
        out(m, node(m, unreal.MaterialExpressionConstant3Vector, -500, 0, constant=unreal.LinearColor(colour[0], colour[1], colour[2], 1.0)), "", MP.MP_BASE_COLOR)
        out(m, const(m, metallic, -500, 200), "", MP.MP_METALLIC)
        out(m, const(m, roughness, -500, 300), "", MP.MP_ROUGHNESS)
        finish(m)


HORSE_CODE = """
// The horse has no skeleton: this is its animation. P is the vertex where it stands in the model (cm, +X forward,
// +Z up). C and A are its vertex colour, which says how much it belongs to the front legs (red), hind legs (green),
// neck and head (blue) and tail (alpha). T is time, PH where in its stride this horse is (0 to 1), and G how hard
// it is running (0 standing, 1 full gallop). Tools/convert_horse.py holds the same motion in Python, with a preview.
float3 p = P;
float front = pow(saturate(C.r), 2.2), hind = pow(saturate(C.g), 2.2), neck = pow(saturate(C.b), 2.2), tail = saturate(A);
float gait = saturate(G);
float cyc = (T * %(strides_per_second)f + PH) * 6.2831853;
float lag = p.y > 0.0 ? %(pair_lag)f : 0.0;

// turns p about a sideways axis through a pivot; a positive angle carries a hanging leg forward
#define FUYU_SWING(pv, ang) { float2 fd = p.xz - pv; float fa = ang; float fs = sin(fa), fc = cos(fa); p.xz = pv + float2(fd.x * fc - fd.y * fs, fd.x * fs + fd.y * fc); }

// each leg folds at the knee as it comes forward through the air, then swings from the shoulder or hip.
// The hind pair strike first and the front pair half a stride later
float u = cyc - 3.14159265 - lag;
float fold = smoothstep(0.0, 1.0, max(cos(u), 0.0));
FUYU_SWING(float2(%(knee_x)f, %(knee_z)f), -%(front_fold)f * gait * fold * front * (1.0 - smoothstep(%(knee_z)f - 7.0, %(knee_z)f + 7.0, p.z)))
FUYU_SWING(float2(%(shoulder_x)f, %(shoulder_z)f), gait * (%(front_swing)f * sin(u) + %(front_reach)f) * front)

u = cyc - lag;
fold = smoothstep(0.0, 1.0, max(cos(u), 0.0));
FUYU_SWING(float2(%(hock_x)f, %(hock_z)f), %(hind_fold)f * gait * fold * hind * (1.0 - smoothstep(%(hock_z)f - 7.0, %(hock_z)f + 7.0, p.z)))
FUYU_SWING(float2(%(hip_x)f, %(hip_z)f), gait * (%(hind_swing)f * sin(u) + %(hind_reach)f) * hind)

// the neck stretches out and nods, the tail streams behind
FUYU_SWING(float2(%(neck_x)f, %(neck_z)f), -gait * (%(neck_stretch)f + %(neck_nod)f * sin(cyc + 1.0)) * neck)
FUYU_SWING(float2(%(tail_x)f, %(tail_z)f), tail * (-%(tail_lift)f * gait + %(tail_wave)f * (0.3 + gait) * sin(cyc * 2.0 + p.z * 0.03)))

// and the whole horse rocks and bounds
FUYU_SWING(float2(%(body_x)f, %(body_z)f), gait * %(rock)f * sin(cyc + 2.2))
#undef FUYU_SWING
p.z += gait * %(bob)f * sin(cyc + 0.6);
return p - P;
"""


def build_horse():
    """
    The horse ("Low poly Horse Brown Colour 3d Model Free" by iRahulRajput on Sketchfab, CC-BY-4.0) and its gallop,
    which is done here in the material because the model has no skeleton. Tools/convert_horse.py must have been run first.
    """
    folder = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "Horse")
    rig_path = os.path.join(folder, "rig.json")
    texture_path = os.path.join(folder, "T_Horse_Color.jpg")
    if not os.path.isfile(rig_path) or not os.path.isfile(texture_path):
        log("horse files not found in Saved/Horse, run Tools/convert_horse.py first. Skipping the horse")
        return

    with open(rig_path) as handle:
        rig = json.load(handle)
    values = dict(rig["gait"])
    for name, joint in rig["joints"].items():
        values[name + "_x"] = joint[0]
        values[name + "_z"] = joint[1]

    texture = import_texture(texture_path, "color")
    texture.set_editor_property("max_texture_size", 2048)
    eal.save_loaded_asset(texture)

    m = new_material("M_FuyuHorse")
    m.set_editor_property("two_sided", True)
    coords = node(m, unreal.MaterialExpressionTextureCoordinate, -1000, 0)
    out(m, sample(m, texture, coords, -600, 0), "RGB", MP.MP_BASE_COLOR)
    out(m, const(m, 0.7, -350, 250), "", MP.MP_ROUGHNESS)
    out(m, const(m, 0.25, -350, 350), "", MP.MP_SPECULAR)

    local = node(m, unreal.MaterialExpressionPreSkinnedPosition, -1400, 600)
    rigging = node(m, unreal.MaterialExpressionVertexColor, -1400, 750)
    time = node(m, unreal.MaterialExpressionTime, -1400, 950)
    phase = node(m, unreal.MaterialExpressionPerInstanceCustomData, -1400, 1050, data_index=0, const_default_value=0.0)
    gait = node(m, unreal.MaterialExpressionPerInstanceCustomData, -1400, 1200, data_index=1, const_default_value=1.0)
    move = custom(m, -900, 700, HORSE_CODE % values, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["P", "C", "A", "T", "PH", "G"])
    link(local, "", move, "P")
    link(rigging, "", move, "C")
    link(rigging, "A", move, "A")
    link(time, "", move, "T")
    link(phase, "", move, "PH")
    link(gait, "", move, "G")

    # the motion is worked out where the horse stands in its model, then carried to wherever this horse is in the world
    sources = unreal.MaterialVectorCoordTransformSource
    source = getattr(sources, "TRANSFORMSOURCE_INSTANCE", None) or sources.TRANSFORMSOURCE_LOCAL
    log("horse motion is transformed from %s" % source)
    world = node(m, unreal.MaterialExpressionTransform, -450, 700, transform_source_type=source, transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    link(move, "", world, "")
    out(m, world, "", MP.MP_WORLD_POSITION_OFFSET)
    finish(m)


def reparent_copy(source, name, parent_class_path):
    path = BP_DIR + "/" + name
    if eal.does_asset_exist(path):
        eal.delete_asset(path)

    blueprint = eal.duplicate_asset(source, path)
    parent = unreal.load_class(None, parent_class_path)
    if blueprint is None or parent is None:
        unreal.log_error("[build_content] could not create %s from %s (parent %s)" % (name, source, parent_class_path))
        return

    unreal.BlueprintEditorLibrary.reparent_blueprint(blueprint, parent)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    eal.save_loaded_asset(blueprint)
    log("built %s (parent %s)" % (name, parent.get_name()))


def build_map():
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    if world is None:
        unreal.log_error("[build_content] could not create a blank map")
        return

    # the game mode spawns the island if the level lacks one, but placing it here lets its sky be tuned in the editor
    world_class = unreal.load_class(None, "/Script/Fuyukaze.FuyuWorld")
    game_mode = unreal.load_class(None, "/Script/Fuyukaze.FuyuGameMode")

    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if actors is not None and world_class is not None:
        island = actors.spawn_actor_from_class(world_class, unreal.Vector(0.0, 0.0, 0.0))
        if island is not None:
            island.set_actor_label("FuyuWorld")

    settings = world.get_world_settings()
    if settings is not None and game_mode is not None:
        settings.set_editor_property("default_game_mode", game_mode)

    if unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH):
        log("built map " + MAP_PATH)
    else:
        unreal.log_error("[build_content] could not save " + MAP_PATH)


def report_mannequin_parameters():
    """Lists the mannequin's tintable parameters, which the enemy types use to tell themselves apart."""
    for path in ("/Game/Characters/Mannequins/Materials/Manny/MI_Manny_01_New", "/Game/Megaplant_Library/Tree_Ginkgo/Materials/MI_Ginkgo_Foliage_01", "/Game/Megaplant_Library/Tree_Norway_Maple/Materials/MI_Norway_Maple_Foliage_01"):
        material = unreal.load_asset(path)
        if material is None:
            continue
        names = [str(n) for n in mel.get_vector_parameter_names(material)]
        log("%s vector parameters: %s" % (path.split("/")[-1], ", ".join(names)))
        log("%s scalar parameters: %s" % (path.split("/")[-1], ", ".join(str(n) for n in mel.get_scalar_parameter_names(material))))


def enable_instancing():
    """Fab's Megascans materials are not flagged for instanced meshes, which is how the island draws grass and props.
    Without the flag the engine swaps in its grey default material at runtime."""
    instances = (
        "/Game/Fab/Megascans/Plants/Dry_Grass_tbbqejqr/High/tbbqejqr_tier_1/Materials/MI_tbbqejqr",
        "/Game/Fab/Megascans/Plants/Dry_Grass_tbbqejqr/High/tbbqejqr_tier_1/Materials/MI_tbbqejqr_Billboard",
        "/Game/Fab/Megascans/Plants/Wild_Grass_vlkhcbxia/High/vlkhcbxia_tier_1/Materials/MI_vlkhcbxia",
        "/Game/Fab/Megascans/Plants/Wild_Grass_vlkhcbxia/High/vlkhcbxia_tier_1/Materials/MI_vlkhcbxia_Billboard",
        "/Game/Fab/Megascans/3D/Japanese_Stone_Lantern_ufnscjdga/High/ufnscjdga_tier_1/Materials/MI_ufnscjdga",
    )
    done = set()
    for path in instances:
        instance = unreal.load_asset(path)
        if instance is None:
            continue
        base = instance.get_base_material()
        if base is None or base.get_path_name() in done:
            continue
        done.add(base.get_path_name())
        base.set_editor_property("used_with_instanced_static_meshes", True)
        mel.recompile_material(base)
        eal.save_loaded_asset(base)
        log("enabled instancing on " + base.get_name())


def part_scanned_grass():
    """Adds the same parting to Fab's Megascans foliage material, which the scanned grass uses. The scalar
    parameter doubles as a marker so that running this twice does not add the effect twice."""
    base = unreal.load_asset("/Game/Fab/Materials/Standard/M_MS_Foliage")
    if base is None:
        return
    if "FuyuGrassPart" in [str(n) for n in mel.get_scalar_parameter_names(base)]:
        log("M_MS_Foliage already parts around the samurai")
        return

    strength = node(base, unreal.MaterialExpressionScalarParameter, 600, 2400, parameter_name="FuyuGrassPart", default_value=1.0)
    part = part_offset(base, 600, 1700, strength)

    existing = mel.get_material_property_input_node(base, MP.MP_WORLD_POSITION_OFFSET)
    if existing is None:
        out(base, part, "", MP.MP_WORLD_POSITION_OFFSET)
    else:
        pin = mel.get_material_property_input_node_output_name(base, MP.MP_WORLD_POSITION_OFFSET)
        both = node(base, unreal.MaterialExpressionAdd, 1300, 1700)
        link(existing, pin, both, "A")
        link(part, "", both, "B")
        out(base, both, "", MP.MP_WORLD_POSITION_OFFSET)

    mel.recompile_material(base)
    eal.save_loaded_asset(base)
    log("added parting to M_MS_Foliage")


def main():
    for directory in (MAT_DIR, BP_DIR, "/Game/Fuyukaze/Maps"):
        if not eal.does_directory_exist(directory):
            eal.make_directory(directory)

    # BuildContent.ps1 -Only sky rebuilds just that one material and leaves every other asset untouched
    only = os.environ.get("FUYU_ONLY")
    if only:
        globals()["build_" + only]()
        log("done")
        return

    build_terrain()
    build_prop()
    build_foliage("M_FuyuFoliage", 13.0, 0.75)
    build_foliage("M_FuyuGrass", 21.0, 0.7)
    build_cloth()
    build_metal()
    build_glow()
    build_water()
    build_streak()
    build_slash()
    build_flower()
    build_bird()
    build_mist()
    build_sky()
    build_katana()
    build_horse()

    reparent_copy("/Game/Variant_Combat/Blueprints/BP_CombatCharacter", "BP_FuyuSamurai", "/Script/Fuyukaze.FuyuSamurai")
    reparent_copy("/Game/Variant_Combat/Blueprints/AI/BP_CombatEnemy", "BP_FuyuEnemy", "/Script/Fuyukaze.FuyuEnemy")

    enable_instancing()
    part_scanned_grass()
    build_map()
    report_mannequin_parameters()
    log("done")


main()
