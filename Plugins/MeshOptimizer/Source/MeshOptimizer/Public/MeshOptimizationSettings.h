#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MeshOptimizationSettings.generated.h"

UENUM(BlueprintType)
enum class EMeshOptimizationPreset : uint8
{
	Preview     UMETA(DisplayName = "Preview (10% tris)"),
	Balanced    UMETA(DisplayName = "Balanced (40% tris)"),
	Quality     UMETA(DisplayName = "Quality (70% tris)"),
	Lossless    UMETA(DisplayName = "Lossless Cleanup"),
	Custom      UMETA(DisplayName = "Custom")
};

UENUM(BlueprintType)
enum class EMeshSelectionMode : uint8
{
	SelectedActors     UMETA(DisplayName = "Selected Actors"),
	EntireLevel        UMETA(DisplayName = "Entire Level"),
	ContentBrowser     UMETA(DisplayName = "Content Browser Selection"),
	ByFolder           UMETA(DisplayName = "By Folder Path")
};

UENUM(BlueprintType)
enum class ECollisionOptimization : uint8
{
	NoChange        UMETA(DisplayName = "No Change"),
	BoxCollision    UMETA(DisplayName = "Simple Box"),
	ConvexHulls     UMETA(DisplayName = "Convex Decomposition"),
	NoCollision     UMETA(DisplayName = "Remove Collision")
};

UENUM(BlueprintType)
enum class EMeshOptImportance : uint8
{
	Off     UMETA(DisplayName = "Off"),
	Low     UMETA(DisplayName = "Low"),
	Normal  UMETA(DisplayName = "Normal"),
	High    UMETA(DisplayName = "High")
};

// Analysis filter settings
USTRUCT(BlueprintType)
struct FMeshAnalysisFilter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter", meta = (ClampMin = "0"))
	int32 MinTriangleCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter", meta = (ClampMin = "0"))
	int32 MinMaterialSlots = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter")
	bool bOnlyMeshesWithoutLODs = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter")
	bool bOnlyMeshesWithoutNanite = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter")
	bool bOnlyMeshesWithoutLightmapUVs = false;
};

// Polygon reduction settings
USTRUCT(BlueprintType)
struct FMeshReductionConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reduction", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float PercentTriangles = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reduction", meta = (ClampMin = "0.0"))
	float MaxDeviation = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reduction")
	bool bUseMaxDeviation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reduction", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float WeldingThreshold = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reduction", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float HardAngleThreshold = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reduction")
	EMeshOptImportance SilhouetteImportance = EMeshOptImportance::High;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reduction")
	EMeshOptImportance TextureImportance = EMeshOptImportance::Normal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reduction")
	EMeshOptImportance ShadingImportance = EMeshOptImportance::Normal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reduction")
	bool bRecalculateNormals = true;
};

// LOD generation settings
USTRUCT(BlueprintType)
struct FLODGenerationConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD", meta = (ClampMin = "1", ClampMax = "7"))
	int32 NumLODs = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD")
	bool bAutoComputeScreenSizes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float LOD1ScreenSize = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float LOD1PercentTriangles = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float LOD2ScreenSize = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float LOD2PercentTriangles = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float LOD3ScreenSize = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float LOD3PercentTriangles = 0.12f;
};

// Nanite conversion settings
USTRUCT(BlueprintType)
struct FNaniteConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nanite")
	bool bEnableNanite = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nanite", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float KeepPercentTriangles = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nanite", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FallbackPercentTriangles = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nanite", meta = (ClampMin = "0.0"))
	float FallbackRelativeError = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nanite")
	bool bUsePercentTrianglesForFallback = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nanite")
	bool bGenerateFallback = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nanite")
	bool bSkipTranslucentMeshes = true;
};

// Mesh merging settings
USTRUCT(BlueprintType)
struct FMeshMergeConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Merge")
	bool bMergeMaterials = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Merge")
	bool bMergePhysicsData = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Merge")
	bool bGenerateLightmapUVs = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Merge", meta = (ClampMin = "1", ClampMax = "8192"))
	int32 AtlasResolution = 1024;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Merge", meta = (ClampMin = "1"))
	int32 GutterSpace = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Merge", meta = (ClampMin = "100"))
	int32 MaxMergedVertices = 65535;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Merge")
	bool bKeepOriginalActors = true;
};

// Collision settings
USTRUCT(BlueprintType)
struct FCollisionConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	ECollisionOptimization CollisionType = ECollisionOptimization::BoxCollision;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision", meta = (ClampMin = "1", ClampMax = "32", EditCondition = "CollisionType == ECollisionOptimization::ConvexHulls"))
	int32 MaxConvexHulls = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision", meta = (ClampMin = "8", ClampMax = "256", EditCondition = "CollisionType == ECollisionOptimization::ConvexHulls"))
	int32 MaxHullVertices = 32;
};

// Lightmap UV settings
USTRUCT(BlueprintType)
struct FLightmapUVConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lightmap UV")
	bool bGenerateLightmapUVs = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lightmap UV", meta = (ClampMin = "0"))
	int32 SrcLightmapIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lightmap UV", meta = (ClampMin = "1"))
	int32 DstLightmapIndex = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lightmap UV", meta = (ClampMin = "4"))
	int32 MinLightmapResolution = 64;
};

// Main settings container
UCLASS(BlueprintType, Config = MeshOptimizer)
class MESHOPTIMIZER_API UMeshOptimizationSettings : public UObject
{
	GENERATED_BODY()

public:
	UMeshOptimizationSettings();

	// Selection
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection")
	EMeshSelectionMode SelectionMode = EMeshSelectionMode::SelectedActors;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection", meta = (EditCondition = "SelectionMode == EMeshSelectionMode::ByFolder"))
	FString FolderPath = TEXT("/Game/Models/");

	// Filter
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter")
	FMeshAnalysisFilter AnalysisFilter;

	// Preset
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset")
	EMeshOptimizationPreset Preset = EMeshOptimizationPreset::Balanced;

	// Reduction
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reduction", meta = (EditCondition = "Preset == EMeshOptimizationPreset::Custom"))
	FMeshReductionConfig ReductionSettings;

	// LOD
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD Generation")
	FLODGenerationConfig LODSettings;

	// Nanite
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nanite")
	FNaniteConfig NaniteSettings;

	// Merge
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Merging")
	FMeshMergeConfig MergeSettings;

	// Collision
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	FCollisionConfig CollisionSettings;

	// Lightmap
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lightmap UV")
	FLightmapUVConfig LightmapSettings;

	// Apply preset values
	void ApplyPreset(EMeshOptimizationPreset InPreset);
};
