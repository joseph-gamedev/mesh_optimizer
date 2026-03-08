#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MeshAnalyzer.h"
#include "MeshOptimizationSettings.h"
#include "MeshOptimizerBPLibrary.generated.h"

UCLASS()
class MESHOPTIMIZER_API UMeshOptimizerBPLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ─── Analysis ────────────────────────────────────────────────

	UFUNCTION(BlueprintCallable, Category = "MeshOptimizer|Analysis")
	static TArray<FMeshAnalysisResult> AnalyzeMeshesInLevel(UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "MeshOptimizer|Analysis")
	static TArray<FMeshAnalysisResult> AnalyzeSelectedMeshes();

	UFUNCTION(BlueprintCallable, Category = "MeshOptimizer|Analysis")
	static TArray<FMeshAnalysisResult> AnalyzeMeshesInFolder(const FString& FolderPath);

	UFUNCTION(BlueprintCallable, Category = "MeshOptimizer|Analysis")
	static FMeshAnalysisResult AnalyzeSingleMesh(UStaticMesh* Mesh);

	UFUNCTION(BlueprintCallable, Category = "MeshOptimizer|Analysis")
	static FSceneAnalysisSummary ComputeSummaryFromResults(const TArray<FMeshAnalysisResult>& Results);

	// ─── Reduction ───────────────────────────────────────────────

	UFUNCTION(BlueprintCallable, Category = "MeshOptimizer|Reduction")
	static bool ReduceStaticMesh(UStaticMesh* Mesh, float PercentTriangles = 0.5f, float MaxDeviation = 1.0f, bool bUseMaxDeviation = false);

	UFUNCTION(BlueprintCallable, Category = "MeshOptimizer|Reduction")
	static int32 BatchReduceStaticMeshes(const TArray<UStaticMesh*>& Meshes, float PercentTriangles = 0.5f);

	// ─── LOD ─────────────────────────────────────────────────────

	UFUNCTION(BlueprintCallable, Category = "MeshOptimizer|LOD")
	static bool GenerateLODsForMesh(UStaticMesh* Mesh, int32 NumLODs = 3, bool bAutoScreenSizes = true);

	UFUNCTION(BlueprintCallable, Category = "MeshOptimizer|LOD")
	static int32 BatchGenerateLODs(const TArray<UStaticMesh*>& Meshes, int32 NumLODs = 3);

	// ─── Nanite ──────────────────────────────────────────────────

	UFUNCTION(BlueprintCallable, Category = "MeshOptimizer|Nanite")
	static bool EnableNaniteOnMesh(UStaticMesh* Mesh, float FallbackPercent = 0.1f, bool bSkipTranslucent = true);

	UFUNCTION(BlueprintCallable, Category = "MeshOptimizer|Nanite")
	static int32 BatchEnableNanite(const TArray<UStaticMesh*>& Meshes, float FallbackPercent = 0.1f);

	UFUNCTION(BlueprintCallable, Category = "MeshOptimizer|Nanite")
	static int32 BatchDisableNanite(const TArray<UStaticMesh*>& Meshes);

	// ─── Collision ───────────────────────────────────────────────

	UFUNCTION(BlueprintCallable, Category = "MeshOptimizer|Collision")
	static bool SetBoxCollision(UStaticMesh* Mesh);

	UFUNCTION(BlueprintCallable, Category = "MeshOptimizer|Collision")
	static bool RemoveCollision(UStaticMesh* Mesh);

	// ─── Lightmap UV ─────────────────────────────────────────────

	UFUNCTION(BlueprintCallable, Category = "MeshOptimizer|LightmapUV")
	static bool GenerateLightmapUVs(UStaticMesh* Mesh, int32 MinResolution = 64);

	// ─── Export ──────────────────────────────────────────────────

	UFUNCTION(BlueprintCallable, Category = "MeshOptimizer|Export")
	static FString ExportAnalysisToCSV(const TArray<FMeshAnalysisResult>& Results);
};
