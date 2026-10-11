"""
Generates the binary assets Fuyukaze needs: its materials, the samurai and enemy blueprints
(re-parented copies of the combat template's), and the level.

Everything else in the game is authored in C++, so this only has to be run once, or again after
changing a material below. Run it from the project folder with:

    Tools\\BuildContent.ps1
"""

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


def wind_offset(material, vertex_color, amplitude, x, y):
    position = node(material, unreal.MaterialExpressionWorldPosition, x, y)
    time = node(material, unreal.MaterialExpressionTime, x, y + 150)
    amp = const(material, amplitude, x, y + 250)
    wind = custom(material, x + 300, y, WIND_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["P", "T", "W", "A"])
    link(position, "", wind, "P")
    link(time, "", wind, "T")
    link(vertex_color, "A", wind, "W")
    link(amp, "", wind, "A")
    return wind


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
    out(m, wind_offset(m, vertex_color, amplitude, -900, 600), "", MP.MP_WORLD_POSITION_OFFSET)
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


def main():
    for directory in (MAT_DIR, BP_DIR, "/Game/Fuyukaze/Maps"):
        if not eal.does_directory_exist(directory):
            eal.make_directory(directory)

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

    reparent_copy("/Game/Variant_Combat/Blueprints/BP_CombatCharacter", "BP_FuyuSamurai", "/Script/Fuyukaze.FuyuSamurai")
    reparent_copy("/Game/Variant_Combat/Blueprints/AI/BP_CombatEnemy", "BP_FuyuEnemy", "/Script/Fuyukaze.FuyuEnemy")

    enable_instancing()
    build_map()
    report_mannequin_parameters()
    log("done")


main()
