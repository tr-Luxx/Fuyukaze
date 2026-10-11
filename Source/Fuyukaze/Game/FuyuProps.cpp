#include "FuyuProps.h"
#include "FuyuMeshBuilder.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"

namespace
{
	FFuyuMaterials GMaterials;
	bool bMaterialsLoaded = false;

	TStrongObjectPtr<UStaticMesh> GMeshes[(int32)FuyuProps::EProp::Count];

	UMaterialInterface* LoadMat(const TCHAR* Name)
	{
		const FString Path = FString::Printf(TEXT("/Game/Fuyukaze/Materials/%s.%s"), Name, Name);
		UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, *Path, nullptr, LOAD_NoWarn);
		if (!Mat)
		{
			UE_LOG(LogTemp, Warning, TEXT("Fuyukaze: material %s is missing. Run Tools/build_content.py to generate it."), Name);
			Mat = UMaterial::GetDefaultMaterial(MD_Surface);
		}

		Mat->AddToRoot();
		return Mat;
	}

	FLinearColor Sway(const FLinearColor& C, float Weight)
	{
		return FLinearColor(C.R, C.G, C.B, Weight);
	}

	// palette
	const FLinearColor Bark(0.075f, 0.05f, 0.032f);
	const FLinearColor BarkLight(0.13f, 0.09f, 0.055f);
	const FLinearColor Wood(0.2f, 0.12f, 0.06f);
	const FLinearColor DarkWood(0.07f, 0.045f, 0.03f);
	const FLinearColor Vermilion(0.6f, 0.06f, 0.025f);
	const FLinearColor Stone(0.3f, 0.3f, 0.29f);
	const FLinearColor DarkStone(0.12f, 0.12f, 0.125f);
	const FLinearColor Straw(0.62f, 0.48f, 0.22f);
	const FLinearColor Canvas(0.55f, 0.5f, 0.4f);
	const FLinearColor Lacquer(0.02f, 0.02f, 0.022f);

	void BuildConifer(FFuyuMeshBuilder& B, int32 TrunkSides, int32 Tiers, int32 TierSides)
	{
		B.Slot = 0;
		B.Cylinder(FVector(0, 0, -40), FVector(0, 0, 1150), 30.0f, 7.0f, TrunkSides, Bark, BarkLight, false);

		B.Slot = 1;
		const FLinearColor Low(0.018f, 0.055f, 0.03f);
		const FLinearColor High(0.05f, 0.13f, 0.055f);
		for (int32 i = 0; i < Tiers; ++i)
		{
			const float T = (float)i / FMath::Max(Tiers - 1, 1);
			const float Z = FMath::Lerp(260.0f, 980.0f, T);
			const float R = FMath::Lerp(310.0f, 95.0f, T);
			const float H = FMath::Lerp(420.0f, 300.0f, T);
			B.Cylinder(FVector(0, 0, Z), FVector(0, 0, Z + H), R, 0.0f, TierSides, Sway(FMath::Lerp(Low, High, T * 0.6f), 0.25f + 0.3f * T), Sway(FMath::Lerp(Low, High, 0.4f + T * 0.6f), 0.5f + 0.5f * T), true);
		}
	}

	void BuildBroadleaf(FFuyuMeshBuilder& B, bool bLow, const FLinearColor& LeafLow, const FLinearColor& LeafHigh, float TrunkHeight, float Spread, float BlobRadius, int32 Seed)
	{
		FRandomStream Rng(Seed);

		B.Slot = 0;
		B.Cylinder(FVector(0, 0, -40), FVector(0, 0, TrunkHeight), 34.0f, 16.0f, bLow ? 5 : 8, Bark, BarkLight, false);

		if (!bLow)
		{
			// a few boughs reaching into the canopy
			for (int32 i = 0; i < 3; ++i)
			{
				const float A = (i * 120.0f + Rng.FRandRange(-25.0f, 25.0f)) * PI / 180.0f;
				const FVector Tip(FMath::Cos(A) * Spread * 0.7f, FMath::Sin(A) * Spread * 0.7f, TrunkHeight + 120.0f);
				B.Cylinder(FVector(0, 0, TrunkHeight * 0.72f), Tip, 15.0f, 6.0f, 5, Bark, BarkLight, false);
			}
		}

		B.Slot = 1;
		const int32 Blobs = bLow ? 3 : 7;
		for (int32 i = 0; i < Blobs; ++i)
		{
			const float A = (360.0f * i / Blobs + Rng.FRandRange(-20.0f, 20.0f)) * PI / 180.0f;
			const float Dist = (i == 0) ? 0.0f : Spread * Rng.FRandRange(0.55f, 1.0f);
			const FVector C(FMath::Cos(A) * Dist, FMath::Sin(A) * Dist, TrunkHeight + (i == 0 ? BlobRadius * 0.75f : Rng.FRandRange(-40.0f, 110.0f)));
			const float R = BlobRadius * (i == 0 ? 1.15f : Rng.FRandRange(0.7f, 1.0f)) * (bLow ? 1.35f : 1.0f);
			B.Blob(C, R, bLow ? 4 : 6, bLow ? 6 : 9, 0.42f, 0.78f, LeafLow, LeafHigh, Seed * 31 + i, 0.55f);
		}
	}

	UStaticMesh* MakeTree(FuyuProps::EProp Prop)
	{
		const FFuyuMaterials& M = FFuyuMaterials::Get();
		FFuyuMeshBuilder L0, L1;
		FName Name;

		switch (Prop)
		{
		case FuyuProps::EProp::Pine:
			BuildConifer(L0, 7, 5, 10);
			BuildConifer(L1, 4, 3, 6);
			Name = TEXT("SM_Pine");
			break;
		case FuyuProps::EProp::Ginkgo:
			BuildBroadleaf(L0, false, FLinearColor(0.62f, 0.36f, 0.02f), FLinearColor(0.98f, 0.74f, 0.09f), 520.0f, 260.0f, 230.0f, 11);
			BuildBroadleaf(L1, true, FLinearColor(0.62f, 0.36f, 0.02f), FLinearColor(0.98f, 0.74f, 0.09f), 520.0f, 260.0f, 230.0f, 11);
			Name = TEXT("SM_Ginkgo");
			break;
		default:
			BuildBroadleaf(L0, false, FLinearColor(0.3f, 0.025f, 0.02f), FLinearColor(0.82f, 0.13f, 0.03f), 400.0f, 300.0f, 210.0f, 23);
			BuildBroadleaf(L1, true, FLinearColor(0.3f, 0.025f, 0.02f), FLinearColor(0.82f, 0.13f, 0.03f), 400.0f, 300.0f, 210.0f, 23);
			Name = TEXT("SM_Maple");
			break;
		}

		UStaticMesh* Mesh = FFuyuMeshBuilder::BuildLODs(GetTransientPackage(), Name, { M.Prop, M.Foliage }, { &L0, &L1 }, { 1.0f, 0.16f });
		FFuyuMeshBuilder::AddCapsuleCollision(Mesh, FVector(0, 0, 220), 34.0f, 440.0f);
		FFuyuMeshBuilder::FinishCollision(Mesh);
		return Mesh;
	}

	UStaticMesh* MakeRock(bool bBoulder)
	{
		const FFuyuMaterials& M = FFuyuMaterials::Get();
		FFuyuMeshBuilder B;
		const float R = bBoulder ? 210.0f : 85.0f;
		B.Blob(FVector(0, 0, R * 0.25f), R, 7, 10, 0.55f, 0.72f, DarkStone, Stone, bBoulder ? 5 : 9);

		UStaticMesh* Mesh = B.Build(GetTransientPackage(), bBoulder ? TEXT("SM_Boulder") : TEXT("SM_Rock"), { M.Prop });
		FFuyuMeshBuilder::AddBoxCollision(Mesh, FVector(0, 0, R * 0.25f), FVector(R * 0.72f, R * 0.72f, R * 0.55f));
		FFuyuMeshBuilder::FinishCollision(Mesh);
		return Mesh;
	}

	UStaticMesh* MakeGround(FuyuProps::EProp Prop)
	{
		const FFuyuMaterials& M = FFuyuMaterials::Get();
		FFuyuMeshBuilder B;
		FRandomStream Rng((int32)Prop * 977 + 3);
		FName Name;

		if (Prop == FuyuProps::EProp::GrassClump)
		{
			// colours are multipliers: the instance tint supplies the hue so grass matches the ground beneath
			for (int32 i = 0; i < 11; ++i)
			{
				const FVector Root(Rng.FRandRange(-42.0f, 42.0f), Rng.FRandRange(-42.0f, 42.0f), -4.0f);
				B.Blade(Root, Rng.FRandRange(0.0f, 360.0f), Rng.FRandRange(38.0f, 78.0f), Rng.FRandRange(5.0f, 8.0f), Rng.FRandRange(8.0f, 30.0f), FLinearColor(0.3f, 0.3f, 0.3f), FLinearColor(1.0f, 0.97f, 0.84f), 3);
			}
			Name = TEXT("SM_GrassClump");
		}
		else if (Prop == FuyuProps::EProp::Pampas)
		{
			// susuki: tall arching stems ending in pale plumes
			for (int32 i = 0; i < 7; ++i)
			{
				const float Yaw = Rng.FRandRange(0.0f, 360.0f);
				const FVector Root(Rng.FRandRange(-14.0f, 14.0f), Rng.FRandRange(-14.0f, 14.0f), -4.0f);
				const float H = Rng.FRandRange(130.0f, 205.0f);
				const float Bend = Rng.FRandRange(25.0f, 70.0f);
				B.Blade(Root, Yaw, H, 5.0f, Bend, FLinearColor(0.22f, 0.17f, 0.06f), FLinearColor(0.55f, 0.44f, 0.18f), 4);

				const FVector Fwd = FRotator(0, Yaw, 0).RotateVector(FVector::ForwardVector);
				B.Blade(Root + FVector::UpVector * H * 0.82f + Fwd * Bend * 0.68f, Yaw, H * 0.26f, 5.5f, Bend * 0.5f, Sway(FLinearColor(0.5f, 0.42f, 0.24f), 0.7f), FLinearColor(0.82f, 0.74f, 0.55f), 2);
			}
			Name = TEXT("SM_Pampas");
		}
		else
		{
			const bool bLily = Prop == FuyuProps::EProp::LilyClump;
			const FLinearColor Petal = bLily ? FLinearColor(0.75f, 0.02f, 0.02f) : FLinearColor(0.92f, 0.92f, 0.86f);

			for (int32 i = 0; i < 6; ++i)
			{
				const FVector Root(Rng.FRandRange(-38.0f, 38.0f), Rng.FRandRange(-38.0f, 38.0f), -4.0f);
				const float Yaw = Rng.FRandRange(0.0f, 360.0f);
				const float H = Rng.FRandRange(34.0f, 58.0f) * (bLily ? 1.25f : 1.0f);
				const float Bend = Rng.FRandRange(4.0f, 14.0f);
				B.Blade(Root, Yaw, H, 3.0f, Bend, FLinearColor(0.05f, 0.11f, 0.03f), FLinearColor(0.1f, 0.2f, 0.05f), 2);

				const FVector Head = Root + FVector::UpVector * H + FRotator(0, Yaw, 0).RotateVector(FVector::ForwardVector) * Bend;
				B.Blob(Head, bLily ? 9.0f : 6.5f, 3, 6, 0.0f, bLily ? 0.45f : 0.6f, Petal * 0.75f, Petal, i, 1.0f);
			}
			Name = bLily ? TEXT("SM_LilyClump") : TEXT("SM_FlowerClump");
		}

		return B.Build(GetTransientPackage(), Name, { M.Grass });
	}

	UStaticMesh* MakeTorii()
	{
		const FFuyuMaterials& M = FFuyuMaterials::Get();
		FFuyuMeshBuilder B;
		const float HalfSpan = 215.0f;

		for (float Side : { -1.0f, 1.0f })
		{
			const float Y = Side * HalfSpan;
			B.Cylinder(FVector(0, Y, -30), FVector(0, Y, 34), 40.0f, 36.0f, 10, DarkStone, Stone);
			B.Cylinder(FVector(0, Y, 30), FVector(0, Y * 0.96f, 470), 24.0f, 20.0f, 12, Vermilion * 0.8f, Vermilion, false);
		}

		B.Box(FVector(0, 0, 368), FVector(13, HalfSpan + 55, 17), Vermilion);       // nuki, the tie beam
		B.Box(FVector(0, 0, 420), FVector(9, 16, 36), Vermilion);                    // central strut
		B.Box(FVector(0, 0, 478), FVector(21, HalfSpan + 100, 15), Vermilion);       // shimaki
		B.Box(FVector(0, 0, 503), FVector(27, HalfSpan + 128, 11), Lacquer);         // kasagi, the lintel

		// upturned lintel ends
		for (float Side : { -1.0f, 1.0f })
		{
			B.Xf = FTransform(FRotator(0, 0, Side * -9.0f), FVector(0, Side * (HalfSpan + 150), 512));
			B.Box(FVector::ZeroVector, FVector(27, 42, 11), Lacquer);
		}
		B.Xf = FTransform::Identity;

		UStaticMesh* Mesh = B.Build(GetTransientPackage(), TEXT("SM_Torii"), { M.Prop });
		FFuyuMeshBuilder::AddCapsuleCollision(Mesh, FVector(0, -HalfSpan, 230), 28.0f, 460.0f);
		FFuyuMeshBuilder::AddCapsuleCollision(Mesh, FVector(0, HalfSpan, 230), 28.0f, 460.0f);
		FFuyuMeshBuilder::FinishCollision(Mesh);
		return Mesh;
	}

	UStaticMesh* MakeTent()
	{
		const FFuyuMaterials& M = FFuyuMaterials::Get();
		FFuyuMeshBuilder B;
		const float L = 260.0f, W = 190.0f, H = 235.0f;

		const FVector RidgeA(-L, 0, H), RidgeB(L, 0, H);
		const FVector A0(-L, -W, 0), A1(L, -W, 0), B0(-L, W, 0), B1(L, W, 0);
		const FLinearColor Shade = Canvas * 0.72f;

		// roof panels, drawn on both faces so the inside is not see-through
		B.Quad(A0, A1, RidgeB, RidgeA, Canvas);
		B.Quad(A1, A0, RidgeA, RidgeB, Shade);
		B.Quad(B1, B0, RidgeA, RidgeB, Canvas);
		B.Quad(B0, B1, RidgeB, RidgeA, Shade);

		// back wall, the front is left open
		B.Quad(B0, A0, RidgeA, RidgeA + FVector(0.1f, 0, 0), Shade);
		B.Quad(A0, B0, RidgeA + FVector(0.1f, 0, 0), RidgeA, Canvas);

		B.Cylinder(FVector(-L, 0, -20), FVector(-L, 0, H + 26), 7.0f, 6.0f, 6, DarkWood, Wood);
		B.Cylinder(FVector(L, 0, -20), FVector(L, 0, H + 26), 7.0f, 6.0f, 6, DarkWood, Wood);
		B.Cylinder(RidgeA + FVector(-20, 0, 4), RidgeB + FVector(20, 0, 4), 5.0f, 5.0f, 6, Wood, Wood);

		// bedroll and crate dress the interior
		B.Box(FVector(-90, 0, 9), FVector(95, 38, 9), FLinearColor(0.23f, 0.08f, 0.06f));
		B.Box(FVector(120, -70, 30), FVector(34, 34, 30), Wood);

		UStaticMesh* Mesh = B.Build(GetTransientPackage(), TEXT("SM_Tent"), { M.Prop });
		FFuyuMeshBuilder::AddBoxCollision(Mesh, FVector(-L + 6, 0, H * 0.5f), FVector(8, W, H * 0.5f));
		FFuyuMeshBuilder::AddBoxCollision(Mesh, FVector(0, -W * 0.62f, 60), FVector(L, 20, 60));
		FFuyuMeshBuilder::AddBoxCollision(Mesh, FVector(0, W * 0.62f, 60), FVector(L, 20, 60));
		FFuyuMeshBuilder::FinishCollision(Mesh);
		return Mesh;
	}

	UStaticMesh* MakePalisade()
	{
		const FFuyuMaterials& M = FFuyuMaterials::Get();
		FFuyuMeshBuilder B;
		FRandomStream Rng(77);

		for (int32 i = 0; i < 7; ++i)
		{
			const float Y = -150.0f + i * 50.0f;
			const float H = Rng.FRandRange(250.0f, 300.0f);
			const float R = Rng.FRandRange(19.0f, 24.0f);
			const FLinearColor C = FMath::Lerp(DarkWood, Wood, Rng.FRand());
			B.Cylinder(FVector(0, Y, -40), FVector(Rng.FRandRange(-5.0f, 5.0f), Y, H), R, R * 0.92f, 7, C * 0.7f, C, false);
			B.Cylinder(FVector(0, Y, H), FVector(0, Y, H + 42), R * 0.92f, 0.0f, 7, C, BarkLight, false);
		}

		B.Box(FVector(-22, 0, 175), FVector(5, 172, 9), DarkWood);

		UStaticMesh* Mesh = B.Build(GetTransientPackage(), TEXT("SM_Palisade"), { M.Prop });
		FFuyuMeshBuilder::AddBoxCollision(Mesh, FVector(0, 0, 150), FVector(24, 175, 150));
		FFuyuMeshBuilder::FinishCollision(Mesh);
		return Mesh;
	}

	UStaticMesh* MakeBanner()
	{
		const FFuyuMaterials& M = FFuyuMaterials::Get();
		FFuyuMeshBuilder B;

		B.Slot = 0;
		B.Cylinder(FVector(0, 0, -30), FVector(0, 0, 520), 6.0f, 4.5f, 6, DarkWood, Wood);
		B.Cylinder(FVector(0, 0, 505), FVector(0, 95, 505), 3.0f, 3.0f, 5, Wood, Wood);

		// the cloth hangs from the cross-arm, a tall nobori. Alpha grows towards the free edge so it flutters
		B.Slot = 1;
		const int32 Rows = 10, Cols = 4;
		const float Width = 88.0f, Height = 330.0f;
		TArray<int32> Grid;
		for (int32 r = 0; r <= Rows; ++r)
		{
			for (int32 c = 0; c <= Cols; ++c)
			{
				const float U = (float)c / Cols;
				const float V = (float)r / Rows;
				const bool bCrest = FMath::Abs(V - 0.3f) < 0.11f && FMath::Abs(U - 0.5f) < 0.3f;
				FLinearColor Col = bCrest ? FLinearColor(0.75f, 0.7f, 0.6f) : FLinearColor(0.03f, 0.03f, 0.035f);
				if (V > 0.86f)
				{
					Col = FLinearColor(0.5f, 0.03f, 0.02f);
				}
				Col.A = FMath::Clamp(U * 0.5f + V * 0.8f, 0.0f, 1.0f);
				Grid.Add(B.AddVertex(FVector(0, 6.0f + U * Width, 503.0f - V * Height), FVector::ForwardVector, FVector2D(U, V), Col));
			}
		}

		for (int32 r = 0; r < Rows; ++r)
		{
			for (int32 c = 0; c < Cols; ++c)
			{
				const int32 I = r * (Cols + 1) + c;
				B.AddQuad(Grid[I], Grid[I + 1], Grid[I + Cols + 2], Grid[I + Cols + 1]);
			}
		}

		return B.Build(GetTransientPackage(), TEXT("SM_Banner"), { M.Prop, M.Cloth });
	}

	UStaticMesh* MakeStoneLantern()
	{
		const FFuyuMaterials& M = FFuyuMaterials::Get();
		FFuyuMeshBuilder B;

		B.Slot = 0;
		B.Cylinder(FVector(0, 0, -20), FVector(0, 0, 16), 38.0f, 32.0f, 6, DarkStone, Stone);
		B.Cylinder(FVector(0, 0, 16), FVector(0, 0, 100), 13.0f, 11.0f, 8, Stone, Stone, false);
		B.Cylinder(FVector(0, 0, 100), FVector(0, 0, 112), 30.0f, 30.0f, 6, Stone, Stone);

		for (int32 i = 0; i < 4; ++i)
		{
			const float A = (45.0f + i * 90.0f) * PI / 180.0f;
			B.Box(FVector(FMath::Cos(A) * 20.0f, FMath::Sin(A) * 20.0f, 131), FVector(3.5f, 3.5f, 19), Stone);
		}

		B.Cylinder(FVector(0, 0, 150), FVector(0, 0, 182), 46.0f, 8.0f, 6, DarkStone, Stone);
		B.Blob(FVector(0, 0, 190), 8.0f, 4, 6, 0.0f, 1.2f, Stone, Stone, 1);

		B.Slot = 1;
		B.Box(FVector(0, 0, 131), FVector(13, 13, 15), FLinearColor(1.0f, 0.55f, 0.16f));

		UStaticMesh* Mesh = B.Build(GetTransientPackage(), TEXT("SM_StoneLantern"), { M.Prop, M.Glow });
		FFuyuMeshBuilder::AddCapsuleCollision(Mesh, FVector(0, 0, 90), 28.0f, 180.0f);
		FFuyuMeshBuilder::FinishCollision(Mesh);
		return Mesh;
	}

	UStaticMesh* MakeShrineHall()
	{
		const FFuyuMaterials& M = FFuyuMaterials::Get();
		FFuyuMeshBuilder B;
		const float HX = 330.0f, HY = 430.0f;
		const float Floor = 90.0f, WallTop = 400.0f;
		const FLinearColor Plaster(0.62f, 0.58f, 0.5f);
		const FLinearColor Roof(0.05f, 0.055f, 0.065f);

		// stone plinth, steps, and the raised floor
		B.Box(FVector(0, 0, 20), FVector(HX + 130, HY + 130, 34), Stone);
		for (int32 i = 0; i < 3; ++i)
		{
			B.Box(FVector(-(HX + 150 + i * 38.0f), 0, 46.0f - i * 15.0f), FVector(20, 150, 12), DarkStone);
		}
		B.Box(FVector(0, 0, Floor - 12), FVector(HX + 70, HY + 70, 12), Wood);

		// vermilion posts carry the roof, plaster walls close three sides
		for (float SX : { -1.0f, 1.0f })
		{
			for (float SY : { -1.0f, -0.34f, 0.34f, 1.0f })
			{
				B.Cylinder(FVector(SX * HX, SY * HY, Floor), FVector(SX * HX, SY * HY, WallTop), 17.0f, 17.0f, 8, Vermilion * 0.8f, Vermilion, false);
			}
		}

		B.Box(FVector(HX, 0, (Floor + WallTop) * 0.5f), FVector(6, HY, (WallTop - Floor) * 0.5f), Plaster);
		B.Box(FVector(0, HY, (Floor + WallTop) * 0.5f), FVector(HX, 6, (WallTop - Floor) * 0.5f), Plaster);
		B.Box(FVector(0, -HY, (Floor + WallTop) * 0.5f), FVector(HX, 6, (WallTop - Floor) * 0.5f), Plaster);
		B.Box(FVector(-HX, 0, WallTop - 24), FVector(12, HY + 20, 24), Vermilion);

		// the offering box and an altar glimpsed through the open front
		B.Box(FVector(-HX + 70, 0, Floor + 30), FVector(34, 62, 30), DarkWood);
		B.Box(FVector(HX - 80, 0, Floor + 60), FVector(40, 110, 60), DarkWood);

		// sweeping hipped roof with deep eaves
		const float EX = HX + 190.0f, EY = HY + 190.0f;
		const float Eave = WallTop + 10.0f, Ridge = WallTop + 290.0f, RidgeHalf = HY * 0.55f;
		const FVector E0(-EX, -EY, Eave), E1(EX, -EY, Eave), E2(EX, EY, Eave), E3(-EX, EY, Eave);
		const FVector R0(0, -RidgeHalf, Ridge), R1(0, RidgeHalf, Ridge);
		B.Quad(E0, E3, R1, R0, Roof);
		B.Quad(E2, E1, R0, R1, Roof);
		B.Quad(E1, E0, R0, R0 + FVector(0, 0.1f, 0), Roof);
		B.Quad(E3, E2, R1, R1 + FVector(0, 0.1f, 0), Roof);
		B.Quad(E0, E1, E2, E3, DarkWood);
		B.Box(FVector(0, 0, Ridge + 8), FVector(12, RidgeHalf + 30, 12), Lacquer);

		B.Slot = 1;
		B.Box(FVector(HX - 80, 0, Floor + 138), FVector(10, 10, 18), FLinearColor(1.0f, 0.6f, 0.2f));

		UStaticMesh* Mesh = B.Build(GetTransientPackage(), TEXT("SM_ShrineHall"), { M.Prop, M.Glow });
		FFuyuMeshBuilder::AddBoxCollision(Mesh, FVector(0, 0, 44), FVector(HX + 70, HY + 70, 46));
		FFuyuMeshBuilder::AddBoxCollision(Mesh, FVector(HX, 0, 250), FVector(12, HY, 160));
		FFuyuMeshBuilder::AddBoxCollision(Mesh, FVector(0, HY, 250), FVector(HX, 12, 160));
		FFuyuMeshBuilder::AddBoxCollision(Mesh, FVector(0, -HY, 250), FVector(HX, 12, 160));
		FFuyuMeshBuilder::FinishCollision(Mesh);
		return Mesh;
	}

	UStaticMesh* MakeCampfire()
	{
		const FFuyuMaterials& M = FFuyuMaterials::Get();
		FFuyuMeshBuilder B;

		B.Slot = 0;
		for (int32 i = 0; i < 9; ++i)
		{
			const float A = i * 40.0f * PI / 180.0f;
			B.Blob(FVector(FMath::Cos(A) * 62.0f, FMath::Sin(A) * 62.0f, 4), 17.0f, 4, 6, 0.4f, 0.7f, DarkStone, Stone, i);
		}
		for (int32 i = 0; i < 5; ++i)
		{
			const float A = i * 72.0f * PI / 180.0f;
			B.Cylinder(FVector(FMath::Cos(A) * 46.0f, FMath::Sin(A) * 46.0f, 4), FVector(-FMath::Cos(A) * 8.0f, -FMath::Sin(A) * 8.0f, 46), 8.0f, 6.0f, 6, Lacquer, DarkWood);
		}

		B.Slot = 1;
		B.Cylinder(FVector(0, 0, 8), FVector(0, 0, 92), 26.0f, 0.0f, 7, FLinearColor(1.0f, 0.32f, 0.04f), FLinearColor(1.0f, 0.8f, 0.25f), false);
		B.Cylinder(FVector(12, 8, 8), FVector(16, 10, 60), 15.0f, 0.0f, 6, FLinearColor(1.0f, 0.25f, 0.03f), FLinearColor(1.0f, 0.7f, 0.2f), false);

		return B.Build(GetTransientPackage(), TEXT("SM_Campfire"), { M.Prop, M.Glow });
	}

	/** Weapons are modelled with the grip at the origin and the business end along +X */
	UStaticMesh* MakeWeapon(FuyuProps::EProp Prop)
	{
		const FFuyuMaterials& M = FFuyuMaterials::Get();
		FFuyuMeshBuilder B;
		const FLinearColor Steel(0.75f, 0.77f, 0.8f);

		if (Prop == FuyuProps::EProp::Katana)
		{
			B.Slot = 0;
			B.Cylinder(FVector(-13, 0, 0), FVector(13, 0, 0), 1.9f, 1.7f, 8, FLinearColor(0.02f, 0.02f, 0.03f), FLinearColor(0.07f, 0.02f, 0.02f));
			B.Cylinder(FVector(13, 0, 0), FVector(14.2f, 0, 0), 4.6f, 4.6f, 10, FLinearColor(0.25f, 0.18f, 0.05f), FLinearColor(0.25f, 0.18f, 0.05f));

			// the blade is a row of short segments so it can carry the gentle curve of a katana
			B.Slot = 1;
			const int32 Segs = 8;
			const float Len = 74.0f;
			for (int32 i = 0; i < Segs; ++i)
			{
				const float T0 = (float)i / Segs, T1 = (float)(i + 1) / Segs;
				const FVector P0(14.2f + Len * T0, 0, -3.2f * T0 * T0);
				const FVector P1(14.2f + Len * T1, 0, -3.2f * T1 * T1);
				const float W0 = 1.55f * (1.0f - 0.25f * T0), W1 = (i == Segs - 1) ? 0.2f : 1.55f * (1.0f - 0.25f * T1);
				const FVector Up0(0, 0, W0), Up1(0, 0, W1);
				const FVector Thick(0, 0.28f, 0);
				B.Quad(P0 - Up0, P1 - Up1, P1 + Up1 + Thick, P0 + Up0 + Thick, Steel);
				B.Quad(P1 - Up1, P0 - Up0, P0 + Up0 - Thick, P1 + Up1 - Thick, Steel);
				B.Quad(P0 + Up0 - Thick, P0 + Up0 + Thick, P1 + Up1 + Thick, P1 + Up1 - Thick, Steel * 0.6f);
			}
			return B.Build(GetTransientPackage(), TEXT("SM_Katana"), { M.Prop, M.Metal });
		}

		if (Prop == FuyuProps::EProp::Spear)
		{
			B.Slot = 0;
			B.Cylinder(FVector(-95, 0, 0), FVector(120, 0, 0), 1.9f, 1.7f, 7, DarkWood, Wood);
			B.Cylinder(FVector(104, 0, 0), FVector(120, 0, 0), 2.6f, 2.6f, 7, Vermilion, Vermilion);
			B.Slot = 1;
			B.Cylinder(FVector(120, 0, 0), FVector(158, 0, 0), 3.0f, 0.0f, 4, Steel * 0.7f, Steel, false);
			return B.Build(GetTransientPackage(), TEXT("SM_Spear"), { M.Prop, M.Metal });
		}

		// kanabo: a heavy studded war club
		B.Slot = 0;
		B.Cylinder(FVector(-16, 0, 0), FVector(22, 0, 0), 2.6f, 2.8f, 8, Lacquer, DarkWood);
		B.Cylinder(FVector(22, 0, 0), FVector(112, 0, 0), 4.2f, 8.0f, 8, DarkWood, Bark);
		B.Slot = 1;
		for (int32 i = 0; i < 4; ++i)
		{
			for (int32 j = 0; j < 4; ++j)
			{
				const float X = 40.0f + i * 19.0f;
				const float A = (j * 90.0f + i * 45.0f) * PI / 180.0f;
				const float R = 5.2f + i * 0.9f;
				B.Cylinder(FVector(X, FMath::Cos(A) * R, FMath::Sin(A) * R), FVector(X, FMath::Cos(A) * (R + 3.4f), FMath::Sin(A) * (R + 3.4f)), 1.7f, 0.0f, 5, Steel * 0.5f, Steel, false);
			}
		}
		return B.Build(GetTransientPackage(), TEXT("SM_Club"), { M.Prop, M.Metal });
	}

	UStaticMesh* MakeKasa()
	{
		const FFuyuMaterials& M = FFuyuMaterials::Get();
		FFuyuMeshBuilder B;

		// a wide, shallow straw hat. Drawn inside and out because the camera often looks up under the brim
		B.Cylinder(FVector(0, 0, 0), FVector(0, 0, 13), 31.0f, 0.0f, 16, Straw * 0.72f, Straw, false);
		B.Cylinder(FVector(0, 0, 12.4f), FVector(0, 0, -0.6f), 0.0f, 31.0f, 16, Straw * 0.4f, Straw * 0.5f, false);
		B.Cylinder(FVector(0, 0, 11.5f), FVector(0, 0, 15.5f), 4.0f, 1.5f, 8, Straw * 0.6f, Straw * 0.6f);
		return B.Build(GetTransientPackage(), TEXT("SM_Kasa"), { M.Prop });
	}

	UStaticMesh* MakeCard(bool bLeaf)
	{
		const FFuyuMaterials& M = FFuyuMaterials::Get();
		FFuyuMeshBuilder B;

		if (bLeaf)
		{
			// a small diamond that reads as a drifting ginkgo or maple leaf
			const FLinearColor C(1, 1, 1, 1);
			const FVector P[4] = { FVector(-5, 0, 0), FVector(0, -3.4f, 0), FVector(5, 0, 0), FVector(0, 3.4f, 0) };
			B.Quad(P[0], P[1], P[2], P[3], C);
			B.Quad(P[3], P[2], P[1], P[0], C);
			return B.Build(GetTransientPackage(), TEXT("SM_Leaf"), { M.Foliage });
		}

		// a long ribbon along +X, UV.x runs head to tail so the material can fade both ends
		const int32 Segs = 8;
		TArray<int32> Top, Bot;
		for (int32 i = 0; i <= Segs; ++i)
		{
			const float T = (float)i / Segs;
			Top.Add(B.AddVertex(FVector(T * 100.0f - 50.0f, 0, 0.5f), FVector::RightVector, FVector2D(T, 0), FLinearColor::White));
			Bot.Add(B.AddVertex(FVector(T * 100.0f - 50.0f, 0, -0.5f), FVector::RightVector, FVector2D(T, 1), FLinearColor::White));
		}
		for (int32 i = 0; i < Segs; ++i)
		{
			B.AddQuad(Top[i], Top[i + 1], Bot[i + 1], Bot[i]);
		}

		// a second ribbon lying flat, so the streak still reads when seen end-on from behind
		Top.Reset();
		Bot.Reset();
		for (int32 i = 0; i <= Segs; ++i)
		{
			const float T = (float)i / Segs;
			Top.Add(B.AddVertex(FVector(T * 100.0f - 50.0f, 0.5f, 0), FVector::UpVector, FVector2D(T, 0), FLinearColor::White));
			Bot.Add(B.AddVertex(FVector(T * 100.0f - 50.0f, -0.5f, 0), FVector::UpVector, FVector2D(T, 1), FLinearColor::White));
		}
		for (int32 i = 0; i < Segs; ++i)
		{
			B.AddQuad(Top[i], Top[i + 1], Bot[i + 1], Bot[i]);
		}

		// a second ribbon lying flat, so the streak still reads when seen end-on from behind
		Top.Reset();
		Bot.Reset();
		for (int32 i = 0; i <= Segs; ++i)
		{
			const float T = (float)i / Segs;
			Top.Add(B.AddVertex(FVector(T * 100.0f - 50.0f, 0.5f, 0), FVector::UpVector, FVector2D(T, 0), FLinearColor::White));
			Bot.Add(B.AddVertex(FVector(T * 100.0f - 50.0f, -0.5f, 0), FVector::UpVector, FVector2D(T, 1), FLinearColor::White));
		}
		for (int32 i = 0; i < Segs; ++i)
		{
			B.AddQuad(Top[i], Top[i + 1], Bot[i + 1], Bot[i]);
		}
		return B.Build(GetTransientPackage(), TEXT("SM_WindStreak"), { M.Streak });
	}
}

