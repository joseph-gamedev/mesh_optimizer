#include "MeshOptimizationProcessor.h"

#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "IMeshReductionInterfaces.h"
#include "IMeshReductionManagerModule.h"
#include "IMeshMergeUtilities.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMeshToMeshDescription.h"
#include "MeshConstraintsUtil.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "MeshSimplification.h"
#include "MeshMergeModule.h"
#include "MeshMerge/MeshMergingSettings.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/PackageName.h"
#include "OverlappingCorners.h"
#include "ScopedTransaction.h"
#include "StaticMeshResources.h"

#define LOCTEXT_NAMESPACE "MeshOptimizationProcessor"

uint8 UMeshOptimizationProcessor::ToEngineImportance(EMeshOptImportance Level)
{
	switch (Level)
	{
	case EMeshOptImportance::Off:    return EMeshFeatureImportance::Off;
	case EMeshOptImportance::Low:    return EMeshFeatureImportance::Low;
	case EMeshOptImportance::Normal: return EMeshFeatureImportance::Normal;
	case EMeshOptImportance::High:   return EMeshFeatureImportance::High;
	default:                       return EMeshFeatureImportance::Normal;
	}
}

namespace
{
struct FScopedNaniteSourceEdit
{
	explicit FScopedNaniteSourceEdit(UStaticMesh* InMesh)
		: Mesh(InMesh)
	{
		if (!Mesh)
		{
			return;
		}

		OriginalSettings = Mesh->GetNaniteSettings();
		bWasNaniteEnabled = OriginalSettings.bEnabled;
		if (bWasNaniteEnabled)
		{
			FMeshNaniteSettings DisabledSettings = OriginalSettings;
			DisabledSettings.bEnabled = false;
			Mesh->SetNaniteSettings(DisabledSettings);
		}
	}

	void Restore() const
	{
		if (Mesh && bWasNaniteEnabled)
		{
			Mesh->SetNaniteSettings(OriginalSettings);
		}
	}

private:
	UStaticMesh* Mesh = nullptr;
	FMeshNaniteSettings OriginalSettings;
	bool bWasNaniteEnabled = false;
};

struct FSafePlanarVertexData
{
	bool bPlanarInterior = false;
	float MinNeighborDistance = TNumericLimits<float>::Max();
	FVector3f AverageNormal = FVector3f::ZeroVector;
};

static bool TryGetTriangleNormal(
	const FMeshDescription& MeshDesc,
	TVertexAttributesConstRef<FVector3f> VertexPositions,
	const FTriangleID TriangleID,
	FVector3f& OutNormal)
{
	const TArrayView<const FVertexInstanceID> TriangleVerts = MeshDesc.GetTriangleVertexInstances(TriangleID);
	if (TriangleVerts.Num() < 3)
	{
		return false;
	}

	const FVector3f P0 = VertexPositions[MeshDesc.GetVertexInstanceVertex(TriangleVerts[0])];
	const FVector3f P1 = VertexPositions[MeshDesc.GetVertexInstanceVertex(TriangleVerts[1])];
	const FVector3f P2 = VertexPositions[MeshDesc.GetVertexInstanceVertex(TriangleVerts[2])];

	OutNormal = FVector3f::CrossProduct(P1 - P0, P2 - P0);
	if (OutNormal.SizeSquared() <= UE_SMALL_NUMBER)
	{
		return false;
	}

	OutNormal.Normalize();
	return true;
}

static TMap<FVertexID, FSafePlanarVertexData> BuildSafePlanarVertexData(
	const FMeshDescription& MeshDesc,
	TVertexAttributesConstRef<FVector3f> VertexPositions,
	const float NormalToleranceDegrees)
{
	TMap<FVertexID, FSafePlanarVertexData> Result;
	const float NormalDotThreshold = FMath::Cos(FMath::DegreesToRadians(NormalToleranceDegrees));

	for (const FVertexID VertexID : MeshDesc.Vertices().GetElementIDs())
	{
		FSafePlanarVertexData Data;

		const TArrayView<const FEdgeID> ConnectedEdges = MeshDesc.GetVertexConnectedEdgeIDs(VertexID);
		bool bHasBoundaryEdge = false;
		for (const FEdgeID EdgeID : ConnectedEdges)
		{
			if (MeshDesc.GetNumEdgeConnectedPolygons(EdgeID) <= 1)
			{
				bHasBoundaryEdge = true;
				break;
			}
		}

		const TArray<FTriangleID> ConnectedTriangles = MeshDesc.GetVertexConnectedTriangles(VertexID);
		const TArray<FVertexID> AdjacentVertices = MeshDesc.GetVertexAdjacentVertices(VertexID);
		const FVector3f VertexPosition = VertexPositions[VertexID];

		for (const FVertexID AdjacentVertexID : AdjacentVertices)
		{
			const float Distance = FVector3f::Distance(VertexPosition, VertexPositions[AdjacentVertexID]);
			if (Distance > UE_SMALL_NUMBER)
			{
				Data.MinNeighborDistance = FMath::Min(Data.MinNeighborDistance, Distance);
			}
		}

		TArray<FVector3f> TriangleNormals;
		for (const FTriangleID TriangleID : ConnectedTriangles)
		{
			FVector3f TriangleNormal;
			if (TryGetTriangleNormal(MeshDesc, VertexPositions, TriangleID, TriangleNormal))
			{
				TriangleNormals.Add(TriangleNormal);
			}
		}

		bool bPlanar = !bHasBoundaryEdge && TriangleNormals.Num() >= 4;
		if (bPlanar)
		{
			const FVector3f ReferenceNormal = TriangleNormals[0];
			for (const FVector3f& TriangleNormal : TriangleNormals)
			{
				if (FVector3f::DotProduct(ReferenceNormal, TriangleNormal) < NormalDotThreshold)
				{
					bPlanar = false;
					break;
				}
			}

			if (bPlanar)
			{
				FVector3f AverageNormal = FVector3f::ZeroVector;
				for (const FVector3f& TriangleNormal : TriangleNormals)
				{
					AverageNormal += TriangleNormal;
				}

				if (AverageNormal.SizeSquared() > UE_SMALL_NUMBER)
				{
					AverageNormal.Normalize();
					Data.AverageNormal = AverageNormal;
				}
				else
				{
					bPlanar = false;
				}
			}
		}

		Data.bPlanarInterior = bPlanar && Data.MinNeighborDistance < TNumericLimits<float>::Max();
		Result.Add(VertexID, Data);
	}

	return Result;
}

static int32 EstimateDegenerateTriangles(
	const FMeshDescription& MeshDesc,
	TVertexAttributesConstRef<FVector3f> VertexPositions,
	const TMap<FVertexID, FVector3f>& OverridePositions)
{
	auto ResolvePosition = [&OverridePositions, &VertexPositions](const FVertexID VertexID) -> FVector3f
	{
		if (const FVector3f* Override = OverridePositions.Find(VertexID))
		{
			return *Override;
		}
		return VertexPositions[VertexID];
	};

	int32 DegenerateCount = 0;
	for (const FTriangleID TriangleID : MeshDesc.Triangles().GetElementIDs())
	{
		const TArrayView<const FVertexInstanceID> TriangleVerts = MeshDesc.GetTriangleVertexInstances(TriangleID);
		if (TriangleVerts.Num() < 3)
		{
			continue;
		}

		const FVector3f P0 = ResolvePosition(MeshDesc.GetVertexInstanceVertex(TriangleVerts[0]));
		const FVector3f P1 = ResolvePosition(MeshDesc.GetVertexInstanceVertex(TriangleVerts[1]));
		const FVector3f P2 = ResolvePosition(MeshDesc.GetVertexInstanceVertex(TriangleVerts[2]));
		const FVector3f TriangleCross = FVector3f::CrossProduct(P1 - P0, P2 - P0);

		if (TriangleCross.SizeSquared() <= UE_SMALL_NUMBER)
		{
			++DegenerateCount;
		}
	}

	return DegenerateCount;
}

static int32 GetBuiltTriangleCount(const UStaticMesh* Mesh)
{
	if (!Mesh || !Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.Num() == 0)
	{
		return 0;
	}

	return Mesh->GetRenderData()->LODResources[0].GetNumTriangles();
}

static TMap<FVertexID, FVector3f> SnapshotVertexPositions(const FMeshDescription& MeshDesc, TVertexAttributesConstRef<FVector3f> VertexPositions)
{
	TMap<FVertexID, FVector3f> Snapshot;
	for (const FVertexID VertexID : MeshDesc.Vertices().GetElementIDs())
	{
		Snapshot.Add(VertexID, VertexPositions[VertexID]);
	}
	return Snapshot;
}

static void RestoreVertexPositions(TVertexAttributesRef<FVector3f> VertexPositions, const TMap<FVertexID, FVector3f>& Snapshot)
{
	for (const TPair<FVertexID, FVector3f>& Pair : Snapshot)
	{
		VertexPositions[Pair.Key] = Pair.Value;
	}
}

static TMap<FVertexID, FVector3f> BuildClusterOverrides(
	const TMap<FVertexID, FSafePlanarVertexData>& VertexData,
	TVertexAttributesConstRef<FVector3f> VertexPositions,
	const float Factor,
	const float NormalDotThreshold,
	const float PlaneOffsetScale)
{
	TMap<FIntVector, TArray<FVertexID>> SpatialHash;
	TMap<FVertexID, FVector3f> OverridePositions;

	for (const TPair<FVertexID, FSafePlanarVertexData>& Pair : VertexData)
	{
		if (!Pair.Value.bPlanarInterior)
		{
			continue;
		}

		const float CellSize = FMath::Max(Pair.Value.MinNeighborDistance * Factor, 0.001f);
		const FVector3f Position = VertexPositions[Pair.Key];
		const FIntVector Cell(
			FMath::FloorToInt(Position.X / CellSize),
			FMath::FloorToInt(Position.Y / CellSize),
			FMath::FloorToInt(Position.Z / CellSize));

		SpatialHash.FindOrAdd(Cell).Add(Pair.Key);
	}

	for (const TPair<FVertexID, FSafePlanarVertexData>& Pair : VertexData)
	{
		const FVertexID VertexID = Pair.Key;
		const FSafePlanarVertexData& Data = Pair.Value;
		if (!Data.bPlanarInterior)
		{
			continue;
		}

		const FVector3f Position = VertexPositions[VertexID];
		const float SearchRadius = FMath::Max(Data.MinNeighborDistance * Factor, 0.001f);
		const FIntVector BaseCell(
			FMath::FloorToInt(Position.X / SearchRadius),
			FMath::FloorToInt(Position.Y / SearchRadius),
			FMath::FloorToInt(Position.Z / SearchRadius));

		float BestDistanceSq = TNumericLimits<float>::Max();
		FVertexID BestTarget = VertexID;

		for (int32 X = -1; X <= 1; ++X)
		{
			for (int32 Y = -1; Y <= 1; ++Y)
			{
				for (int32 Z = -1; Z <= 1; ++Z)
				{
					const FIntVector NeighborCell = BaseCell + FIntVector(X, Y, Z);
					const TArray<FVertexID>* Bucket = SpatialHash.Find(NeighborCell);
					if (!Bucket)
					{
						continue;
					}

					for (const FVertexID OtherVertexID : *Bucket)
					{
						if (OtherVertexID == VertexID || OtherVertexID.GetValue() >= VertexID.GetValue())
						{
							continue;
						}

						const FSafePlanarVertexData* OtherData = VertexData.Find(OtherVertexID);
						if (!OtherData || !OtherData->bPlanarInterior)
						{
							continue;
						}

						if (FVector3f::DotProduct(Data.AverageNormal, OtherData->AverageNormal) < NormalDotThreshold)
						{
							continue;
						}

						const FVector3f Delta = Position - VertexPositions[OtherVertexID];
						const float DistanceSq = Delta.SizeSquared();
						const float PairRadius = FMath::Min(Data.MinNeighborDistance, OtherData->MinNeighborDistance) * Factor;
						if (DistanceSq > FMath::Square(PairRadius) || DistanceSq >= BestDistanceSq)
						{
							continue;
						}

						const float PlaneOffset = FMath::Abs(FVector3f::DotProduct(Data.AverageNormal, Delta));
						if (PlaneOffset > PairRadius * PlaneOffsetScale)
						{
							continue;
						}

						BestDistanceSq = DistanceSq;
						BestTarget = OtherVertexID;
					}
				}
			}
		}

		if (BestTarget != VertexID)
		{
			OverridePositions.Add(VertexID, VertexPositions[BestTarget]);
		}
	}

	return OverridePositions;
}

static FString GetBackupPackageName(const UStaticMesh* Mesh)
{
	if (!Mesh)
	{
		return FString();
	}

	const FString SourcePackageName = Mesh->GetOutermost()->GetName();
	if (SourcePackageName.StartsWith(TEXT("/Game/")))
	{
		return FString::Printf(TEXT("/Game/__MeshOptimizerBackups/%s__MeshOptimizerBackup"), *SourcePackageName.RightChop(6));
	}

	return FString::Printf(TEXT("%s__MeshOptimizerBackup"), *SourcePackageName);
}

static UStaticMesh* FindBackupMesh(const UStaticMesh* Mesh)
{
	if (!Mesh)
	{
		return nullptr;
	}

	const FString BackupPackageName = GetBackupPackageName(Mesh);
	const FString BackupAssetName = FPackageName::GetLongPackageAssetName(BackupPackageName);
	const FString BackupObjectPath = FString::Printf(TEXT("%s.%s"), *BackupPackageName, *BackupAssetName);

	if (UStaticMesh* ExistingBackup = FindObject<UStaticMesh>(nullptr, *BackupObjectPath))
	{
		return ExistingBackup;
	}

	return LoadObject<UStaticMesh>(nullptr, *BackupObjectPath);
}

static void CopyStaticMeshState(UStaticMesh* TargetMesh, const UStaticMesh* SourceMesh)
{
	check(TargetMesh);
	check(SourceMesh);

	TargetMesh->SetNumSourceModels(SourceMesh->GetNumSourceModels());
	for (int32 LODIndex = 0; LODIndex < SourceMesh->GetNumSourceModels(); ++LODIndex)
	{
		FMeshDescription MeshDescriptionCopy;
		if (SourceMesh->CloneMeshDescription(LODIndex, MeshDescriptionCopy))
		{
			TargetMesh->CreateMeshDescription(LODIndex, MoveTemp(MeshDescriptionCopy));
			TargetMesh->CommitMeshDescription(LODIndex);
		}

		const FStaticMeshSourceModel& SourceModel = SourceMesh->GetSourceModel(LODIndex);
		FStaticMeshSourceModel& TargetModel = TargetMesh->GetSourceModel(LODIndex);
		TargetModel.BuildSettings = SourceModel.BuildSettings;
		TargetModel.ReductionSettings = SourceModel.ReductionSettings;
		TargetModel.ScreenSize = SourceModel.ScreenSize;
		TargetModel.SourceImportFilename = SourceModel.SourceImportFilename;
#if WITH_EDITORONLY_DATA
		TargetModel.bImportWithBaseMesh = SourceModel.bImportWithBaseMesh;
#endif
	}

	TargetMesh->SetStaticMaterials(SourceMesh->GetStaticMaterials());
	TargetMesh->GetSectionInfoMap() = SourceMesh->GetSectionInfoMap();
	TargetMesh->GetOriginalSectionInfoMap() = SourceMesh->GetOriginalSectionInfoMap();
	TargetMesh->SetLightMapResolution(SourceMesh->GetLightMapResolution());
	TargetMesh->SetNaniteSettings(SourceMesh->GetNaniteSettings());
#if WITH_EDITORONLY_DATA
		TargetMesh->ComplexCollisionMesh = SourceMesh->ComplexCollisionMesh;
#endif

	if (const UBodySetup* SourceBodySetup = SourceMesh->GetBodySetup())
	{
		TargetMesh->CreateBodySetup();
		if (UBodySetup* TargetBodySetup = TargetMesh->GetBodySetup())
		{
			TargetBodySetup->Modify();
			UBodySetup* DuplicatedBodySetup = DuplicateObject<UBodySetup>(SourceBodySetup, TargetMesh, *SourceBodySetup->GetName());
			TargetMesh->SetBodySetup(DuplicatedBodySetup);
		}
	}
}
}

