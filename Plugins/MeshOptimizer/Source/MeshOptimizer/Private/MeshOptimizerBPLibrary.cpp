#include "MeshOptimizerBPLibrary.h"
#include "MeshOptimizationProcessor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Selection.h"
#include "EngineUtils.h"

// ─── Analysis ────────────────────────────────────────────────────────

TArray<FMeshAnalysisResult> UMeshOptimizerBPLibrary::AnalyzeMeshesInLevel(UObject* WorldContextObject)
{
	UMeshOptimizationSettings* TempSettings = NewObject<UMeshOptimizationSettings>();
	TempSettings->SelectionMode = EMeshSelectionMode::EntireLevel;
	UMeshAnalyzer* TempAnalyzer = NewObject<UMeshAnalyzer>();

	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World && GEditor)
	{
		World = GEditor->GetEditorWorldContext().World();
	}

	TempAnalyzer->AnalyzeMeshes(TempSettings, World);
	return TempAnalyzer->GetResults();
}

TArray<FMeshAnalysisResult> UMeshOptimizerBPLibrary::AnalyzeSelectedMeshes()
{
	UMeshOptimizationSettings* TempSettings = NewObject<UMeshOptimizationSettings>();
	TempSettings->SelectionMode = EMeshSelectionMode::SelectedActors;
	UMeshAnalyzer* TempAnalyzer = NewObject<UMeshAnalyzer>();
	TempAnalyzer->AnalyzeMeshes(TempSettings, nullptr);
	return TempAnalyzer->GetResults();
}

TArray<FMeshAnalysisResult> UMeshOptimizerBPLibrary::AnalyzeMeshesInFolder(const FString& FolderPath)
{
	UMeshOptimizationSettings* TempSettings = NewObject<UMeshOptimizationSettings>();
	TempSettings->SelectionMode = EMeshSelectionMode::ByFolder;
	TempSettings->FolderPath = FolderPath;
	UMeshAnalyzer* TempAnalyzer = NewObject<UMeshAnalyzer>();
	TempAnalyzer->AnalyzeMeshes(TempSettings, nullptr);
	return TempAnalyzer->GetResults();
}

FMeshAnalysisResult UMeshOptimizerBPLibrary::AnalyzeSingleMesh(UStaticMesh* Mesh)
{
	FMeshAnalysisResult Result;
	if (!Mesh)
	{
		return Result;
	}

	UMeshOptimizationSettings* TempSettings = NewObject<UMeshOptimizationSettings>();
	TempSettings->SelectionMode = EMeshSelectionMode::ContentBrowser;
	UMeshAnalyzer* TempAnalyzer = NewObject<UMeshAnalyzer>();

	// Use the analyzer's internal method via a small wrapper
	TempAnalyzer->AnalyzeMeshes(TempSettings, nullptr);

	// Since content browser might not have it selected, analyze directly
	Result.StaticMesh = Mesh;
	Result.MeshName = Mesh->GetName();
	Result.AssetPath = Mesh->GetPathName();

	if (Mesh->GetRenderData() && Mesh->GetRenderData()->LODResources.Num() > 0)
	{
		const FStaticMeshLODResources& LOD0 = Mesh->GetRenderData()->LODResources[0];
		Result.TriangleCount = LOD0.GetNumTriangles();
		Result.VertexCount = LOD0.GetNumVertices();
		Result.UVChannelCount = LOD0.GetNumTexCoords();
	}
	Result.LODCount = Mesh->GetNumLODs();
	Result.MaterialSlotCount = Mesh->GetStaticMaterials().Num();
	Result.bHasNanite = Mesh->GetNaniteSettings().bEnabled;
	Result.LightmapResolution = Mesh->GetLightMapResolution();
	Result.bHasLightmapUVs = (Result.UVChannelCount >= 2);

	FBoxSphereBounds Bounds = Mesh->GetBounds();
	Result.BoundsSize = Bounds.BoxExtent * 2.0f;

	FResourceSizeEx ResourceSize;
	Mesh->GetResourceSizeEx(ResourceSize);
	Result.ApproxMemoryMB = ResourceSize.GetTotalMemoryBytes() / (1024.0f * 1024.0f);

	return Result;
}

