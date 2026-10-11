// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Fuyukaze : ModuleRules
{
	public Fuyukaze(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"SlateCore",
			"Niagara",
			"PhysicsCore",
			"ProceduralMeshComponent",
			"MeshDescription",
			"StaticMeshDescription"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Fuyukaze",
			"Fuyukaze/Variant_Combat",
			"Fuyukaze/Variant_Combat/AI",
			"Fuyukaze/Variant_Combat/Animation",
			"Fuyukaze/Variant_Combat/Gameplay",
			"Fuyukaze/Variant_Combat/Interfaces",
			"Fuyukaze/Variant_Combat/UI",
			"Fuyukaze/Game"
		});
	}
}