// ─── Polygon Reduction ──────────────────────────────────────────────

bool UMeshOptimizationProcessor::HasOriginalBackup(UStaticMesh* Mesh) const
{
	return FindBackupMesh(Mesh) != nullptr;
}

UStaticMesh* UMeshOptimizationProcessor::GetOptimizationBaseMesh(UStaticMesh* Mesh) const
{
	if (UStaticMesh* BackupMesh = FindBackupMesh(Mesh))
	{
		return BackupMesh;
	}

	return Mesh;
}

bool UMeshOptimizationProcessor::EnsureOriginalBackup(UStaticMesh* Mesh)
{
	if (!Mesh)
	{
		return false;
	}

	if (FindBackupMesh(Mesh))
	{
		return true;
	}

	const FString BackupPackageName = GetBackupPackageName(Mesh);
	if (BackupPackageName.IsEmpty())
	{
		return false;
	}

	UPackage* BackupPackage = CreatePackage(*BackupPackageName);
	if (!BackupPackage)
	{
		return false;
	}

	const FString BackupAssetName = FPackageName::GetLongPackageAssetName(BackupPackageName);
	UStaticMesh* BackupMesh = DuplicateObject<UStaticMesh>(Mesh, BackupPackage, *BackupAssetName);
	if (!BackupMesh)
	{
		return false;
	}

	BackupMesh->SetFlags(RF_Public | RF_Standalone);
	BackupPackage->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(BackupMesh);
	return true;
}

bool UMeshOptimizationProcessor::RestoreOriginalMesh(UStaticMesh* Mesh)
{
	if (!Mesh)
	{
		return false;
	}

	const UStaticMesh* BackupMesh = FindBackupMesh(Mesh);
	if (!BackupMesh)
	{
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("RestoreOriginalMesh", "Restore Original Mesh"));
	Mesh->Modify();
	Mesh->ModifyAllMeshDescriptions();
	if (UBodySetup* BodySetup = Mesh->GetBodySetup())
	{
		BodySetup->Modify();
	}

	CopyStaticMeshState(Mesh, BackupMesh);
	Mesh->Build(false);
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();
	return true;
}

