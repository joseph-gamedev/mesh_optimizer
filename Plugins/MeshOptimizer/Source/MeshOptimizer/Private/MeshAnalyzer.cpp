#include "MeshAnalyzer.h"

#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Selection.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "EngineUtils.h"
#include "Misc/ScopedSlowTask.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshResources.h"

void UMeshAnalyzer::AnalyzeMeshes(const UMeshOptimizationSettings* Settings, UWorld* World)
{
	ClearResults();

	if (!Settings)
	{
		return;
	}

	TMap<UStaticMesh*, int32> MeshInstanceCounts;

	switch (Settings->SelectionMode)
	{
	case EMeshSelectionMode::SelectedActors:
		GatherMeshesFromSelection(MeshInstanceCounts);
		break;
	case EMeshSelectionMode::EntireLevel:
		GatherMeshesFromLevel(World, MeshInstanceCounts);
		break;
	case EMeshSelectionMode::ContentBrowser:
		GatherMeshesFromContentBrowser(MeshInstanceCounts);
		break;
	case EMeshSelectionMode::ByFolder:
		GatherMeshesFromFolder(Settings->FolderPath, MeshInstanceCounts);
		break;
	}

	FScopedSlowTask SlowTask(MeshInstanceCounts.Num(), FText::FromString(TEXT("Analyzing static meshes...")));
	SlowTask.MakeDialog();

	for (auto& Pair : MeshInstanceCounts)
	{
		SlowTask.EnterProgressFrame(1.0f);

		UStaticMesh* Mesh = Pair.Key;
		if (!Mesh)
		{
			continue;
		}

		FMeshAnalysisResult Result = AnalyzeSingleMesh(Mesh);
		Result.InstanceCount = Pair.Value;

		if (PassesFilter(Result, Settings->AnalysisFilter))
		{
			Results.Add(Result);
		}
	}

	ComputeSummary();
	SortByTriangleCount(true);
}

FMeshAnalysisResult UMeshAnalyzer::AnalyzeSingleMesh(UStaticMesh* Mesh) const
{
	FMeshAnalysisResult Result;
	Result.StaticMesh = Mesh;
	Result.MeshName = Mesh->GetName();
	Result.AssetPath = Mesh->GetPathName();

	// Render data
	if (Mesh->GetRenderData() && Mesh->GetRenderData()->LODResources.Num() > 0)
	{
		const FStaticMeshLODResources& LOD0 = Mesh->GetRenderData()->LODResources[0];
		Result.TriangleCount = LOD0.GetNumTriangles();
		Result.VertexCount = LOD0.GetNumVertices();
		Result.UVChannelCount = LOD0.GetNumTexCoords();
	}

	// LODs
	Result.LODCount = Mesh->GetNumLODs();

	// Materials
	Result.MaterialSlotCount = Mesh->GetStaticMaterials().Num();

	// Nanite
	Result.bHasNanite = Mesh->GetNaniteSettings().bEnabled;

	// Lightmap
	Result.LightmapResolution = Mesh->GetLightMapResolution();
	Result.bHasLightmapUVs = (Result.UVChannelCount >= 2);

	// Collision
	Result.bHasCollision = (Mesh->GetBodySetup() != nullptr &&
		(Mesh->GetBodySetup()->AggGeom.GetElementCount() > 0 ||
		 Mesh->GetBodySetup()->CollisionTraceFlag == ECollisionTraceFlag::CTF_UseComplexAsSimple));

	// Bounds
	FBoxSphereBounds Bounds = Mesh->GetBounds();
	Result.BoundsSize = Bounds.BoxExtent * 2.0f; // full extent

	// Triangle density (tris per cubic meter of bounding volume)
	float Volume = (Result.BoundsSize.X * Result.BoundsSize.Y * Result.BoundsSize.Z) / 1000000.0f; // cm^3 to m^3
	Result.TriDensity = (Volume > KINDA_SMALL_NUMBER) ? (float)Result.TriangleCount / Volume : 0.0f;

	// Approximate memory
	FResourceSizeEx ResourceSize;
	Mesh->GetResourceSizeEx(ResourceSize);
	Result.ApproxMemoryMB = ResourceSize.GetTotalMemoryBytes() / (1024.0f * 1024.0f);

	return Result;
}

