#include "FuyuMeshBuilder.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "PhysicsEngine/BodySetup.h"
#include "Materials/MaterialInterface.h"
#include "StaticMeshResources.h"

int32 FFuyuMeshBuilder::AddVertex(const FVector& P, const FVector& N, const FVector2D& UV, const FLinearColor& C)
{
	Positions.Add(FVector3f(Xf.TransformPosition(P)));
	Normals.Add(FVector3f(Xf.TransformVectorNoScale(N).GetSafeNormal()));
	UVs.Add(FVector2f(UV));
	Colors.Add(C);
	return Positions.Num() - 1;
}

void FFuyuMeshBuilder::AddTri(int32 A, int32 B, int32 C)
{
	if (A == B || B == C || A == C)
	{
		return;
	}

	while (Sections.Num() <= Slot)
	{
		Sections.AddDefaulted();
	}

	Sections[Slot].Append({ A, B, C });
}

void FFuyuMeshBuilder::AddQuad(int32 A, int32 B, int32 C, int32 D)
{
	AddTri(A, B, C);
	AddTri(A, C, D);
}

void FFuyuMeshBuilder::Quad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FLinearColor& Col)
{
	// Unreal treats (V2 - V0) x (V1 - V0) as the front of a triangle, so the corners are emitted in reverse
	const FVector N = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
	const int32 I0 = AddVertex(A, N, FVector2D(0, 0), Col);
	const int32 I1 = AddVertex(B, N, FVector2D(1, 0), Col);
	const int32 I2 = AddVertex(C, N, FVector2D(1, 1), Col);
	const int32 I3 = AddVertex(D, N, FVector2D(0, 1), Col);
	AddQuad(I0, I3, I2, I1);
}

void FFuyuMeshBuilder::Box(const FVector& Center, const FVector& H, const FLinearColor& Col)
{
	const FVector P[8] = {
		Center + FVector(-H.X, -H.Y, -H.Z), Center + FVector(H.X, -H.Y, -H.Z),
		Center + FVector(H.X, H.Y, -H.Z),   Center + FVector(-H.X, H.Y, -H.Z),
		Center + FVector(-H.X, -H.Y, H.Z),  Center + FVector(H.X, -H.Y, H.Z),
		Center + FVector(H.X, H.Y, H.Z),    Center + FVector(-H.X, H.Y, H.Z)
	};

	Quad(P[4], P[5], P[6], P[7], Col); // top
	Quad(P[3], P[2], P[1], P[0], Col); // bottom
	Quad(P[0], P[1], P[5], P[4], Col); // -Y
	Quad(P[2], P[3], P[7], P[6], Col); // +Y
	Quad(P[1], P[2], P[6], P[5], Col); // +X
	Quad(P[3], P[0], P[4], P[7], Col); // -X
}