bool UMeshOptimizationProcessor::ReduceMesh(UStaticMesh* Mesh, const FMeshReductionConfig& Config)
{
	if (!Mesh || !Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.Num() == 0)
	{
		return false;
	}

	if (!EnsureOriginalBackup(Mesh))
	{
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("ReduceMesh", "Reduce Static Mesh"));
	Mesh->Modify();
	Mesh->ModifyMeshDescription(0);
	FScopedNaniteSourceEdit NaniteSourceEdit(Mesh);

	// Ensure we have at least 1 source model
	if (Mesh->GetNumSourceModels() == 0)
	{
		return false;
	}

	FMeshDescription* MeshDesc = Mesh->GetMeshDescription(0);
	if (!MeshDesc)
	{
		return false;
	}

	FStaticMeshAttributes(*MeshDesc).Register();

	FStaticMeshSourceModel& SourceModel = Mesh->GetSourceModel(0);
	FMeshBuildSettings& BuildSettings = SourceModel.BuildSettings;
	BuildSettings.bRecomputeNormals = Config.bRecalculateNormals;
	BuildSettings.bRecomputeTangents = Config.bRecalculateNormals;
	BuildSettings.bRemoveDegenerates = true;

	FMeshReductionSettings ReductionSettings = SourceModel.ReductionSettings;
	ReductionSettings.PercentTriangles = Config.PercentTriangles;
	ReductionSettings.MaxDeviation = Config.MaxDeviation;
	ReductionSettings.WeldingThreshold = Config.WeldingThreshold;
	ReductionSettings.bRecalculateNormals = Config.bRecalculateNormals;
	ReductionSettings.HardAngleThreshold = Config.HardAngleThreshold;
	ReductionSettings.SilhouetteImportance = static_cast<EMeshFeatureImportance::Type>(ToEngineImportance(Config.SilhouetteImportance));
	ReductionSettings.TextureImportance = static_cast<EMeshFeatureImportance::Type>(ToEngineImportance(Config.TextureImportance));
	ReductionSettings.ShadingImportance = static_cast<EMeshFeatureImportance::Type>(ToEngineImportance(Config.ShadingImportance));
	ReductionSettings.BaseLODModel = 0;
	ReductionSettings.TerminationCriterion = EStaticMeshReductionTerimationCriterion::Triangles;

	if (Config.bUseMaxDeviation)
	{
		ReductionSettings.MaxDeviation = Config.MaxDeviation;
	}

	IMeshReduction* MeshReduction = FModuleManager::Get()
		.LoadModuleChecked<IMeshReductionManagerModule>("MeshReductionInterface")
		.GetStaticMeshReductionInterface();
	if (!MeshReduction || !MeshReduction->IsSupported())
	{
		return false;
	}

	FOverlappingCorners OverlappingCorners;
	FStaticMeshOperations::FindOverlappingCorners(OverlappingCorners, *MeshDesc, UE_THRESH_POINTS_ARE_SAME);

	FMeshDescription ReducedMeshDescription;
	FStaticMeshAttributes(ReducedMeshDescription).Register();
	float OutMaxDeviation = ReductionSettings.MaxDeviation;
	MeshReduction->ReduceMeshDescription(ReducedMeshDescription, OutMaxDeviation, *MeshDesc, OverlappingCorners, ReductionSettings);

	if (ReducedMeshDescription.Triangles().Num() <= 0)
	{
		return false;
	}

	Mesh->CreateMeshDescription(0, MoveTemp(ReducedMeshDescription));
	Mesh->CommitMeshDescription(0);

	NaniteSourceEdit.Restore();
	Mesh->Build(false);
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();

	return true;
}

int32 UMeshOptimizationProcessor::BatchReduceMeshes(const TArray<FMeshAnalysisResult>& Meshes, const FMeshReductionConfig& Config)
{
	int32 ProcessedCount = 0;

	FScopedSlowTask SlowTask(Meshes.Num(), LOCTEXT("BatchReduce", "Batch reducing meshes..."));
	SlowTask.MakeDialog(true);

	for (const FMeshAnalysisResult& MeshInfo : Meshes)
	{
		if (SlowTask.ShouldCancel())
		{
			break;
		}
		SlowTask.EnterProgressFrame(1.0f, FText::FromString(FString::Printf(TEXT("Reducing: %s"), *MeshInfo.MeshName)));

		if (MeshInfo.StaticMesh.IsValid() && ReduceMesh(MeshInfo.StaticMesh.Get(), Config))
		{
			ProcessedCount++;
		}
	}

	return ProcessedCount;
}

// ─── LOD Generation ─────────────────────────────────────────────────

bool UMeshOptimizationProcessor::GenerateLODs(UStaticMesh* Mesh, const FLODGenerationConfig& Config)
{
	if (!Mesh)
	{
		return false;
	}

	if (!EnsureOriginalBackup(Mesh))
	{
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("GenerateLODs", "Generate LODs"));
	Mesh->Modify();

	int32 TotalLODs = FMath::Clamp(Config.NumLODs + 1, 2, 8); // +1 because LOD0 is the base
	Mesh->SetNumSourceModels(TotalLODs);

	// Configure each LOD level
	float PercentTriangles[] = { 1.0f, Config.LOD1PercentTriangles, Config.LOD2PercentTriangles, Config.LOD3PercentTriangles, 0.06f, 0.03f, 0.015f };
	float ScreenSizes[] = { 1.0f, Config.LOD1ScreenSize, Config.LOD2ScreenSize, Config.LOD3ScreenSize, 0.05f, 0.025f, 0.01f };

	for (int32 LODIndex = 1; LODIndex < TotalLODs; ++LODIndex)
	{
		FStaticMeshSourceModel& SourceModel = Mesh->GetSourceModel(LODIndex);
		FMeshReductionSettings& ReductionSettings = SourceModel.ReductionSettings;

		int32 ConfigIndex = FMath::Min(LODIndex, 6);
		if (Config.bAutoComputeScreenSizes)
		{
			ReductionSettings.PercentTriangles = 1.0f / FMath::Pow(2.0f, (float)LODIndex);
			SourceModel.ScreenSize.Default = 0.5f / (float)LODIndex;
		}
		else
		{
			ReductionSettings.PercentTriangles = PercentTriangles[ConfigIndex];
			SourceModel.ScreenSize.Default = ScreenSizes[ConfigIndex];
		}

		// All LODs reduce from LOD0
		ReductionSettings.BaseLODModel = 0;
		ReductionSettings.bRecalculateNormals = true;
		ReductionSettings.SilhouetteImportance = EMeshFeatureImportance::Normal;
	}

	Mesh->Build(false);
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();

	return true;
}

int32 UMeshOptimizationProcessor::BatchGenerateLODs(const TArray<FMeshAnalysisResult>& Meshes, const FLODGenerationConfig& Config)
{
	int32 ProcessedCount = 0;

	FScopedSlowTask SlowTask(Meshes.Num(), LOCTEXT("BatchLOD", "Generating LODs..."));
	SlowTask.MakeDialog(true);

	for (const FMeshAnalysisResult& MeshInfo : Meshes)
	{
		if (SlowTask.ShouldCancel())
		{
			break;
		}
		SlowTask.EnterProgressFrame(1.0f, FText::FromString(FString::Printf(TEXT("LODs: %s"), *MeshInfo.MeshName)));

		if (MeshInfo.StaticMesh.IsValid() && GenerateLODs(MeshInfo.StaticMesh.Get(), Config))
		{
			ProcessedCount++;
		}
	}

	return ProcessedCount;
}

// ─── Nanite ─────────────────────────────────────────────────────────

bool UMeshOptimizationProcessor::SetNanite(UStaticMesh* Mesh, const FNaniteConfig& Config)
{
	if (!Mesh)
	{
		return false;
	}

	if (!EnsureOriginalBackup(Mesh))
	{
		return false;
	}

	// Skip translucent meshes if requested
	if (Config.bSkipTranslucentMeshes)
	{
		for (const FStaticMaterial& Mat : Mesh->GetStaticMaterials())
		{
			if (Mat.MaterialInterface)
			{
				EBlendMode BlendMode = Mat.MaterialInterface->GetBlendMode();
				if (BlendMode == BLEND_Translucent || BlendMode == BLEND_Additive || BlendMode == BLEND_Modulate)
				{
					return false; // skip this mesh
				}
			}
		}
	}

	FScopedTransaction Transaction(LOCTEXT("SetNanite", "Set Nanite"));
	Mesh->Modify();

	FMeshNaniteSettings NaniteSettings = Mesh->GetNaniteSettings();
	NaniteSettings.bEnabled = Config.bEnableNanite;
	NaniteSettings.KeepPercentTriangles = Config.KeepPercentTriangles;
	NaniteSettings.GenerateFallback = Config.bGenerateFallback ? ENaniteGenerateFallback::Enabled : ENaniteGenerateFallback::PlatformDefault;
	NaniteSettings.FallbackTarget = Config.bUsePercentTrianglesForFallback
		? ENaniteFallbackTarget::PercentTriangles
		: ENaniteFallbackTarget::RelativeError;
	NaniteSettings.FallbackPercentTriangles = Config.FallbackPercentTriangles;
	NaniteSettings.FallbackRelativeError = Config.FallbackRelativeError;
	Mesh->SetNaniteSettings(NaniteSettings);

	Mesh->Build(false);
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();

	return true;
}

int32 UMeshOptimizationProcessor::BatchSetNanite(const TArray<FMeshAnalysisResult>& Meshes, const FNaniteConfig& Config)
{
	int32 ProcessedCount = 0;

	FScopedSlowTask SlowTask(Meshes.Num(), LOCTEXT("BatchNanite", "Converting to Nanite..."));
	SlowTask.MakeDialog(true);

	for (const FMeshAnalysisResult& MeshInfo : Meshes)
	{
		if (SlowTask.ShouldCancel())
		{
			break;
		}
		SlowTask.EnterProgressFrame(1.0f, FText::FromString(FString::Printf(TEXT("Nanite: %s"), *MeshInfo.MeshName)));

		if (MeshInfo.StaticMesh.IsValid() && SetNanite(MeshInfo.StaticMesh.Get(), Config))
		{
			ProcessedCount++;
		}
	}

	return ProcessedCount;
}

