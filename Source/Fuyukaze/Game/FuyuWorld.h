#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FuyuProps.h"
#include "FuyuWorld.generated.h"

class UProceduralMeshComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UInstancedSkinnedMeshComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UDirectionalLightComponent;
class USkyLightComponent;
class USkyAtmosphereComponent;
class UExponentialHeightFogComponent;
class UVolumetricCloudComponent;
class UPostProcessComponent;
class UPointLightComponent;
struct FFuyuSite;

/**
 *  Builds and owns the island: terrain, sky, forests, grass, the structures at each site,
 *  and the wind that drifts leaves across it and points the way to the next objective.
 */
UCLASS()
class AFuyuWorld : public AActor
{
	GENERATED_BODY()

public:

	AFuyuWorld();

	/** Finds the world actor for this level, if one has been placed */
	static AFuyuWorld* Get(const UObject* WorldContext);

	/** Direction the ambient wind blows, on the ground plane */
	FVector GetWindDirection() const { return WindDirection; }

	/** Sends a visible gust from the viewer towards Target for a few seconds */
	UFUNCTION(BlueprintCallable, Category="Fuyukaze")
	void BlowGuidingWind(const FVector& Target);

	/** Black-and-white, high-grain presentation in the style of classic samurai cinema */
	UFUNCTION(BlueprintCallable, Category="Fuyukaze")
	void SetCinemaMode(bool bEnabled);

	bool IsCinemaMode() const { return bCinemaMode; }

	/** Takes down the occupiers' banners and puts out their fire once a camp has been freed */
	void SetSiteLiberated(FName SiteId);

	/** Throws a fan of glowing slivers out from a point, biased along Direction */
	void SpawnSparks(const FVector& Location, const FVector& Direction, const FLinearColor& Color, int32 Count, float Speed);

	/** Bursts a swirl of autumn leaves from a point, for a foe cut down */
	void SpawnLeafBurst(const FVector& Location, int32 Count);

	/** Hangs a glowing crescent in the air along a sword cut. Roll tilts the plane of the cut */
	void SpawnSlash(const FVector& Origin, const FVector& Forward, float RollDegrees, float Size, const FLinearColor& Color);

	/** Kicks the picture for an instant: colour fringing, bloom and a tighter vignette */
	void PunchScreen(float Strength);

	/** World-space point on the ground at a site-local offset (X towards the site's facing) */
	static FVector SitePoint(const FFuyuSite& Site, float Forward, float Right);

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	void BuildTerrain();
	void BuildScatter();
	void BuildSites();
	void BuildCamp(const FFuyuSite& Site);
	void BuildShrine(const FFuyuSite& Site);

	/** Adds an instance of a prop, standing on the terrain unless bSnapToGround is false */
	void Place(FuyuProps::EProp Prop, const FVector& Location, float Yaw, float Scale = 1.0f, bool bSnapToGround = true);

	/** Adds a prop as its own component so it can be hidden or moved later */
	UStaticMeshComponent* PlaceUnique(FuyuProps::EProp Prop, const FVector& Location, float Yaw, bool bCollide);

	UHierarchicalInstancedStaticMeshComponent* GetScatter(FuyuProps::EProp Prop);

	/**
	 *  Draws a species with scanned Megaplants trees if they have been imported from Fab (Content/Megaplant_Library).
	 *  Returns false if none are present, in which case the code-built stylised tree is shown instead
	 */
	bool PlaceScannedTrees(FuyuProps::EProp Species, const TArray<FTransform>& Transforms);

	void UpdateGrass(const FVector& ViewLocation, bool bBuildEverything);
	void BuildGrassCell(struct FFuyuGrassCell& Cell, bool bNear);
	void ReleaseGrassCell(struct FFuyuGrassCell& Cell);
	UInstancedStaticMeshComponent* AcquireGrassComponent(uint8 Key);

	void UpdateLeaves(float DeltaSeconds, const FVector& ViewLocation);
	void UpdateStreaks(float DeltaSeconds, const FVector& ViewLocation);
	void UpdateEffects(float DeltaSeconds);
	void ApplyGrade();

protected:

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UDirectionalLightComponent> Sun;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USkyAtmosphereComponent> Atmosphere;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UExponentialHeightFogComponent> Fog;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UVolumetricCloudComponent> Clouds;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UPostProcessComponent> PostProcess;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> Water;