namespace
{
	UStaticMesh* MakeEffect(FuyuProps::EProp Prop)
	{
		const FFuyuMaterials& M = FFuyuMaterials::Get();
		FFuyuMeshBuilder B;

		if (Prop == FuyuProps::EProp::Spark)
		{
			// a sliver that is stretched along its direction of travel
			const FLinearColor C = FLinearColor::White;
			const FVector P[4] = { FVector(-9, 0, 0), FVector(0, -1.3f, 0), FVector(9, 0, 0), FVector(0, 1.3f, 0) };
			const FVector Q[4] = { FVector(-9, 0, 0), FVector(0, 0, -1.3f), FVector(9, 0, 0), FVector(0, 0, 1.3f) };
			B.Quad(P[0], P[1], P[2], P[3], C);
			B.Quad(Q[0], Q[1], Q[2], Q[3], C);
			return B.Build(GetTransientPackage(), TEXT("SM_Spark"), { M.Glow });
		}

		// a flat crescent sweeping from -80 to +80 degrees about +X. U runs along the arc, V from the inner to the outer edge
		const int32 Segs = 20;
		TArray<int32> Inner, Outer;
		for (int32 i = 0; i <= Segs; ++i)
		{
			const float T = (float)i / Segs;
			const float A = FMath::DegreesToRadians(FMath::Lerp(-80.0f, 80.0f, T));
			const float Taper = FMath::Sin(PI * T);
			const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0.0f);
			Inner.Add(B.AddVertex(Dir * (152.0f - 46.0f * Taper), FVector::UpVector, FVector2D(T, 0), FLinearColor::White));
			Outer.Add(B.AddVertex(Dir * 160.0f, FVector::UpVector, FVector2D(T, 1), FLinearColor::White));
		}
		for (int32 i = 0; i < Segs; ++i)
		{
			B.AddQuad(Inner[i], Inner[i + 1], Outer[i + 1], Outer[i]);
		}
		return B.Build(GetTransientPackage(), TEXT("SM_SlashArc"), { M.Slash });
	}
}