void UMeshAnalyzer::GatherMeshesFromLevel(UWorld* World, TMap<UStaticMesh*, int32>& OutMeshInstanceCounts)
{
	if (!World)
	{
		return;
	}

	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		AStaticMeshActor* Actor = *It;
		if (!Actor || !Actor->GetStaticMeshComponent())
		{
			continue;
		}

		UStaticMesh* Mesh = Actor->GetStaticMeshComponent()->GetStaticMesh();
		if (Mesh)
		{
			int32& Count = OutMeshInstanceCounts.FindOrAdd(Mesh, 0);
			Count++;
		}
	}
}

void UMeshAnalyzer::GatherMeshesFromSelection(TMap<UStaticMesh*, int32>& OutMeshInstanceCounts)
{
	USelection* SelectedActors = GEditor->GetSelectedActors();
	if (!SelectedActors)
	{
		return;
	}

	for (int32 i = 0; i < SelectedActors->Num(); ++i)
	{
		AActor* Actor = Cast<AActor>(SelectedActors->GetSelectedObject(i));
		if (!Actor)
		{
			continue;
		}

		TArray<UStaticMeshComponent*> MeshComponents;
		Actor->GetComponents<UStaticMeshComponent>(MeshComponents);

		for (UStaticMeshComponent* Comp : MeshComponents)
		{
			UStaticMesh* Mesh = Comp ? Comp->GetStaticMesh() : nullptr;
			if (Mesh)
			{
				int32& Count = OutMeshInstanceCounts.FindOrAdd(Mesh, 0);
				Count++;
			}
		}
	}
}

void UMeshAnalyzer::GatherMeshesFromFolder(const FString& FolderPath, TMap<UStaticMesh*, int32>& OutMeshes)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssetsByPath(FName(*FolderPath), AssetDataList, true);

	for (const FAssetData& AssetData : AssetDataList)
	{
		if (AssetData.GetClass() == UStaticMesh::StaticClass())
		{
			UStaticMesh* Mesh = Cast<UStaticMesh>(AssetData.GetAsset());
			if (Mesh)
			{
				OutMeshes.FindOrAdd(Mesh, 1);
			}
		}
	}
}

void UMeshAnalyzer::GatherMeshesFromContentBrowser(TMap<UStaticMesh*, int32>& OutMeshes)
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<FAssetData> SelectedAssets;
	ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

	for (const FAssetData& AssetData : SelectedAssets)
	{
		UStaticMesh* Mesh = Cast<UStaticMesh>(AssetData.GetAsset());
		if (Mesh)
		{
			OutMeshes.FindOrAdd(Mesh, 1);
		}
	}
}

bool UMeshAnalyzer::PassesFilter(const FMeshAnalysisResult& Result, const FMeshAnalysisFilter& Filter) const
{
	if (Result.TriangleCount < Filter.MinTriangleCount)
	{
		return false;
	}
	if (Result.MaterialSlotCount < Filter.MinMaterialSlots)
	{
		return false;
	}
	if (Filter.bOnlyMeshesWithoutLODs && Result.LODCount > 1)
	{
		return false;
	}
	if (Filter.bOnlyMeshesWithoutNanite && Result.bHasNanite)
	{
		return false;
	}
	if (Filter.bOnlyMeshesWithoutLightmapUVs && Result.bHasLightmapUVs)
	{
		return false;
	}
	return true;
}