void FFuyuMeshBuilder::Cylinder(const FVector& Base, const FVector& Top, float BaseRadius, float TopRadius, int32 Sides, const FLinearColor& BaseCol, const FLinearColor& TopCol, bool bCaps)
{
	const FVector Axis = (Top - Base).GetSafeNormal();
	FVector U, V;
	Axis.FindBestAxisVectors(U, V);

	const float Length = FVector::Dist(Base, Top);
	const float Slope = (BaseRadius - TopRadius) / FMath::Max(Length, KINDA_SMALL_NUMBER);

	TArray<int32> Ring0, Ring1;
	for (int32 i = 0; i <= Sides; ++i)
	{
		const float A = 2.0f * PI * i / Sides;
		const FVector Dir = U * FMath::Cos(A) + V * FMath::Sin(A);
		const FVector N = (Dir + Axis * Slope).GetSafeNormal();
		const float T = (float)i / Sides;
		Ring0.Add(AddVertex(Base + Dir * BaseRadius, N, FVector2D(T, 1), BaseCol));
		Ring1.Add(AddVertex(Top + Dir * TopRadius, N, FVector2D(T, 0), TopCol));
	}

	for (int32 i = 0; i < Sides; ++i)
	{
		if (FVector::DotProduct(FVector::CrossProduct(U, V), Axis) > 0.0f)
		{
			AddQuad(Ring0[i], Ring1[i], Ring1[i + 1], Ring0[i + 1]);
		}
		else
		{
			AddQuad(Ring0[i], Ring0[i + 1], Ring1[i + 1], Ring1[i]);
		}
	}

	if (bCaps)
	{
		for (int32 Cap = 0; Cap < 2; ++Cap)
		{
			const bool bTop = Cap == 1;
			const float R = bTop ? TopRadius : BaseRadius;
			if (R <= KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const FVector C = bTop ? Top : Base;
			const FVector N = bTop ? Axis : -Axis;
			const FLinearColor Col = bTop ? TopCol : BaseCol;
			const int32 CenterIdx = AddVertex(C, N, FVector2D(0.5, 0.5), Col);

			TArray<int32> Rim;
			for (int32 i = 0; i <= Sides; ++i)
			{
				const float A = 2.0f * PI * i / Sides;
				const FVector Dir = U * FMath::Cos(A) + V * FMath::Sin(A);
				Rim.Add(AddVertex(C + Dir * R, N, FVector2D(0.5 + 0.5 * FMath::Cos(A), 0.5 + 0.5 * FMath::Sin(A)), Col));
			}

			const bool bFlip = (FVector::DotProduct(FVector::CrossProduct(U, V), Axis) > 0.0f) == bTop;
			for (int32 i = 0; i < Sides; ++i)
			{
				if (bFlip)
				{
					AddTri(CenterIdx, Rim[i + 1], Rim[i]);
				}
				else
				{
					AddTri(CenterIdx, Rim[i], Rim[i + 1]);
				}
			}
		}
	}
}

void FFuyuMeshBuilder::Blob(const FVector& Center, float Radius, int32 Rings, int32 Sides, float Noise, float Squash, const FLinearColor& LowCol, const FLinearColor& HighCol, int32 Seed, float SwayWeight)
{
	const FVector NoiseOffset(Seed * 13.37f, Seed * 7.13f, Seed * 3.71f);

	TArray<int32> Grid;
	Grid.Reserve((Rings + 1) * (Sides + 1));

	for (int32 r = 0; r <= Rings; ++r)
	{
		const float Phi = PI * r / Rings;
		for (int32 s = 0; s <= Sides; ++s)
		{
			// wrap the seam so both edge columns displace identically
			const float Theta = 2.0f * PI * (s % Sides) / Sides;
			const FVector Dir(FMath::Sin(Phi) * FMath::Cos(Theta), FMath::Sin(Phi) * FMath::Sin(Theta), FMath::Cos(Phi));
			const float Disp = 1.0f + Noise * FMath::PerlinNoise3D(Dir * 1.7f + NoiseOffset);
			FVector P = Dir * Radius * Disp;
			P.Z *= Squash;

			const float Height = 0.5f + 0.5f * Dir.Z;
			FLinearColor Col = FMath::Lerp(LowCol, HighCol, Height);
			Col.A = SwayWeight * (0.4f + 0.6f * Height);

			FVector N = Dir;
			N.Z /= FMath::Max(Squash, 0.1f);
			Grid.Add(AddVertex(Center + P, N.GetSafeNormal(), FVector2D((float)s / Sides, (float)r / Rings), Col));
		}
	}

	for (int32 r = 0; r < Rings; ++r)
	{
		for (int32 s = 0; s < Sides; ++s)
		{
			const int32 A = Grid[r * (Sides + 1) + s];
			const int32 B = Grid[r * (Sides + 1) + s + 1];
			const int32 C = Grid[(r + 1) * (Sides + 1) + s + 1];
			const int32 D = Grid[(r + 1) * (Sides + 1) + s];

			if (r > 0)
			{
				AddTri(A, B, D);
			}
			if (r < Rings - 1)
			{
				AddTri(B, C, D);
			}
		}
	}
}

void FFuyuMeshBuilder::Blade(const FVector& Root, float Yaw, float Height, float Width, float Bend, const FLinearColor& RootCol, const FLinearColor& TipCol, int32 Segments)
{
	const FVector Side = FRotator(0, Yaw, 0).RotateVector(FVector::RightVector);
	const FVector Fwd = FRotator(0, Yaw, 0).RotateVector(FVector::ForwardVector);

	TArray<int32> L, R;
	for (int32 i = 0; i <= Segments; ++i)
	{
		const float T = (float)i / Segments;
		const FVector C = Root + FVector::UpVector * (Height * T) + Fwd * (Bend * T * T);
		const float W = Width * (1.0f - T * 0.85f) * 0.5f;

		FLinearColor Col = FMath::Lerp(RootCol, TipCol, T);
		Col.A = T * T;

		// normals lean upward so grass shades as a soft carpet rather than as individual cards
		const FVector N = (FVector::UpVector * 0.8f + Fwd * 0.2f).GetSafeNormal();
		L.Add(AddVertex(C - Side * W, N, FVector2D(0, 1 - T), Col));
		R.Add(AddVertex(C + Side * W, N, FVector2D(1, 1 - T), Col));
	}

	for (int32 i = 0; i < Segments; ++i)
	{
		AddQuad(L[i], L[i + 1], R[i + 1], R[i]);
	}
}

/** Converts one builder into a mesh description, creating one polygon group per material slot */
static void FuyuFillMeshDescription(const FFuyuMeshBuilder& B, int32 NumSlots, FMeshDescription& MeshDesc)
{
	FStaticMeshAttributes Attributes(MeshDesc);
	Attributes.Register();

	TVertexAttributesRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> InstNormals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> InstTangents = Attributes.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> InstBinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector2f> InstUVs = Attributes.GetVertexInstanceUVs();
	TVertexInstanceAttributesRef<FVector4f> InstColors = Attributes.GetVertexInstanceColors();
	TPolygonGroupAttributesRef<FName> SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();

	InstUVs.SetNumChannels(1);

	MeshDesc.ReserveNewVertices(B.Positions.Num());
	MeshDesc.ReserveNewVertexInstances(B.Positions.Num());

	TArray<FVertexInstanceID> Instances;
	Instances.Reserve(B.Positions.Num());

	for (int32 i = 0; i < B.Positions.Num(); ++i)
	{
		const FVertexID V = MeshDesc.CreateVertex();
		VertexPositions[V] = B.Positions[i];

		const FVertexInstanceID VI = MeshDesc.CreateVertexInstance(V);
		const FVector3f N = B.Normals[i];

		// any stable tangent will do, none of the materials use tangent-space normal maps
		const FVector3f T = FVector3f::CrossProduct(FMath::Abs(N.Z) < 0.99f ? FVector3f::UpVector : FVector3f::ForwardVector, N).GetSafeNormal();

		InstNormals[VI] = N;
		InstTangents[VI] = T;
		InstBinormalSigns[VI] = 1.0f;
		InstUVs.Set(VI, 0, B.UVs[i]);
		InstColors[VI] = FVector4f(B.Colors[i].R, B.Colors[i].G, B.Colors[i].B, B.Colors[i].A);
		Instances.Add(VI);
	}

	for (int32 s = 0; s < NumSlots; ++s)
	{
		const FPolygonGroupID Group = MeshDesc.CreatePolygonGroup();
		SlotNames[Group] = FName(*FString::Printf(TEXT("Slot%d"), s));

		if (!B.Sections.IsValidIndex(s))
		{
			continue;
		}

		const TArray<int32>& Idx = B.Sections[s];
		for (int32 t = 0; t + 2 < Idx.Num(); t += 3)
		{
			MeshDesc.CreateTriangle(Group, { Instances[Idx[t]], Instances[Idx[t + 1]], Instances[Idx[t + 2]] });
		}
	}
}

UStaticMesh* FFuyuMeshBuilder::Build(UObject* Outer, FName Name, const TArray<UMaterialInterface*>& Materials) const
{
	return BuildLODs(Outer, Name, Materials, { this }, { 1.0f });
}

UStaticMesh* FFuyuMeshBuilder::BuildLODs(UObject* Outer, FName Name, const TArray<UMaterialInterface*>& Materials, const TArray<const FFuyuMeshBuilder*>& LODs, const TArray<float>& ScreenSizes)
{
	int32 NumSlots = 1;
	for (const FFuyuMeshBuilder* LOD : LODs)
	{
		NumSlots = FMath::Max(NumSlots, LOD->Sections.Num());
	}

	TArray<FMeshDescription> Descs;
	Descs.SetNum(LODs.Num());

	TArray<const FMeshDescription*> DescPtrs;
	for (int32 i = 0; i < LODs.Num(); ++i)
	{
		FuyuFillMeshDescription(*LODs[i], NumSlots, Descs[i]);
		DescPtrs.Add(&Descs[i]);
	}

	UStaticMesh* Mesh = NewObject<UStaticMesh>(Outer, MakeUniqueObjectName(Outer, UStaticMesh::StaticClass(), Name), RF_Transient);

	for (int32 s = 0; s < NumSlots; ++s)
	{
		const FName SlotName(*FString::Printf(TEXT("Slot%d"), s));
		Mesh->GetStaticMaterials().Add(FStaticMaterial(Materials.IsValidIndex(s) ? Materials[s] : nullptr, SlotName, SlotName));
	}

	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	Params.bFastBuild = true;
	Params.bCommitMeshDescription = false;
	Mesh->BuildFromMeshDescriptions(DescPtrs, Params);

	if (FStaticMeshRenderData* RenderData = Mesh->GetRenderData())
	{
		for (int32 i = 0; i < LODs.Num() && i < MAX_STATIC_MESH_LODS; ++i)
		{
			RenderData->ScreenSize[i].Default = ScreenSizes.IsValidIndex(i) ? ScreenSizes[i] : 0.0f;
		}
	}

	return Mesh;
}
static UBodySetup* FuyuGetBodySetup(UStaticMesh* Mesh)
{
	if (!Mesh->GetBodySetup())
	{
		Mesh->CreateBodySetup();
	}

	UBodySetup* Body = Mesh->GetBodySetup();
	Body->CollisionTraceFlag = CTF_UseSimpleAsComplex;
	return Body;
}

void FFuyuMeshBuilder::AddBoxCollision(UStaticMesh* Mesh, const FVector& Center, const FVector& HalfExtent)
{
	FKBoxElem Elem(HalfExtent.X * 2.0f, HalfExtent.Y * 2.0f, HalfExtent.Z * 2.0f);
	Elem.Center = Center;
	FuyuGetBodySetup(Mesh)->AggGeom.BoxElems.Add(Elem);
}

void FFuyuMeshBuilder::AddCapsuleCollision(UStaticMesh* Mesh, const FVector& Center, float Radius, float Length)
{
	FKSphylElem Elem(Radius, Length);
	Elem.Center = Center;
	FuyuGetBodySetup(Mesh)->AggGeom.SphylElems.Add(Elem);
}

void FFuyuMeshBuilder::FinishCollision(UStaticMesh* Mesh)
{
	UBodySetup* Body = FuyuGetBodySetup(Mesh);
	Body->InvalidatePhysicsData();
	Body->CreatePhysicsMeshes();
}