// ─── Mesh Merging ───────────────────────────────────────────────────

UStaticMesh* UMeshOptimizationProcessor::MergeMeshActors(const TArray<AActor*>& Actors, UWorld* World, const FMeshMergeConfig& Config, const FString& PackagePath)
{
	if (Actors.Num() < 2 || !World)
	{
		return nullptr;
	}

	// Gather components
	TArray<UPrimitiveComponent*> Components;
	for (AActor* Actor : Actors)
	{
		if (!Actor)
		{
			continue;
		}

		TArray<UStaticMeshComponent*> MeshComps;
		Actor->GetComponents<UStaticMeshComponent>(MeshComps);
		for (UStaticMeshComponent* Comp : MeshComps)
		{
			Components.Add(Comp);
		}
	}

	if (Components.Num() == 0)
	{
		return nullptr;
	}

	const IMeshMergeUtilities& MeshMergeUtilities = FModuleManager::Get().LoadModuleChecked<IMeshMergeModule>("MeshMergeUtilities").GetUtilities();

	FMeshMergingSettings MergeSettings;
	MergeSettings.bMergeMaterials = Config.bMergeMaterials;
	MergeSettings.bMergePhysicsData = Config.bMergePhysicsData;
	MergeSettings.bBakeVertexDataToMesh = false;

	if (Config.bMergeMaterials)
	{
		MergeSettings.MaterialSettings.TextureSize = FIntPoint(Config.AtlasResolution, Config.AtlasResolution);
		MergeSettings.MaterialSettings.GutterSpace = Config.GutterSpace;
	}

	if (Config.bGenerateLightmapUVs)
	{
		MergeSettings.bComputedLightMapResolution = true;
	}

	FScopedTransaction Transaction(LOCTEXT("MergeMeshes", "Merge Static Meshes"));

	TArray<UObject*> OutAssetsToSync;
	FVector MergedLocation;

	MeshMergeUtilities.MergeComponentsToStaticMesh(
		Components,
		World,
		MergeSettings,
		nullptr, // base material
		nullptr, // outer package - let the system create one
		PackagePath,
		OutAssetsToSync,
		MergedLocation,
		1.0f,  // screen size
		true   // silent
	);

	// Hide original actors if requested
	if (Config.bKeepOriginalActors)
	{
		for (AActor* Actor : Actors)
		{
			if (Actor)
			{
				Actor->Modify();
				Actor->SetIsTemporarilyHiddenInEditor(true);
			}
		}
	}

	// Find the merged mesh in output assets
	for (UObject* Asset : OutAssetsToSync)
	{
		if (UStaticMesh* MergedMesh = Cast<UStaticMesh>(Asset))
		{
			return MergedMesh;
		}
	}

	return nullptr;
}

// ─── Collision ──────────────────────────────────────────────────────

bool UMeshOptimizationProcessor::SetCollision(UStaticMesh* Mesh, const FCollisionConfig& Config)
{
	if (!Mesh)
	{
		return false;
	}

	if (!EnsureOriginalBackup(Mesh))
	{
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("SetCollision", "Set Collision"));
	Mesh->Modify();

	UBodySetup* BodySetup = Mesh->GetBodySetup();
	if (!BodySetup)
	{
		Mesh->CreateBodySetup();
		BodySetup = Mesh->GetBodySetup();
	}

	if (!BodySetup)
	{
		return false;
	}

	BodySetup->Modify();

	switch (Config.CollisionType)
	{
	case ECollisionOptimization::BoxCollision:
	{
		BodySetup->AggGeom.EmptyElements();
		FKBoxElem BoxElem;
		FBoxSphereBounds Bounds = Mesh->GetBounds();
		BoxElem.Center = FVector::ZeroVector;
		BoxElem.X = Bounds.BoxExtent.X * 2.0f;
		BoxElem.Y = Bounds.BoxExtent.Y * 2.0f;
		BoxElem.Z = Bounds.BoxExtent.Z * 2.0f;
		BodySetup->AggGeom.BoxElems.Add(BoxElem);
		BodySetup->CollisionTraceFlag = ECollisionTraceFlag::CTF_UseDefault;
		break;
	}
	case ECollisionOptimization::ConvexHulls:
	{
		// Generate a convex hull from the mesh bounding box
		BodySetup->AggGeom.EmptyElements();
		BodySetup->CollisionTraceFlag = ECollisionTraceFlag::CTF_UseSimpleAsComplex;

		FKConvexElem ConvexElem;
		FBoxSphereBounds MeshBounds = Mesh->GetBounds();
		FVector Ext = MeshBounds.BoxExtent;
		ConvexElem.VertexData.Add(FVector(-Ext.X, -Ext.Y, -Ext.Z));
		ConvexElem.VertexData.Add(FVector( Ext.X, -Ext.Y, -Ext.Z));
		ConvexElem.VertexData.Add(FVector( Ext.X,  Ext.Y, -Ext.Z));
		ConvexElem.VertexData.Add(FVector(-Ext.X,  Ext.Y, -Ext.Z));
		ConvexElem.VertexData.Add(FVector(-Ext.X, -Ext.Y,  Ext.Z));
		ConvexElem.VertexData.Add(FVector( Ext.X, -Ext.Y,  Ext.Z));
		ConvexElem.VertexData.Add(FVector( Ext.X,  Ext.Y,  Ext.Z));
		ConvexElem.VertexData.Add(FVector(-Ext.X,  Ext.Y,  Ext.Z));
		ConvexElem.UpdateElemBox();
		BodySetup->AggGeom.ConvexElems.Add(ConvexElem);
		break;
	}
	case ECollisionOptimization::NoCollision:
	{
		BodySetup->AggGeom.EmptyElements();
		BodySetup->CollisionTraceFlag = ECollisionTraceFlag::CTF_UseDefault;
		break;
	}
	case ECollisionOptimization::NoChange:
	default:
		return true;
	}

	BodySetup->InvalidatePhysicsData();
	BodySetup->CreatePhysicsMeshes();
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();

	return true;
}

int32 UMeshOptimizationProcessor::BatchSetCollision(const TArray<FMeshAnalysisResult>& Meshes, const FCollisionConfig& Config)
{
	int32 ProcessedCount = 0;

	FScopedSlowTask SlowTask(Meshes.Num(), LOCTEXT("BatchCollision", "Setting collision..."));
	SlowTask.MakeDialog(true);

	for (const FMeshAnalysisResult& MeshInfo : Meshes)
	{
		if (SlowTask.ShouldCancel())
		{
			break;
		}
		SlowTask.EnterProgressFrame(1.0f);

		if (MeshInfo.StaticMesh.IsValid() && SetCollision(MeshInfo.StaticMesh.Get(), Config))
		{
			ProcessedCount++;
		}
	}

	return ProcessedCount;
}

// ─── Lightmap UV ────────────────────────────────────────────────────

bool UMeshOptimizationProcessor::GenerateLightmapUVs(UStaticMesh* Mesh, const FLightmapUVConfig& Config)
{
	if (!Mesh || !Config.bGenerateLightmapUVs)
	{
		return false;
	}

	if (!EnsureOriginalBackup(Mesh))
	{
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("GenLightmapUV", "Generate Lightmap UVs"));
	Mesh->Modify();

	if (Mesh->GetNumSourceModels() > 0)
	{
		FStaticMeshSourceModel& SourceModel = Mesh->GetSourceModel(0);
		SourceModel.BuildSettings.bGenerateLightmapUVs = true;
		SourceModel.BuildSettings.SrcLightmapIndex = Config.SrcLightmapIndex;
		SourceModel.BuildSettings.DstLightmapIndex = Config.DstLightmapIndex;
		SourceModel.BuildSettings.MinLightmapResolution = Config.MinLightmapResolution;
	}

	Mesh->SetLightMapResolution(Config.MinLightmapResolution);
	Mesh->Build(false);
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();

	return true;
}

int32 UMeshOptimizationProcessor::BatchGenerateLightmapUVs(const TArray<FMeshAnalysisResult>& Meshes, const FLightmapUVConfig& Config)
{
	int32 ProcessedCount = 0;

	FScopedSlowTask SlowTask(Meshes.Num(), LOCTEXT("BatchLightmap", "Generating lightmap UVs..."));
	SlowTask.MakeDialog(true);

	for (const FMeshAnalysisResult& MeshInfo : Meshes)
	{
		if (SlowTask.ShouldCancel())
		{
			break;
		}
		SlowTask.EnterProgressFrame(1.0f);

		if (MeshInfo.StaticMesh.IsValid() && GenerateLightmapUVs(MeshInfo.StaticMesh.Get(), Config))
		{
			ProcessedCount++;
		}
	}

	return ProcessedCount;
}

// ─── Material Cleanup ───────────────────────────────────────────────