	/** Sun elevation above the horizon. Low angles give the long golden light the setting calls for */
	UPROPERTY(EditAnywhere, Category="Fuyukaze|Sky", meta = (ClampMin = 2, ClampMax = 85, Units = "deg"))
	float SunElevation = 31.0f;

	/** Compass direction the sunlight travels towards */
	UPROPERTY(EditAnywhere, Category="Fuyukaze|Sky", meta = (Units = "deg"))
	float SunHeading = 52.0f;

	/** Compass direction the wind blows towards */
	UPROPERTY(EditAnywhere, Category="Fuyukaze|Wind", meta = (Units = "deg"))
	float WindHeading = 25.0f;

	/** How far out grass is drawn, in 16 m cells */
	UPROPERTY(EditAnywhere, Category="Fuyukaze|Grass", meta = (ClampMin = 1, ClampMax = 8))
	int32 GrassRadiusCells = 5;

	/** Number of leaves kept drifting around the viewer */
	UPROPERTY(EditAnywhere, Category="Fuyukaze|Wind", meta = (ClampMin = 0, ClampMax = 600))
	int32 LeafCount = 170;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> TerrainChunks;

	UPROPERTY(Transient)
	TMap<uint8, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> Scatter;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> GrassComponents;

	/** Scanned Megascans grass clumps, used in place of the code-built clump when imported. Dry varieties first */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> ScannedGrass;
	int32 NumDryGrass = 0;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedSkinnedMeshComponent>> ScannedTrees;

	/** One scanned component per species, for the handful of feature trees placed individually at sites */
	TMap<uint8, UInstancedSkinnedMeshComponent*> ScannedBySpecies;

	/** How far away scanned trees are drawn. They are far heavier than the stylised ones */
	UPROPERTY(EditAnywhere, Category="Fuyukaze|Trees", meta = (Units = "cm"))
	float ScannedTreeDrawDistance = 220000.0f;

	/** Size multiplier applied to scanned trees, which are modelled at real-world scale */
	UPROPERTY(EditAnywhere, Category="Fuyukaze|Trees")
	float ScannedTreeScale = 1.5f;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> Leaves;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> Streaks;

	/** Banners and fires belonging to each camp, so they can be cleared when it is liberated */
	TMap<FName, TArray<TWeakObjectPtr<USceneComponent>>> SiteDressing;

	TMap<FIntPoint, TSharedPtr<struct FFuyuGrassCell>> GrassCells;
	TMap<uint8, TArray<UInstancedStaticMeshComponent*>> GrassPool;
	FIntPoint LastGrassCell = FIntPoint(MAX_int32, MAX_int32);

	struct FLeaf
	{
		FVector Pos;
		FVector Axis;
		float Phase;
		float Speed;
	};
	TArray<FLeaf> LeafState;

	struct FStreak
	{
		FVector Pos;
		float Age;
		float Life;
		float Phase;
		float Length;
	};
	TArray<FStreak> StreakState;

	/** One spark or burst leaf. Both pools are fixed-size rings, so a busy fight simply recycles the oldest */
	struct FParticle
	{
		FVector Pos = FVector::ZeroVector;
		FVector Vel = FVector::ZeroVector;
		FVector Axis = FVector::UpVector;
		float Age = 1.0f;
		float Life = 0.0f;
		float Size = 1.0f;
	};

	struct FParticlePool
	{
		TArray<FParticle> Particles;
		int32 Next = 0;
		bool bDirty = false;
	};

	void InitPool(FParticlePool& Pool, UInstancedStaticMeshComponent*& Component, FuyuProps::EProp Prop, int32 Count);
	int32 Emit(FParticlePool& Pool, UInstancedStaticMeshComponent* Component, const FParticle& Particle, const FLinearColor& Color);

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> SparkMesh;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> BurstMesh;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> SlashMeshes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<class UMaterialInstanceDynamic>> SlashMaterials;

	struct FSlash
	{
		float Age = 1.0f;
		float Life = 0.0f;
		float Size = 1.0f;
	};
	TArray<FSlash> SlashState;
	int32 NextSlash = 0;

	FParticlePool Sparks;
	FParticlePool BurstLeaves;
	float ScreenPunch = 0.0f;

	FVector WindDirection = FVector::ForwardVector;
	FVector GuideTarget = FVector::ZeroVector;
	float GuideTimeLeft = 0.0f;
	bool bCinemaMode = false;
};