namespace
{
	UStaticMesh* MakeScenery(FuyuProps::EProp Prop)
	{
		const FFuyuMaterials& M = FFuyuMaterials::Get();
		FFuyuMeshBuilder B;

		if (Prop == FuyuProps::EProp::FlowerCarpet)
		{
			// a patch of low leaves crowded with open, five-petalled flowers. Petals are white so that the
			// instance tint alone decides whether this is a blue, violet, yellow or white field
			FRandomStream Rng(515);
			const FLinearColor LeafLow(0.03f, 0.075f, 0.02f), LeafHigh(0.085f, 0.19f, 0.05f);

			for (int32 i = 0; i < 26; ++i)
			{
				const FVector Root(Rng.FRandRange(-46.0f, 46.0f), Rng.FRandRange(-46.0f, 46.0f), -3.0f);
				B.Blade(Root, Rng.FRandRange(0.0f, 360.0f), Rng.FRandRange(14.0f, 30.0f), Rng.FRandRange(3.5f, 6.0f), Rng.FRandRange(6.0f, 16.0f), LeafLow, LeafHigh, 2);
			}

			for (int32 i = 0; i < 24; ++i)
			{
				const float H = Rng.FRandRange(22.0f, 40.0f);
				const FVector Head(Rng.FRandRange(-46.0f, 46.0f), Rng.FRandRange(-46.0f, 46.0f), H);
				const float Size = Rng.FRandRange(3.4f, 5.2f);
				const float Spin = Rng.FRandRange(0.0f, 72.0f);

				// tilt each bloom a little so the field catches the light unevenly
				const FVector Up = (FVector::UpVector + FVector(Rng.FRandRange(-0.35f, 0.35f), Rng.FRandRange(-0.35f, 0.35f), 0.0f)).GetSafeNormal();
				FVector AxisU, AxisV;
				Up.FindBestAxisVectors(AxisU, AxisV);

				const int32 Center = B.AddVertex(Head, Up, FVector2D(0.5, 0.5), FLinearColor(0.55f, 0.5f, 0.2f, 0.8f));
				for (int32 p = 0; p < 5; ++p)
				{
					const float A0 = FMath::DegreesToRadians(Spin + p * 72.0f - 27.0f);
					const float A1 = FMath::DegreesToRadians(Spin + p * 72.0f + 27.0f);
					const float Am = FMath::DegreesToRadians(Spin + p * 72.0f);
					const FLinearColor Petal(1.0f, 1.0f, 1.0f, 0.8f);
					const int32 L = B.AddVertex(Head + (AxisU * FMath::Cos(A0) + AxisV * FMath::Sin(A0)) * Size * 0.72f + Up * 1.2f, Up, FVector2D(0, 0), Petal);
					const int32 R = B.AddVertex(Head + (AxisU * FMath::Cos(A1) + AxisV * FMath::Sin(A1)) * Size * 0.72f + Up * 1.2f, Up, FVector2D(1, 0), Petal);
					const int32 T = B.AddVertex(Head + (AxisU * FMath::Cos(Am) + AxisV * FMath::Sin(Am)) * Size + Up * 2.2f, Up, FVector2D(0.5, 1), Petal);
					B.AddTri(Center, L, R);
					B.AddTri(L, T, R);
				}
			}
			return B.Build(GetTransientPackage(), TEXT("SM_FlowerCarpet"), { M.Flower });
		}

		if (Prop == FuyuProps::EProp::GravePillar)
		{
			// a weathered square marker, slightly tapered, on a wider foot
			const FLinearColor Low(0.2f, 0.23f, 0.2f), High(0.36f, 0.38f, 0.35f);
			B.Box(FVector(0, 0, 9), FVector(34, 34, 12), Low);
			B.Cylinder(FVector(0, 0, 18), FVector(0, 0, 250), 27.0f, 22.0f, 4, Low, High, false);
			B.Cylinder(FVector(0, 0, 250), FVector(0, 0, 268), 22.0f, 6.0f, 4, High, High, true);

			UStaticMesh* Mesh = B.Build(GetTransientPackage(), TEXT("SM_GravePillar"), { M.Prop });
			FFuyuMeshBuilder::AddBoxCollision(Mesh, FVector(0, 0, 130), FVector(22, 22, 130));
			FFuyuMeshBuilder::FinishCollision(Mesh);
			return Mesh;
		}

		if (Prop == FuyuProps::EProp::PathSlab)
		{
			// one irregular paving stone, lying flush with the ground
			const FLinearColor Paving(0.2f, 0.205f, 0.2f);
			const FVector Corners[6] = { FVector(-52, -30, 0), FVector(-8, -44, 0), FVector(50, -28, 0), FVector(56, 22, 0), FVector(6, 42, 0), FVector(-48, 30, 0) };
			const int32 Mid = B.AddVertex(FVector(0, 0, 7), FVector::UpVector, FVector2D(0.5, 0.5), Paving);
			TArray<int32> Rim, Skirt;
			for (const FVector& P : Corners)
			{
				Rim.Add(B.AddVertex(P + FVector(0, 0, 6), FVector::UpVector, FVector2D(0, 0), Paving * 0.9f));
				Skirt.Add(B.AddVertex(P * 1.06f + FVector(0, 0, -4), P.GetSafeNormal(), FVector2D(0, 1), Paving * 0.55f));
			}
			for (int32 i = 0; i < 6; ++i)
			{
				const int32 j = (i + 1) % 6;
				B.AddTri(Mid, Rim[j], Rim[i]);
				B.AddQuad(Rim[i], Rim[j], Skirt[j], Skirt[i]);
			}
			return B.Build(GetTransientPackage(), TEXT("SM_PathSlab"), { M.Prop });
		}

		if (Prop == FuyuProps::EProp::Pagoda)
		{
			// a five-storey pagoda: each storey a little smaller than the one below, under a wide dark roof
			const FLinearColor Timber(0.42f, 0.07f, 0.035f), Daub(0.6f, 0.56f, 0.48f), Tile(0.045f, 0.05f, 0.06f), Gilt(0.7f, 0.5f, 0.12f);
			B.Box(FVector(0, 0, 40), FVector(560, 560, 60), FLinearColor(0.3f, 0.3f, 0.29f));

			float Z = 100.0f;
			for (int32 Tier = 0; Tier < 5; ++Tier)
			{
				const float Half = 360.0f - Tier * 46.0f;
				const float Wall = 300.0f - Tier * 14.0f;
				B.Box(FVector(0, 0, Z + Wall * 0.5f), FVector(Half, Half, Wall * 0.5f), Tier % 2 == 0 ? Daub : Timber);

				for (float SX : { -1.0f, 1.0f })
				{
					for (float SY : { -1.0f, 1.0f })
					{
						B.Box(FVector(SX * Half, SY * Half, Z + Wall * 0.5f), FVector(16, 16, Wall * 0.5f), Timber);
					}
				}

				// the roof sweeps out well past the walls and lifts at the corners
				const float Eave = Half + 250.0f, Base = Z + Wall, Rise = 150.0f, Inner = Half * 0.72f;
				const FVector E[4] = { FVector(-Eave, -Eave, Base + 26), FVector(Eave, -Eave, Base + 26), FVector(Eave, Eave, Base + 26), FVector(-Eave, Eave, Base + 26) };
				const FVector R[4] = { FVector(-Inner, -Inner, Base + Rise), FVector(Inner, -Inner, Base + Rise), FVector(Inner, Inner, Base + Rise), FVector(-Inner, Inner, Base + Rise) };
				for (int32 s = 0; s < 4; ++s)
				{
					const int32 n = (s + 1) % 4;
					const FVector MidEave = (E[s] + E[n]) * 0.5f - FVector(0, 0, 26);
					B.Quad(E[s], MidEave, (R[s] + R[n]) * 0.5f, R[s], Tile);
					B.Quad(MidEave, E[n], R[n], (R[s] + R[n]) * 0.5f, Tile);
				}
				B.Quad(E[0], E[1], E[2], E[3], Timber * 0.5f);
				B.Quad(R[3], R[2], R[1], R[0], Tile);

				Z = Base + Rise - 10.0f;
			}

			// the finial: a slender mast of rings
			B.Cylinder(FVector(0, 0, Z), FVector(0, 0, Z + 620), 14.0f, 5.0f, 8, Gilt, Gilt);
			for (int32 i = 0; i < 7; ++i)
			{
				B.Cylinder(FVector(0, 0, Z + 90 + i * 62), FVector(0, 0, Z + 104 + i * 62), 52.0f - i * 5.0f, 52.0f - i * 5.0f, 10, Gilt, Gilt);
			}

			UStaticMesh* Mesh = B.Build(GetTransientPackage(), TEXT("SM_Pagoda"), { M.Prop });
			FFuyuMeshBuilder::AddBoxCollision(Mesh, FVector(0, 0, 50), FVector(560, 560, 60));
			FFuyuMeshBuilder::AddBoxCollision(Mesh, FVector(0, 0, 900), FVector(360, 360, 800));
			FFuyuMeshBuilder::FinishCollision(Mesh);
			return Mesh;
		}

		if (Prop == FuyuProps::EProp::TrailLine)
		{
			// a streak of light one metre long, as two crossed ribbons so it reads from any side. V is 1 along
			// the spine and 0 at the edges, which the slash material turns into a bright core with a soft falloff
			for (int32 Plane = 0; Plane < 2; ++Plane)
			{
				const FVector Across = Plane == 0 ? FVector(0, 1, 0) : FVector(0, 0, 1);
				const int32 Segs = 12;
				TArray<int32> Rows[3];
				for (int32 i = 0; i <= Segs; ++i)
				{
					const float T = (float)i / Segs;
					for (int32 r = 0; r < 3; ++r)
					{
						Rows[r].Add(B.AddVertex(FVector(T * 100.0f, 0, 0) + Across * (r - 1.0f), FVector::UpVector, FVector2D(T, r == 1 ? 1.0 : 0.0), FLinearColor::White));
					}
				}
				for (int32 i = 0; i < Segs; ++i)
				{
					B.AddQuad(Rows[0][i], Rows[0][i + 1], Rows[1][i + 1], Rows[1][i]);
					B.AddQuad(Rows[1][i], Rows[1][i + 1], Rows[2][i + 1], Rows[2][i]);
				}
			}
			return B.Build(GetTransientPackage(), TEXT("SM_TrailLine"), { M.Slash });
		}

		// a bird seen from far below: a small body and two long wings. Alpha rises towards the wingtips,
		// which the bird material uses to make them beat
		const FLinearColor Body(1, 1, 1, 0), Mid(1, 1, 1, 0.45f), Tip(1, 1, 1, 1);
		const int32 Nose = B.AddVertex(FVector(22, 0, 0), FVector::UpVector, FVector2D(0.5, 0), Body);
		const int32 Tail = B.AddVertex(FVector(-26, 0, 0), FVector::UpVector, FVector2D(0.5, 1), Body);
		for (float Side : { -1.0f, 1.0f })
		{
			const int32 Elbow = B.AddVertex(FVector(4, Side * 30, 0), FVector::UpVector, FVector2D(0.5 + Side * 0.25, 0.4), Mid);
			const int32 Back = B.AddVertex(FVector(-12, Side * 26, 0), FVector::UpVector, FVector2D(0.5 + Side * 0.25, 0.7), Mid);
			const int32 Wing = B.AddVertex(FVector(-8, Side * 64, 0), FVector::UpVector, FVector2D(0.5 + Side * 0.5, 0.5), Tip);
			B.AddTri(Nose, Elbow, Tail);
			B.AddTri(Elbow, Back, Tail);
			B.AddTri(Elbow, Wing, Back);
		}
		return B.Build(GetTransientPackage(), TEXT("SM_Bird"), { M.Bird });
	}
}