FSceneAnalysisSummary UMeshOptimizerBPLibrary::ComputeSummaryFromResults(const TArray<FMeshAnalysisResult>& Results)
{
	FSceneAnalysisSummary Summary;
	Summary.TotalMeshAssets = Results.Num();

	for (const FMeshAnalysisResult& R : Results)
	{
		Summary.TotalActors += R.InstanceCount;
		Summary.TotalTriangles += R.TriangleCount;
		Summary.TotalVertices += R.VertexCount;
		Summary.TotalApproxMemoryMB += R.ApproxMemoryMB;
		if (R.LODCount <= 1) Summary.MeshesWithoutLODs++;
		if (!R.bHasNanite) Summary.MeshesWithoutNanite++;
		if (!R.bHasLightmapUVs) Summary.MeshesWithoutLightmapUVs++;
		if (R.TriDensity > 10000.0f) Summary.MeshesWithHighTriDensity++;
		if (R.MaterialSlotCount > 5) Summary.MeshesWithManyMaterials++;
		if (R.InstanceCount > 1) Summary.DuplicateMeshGroups++;
	}
	return Summary;
}

// ─── Reduction ──────────────────────────────────────────────────────

bool UMeshOptimizerBPLibrary::ReduceStaticMesh(UStaticMesh* Mesh, float PercentTriangles, float MaxDeviation, bool bUseMaxDeviation)
{
	UMeshOptimizationProcessor* Proc = NewObject<UMeshOptimizationProcessor>();
	FMeshReductionConfig Config;
	Config.PercentTriangles = PercentTriangles;
	Config.MaxDeviation = MaxDeviation;
	Config.bUseMaxDeviation = bUseMaxDeviation;
	return Proc->ReduceMesh(Mesh, Config);
}

int32 UMeshOptimizerBPLibrary::BatchReduceStaticMeshes(const TArray<UStaticMesh*>& Meshes, float PercentTriangles)
{
	UMeshOptimizationProcessor* Proc = NewObject<UMeshOptimizationProcessor>();
	FMeshReductionConfig Config;
	Config.PercentTriangles = PercentTriangles;

	TArray<FMeshAnalysisResult> Results;
	for (UStaticMesh* M : Meshes)
	{
		if (M)
		{
			FMeshAnalysisResult R;
			R.StaticMesh = M;
			R.MeshName = M->GetName();
			Results.Add(R);
		}
	}
	return Proc->BatchReduceMeshes(Results, Config);
}

// ─── LOD ─────────────────────────────────────────────────────────────

bool UMeshOptimizerBPLibrary::GenerateLODsForMesh(UStaticMesh* Mesh, int32 NumLODs, bool bAutoScreenSizes)
{
	UMeshOptimizationProcessor* Proc = NewObject<UMeshOptimizationProcessor>();
	FLODGenerationConfig Config;
	Config.NumLODs = NumLODs;
	Config.bAutoComputeScreenSizes = bAutoScreenSizes;
	return Proc->GenerateLODs(Mesh, Config);
}

int32 UMeshOptimizerBPLibrary::BatchGenerateLODs(const TArray<UStaticMesh*>& Meshes, int32 NumLODs)
{
	UMeshOptimizationProcessor* Proc = NewObject<UMeshOptimizationProcessor>();
	FLODGenerationConfig Config;
	Config.NumLODs = NumLODs;

	TArray<FMeshAnalysisResult> Results;
	for (UStaticMesh* M : Meshes)
	{
		if (M)
		{
			FMeshAnalysisResult R;
			R.StaticMesh = M;
			Results.Add(R);
		}
	}
	return Proc->BatchGenerateLODs(Results, Config);
}

// ─── Nanite ──────────────────────────────────────────────────────────

