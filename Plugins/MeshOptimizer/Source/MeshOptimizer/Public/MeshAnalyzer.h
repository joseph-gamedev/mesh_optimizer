#pragma once

#include "CoreMinimal.h"
#include "MeshOptimizationSettings.h"
#include "MeshAnalyzer.generated.h"

// Per-mesh analysis result
USTRUCT(BlueprintType)
struct MESHOPTIMIZER_API FMeshAnalysisResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Analysis")
	FString MeshName;

	UPROPERTY(BlueprintReadOnly, Category = "Analysis")
	FString AssetPath;

	UPROPERTY(BlueprintReadOnly, Category = "Analysis")
	int32 TriangleCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Analysis")
	int32 VertexCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Analysis")
	int32 MaterialSlotCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Analysis")
	int32 LODCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Analysis")
	int32 UVChannelCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Analysis")
	bool bHasNanite = false;

	UPROPERTY(BlueprintReadOnly, Category = "Analysis")
	bool bHasLightmapUVs = false;

	UPROPERTY(BlueprintReadOnly, Category = "Analysis")
	bool bHasCollision = false;

	UPROPERTY(BlueprintReadOnly, Category = "Analysis")
	FVector BoundsSize = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Analysis")
	float ApproxMemoryMB = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Analysis")
	int32 LightmapResolution = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Analysis")
	float TriDensity = 0.0f; // triangles per cubic meter (over-tessellation indicator)

	UPROPERTY(BlueprintReadOnly, Category = "Analysis")
	int32 InstanceCount = 0; // how many actors reference this mesh in the level

	// Weak pointer to the actual mesh
	TWeakObjectPtr<UStaticMesh> StaticMesh;
};

// Scene-wide summary
USTRUCT(BlueprintType)
struct MESHOPTIMIZER_API FSceneAnalysisSummary
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Summary")
	int32 TotalMeshAssets = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Summary")
	int32 TotalActors = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Summary")
	int64 TotalTriangles = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Summary")
	int64 TotalVertices = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Summary")
	int32 MeshesWithoutLODs = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Summary")
	int32 MeshesWithoutNanite = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Summary")
	int32 MeshesWithoutLightmapUVs = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Summary")
	int32 MeshesWithHighTriDensity = 0; // potential over-tessellation

	UPROPERTY(BlueprintReadOnly, Category = "Summary")
	int32 MeshesWithManyMaterials = 0; // > 5 material slots

	UPROPERTY(BlueprintReadOnly, Category = "Summary")
	float TotalApproxMemoryMB = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Summary")
	int32 DuplicateMeshGroups = 0; // meshes used by multiple actors (instancing candidates)
};

UCLASS()
class MESHOPTIMIZER_API UMeshAnalyzer : public UObject
{
	GENERATED_BODY()

public:
	// Analyze meshes based on selection mode and filter
	void AnalyzeMeshes(const UMeshOptimizationSettings* Settings, UWorld* World);

	// Get results
	const TArray<FMeshAnalysisResult>& GetResults() const { return Results; }
	const FSceneAnalysisSummary& GetSummary() const { return Summary; }

	// Sort results
	void SortByTriangleCount(bool bDescending = true);
	void SortByMemory(bool bDescending = true);
	void SortByMaterialSlots(bool bDescending = true);
	void SortByTriDensity(bool bDescending = true);

	// Export
	FString ExportToCSV() const;

	// Clear
	void ClearResults();

private:
	FMeshAnalysisResult AnalyzeSingleMesh(UStaticMesh* Mesh) const;
	void GatherMeshesFromLevel(UWorld* World, TMap<UStaticMesh*, int32>& OutMeshInstanceCounts);
	void GatherMeshesFromFolder(const FString& FolderPath, TMap<UStaticMesh*, int32>& OutMeshes);
	void GatherMeshesFromSelection(TMap<UStaticMesh*, int32>& OutMeshInstanceCounts);
	void GatherMeshesFromContentBrowser(TMap<UStaticMesh*, int32>& OutMeshes);
	bool PassesFilter(const FMeshAnalysisResult& Result, const FMeshAnalysisFilter& Filter) const;
	void ComputeSummary();

	TArray<FMeshAnalysisResult> Results;
	FSceneAnalysisSummary Summary;
};