void FFuyuMaterials::Load()
{
	Terrain = LoadMat(TEXT("M_FuyuTerrain"));
	Prop = LoadMat(TEXT("M_FuyuProp"));
	Foliage = LoadMat(TEXT("M_FuyuFoliage"));
	Grass = LoadMat(TEXT("M_FuyuGrass"));
	Cloth = LoadMat(TEXT("M_FuyuCloth"));
	Metal = LoadMat(TEXT("M_FuyuMetal"));
	Glow = LoadMat(TEXT("M_FuyuGlow"));
	Water = LoadMat(TEXT("M_FuyuWater"));
	Streak = LoadMat(TEXT("M_FuyuStreak"));
	Slash = LoadMat(TEXT("M_FuyuSlash"));
	Flower = LoadMat(TEXT("M_FuyuFlower"));
	Bird = LoadMat(TEXT("M_FuyuBird"));
	Mist = LoadMat(TEXT("M_FuyuMist"));
}

const FFuyuMaterials& FFuyuMaterials::Get()
{
	if (!bMaterialsLoaded)
	{
		GMaterials.Load();
		bMaterialsLoaded = true;
	}
	return GMaterials;
}

UStaticMesh* FuyuProps::Get(EProp Prop)
{
	TStrongObjectPtr<UStaticMesh>& Cached = GMeshes[(int32)Prop];
	if (Cached.IsValid())
	{
		return Cached.Get();
	}

	UStaticMesh* Mesh = nullptr;
	switch (Prop)
	{
	case EProp::Pine:
	case EProp::Ginkgo:
	case EProp::Maple:
		Mesh = MakeTree(Prop);
		break;
	case EProp::Rock:
		Mesh = MakeRock(false);
		break;
	case EProp::Boulder:
		Mesh = MakeRock(true);
		break;
	case EProp::GrassClump:
	case EProp::FlowerClump:
	case EProp::LilyClump:
	case EProp::Pampas:
		Mesh = MakeGround(Prop);
		break;
	case EProp::Torii:
		Mesh = MakeTorii();
		break;
	case EProp::Tent:
		Mesh = MakeTent();
		break;
	case EProp::Palisade:
		Mesh = MakePalisade();
		break;
	case EProp::Banner:
		Mesh = MakeBanner();
		break;
	case EProp::StoneLantern:
		// prefer the scanned Megascans lantern when it has been imported from Fab
		Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Fab/Megascans/3D/Japanese_Stone_Lantern_ufnscjdga/High/ufnscjdga_tier_1/StaticMeshes/ufnscjdga_tier_1.ufnscjdga_tier_1"), nullptr, LOAD_NoWarn);
		if (!Mesh)
		{
			Mesh = MakeStoneLantern();
		}
		break;
	case EProp::ShrineHall:
		Mesh = MakeShrineHall();
		break;
	case EProp::Campfire:
		Mesh = MakeCampfire();
		break;
	case EProp::Katana:
	case EProp::Spear:
	case EProp::Club:
		Mesh = MakeWeapon(Prop);
		break;
	case EProp::Kasa:
		Mesh = MakeKasa();
		break;
	case EProp::WindStreak:
		Mesh = MakeCard(false);
		break;
	case EProp::Leaf:
		Mesh = MakeCard(true);
		break;
	case EProp::SlashArc:
	case EProp::Spark:
		Mesh = MakeEffect(Prop);
		break;
	case EProp::FlowerCarpet:
	case EProp::GravePillar:
	case EProp::PathSlab:
	case EProp::Pagoda:
	case EProp::Bird:
	case EProp::TrailLine:
		Mesh = MakeScenery(Prop);
		break;
	default:
		break;
	}

	Cached.Reset(Mesh);
	return Mesh;
}

void FuyuProps::Reset()
{
	for (TStrongObjectPtr<UStaticMesh>& Mesh : GMeshes)
	{
		Mesh.Reset();
	}
}