int32 UMeshOptimizationProcessor::RemoveUnusedMaterialSlots(UStaticMesh* Mesh)
{
	if (!Mesh || !Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.Num() == 0)
	{
		return 0;
	}

	if (!EnsureOriginalBackup(Mesh))
	{
		return 0;
	}

	// Find which material indices are actually used by mesh sections
	TSet<int32> UsedIndices;
	for (int32 LODIdx = 0; LODIdx < Mesh->GetRenderData()->LODResources.Num(); ++LODIdx)
	{
		const FStaticMeshLODResources& LODRes = Mesh->GetRenderData()->LODResources[LODIdx];
		for (int32 SecIdx = 0; SecIdx < LODRes.Sections.Num(); ++SecIdx)
		{
			UsedIndices.Add(LODRes.Sections[SecIdx].MaterialIndex);
		}
	}

	TArray<FStaticMaterial> CurrentMaterials = Mesh->GetStaticMaterials();
	int32 OriginalCount = CurrentMaterials.Num();

	// If all slots are used, nothing to remove
	if (UsedIndices.Num() >= OriginalCount)
	{
		return 0;
	}

	// Build list of unused indices (not referenced by any section)
	TArray<int32> UnusedIndices;
	for (int32 i = 0; i < OriginalCount; ++i)
	{
		if (!UsedIndices.Contains(i))
		{
			UnusedIndices.Add(i);
		}
	}

	if (UnusedIndices.Num() == 0)
	{
		return 0;
	}

	FScopedTransaction Transaction(LOCTEXT("RemoveUnusedMats", "Remove Unused Material Slots"));
	Mesh->Modify();
	Mesh->ModifyAllMeshDescriptions();

	// Build new material list (only used slots) and remap
	TArray<FStaticMaterial> NewMaterials;
	TMap<int32, int32> IndexRemap; // old index -> new index

	for (int32 i = 0; i < OriginalCount; ++i)
	{
		if (UsedIndices.Contains(i))
		{
			IndexRemap.Add(i, NewMaterials.Num());
			NewMaterials.Add(CurrentMaterials[i]);
		}
	}

	Mesh->GetStaticMaterials() = NewMaterials;

	// Remap section material indices in MeshDescription for each source model
	for (int32 LODIdx = 0; LODIdx < Mesh->GetNumSourceModels(); ++LODIdx)
	{
		FMeshDescription* MeshDesc = Mesh->GetMeshDescription(LODIdx);
		if (!MeshDesc) continue;

		FStaticMeshAttributes Attributes(*MeshDesc);
		TPolygonGroupAttributesRef<FName> MaterialSlotNames = Attributes.GetPolygonGroupMaterialSlotNames();

		// Update polygon group material assignments
		for (FPolygonGroupID GroupID : MeshDesc->PolygonGroups().GetElementIDs())
		{
			// Find current material index for this group
			FName CurrentName = MaterialSlotNames[GroupID];
			for (int32 OldIdx = 0; OldIdx < CurrentMaterials.Num(); ++OldIdx)
			{
				if (CurrentMaterials[OldIdx].MaterialSlotName == CurrentName)
				{
					if (int32* NewIdx = IndexRemap.Find(OldIdx))
					{
						MaterialSlotNames[GroupID] = NewMaterials[*NewIdx].MaterialSlotName;
					}
					break;
				}
			}
		}
	}

	int32 Removed = OriginalCount - NewMaterials.Num();

	Mesh->Build(false);
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();

	return Removed;
}

int32 UMeshOptimizationProcessor::ConsolidateDuplicateMaterials(UStaticMesh* Mesh)
{
	if (!Mesh || !Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.Num() == 0)
	{
		return 0;
	}

	if (!EnsureOriginalBackup(Mesh))
	{
		return 0;
	}

	TArray<FStaticMaterial>& Materials = Mesh->GetStaticMaterials();
	if (Materials.Num() <= 1)
	{
		return 0;
	}

	// Find duplicate material interfaces - keep first occurrence, remap duplicates
	TMap<UMaterialInterface*, int32> FirstOccurrence;
	TMap<int32, int32> IndexRemap; // old index -> canonical index
	int32 DuplicateCount = 0;

	for (int32 i = 0; i < Materials.Num(); ++i)
	{
		UMaterialInterface* MatInterface = Materials[i].MaterialInterface;
		if (int32* ExistingIdx = FirstOccurrence.Find(MatInterface))
		{
			IndexRemap.Add(i, *ExistingIdx);
			DuplicateCount++;
		}
		else
		{
			FirstOccurrence.Add(MatInterface, i);
			IndexRemap.Add(i, i);
		}
	}

	if (DuplicateCount == 0)
	{
		return 0;
	}

	FScopedTransaction Transaction(LOCTEXT("ConsolidateMats", "Consolidate Duplicate Materials"));
	Mesh->Modify();

	// Build compacted material list and final remap (old -> new compact index)
	TArray<FStaticMaterial> NewMaterials;
	TMap<int32, int32> FinalRemap;

	for (int32 i = 0; i < Materials.Num(); ++i)
	{
		int32 CanonicalIdx = IndexRemap[i];
		if (CanonicalIdx == i) // this is a first occurrence
		{
			FinalRemap.Add(i, NewMaterials.Num());
			NewMaterials.Add(Materials[i]);
		}
	}

	// Map duplicates to their canonical's new index
	for (int32 i = 0; i < Materials.Num(); ++i)
	{
		if (!FinalRemap.Contains(i))
		{
			int32 CanonicalIdx = IndexRemap[i];
			FinalRemap.Add(i, FinalRemap[CanonicalIdx]);
		}
	}

	Materials = NewMaterials;

	// Note: MeshDescription polygon groups reference materials by slot name.
	// Since we kept the canonical slot names and just removed duplicates,
	// the rebuild will handle remapping sections to the correct new indices.

	Mesh->Build(false);
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();

	return DuplicateCount;
}

int32 UMeshOptimizationProcessor::SafeOptimizeMeshByPercent(UStaticMesh* Mesh, float TargetReductionPercent, float NormalToleranceDegrees)
{
	if (!Mesh || Mesh->GetNumSourceModels() == 0)
	{
		return 0;
	}

	if (!EnsureOriginalBackup(Mesh))
	{
		return 0;
	}

	FScopedNaniteSourceEdit NaniteSourceEdit(Mesh);

	FMeshDescription* MeshDesc = Mesh->GetMeshDescription(0);
	if (!MeshDesc)
	{
		return 0;
	}

	TargetReductionPercent = FMath::Clamp(TargetReductionPercent, 0.01f, 0.95f);

	TVertexAttributesRef<FVector3f> VertexPositions = MeshDesc->GetVertexPositions();
	TUniquePtr<FScopedTransaction> Transaction;

	struct FCandidateResult
	{
		float Factor = 0.0f;
		int32 CollapsedVertices = 0;
		TMap<FVertexID, FVector3f> OverridePositions;
	};

	const int32 OriginalTriangleCount = Mesh->GetRenderData() && Mesh->GetRenderData()->LODResources.Num() > 0
		? Mesh->GetRenderData()->LODResources[0].GetNumTriangles()
		: MeshDesc->Triangles().Num();

	const float NormalDotThreshold = FMath::Cos(FMath::DegreesToRadians(NormalToleranceDegrees));
	int32 TotalCollapsedVertices = 0;
	int32 EstimatedTrianglesRemoved = 0;

	for (int32 PassIndex = 0; PassIndex < 6; ++PassIndex)
	{
		const TMap<FVertexID, FSafePlanarVertexData> VertexData = BuildSafePlanarVertexData(*MeshDesc, VertexPositions, NormalToleranceDegrees);
		FCandidateResult BestResult;
		float BestScore = TNumericLimits<float>::Max();

		for (int32 Step = 1; Step <= 16; ++Step)
		{
			FCandidateResult Candidate;
			Candidate.Factor = FMath::Lerp(0.12f, 2.25f, (float)Step / 16.0f);

			TMap<FIntVector, TArray<FVertexID>> SpatialHash;

			for (const TPair<FVertexID, FSafePlanarVertexData>& Pair : VertexData)
			{
				if (!Pair.Value.bPlanarInterior)
				{
					continue;
				}

				const float CellSize = FMath::Max(Pair.Value.MinNeighborDistance * Candidate.Factor, 0.001f);
				const FVector3f Position = VertexPositions[Pair.Key];
				const FIntVector Cell(
					FMath::FloorToInt(Position.X / CellSize),
					FMath::FloorToInt(Position.Y / CellSize),
					FMath::FloorToInt(Position.Z / CellSize));

				SpatialHash.FindOrAdd(Cell).Add(Pair.Key);
			}

			for (const TPair<FVertexID, FSafePlanarVertexData>& Pair : VertexData)
			{
				const FVertexID VertexID = Pair.Key;
				const FSafePlanarVertexData& Data = Pair.Value;
				if (!Data.bPlanarInterior)
				{
					continue;
				}

				const FVector3f Position = VertexPositions[VertexID];
				const float SearchRadius = FMath::Max(Data.MinNeighborDistance * Candidate.Factor, 0.001f);
				const FIntVector BaseCell(
					FMath::FloorToInt(Position.X / SearchRadius),
					FMath::FloorToInt(Position.Y / SearchRadius),
					FMath::FloorToInt(Position.Z / SearchRadius));

				float BestDistanceSq = TNumericLimits<float>::Max();
				FVertexID BestTarget = VertexID;

				for (int32 X = -1; X <= 1; ++X)
				{
					for (int32 Y = -1; Y <= 1; ++Y)
					{
						for (int32 Z = -1; Z <= 1; ++Z)
						{
							const FIntVector NeighborCell = BaseCell + FIntVector(X, Y, Z);
							const TArray<FVertexID>* Bucket = SpatialHash.Find(NeighborCell);
							if (!Bucket)
							{
								continue;
							}

							for (const FVertexID OtherVertexID : *Bucket)
							{
								if (OtherVertexID == VertexID || OtherVertexID.GetValue() >= VertexID.GetValue())
								{
									continue;
								}

								const FSafePlanarVertexData* OtherData = VertexData.Find(OtherVertexID);
								if (!OtherData || !OtherData->bPlanarInterior)
								{
									continue;
								}

								if (FVector3f::DotProduct(Data.AverageNormal, OtherData->AverageNormal) < NormalDotThreshold)
								{
									continue;
								}

								const FVector3f Delta = Position - VertexPositions[OtherVertexID];
								const float DistanceSq = Delta.SizeSquared();
								const float PairRadius = FMath::Min(Data.MinNeighborDistance, OtherData->MinNeighborDistance) * Candidate.Factor;
								if (DistanceSq > FMath::Square(PairRadius) || DistanceSq >= BestDistanceSq)
								{
									continue;
								}

								const float PlaneOffset = FMath::Abs(FVector3f::DotProduct(Data.AverageNormal, Delta));
								if (PlaneOffset > PairRadius * 0.2f)
								{
									continue;
								}

								BestDistanceSq = DistanceSq;
								BestTarget = OtherVertexID;
							}
						}
					}
				}

				if (BestTarget != VertexID)
				{
					Candidate.OverridePositions.Add(VertexID, VertexPositions[BestTarget]);
				}
			}

			Candidate.CollapsedVertices = Candidate.OverridePositions.Num();
			const int32 CandidateEstimatedRemoved = EstimateDegenerateTriangles(*MeshDesc, VertexPositions, Candidate.OverridePositions);
			const float CandidateEstimatedReduction = OriginalTriangleCount > 0
				? (float)(EstimatedTrianglesRemoved + CandidateEstimatedRemoved) / (float)OriginalTriangleCount
				: 0.0f;
			const float TargetGap = FMath::Abs(CandidateEstimatedReduction - TargetReductionPercent);
			const float Score = TargetGap - ((float)Candidate.CollapsedVertices * 0.0001f);

			if (Candidate.CollapsedVertices > 0 && Score < BestScore)
			{
				BestScore = Score;
				BestResult = MoveTemp(Candidate);
			}
		}

		if (BestResult.CollapsedVertices == 0)
		{
			break;
		}

		if (TotalCollapsedVertices == 0)
		{
			Transaction = MakeUnique<FScopedTransaction>(LOCTEXT("SafeOptimizePercent", "Safe Optimize Mesh By Percentage"));
			Mesh->Modify();
			Mesh->ModifyMeshDescription(0);
		}

		for (const TPair<FVertexID, FVector3f>& Pair : BestResult.OverridePositions)
		{
			VertexPositions[Pair.Key] = Pair.Value;
		}

		TotalCollapsedVertices += BestResult.CollapsedVertices;
		EstimatedTrianglesRemoved = EstimateDegenerateTriangles(*MeshDesc, VertexPositions, TMap<FVertexID, FVector3f>());

		const float EstimatedReduction = OriginalTriangleCount > 0
			? (float)EstimatedTrianglesRemoved / (float)OriginalTriangleCount
			: 0.0f;
		if (EstimatedReduction >= TargetReductionPercent)
		{
			break;
		}
	}

	if (TotalCollapsedVertices == 0)
	{
		return 0;
	}

	FStaticMeshSourceModel& SourceModel = Mesh->GetSourceModel(0);
	SourceModel.BuildSettings.bRemoveDegenerates = true;
	SourceModel.BuildSettings.bRecomputeNormals = true;
	SourceModel.BuildSettings.bRecomputeTangents = true;

	Mesh->CommitMeshDescription(0);
	NaniteSourceEdit.Restore();
	Mesh->Build(false);
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();

	return TotalCollapsedVertices;
}

