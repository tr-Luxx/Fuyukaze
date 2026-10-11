#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
class UMaterialInterface;

/** Materials shared by every code-authored mesh. Created by Tools/build_content.py */
struct FFuyuMaterials
{
	UMaterialInterface* Terrain = nullptr;
	UMaterialInterface* Prop = nullptr;
	UMaterialInterface* Foliage = nullptr;
	UMaterialInterface* Grass = nullptr;
	UMaterialInterface* Cloth = nullptr;
	UMaterialInterface* Metal = nullptr;
	UMaterialInterface* Glow = nullptr;
	UMaterialInterface* Water = nullptr;
	UMaterialInterface* Streak = nullptr;
	UMaterialInterface* Slash = nullptr;
	UMaterialInterface* Flower = nullptr;
	UMaterialInterface* Bird = nullptr;
	UMaterialInterface* Mist = nullptr;

	void Load();
	static const FFuyuMaterials& Get();
};

/**
 *  Code-authored static meshes. Each is built once on first use and cached for the session,
 *  so placing a thousand trees costs one mesh build.
 */
namespace FuyuProps
{
	enum class EProp : uint8
	{
		Pine,
		Ginkgo,
		Maple,
		Rock,
		Boulder,
		GrassClump,
		FlowerClump,
		LilyClump,
		Pampas,
		Torii,
		Tent,
		Palisade,
		Banner,
		StoneLantern,
		ShrineHall,
		Campfire,
		Katana,
		Kasa,
		Spear,
		Club,
		WindStreak,
		Leaf,
		SlashArc,
		Spark,
		FlowerCarpet,
		GravePillar,
		PathSlab,
		Pagoda,
		Bird,
		TrailLine,
		Count
	};

	UStaticMesh* Get(EProp Prop);

	/** Drops every cached mesh. Called when the world actor goes away so PIE sessions don't leak */
	void Reset();
}
