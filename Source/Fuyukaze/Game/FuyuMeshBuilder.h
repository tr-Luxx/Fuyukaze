#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
class UMaterialInterface;

/**
 *  Small immediate-mode geometry builder used to author the game's stylised props in code.
 *  Every vertex carries a colour, which the vertex-colour materials read as albedo
 *  (alpha is the wind sway weight on foliage).
 */
struct FFuyuMeshBuilder
{
	TArray<FVector3f> Positions;
	TArray<FVector3f> Normals;
	TArray<FVector2f> UVs;
	TArray<FLinearColor> Colors;

	/** Optional: tangent and binormal sign per vertex, for imported models whose materials use normal maps */
	TArray<FVector4f> Tangents;

	/** Triangle index lists, one per material slot */
	TArray<TArray<int32>> Sections;

	/** Transform applied to everything added from now on */
	FTransform Xf = FTransform::Identity;

	/** Material slot that new triangles are added to */
	int32 Slot = 0;

	int32 AddVertex(const FVector& P, const FVector& N, const FVector2D& UV, const FLinearColor& C);
	void AddTri(int32 A, int32 B, int32 C);
	void AddQuad(int32 A, int32 B, int32 C, int32 D);

	/** Flat-shaded quad from four corners given counter-clockwise when seen from the front */
	void Quad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FLinearColor& Col);

	/** Axis-aligned box centred on Center */
	void Box(const FVector& Center, const FVector& HalfExtent, const FLinearColor& Col);

	/** Tapered cylinder running from Base to Top. A zero top radius makes a cone */
	void Cylinder(const FVector& Base, const FVector& Top, float BaseRadius, float TopRadius, int32 Sides, const FLinearColor& BaseCol, const FLinearColor& TopCol, bool bCaps = true);

	/** Lumpy sphere. Noise displaces the surface, Squash scales Z */
	void Blob(const FVector& Center, float Radius, int32 Rings, int32 Sides, float Noise, float Squash, const FLinearColor& LowCol, const FLinearColor& HighCol, int32 Seed, float SwayWeight = 0.0f);

	/** A curved, tapering blade of grass or reed. Alpha rises from root to tip so it sways */
	void Blade(const FVector& Root, float Yaw, float Height, float Width, float Bend, const FLinearColor& RootCol, const FLinearColor& TipCol, int32 Segments = 3);

	/** Builds a renderable static mesh. Collision, if any, is added afterwards with the helpers below */
	UStaticMesh* Build(UObject* Outer, FName Name, const TArray<UMaterialInterface*>& Materials) const;

	/** Builds a static mesh with one LOD per builder. ScreenSizes[i] is the screen size at which LOD i takes over */
	static UStaticMesh* BuildLODs(UObject* Outer, FName Name, const TArray<UMaterialInterface*>& Materials, const TArray<const FFuyuMeshBuilder*>& LODs, const TArray<float>& ScreenSizes);

	static void AddBoxCollision(UStaticMesh* Mesh, const FVector& Center, const FVector& HalfExtent);
	static void AddCapsuleCollision(UStaticMesh* Mesh, const FVector& Center, float Radius, float Length);
	static void FinishCollision(UStaticMesh* Mesh);
};