// ─── BIM Optimization ───────────────────────────────────────────────
// Custom algorithm for BIM/CAD meshes: removes interior vertices from
// coplanar regions and welds near-duplicate vertices. The engine's
// built-in reducer (quadric edge collapse) doesn't understand planar
// regions, so it creates holes at edges while leaving flat surfaces
// over-tessellated. This algorithm targets exactly those flat surfaces.

int32 UMeshOptimizationProcessor::AggressiveOptimizeMeshByPercent(
	UStaticMesh* Mesh,
	float TargetReductionPercent,
	float NormalToleranceDegrees,
	const FAggressiveOptimizeOptions& Options)
{
	if (!Mesh || Mesh->GetNumSourceModels() == 0)
	{
		return 0;
	}

	if (!EnsureOriginalBackup(Mesh))
	{
		return 0;
	}

	(void)NormalToleranceDegrees;

	const int32 InitialTriangleCount = GetBuiltTriangleCount(Mesh);
	if (InitialTriangleCount <= 0)
	{
		return 0;
	}

	TargetReductionPercent = FMath::Clamp(TargetReductionPercent, 0.01f, 0.95f);
	const int32 TargetTriangleCount = FMath::Max(1, FMath::RoundToInt((float)InitialTriangleCount * (1.0f - TargetReductionPercent)));
	if (InitialTriangleCount <= TargetTriangleCount)
	{
		return 0;
	}

	FScopedTransaction Transaction(LOCTEXT("AggressiveOptimizePercent", "Aggressive Optimize Mesh By Percentage"));
	Mesh->Modify();
	Mesh->ModifyMeshDescription(0);
	FScopedNaniteSourceEdit NaniteSourceEdit(Mesh);

	FScopedSlowTask SlowTask(
		5.0f,
		FText::FromString(FString::Printf(TEXT("Aggressively optimizing %s to %d triangles..."), *Mesh->GetName(), TargetTriangleCount)));
	SlowTask.MakeDialog(true);

	SlowTask.EnterProgressFrame(1.0f, LOCTEXT("AggressiveOptimizeConvert", "Converting static mesh to dynamic mesh"));
	if (SlowTask.ShouldCancel())
	{
		return 0;
	}

	FMeshDescription* MeshDesc = Mesh->GetMeshDescription(0);
	if (!MeshDesc)
	{
		return 0;
	}

	UE::Geometry::FDynamicMesh3 DynamicMesh;
	FMeshDescriptionToDynamicMesh ToDynamicMesh;
	ToDynamicMesh.bEnableOutputGroups = true;
	ToDynamicMesh.bUseCompactedPolygonGroupIDValues = true;
	ToDynamicMesh.Convert(MeshDesc, DynamicMesh, false);

	const int32 InitialVertexCount = DynamicMesh.VertexCount();
	if (DynamicMesh.TriangleCount() <= TargetTriangleCount)
	{
		return 0;
	}

	UE::Geometry::FMeshConstraints Constraints;
	const UE::Geometry::EEdgeRefineFlags MeshBoundaryFlags =
		Options.bPreserveMeshBoundary ? UE::Geometry::EEdgeRefineFlags::FullyConstrained : UE::Geometry::EEdgeRefineFlags::NoConstraint;
	const UE::Geometry::EEdgeRefineFlags GroupBoundaryFlags =
		Options.bPreserveGroupBoundary ? UE::Geometry::EEdgeRefineFlags::FullyConstrained : UE::Geometry::EEdgeRefineFlags::NoConstraint;
	const UE::Geometry::EEdgeRefineFlags MaterialBoundaryFlags =
		Options.bPreserveMaterialBoundary ? UE::Geometry::EEdgeRefineFlags::FullyConstrained : UE::Geometry::EEdgeRefineFlags::NoConstraint;
	const UE::Geometry::EEdgeRefineFlags SeamFlags =
		Options.bPreserveAttributeSeams ? UE::Geometry::EEdgeRefineFlags::FullyConstrained : UE::Geometry::EEdgeRefineFlags::NoConstraint;

	for (int32 EdgeID : DynamicMesh.EdgeIndicesItr())
	{
		UE::Geometry::FEdgeConstraint EdgeConstraint;
		UE::Geometry::FVertexConstraint VertexConstraintA;
		UE::Geometry::FVertexConstraint VertexConstraintB;
		if (!UE::Geometry::FMeshConstraintsUtil::ConstrainEdgeBoundariesAndSeams(
			EdgeID,
			DynamicMesh,
			MeshBoundaryFlags,
			GroupBoundaryFlags,
			MaterialBoundaryFlags,
			SeamFlags,
			false,
			EdgeConstraint,
			VertexConstraintA,
			VertexConstraintB))
		{
			continue;
		}

		Constraints.SetOrUpdateEdgeConstraint(EdgeID, EdgeConstraint);
		const auto EdgeVertices = DynamicMesh.GetEdgeV(EdgeID);
		Constraints.SetOrUpdateVertexConstraint(EdgeVertices.A, VertexConstraintA);
		Constraints.SetOrUpdateVertexConstraint(EdgeVertices.B, VertexConstraintB);
	}

	FProgressCancel SimplifyProgress;
	SimplifyProgress.CancelF = [&SlowTask]()
	{
		return SlowTask.ShouldCancel();
	};

	SlowTask.EnterProgressFrame(1.0f, LOCTEXT("AggressiveOptimizeReduce", "Reducing topology with Unreal dynamic-mesh simplifier"));
	if (SlowTask.ShouldCancel())
	{
		return 0;
	}

	UE::Geometry::FAttrMeshSimplification Simplifier(&DynamicMesh);
	Simplifier.SetExternalConstraints(MoveTemp(Constraints));
	Simplifier.Progress = &SimplifyProgress;
	Simplifier.CollapseMode = UE::Geometry::FAttrMeshSimplification::ESimplificationCollapseModes::MinimalExistingVertexError;
	Simplifier.bRetainQuadricMemory = Options.bRetainQuadricMemory;
	Simplifier.bPreserveBoundaryShape = Options.bPreserveBoundaryShape;
	Simplifier.bAllowSeamCollapse = !Options.bPreserveAttributeSeams;
	Simplifier.bPreventTinyTriangles = Options.bPreventTinyTriangles;
	Simplifier.MeshBoundaryConstraint = Options.bPreserveMeshBoundary ? UE::Geometry::EEdgeRefineFlags::FullyConstrained : UE::Geometry::EEdgeRefineFlags::NoConstraint;
	Simplifier.GroupBoundaryConstraint = Options.bPreserveGroupBoundary ? UE::Geometry::EEdgeRefineFlags::FullyConstrained : UE::Geometry::EEdgeRefineFlags::NoConstraint;
	Simplifier.MaterialBoundaryConstraint = Options.bPreserveMaterialBoundary ? UE::Geometry::EEdgeRefineFlags::FullyConstrained : UE::Geometry::EEdgeRefineFlags::NoConstraint;
	Simplifier.SimplifyToTriangleCount(TargetTriangleCount);

	if (SimplifyProgress.Cancelled() || SlowTask.ShouldCancel())
	{
		return 0;
	}

	SlowTask.EnterProgressFrame(1.0f, LOCTEXT("AggressiveOptimizeCommit", "Writing optimized dynamic mesh back to static mesh"));
	if (SlowTask.ShouldCancel())
	{
		return 0;
	}

	FDynamicMeshToMeshDescription ToMeshDescription;
	ToMeshDescription.Convert(&DynamicMesh, *MeshDesc, false);

	FStaticMeshSourceModel& SourceModel = Mesh->GetSourceModel(0);
	SourceModel.BuildSettings.bRemoveDegenerates = true;
	SourceModel.BuildSettings.bRecomputeNormals = true;
	SourceModel.BuildSettings.bRecomputeTangents = true;
	SlowTask.EnterProgressFrame(1.0f, LOCTEXT("AggressiveOptimizeBuild", "Rebuilding static mesh render data"));
	if (SlowTask.ShouldCancel())
	{
		return 0;
	}

	Mesh->CommitMeshDescription(0);
	NaniteSourceEdit.Restore();
	Mesh->Build(false);
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();

	return FMath::Max(0, InitialVertexCount - DynamicMesh.VertexCount());
}