void UMeshAnalyzer::ComputeSummary()
{
	Summary = FSceneAnalysisSummary();
	Summary.TotalMeshAssets = Results.Num();

	for (const FMeshAnalysisResult& Result : Results)
	{
		Summary.TotalActors += Result.InstanceCount;
		Summary.TotalTriangles += (int64)Result.TriangleCount;
		Summary.TotalVertices += (int64)Result.VertexCount;
		Summary.TotalApproxMemoryMB += Result.ApproxMemoryMB;

		if (Result.LODCount <= 1)
		{
			Summary.MeshesWithoutLODs++;
		}
		if (!Result.bHasNanite)
		{
			Summary.MeshesWithoutNanite++;
		}
		if (!Result.bHasLightmapUVs)
		{
			Summary.MeshesWithoutLightmapUVs++;
		}
		if (Result.TriDensity > 10000.0f) // heuristic: >10k tris per m^3 suggests over-tessellation
		{
			Summary.MeshesWithHighTriDensity++;
		}
		if (Result.MaterialSlotCount > 5)
		{
			Summary.MeshesWithManyMaterials++;
		}
		if (Result.InstanceCount > 1)
		{
			Summary.DuplicateMeshGroups++;
		}
	}
}

void UMeshAnalyzer::SortByTriangleCount(bool bDescending)
{
	Results.Sort([bDescending](const FMeshAnalysisResult& A, const FMeshAnalysisResult& B)
	{
		return bDescending ? (A.TriangleCount > B.TriangleCount) : (A.TriangleCount < B.TriangleCount);
	});
}

void UMeshAnalyzer::SortByMemory(bool bDescending)
{
	Results.Sort([bDescending](const FMeshAnalysisResult& A, const FMeshAnalysisResult& B)
	{
		return bDescending ? (A.ApproxMemoryMB > B.ApproxMemoryMB) : (A.ApproxMemoryMB < B.ApproxMemoryMB);
	});
}

void UMeshAnalyzer::SortByMaterialSlots(bool bDescending)
{
	Results.Sort([bDescending](const FMeshAnalysisResult& A, const FMeshAnalysisResult& B)
	{
		return bDescending ? (A.MaterialSlotCount > B.MaterialSlotCount) : (A.MaterialSlotCount < B.MaterialSlotCount);
	});
}

void UMeshAnalyzer::SortByTriDensity(bool bDescending)
{
	Results.Sort([bDescending](const FMeshAnalysisResult& A, const FMeshAnalysisResult& B)
	{
		return bDescending ? (A.TriDensity > B.TriDensity) : (A.TriDensity < B.TriDensity);
	});
}

FString UMeshAnalyzer::ExportToCSV() const
{
	FString CSV;
	CSV += TEXT("Name,AssetPath,Triangles,Vertices,Materials,LODs,UVChannels,Nanite,LightmapUV,Collision,BoundsX,BoundsY,BoundsZ,MemoryMB,TriDensity,Instances\n");

	for (const FMeshAnalysisResult& R : Results)
	{
		CSV += FString::Printf(
			TEXT("%s,%s,%d,%d,%d,%d,%d,%s,%s,%s,%.1f,%.1f,%.1f,%.2f,%.1f,%d\n"),
			*R.MeshName,
			*R.AssetPath,
			R.TriangleCount,
			R.VertexCount,
			R.MaterialSlotCount,
			R.LODCount,
			R.UVChannelCount,
			R.bHasNanite ? TEXT("Yes") : TEXT("No"),
			R.bHasLightmapUVs ? TEXT("Yes") : TEXT("No"),
			R.bHasCollision ? TEXT("Yes") : TEXT("No"),
			R.BoundsSize.X,
			R.BoundsSize.Y,
			R.BoundsSize.Z,
			R.ApproxMemoryMB,
			R.TriDensity,
			R.InstanceCount
		);
	}

	return CSV;
}

void UMeshAnalyzer::ClearResults()
{
	Results.Empty();
	Summary = FSceneAnalysisSummary();
}