bool UMeshOptimizerBPLibrary::EnableNaniteOnMesh(UStaticMesh* Mesh, float FallbackPercent, bool bSkipTranslucent)
{
	UMeshOptimizationProcessor* Proc = NewObject<UMeshOptimizationProcessor>();
	FNaniteConfig Config;
	Config.bEnableNanite = true;
	Config.FallbackPercentTriangles = FallbackPercent;
	Config.bSkipTranslucentMeshes = bSkipTranslucent;
	return Proc->SetNanite(Mesh, Config);
}

int32 UMeshOptimizerBPLibrary::BatchEnableNanite(const TArray<UStaticMesh*>& Meshes, float FallbackPercent)
{
	UMeshOptimizationProcessor* Proc = NewObject<UMeshOptimizationProcessor>();
	FNaniteConfig Config;
	Config.bEnableNanite = true;
	Config.FallbackPercentTriangles = FallbackPercent;

	TArray<FMeshAnalysisResult> Results;
	for (UStaticMesh* M : Meshes)
	{
		if (M)
		{
			FMeshAnalysisResult R;
			R.StaticMesh = M;
			Results.Add(R);
		}
	}
	return Proc->BatchSetNanite(Results, Config);
}

int32 UMeshOptimizerBPLibrary::BatchDisableNanite(const TArray<UStaticMesh*>& Meshes)
{
	UMeshOptimizationProcessor* Proc = NewObject<UMeshOptimizationProcessor>();
	FNaniteConfig Config;
	Config.bEnableNanite = false;

	TArray<FMeshAnalysisResult> Results;
	for (UStaticMesh* M : Meshes)
	{
		if (M)
		{
			FMeshAnalysisResult R;
			R.StaticMesh = M;
			Results.Add(R);
		}
	}
	return Proc->BatchSetNanite(Results, Config);
}

// ─── Collision ──────────────────────────────────────────────────────

bool UMeshOptimizerBPLibrary::SetBoxCollision(UStaticMesh* Mesh)
{
	UMeshOptimizationProcessor* Proc = NewObject<UMeshOptimizationProcessor>();
	FCollisionConfig Config;
	Config.CollisionType = ECollisionOptimization::BoxCollision;
	return Proc->SetCollision(Mesh, Config);
}

bool UMeshOptimizerBPLibrary::RemoveCollision(UStaticMesh* Mesh)
{
	UMeshOptimizationProcessor* Proc = NewObject<UMeshOptimizationProcessor>();
	FCollisionConfig Config;
	Config.CollisionType = ECollisionOptimization::NoCollision;
	return Proc->SetCollision(Mesh, Config);
}

// ─── Lightmap UV ────────────────────────────────────────────────────

bool UMeshOptimizerBPLibrary::GenerateLightmapUVs(UStaticMesh* Mesh, int32 MinResolution)
{
	UMeshOptimizationProcessor* Proc = NewObject<UMeshOptimizationProcessor>();
	FLightmapUVConfig Config;
	Config.bGenerateLightmapUVs = true;
	Config.MinLightmapResolution = MinResolution;
	return Proc->GenerateLightmapUVs(Mesh, Config);
}

// ─── Export ─────────────────────────────────────────────────────────

FString UMeshOptimizerBPLibrary::ExportAnalysisToCSV(const TArray<FMeshAnalysisResult>& Results)
{
	FString CSV;
	CSV += TEXT("Name,Triangles,Vertices,Materials,LODs,Nanite,LightmapUV,MemoryMB,TriDensity,Instances\n");

	for (const FMeshAnalysisResult& R : Results)
	{
		CSV += FString::Printf(
			TEXT("%s,%d,%d,%d,%d,%s,%s,%.2f,%.1f,%d\n"),
			*R.MeshName, R.TriangleCount, R.VertexCount, R.MaterialSlotCount, R.LODCount,
			R.bHasNanite ? TEXT("Yes") : TEXT("No"),
			R.bHasLightmapUVs ? TEXT("Yes") : TEXT("No"),
			R.ApproxMemoryMB, R.TriDensity, R.InstanceCount);
	}
	return CSV;
}