int32 UMeshOptimizationProcessor::BIMOptimizeMesh(UStaticMesh* Mesh, float WeldThreshold, float PlanarAngleTolerance)
{
	if (!Mesh || Mesh->GetNumSourceModels() == 0)
	{
		return 0;
	}

	if (!EnsureOriginalBackup(Mesh))
	{
		return 0;
	}

	FMeshDescription* MeshDesc = Mesh->GetMeshDescription(0);
	if (!MeshDesc)
	{
		return 0;
	}

	FScopedTransaction Transaction(LOCTEXT("BIMOptimize", "BIM Optimize Mesh"));
	Mesh->Modify();
	Mesh->ModifyMeshDescription(0);
	FScopedNaniteSourceEdit NaniteSourceEdit(Mesh);

	int32 TotalRemoved = 0;
	const float CosAngleTolerance = FMath::Cos(FMath::DegreesToRadians(PlanarAngleTolerance));
	const float WeldThresholdSq = WeldThreshold * WeldThreshold;

	// Crash-safe BIM pass: collapse near-duplicate positions, then rebuild with
	// degenerate removal enabled. The older in-place topology deletion path
	// below is intentionally bypassed because it can invalidate MeshDescription
	// IDs mid-iteration and crash the editor.
	{
		TVertexAttributesRef<FVector3f> VertexPositions = MeshDesc->GetVertexPositions();
		TArray<FVertexID> AllVertices;
		for (FVertexID VertexID : MeshDesc->Vertices().GetElementIDs())
		{
			AllVertices.Add(VertexID);
		}

		TSet<FVertexID> ConsumedVertices;
		for (int32 IndexA = 0; IndexA < AllVertices.Num(); ++IndexA)
		{
			const FVertexID VertexA = AllVertices[IndexA];
			if (ConsumedVertices.Contains(VertexA))
			{
				continue;
			}

			const FVector3f PositionA = VertexPositions[VertexA];
			for (int32 IndexB = IndexA + 1; IndexB < AllVertices.Num(); ++IndexB)
			{
				const FVertexID VertexB = AllVertices[IndexB];
				if (ConsumedVertices.Contains(VertexB))
				{
					continue;
				}

				if (FVector3f::DistSquared(PositionA, VertexPositions[VertexB]) < WeldThresholdSq)
				{
					VertexPositions[VertexB] = PositionA;
					ConsumedVertices.Add(VertexB);
					++TotalRemoved;
				}
			}
		}

		FStaticMeshSourceModel& SourceModel = Mesh->GetSourceModel(0);
		SourceModel.BuildSettings.bRemoveDegenerates = true;
		SourceModel.BuildSettings.bRecomputeNormals = true;
		SourceModel.BuildSettings.bRecomputeTangents = true;

		(void)CosAngleTolerance;
		(void)PlanarAngleTolerance;
	}

	#if 0

	// ── Step 1: Vertex Welding ──────────────────────────────────────
	// BIM exports often have near-duplicate vertices at seams.
	// Weld them before doing anything else.
	{
		TVertexAttributesRef<FVector3f> VertexPositions = MeshDesc->GetVertexPositions();
		TArray<FVertexID> AllVertices;
		for (FVertexID VID : MeshDesc->Vertices().GetElementIDs())
		{
			AllVertices.Add(VID);
		}

		// Simple O(n^2) welding - fine for per-mesh use, not batch
		TMap<FVertexID, FVertexID> WeldMap; // maps duplicate -> canonical
		TSet<FVertexID> Removed;

		for (int32 i = 0; i < AllVertices.Num(); ++i)
		{
			FVertexID VA = AllVertices[i];
			if (Removed.Contains(VA)) continue;

			FVector3f PosA = VertexPositions[VA];

			for (int32 j = i + 1; j < AllVertices.Num(); ++j)
			{
				FVertexID VB = AllVertices[j];
				if (Removed.Contains(VB)) continue;

				FVector3f PosB = VertexPositions[VB];
				if (FVector3f::DistSquared(PosA, PosB) < WeldThresholdSq)
				{
					WeldMap.Add(VB, VA);
					Removed.Add(VB);
				}
			}
		}

		// Remap vertex instances from duplicate vertices to canonical vertices
		for (auto& Pair : WeldMap)
		{
			FVertexID DuplicateVert = Pair.Key;
			FVertexID CanonicalVert = Pair.Value;

			TArray<FVertexInstanceID> InstancesToRemap;
			for (FVertexInstanceID VIID : MeshDesc->GetVertexVertexInstanceIDs(DuplicateVert))
			{
				InstancesToRemap.Add(VIID);
			}

			for (FVertexInstanceID VIID : InstancesToRemap)
			{
				// Can't easily remap vertex instances to different vertex in MeshDescription.
				// Instead, just move the duplicate vertex position to the canonical position.
				VertexPositions[DuplicateVert] = VertexPositions[CanonicalVert];
			}
			TotalRemoved++; // Count welded vertices
		}
	}

	// ── Step 2: Remove Degenerate Triangles ─────────────────────────
	{
		TVertexAttributesRef<FVector3f> VertexPositions = MeshDesc->GetVertexPositions();
		TArray<FTriangleID> DegenerateTriangles;

		for (FTriangleID TriID : MeshDesc->Triangles().GetElementIDs())
		{
			TArrayView<const FVertexInstanceID> TriVerts = MeshDesc->GetTriangleVertexInstances(TriID);
			if (TriVerts.Num() < 3) continue;

			FVector3f P0 = VertexPositions[MeshDesc->GetVertexInstanceVertex(TriVerts[0])];
			FVector3f P1 = VertexPositions[MeshDesc->GetVertexInstanceVertex(TriVerts[1])];
			FVector3f P2 = VertexPositions[MeshDesc->GetVertexInstanceVertex(TriVerts[2])];

			FVector3f Cross = FVector3f::CrossProduct(P1 - P0, P2 - P0);
			float AreaSq = Cross.SizeSquared();

			// Remove zero-area or near-zero-area triangles
			if (AreaSq < UE_SMALL_NUMBER)
			{
				DegenerateTriangles.Add(TriID);
			}
		}

		for (FTriangleID TriID : DegenerateTriangles)
		{
			// Get the polygon containing this triangle and delete it
			TArray<FPolygonID> TriPolygons;
			// In MeshDescription, triangles belong to polygons
			// We need to find and delete polygons with degenerate triangles
			FPolygonID PolyID = MeshDesc->GetTrianglePolygon(TriID);
			if (PolyID != FPolygonID(INDEX_NONE))
			{
				MeshDesc->DeletePolygon(PolyID);
				TotalRemoved++;
			}
		}
	}

	// ── Step 3: Coplanar Interior Vertex Removal ────────────────────
	// This is the key BIM optimization. For each vertex, check if ALL
	// adjacent triangles have the same face normal. If so, the vertex
	// is interior to a planar region and can be safely removed.
	// The hole left behind is retriangulated with fan triangulation.
	{
		TVertexAttributesRef<FVector3f> VertexPositions = MeshDesc->GetVertexPositions();
		bool bMadeProgress = true;
		int32 PassCount = 0;
		const int32 MaxPasses = 20; // prevent infinite loops

		while (bMadeProgress && PassCount < MaxPasses)
		{
			bMadeProgress = false;
			PassCount++;

			TArray<FVertexID> VerticesToRemove;

			for (FVertexID VID : MeshDesc->Vertices().GetElementIDs())
			{
				// Get all polygons connected to this vertex
				TArray<FPolygonID> ConnectedPolygons;
				for (FVertexInstanceID VIID : MeshDesc->GetVertexVertexInstanceIDs(VID))
				{
					for (FPolygonID PolyID : MeshDesc->GetVertexInstanceConnectedPolygons(VIID))
					{
						ConnectedPolygons.AddUnique(PolyID);
					}
				}

				if (ConnectedPolygons.Num() < 3)
				{
					continue; // boundary or isolated vertex, skip
				}

				// Compute face normal of first polygon
				FVector3f ReferenceNormal = FVector3f::ZeroVector;
				bool bAllCoplanar = true;

				for (int32 i = 0; i < ConnectedPolygons.Num(); ++i)
				{
					FPolygonID PolyID = ConnectedPolygons[i];
					TArrayView<const FVertexInstanceID> PolyVerts = MeshDesc->GetPolygonVertexInstances(PolyID);
					if (PolyVerts.Num() < 3) { bAllCoplanar = false; break; }

					FVector3f P0 = VertexPositions[MeshDesc->GetVertexInstanceVertex(PolyVerts[0])];
					FVector3f P1 = VertexPositions[MeshDesc->GetVertexInstanceVertex(PolyVerts[1])];
					FVector3f P2 = VertexPositions[MeshDesc->GetVertexInstanceVertex(PolyVerts[2])];

					FVector3f Normal = FVector3f::CrossProduct(P1 - P0, P2 - P0);
					if (Normal.SizeSquared() < UE_SMALL_NUMBER)
					{
						bAllCoplanar = false;
						break;
					}
					Normal.Normalize();

					if (i == 0)
					{
						ReferenceNormal = Normal;
					}
					else
					{
						float Dot = FVector3f::DotProduct(ReferenceNormal, Normal);
						if (FMath::Abs(Dot) < CosAngleTolerance)
						{
							bAllCoplanar = false;
							break;
						}
					}
				}

				if (bAllCoplanar)
				{
					VerticesToRemove.Add(VID);
				}
			}

			// Now remove the identified interior vertices
			for (FVertexID VID : VerticesToRemove)
			{
				// Verify vertex still exists and is still valid
				if (!MeshDesc->IsVertexValid(VID))
				{
					continue;
				}

				// Get the ring of connected polygons
				TArray<FPolygonID> ConnectedPolygons;
				for (FVertexInstanceID VIID : MeshDesc->GetVertexVertexInstanceIDs(VID))
				{
					for (FPolygonID PolyID : MeshDesc->GetVertexInstanceConnectedPolygons(VIID))
					{
						ConnectedPolygons.AddUnique(PolyID);
					}
				}

				if (ConnectedPolygons.Num() < 3)
				{
					continue;
				}

				// Collect the boundary ring: all edges connected to this vertex
				// that are on the boundary of the polygon fan.
				// Get all unique neighbor vertices (connected via edges through polygons)
				TArray<FVertexInstanceID> RingVertexInstances;
				FPolygonGroupID GroupID = FPolygonGroupID(INDEX_NONE);

				// Get the polygon group from the first polygon
				if (ConnectedPolygons.Num() > 0)
				{
					GroupID = MeshDesc->GetPolygonPolygonGroup(ConnectedPolygons[0]);
				}

				// Collect all vertex instances from the connected polygons that are NOT
				// instances of the vertex being removed
				TSet<FVertexID> RingVertexIDs;
				TMap<FVertexID, FVertexInstanceID> VertexToInstance;

				for (FPolygonID PolyID : ConnectedPolygons)
				{
					TArrayView<const FVertexInstanceID> PolyVerts = MeshDesc->GetPolygonVertexInstances(PolyID);
					for (FVertexInstanceID VIID : PolyVerts)
					{
						FVertexID NeighborVID = MeshDesc->GetVertexInstanceVertex(VIID);
						if (NeighborVID != VID)
						{
							RingVertexIDs.Add(NeighborVID);
							VertexToInstance.Add(NeighborVID, VIID);
						}
					}
				}

				if (RingVertexIDs.Num() < 3)
				{
					continue;
				}

				// Order the ring vertices by angle around the removed vertex
				FVector3f CenterPos = VertexPositions[VID];
				FVector3f PlaneNormal = FVector3f::ZeroVector;

				// Get the normal from the first polygon
				{
					TArrayView<const FVertexInstanceID> FirstPolyVerts = MeshDesc->GetPolygonVertexInstances(ConnectedPolygons[0]);
					if (FirstPolyVerts.Num() >= 3)
					{
						FVector3f P0 = VertexPositions[MeshDesc->GetVertexInstanceVertex(FirstPolyVerts[0])];
						FVector3f P1 = VertexPositions[MeshDesc->GetVertexInstanceVertex(FirstPolyVerts[1])];
						FVector3f P2 = VertexPositions[MeshDesc->GetVertexInstanceVertex(FirstPolyVerts[2])];
						PlaneNormal = FVector3f::CrossProduct(P1 - P0, P2 - P0);
						PlaneNormal.Normalize();
					}
				}

				if (PlaneNormal.IsNearlyZero())
				{
					continue;
				}

				// Create local 2D coordinate system on the plane
				FVector3f TangentU, TangentV;
				{
					// Find a non-parallel vector
					FVector3f Up = (FMath::Abs(PlaneNormal.Z) < 0.9f)
						? FVector3f(0, 0, 1) : FVector3f(1, 0, 0);
					TangentU = FVector3f::CrossProduct(PlaneNormal, Up);
					TangentU.Normalize();
					TangentV = FVector3f::CrossProduct(PlaneNormal, TangentU);
					TangentV.Normalize();
				}

				// Project ring vertices to 2D and sort by angle
				struct FRingEntry
				{
					FVertexID VertexID;
					FVertexInstanceID InstanceID;
					float Angle;
				};

				TArray<FRingEntry> Ring;
				for (FVertexID RingVID : RingVertexIDs)
				{
					FVector3f Dir = VertexPositions[RingVID] - CenterPos;
					float U = FVector3f::DotProduct(Dir, TangentU);
					float V = FVector3f::DotProduct(Dir, TangentV);

					FRingEntry Entry;
					Entry.VertexID = RingVID;
					Entry.InstanceID = VertexToInstance[RingVID];
					Entry.Angle = FMath::Atan2(V, U);
					Ring.Add(Entry);
				}

				Ring.Sort([](const FRingEntry& A, const FRingEntry& B) { return A.Angle < B.Angle; });

				// Delete all polygons connected to this vertex
				for (FPolygonID PolyID : ConnectedPolygons)
				{
					if (MeshDesc->IsPolygonValid(PolyID))
					{
						MeshDesc->DeletePolygon(PolyID);
					}
				}

				// Fan triangulation: create new triangles from ring
				// Use Ring[0] as the pivot, create triangles to each consecutive pair
				if (Ring.Num() >= 3 && GroupID != FPolygonGroupID(INDEX_NONE))
				{
					for (int32 i = 1; i < Ring.Num() - 1; ++i)
					{
						TArray<FVertexInstanceID> NewTriVerts;
						NewTriVerts.Add(Ring[0].InstanceID);
						NewTriVerts.Add(Ring[i].InstanceID);
						NewTriVerts.Add(Ring[i + 1].InstanceID);

						MeshDesc->CreatePolygon(GroupID, NewTriVerts);
					}
				}

				// Delete vertex instances and vertex
				TArray<FVertexInstanceID> VertInstances;
				for (FVertexInstanceID VIID : MeshDesc->GetVertexVertexInstanceIDs(VID))
				{
					VertInstances.Add(VIID);
				}
				for (FVertexInstanceID VIID : VertInstances)
				{
					MeshDesc->DeleteVertexInstance(VIID);
				}
				MeshDesc->DeleteVertex(VID);

				TotalRemoved++;
				bMadeProgress = true;
			}
		}
	}

	#endif
	Mesh->CommitMeshDescription(0);
	NaniteSourceEdit.Restore();
	Mesh->Build(false);
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();

	return TotalRemoved;
}

int32 UMeshOptimizationProcessor::BatchBIMOptimize(const TArray<FMeshAnalysisResult>& Meshes, float WeldThreshold, float PlanarAngleTolerance)
{
	int32 TotalOptimized = 0;

	FScopedSlowTask SlowTask(Meshes.Num(), LOCTEXT("BatchBIM", "BIM optimizing meshes..."));
	SlowTask.MakeDialog(true);

	for (const FMeshAnalysisResult& MeshInfo : Meshes)
	{
		if (SlowTask.ShouldCancel())
		{
			break;
		}
		SlowTask.EnterProgressFrame(1.0f, FText::FromString(FString::Printf(TEXT("BIM Optimize: %s"), *MeshInfo.MeshName)));

		if (MeshInfo.StaticMesh.IsValid())
		{
			int32 Removed = BIMOptimizeMesh(MeshInfo.StaticMesh.Get(), WeldThreshold, PlanarAngleTolerance);
			if (Removed > 0)
			{
				TotalOptimized++;
			}
		}
	}

	return TotalOptimized;
}

#undef LOCTEXT_NAMESPACE

