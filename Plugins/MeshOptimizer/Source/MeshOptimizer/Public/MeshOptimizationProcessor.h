#pragma once

#include "CoreMinimal.h"
#include "MeshOptimizationSettings.h"
#include "MeshAnalyzer.h"
#include "MeshOptimizationProcessor.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnOptimizationProgress, int32, Current, int32, Total);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnOptimizationComplete, int32, ProcessedCount);

struct FAggressiveOptimizeOptions
{
	bool bPreserveMeshBoundary = true;
	bool bPreserveGroupBoundary = true;
	bool bPreserveMaterialBoundary = true;
	bool bPreserveAttributeSeams = true;
	bool bPreventTinyTriangles = true;
	bool bPreserveBoundaryShape = true;
	bool bRetainQuadricMemory = true;
};

UCLASS()
class MESHOPTIMIZER_API UMeshOptimizationProcessor : public UObject
{
	GENERATED_BODY()

public:
	UStaticMesh* GetOptimizationBaseMesh(UStaticMesh* Mesh) const;
	bool HasOriginalBackup(UStaticMesh* Mesh) const;
	bool RestoreOriginalMesh(UStaticMesh* Mesh);

	// Polygon reduction on a single mesh
	bool ReduceMesh(UStaticMesh* Mesh, const FMeshReductionConfig& Config);

	// Batch polygon reduction
	int32 BatchReduceMeshes(const TArray<FMeshAnalysisResult>& Meshes, const FMeshReductionConfig& Config);

	// Generate LODs for a single mesh
	bool GenerateLODs(UStaticMesh* Mesh, const FLODGenerationConfig& Config);

	// Batch LOD generation
	int32 BatchGenerateLODs(const TArray<FMeshAnalysisResult>& Meshes, const FLODGenerationConfig& Config);

	// Enable/disable Nanite on a single mesh
	bool SetNanite(UStaticMesh* Mesh, const FNaniteConfig& Config);

	// Batch Nanite conversion
	int32 BatchSetNanite(const TArray<FMeshAnalysisResult>& Meshes, const FNaniteConfig& Config);

	// Merge selected actors' meshes into one
	UStaticMesh* MergeMeshActors(const TArray<AActor*>& Actors, UWorld* World, const FMeshMergeConfig& Config, const FString& PackagePath);

	// Set collision on a single mesh
	bool SetCollision(UStaticMesh* Mesh, const FCollisionConfig& Config);

	// Batch collision
	int32 BatchSetCollision(const TArray<FMeshAnalysisResult>& Meshes, const FCollisionConfig& Config);

	// Generate lightmap UVs on a single mesh
	bool GenerateLightmapUVs(UStaticMesh* Mesh, const FLightmapUVConfig& Config);

	// Batch lightmap UV generation
	int32 BatchGenerateLightmapUVs(const TArray<FMeshAnalysisResult>& Meshes, const FLightmapUVConfig& Config);

	// Remove unused material slots (slots not referenced by any section)
	int32 RemoveUnusedMaterialSlots(UStaticMesh* Mesh);

	// Consolidate duplicate material slots (merge slots using the same material)
	int32 ConsolidateDuplicateMaterials(UStaticMesh* Mesh);

	// BIM-specific optimization: removes interior vertices from coplanar regions
	// and welds near-duplicate vertices. Lossless for flat surfaces.
	// Returns number of vertices removed.
	int32 BIMOptimizeMesh(UStaticMesh* Mesh, float WeldThreshold = 0.01f, float PlanarAngleTolerance = 1.0f);

	// Custom safe optimizer for CAD/BIM-style meshes. Uses planar interior
	// vertex clustering with a target reduction percentage, without the engine's
	// default reduction pipeline.
	int32 SafeOptimizeMeshByPercent(UStaticMesh* Mesh, float TargetReductionPercent, float NormalToleranceDegrees = 1.0f);

	// More aggressive custom optimizer that searches for the closest achievable
	// final triangle count to the requested reduction percentage, without using
	// Unreal's default reducer.
	int32 AggressiveOptimizeMeshByPercent(
		UStaticMesh* Mesh,
		float TargetReductionPercent,
		float NormalToleranceDegrees = 5.0f,
		const FAggressiveOptimizeOptions& Options = FAggressiveOptimizeOptions());

	// Batch BIM optimization
	int32 BatchBIMOptimize(const TArray<FMeshAnalysisResult>& Meshes, float WeldThreshold = 0.01f, float PlanarAngleTolerance = 1.0f);

private:
	bool EnsureOriginalBackup(UStaticMesh* Mesh);

	// Map our EMeshOptImportance to engine EMeshFeatureImportance::Type
	static uint8 ToEngineImportance(EMeshOptImportance Level);
};
