#pragma once

#include "CoreMinimal.h"

/** What a point of interest on the island is used for */
enum class EFuyuSiteType : uint8
{
	Start,
	Camp,
	Shrine,
	Lookout
};

struct FFuyuSite
{
	FName Id;
	FText DisplayName;
	EFuyuSiteType Type = EFuyuSiteType::Camp;
	FVector2D Pos = FVector2D::ZeroVector;

	/** Ground inside this radius is levelled so structures sit flat */
	float Radius = 2500.0f;

	/** Yaw the site faces, used to orient gates and spawn points */
	float Yaw = 0.0f;
};

/**
 *  The island is defined analytically: every system that needs ground height, slope or biome
 *  asks these functions, so the terrain mesh, foliage and gameplay always agree.
 *  Units are centimetres, +X is north, sea level is Z = 0.
 */
namespace FuyuTerrain
{
	/** Half the side length of the square terrain mesh */
	constexpr float HalfSize = 150000.0f;

	/** Number of quads along one side of the terrain */
	constexpr int32 Cells = 400;

	constexpr float Step = (HalfSize * 2.0f) / Cells;

	/** Centre of the volcano that dominates the northern skyline */
	const FVector2D MountainCenter(132000.0, 5000.0);
	constexpr float MountainRadius = 150000.0f;
	constexpr float MountainPeak = 86000.0f;

	/** The mountain's far side lies beyond the playable square; this much extra terrain is drawn north of it, without collision */
	constexpr float BackdropDepth = 141000.0f;

	const FVector2D LakeCenter(-42000.0, 60000.0);

	/** Height above which the ground turns to bare rock, then snow */
	constexpr float RockLine = 19000.0f;
	constexpr float SnowLine = 46000.0f;

	const TArray<FFuyuSite>& GetSites();
	const FFuyuSite* FindSite(FName Id);

	float Height(float X, float Y);
	FVector Normal(float X, float Y);
	FVector Ground(const FVector2D& P);

	/** Distance to the nearest trail */
	float PathDistance(float X, float Y);

	/** 0 = open ground, 1 = dense forest */
	float ForestMask(float X, float Y);

	/** 0 = plain grass, 1 = flower meadow */
	float FlowerMask(float X, float Y);

	/** How much grass grows here, 0 on rock, sand, snow and inside sites */
	float GrassDensity(float X, float Y, float H, float NormalZ);

	/** Albedo painted into the terrain's vertex colours (alpha = how rocky), also used to tint the grass growing on it */
	FLinearColor GroundColor(float X, float Y, float H, float NormalZ);

	/** True if the point is on land, walkable and outside every site's cleared ground */
	bool IsOpenGround(float X, float Y, float MaxSlopeDeg = 28.0f, float SiteMargin = 600.0f);
}
